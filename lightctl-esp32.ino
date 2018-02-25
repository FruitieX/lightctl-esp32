#include <ArduinoJson.h>
#include "FastLED.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>

WebSocketsClient webSocket;

const char* ssid = "*********";
const char* password = "*********";

// LED strip
#define NUM_LEDS 60
#define DATA_PIN 13

#define NUM_COLORS 1

// Color dots
#define MAX_DOTS 5 // Try lowering if you experience crashes (out of memory)
#define MIN_VELOCITY 1
#define MAX_VELOCITY 3
#define TRAIL_LENGTH 8
//#define SPAWN_RATE 10000000 // Chance every frame (out of 2,147,483,647) that a new dot is spawned.
#define SPAWN_RATE 1

// Color
//#define HUE_VELOCITY 100 // Larger means slower (increment every X frames)
#define COLOR_JITTER 35
#define COLOR_WHEEL_SPLITS 3
#define SATURATION 160
#define SATURATION_JITTER 80 // S = SATURATION + random(SATURATION_JITTER)
//#define VALUE 200
#define COLOR_CORRECTION TypicalSMD5050
//#define COLOR_CORRECTION 0xAF70FF
#define DITHER_TRESHOLD 42

// Power limiter
// #define MAX_MA 2000

CRGB leds[NUM_LEDS];
uint8_t counter = 0;
uint8_t hue = 0;

struct dot {
  bool active; // whether the dot is still visible (i.e. can it be respawned?)
  int pos; // uses 255 steps per pixel to represent sub-pixel accuracy
  uint8_t velocity; // how many subpixels to move per iteration
  int8_t dir; // which direction is the dot moving in (-1 = left, 1 = right)
  uint8_t color;
};

StaticJsonBuffer<1024> jsonBuffer;
CHSV colors[NUM_COLORS];
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_CONNECTED:
      webSocket.sendTXT("{\"id\": \"Skumppaflaska\"}");
      break;
    case WStype_TEXT:
      jsonBuffer.clear();
      JsonArray& array = jsonBuffer.parseArray((char *) payload);
      
      /*
      if (!array.success()) {
        Serial.println("Invalid JSON received.");
        Serial.println((char *) payload);
      }
      */

      for (int i = 0; i < NUM_COLORS && i < array.size(); i++) {
        float hue = array[i]["nextState"][0];
        float sat = array[i]["nextState"][1];
        float val = array[i]["nextState"][2];

        colors[i] = CHSV(
          hue / 360 * 255,
          sat / 100 * 255,
          val / 100 * 255
         );

         Serial.printf("setting color %d to %d,%d,%d\n", i, colors[i].h, colors[i].s, colors[i].v);
      }

      break;
  }
}

struct dot dots[MAX_DOTS];

void spawn_dot() {
  for (int i = 0; i < MAX_DOTS; i++) {
    if (!dots[i].active) {
      dots[i].active = true;

      bool startEdge = random(2);
      dots[i].pos = startEdge ? 0 : NUM_LEDS * 255;
      dots[i].velocity = random(MIN_VELOCITY - 1, MAX_VELOCITY) + 1;
      dots[i].dir = startEdge ? 1 : -1;

      //uint8_t rand_hue = hue + (255 / COLOR_WHEEL_SPLITS) * random(COLOR_WHEEL_SPLITS);
      //uint8_t rand_hue = hue + COLOR_JITTER * random(COLOR_WHEEL_SPLITS);
        
      //dots[i].color = CHSV(rand_hue, SATURATION + random(SATURATION_JITTER), 255);

      dots[i].color = random(NUM_COLORS);

      // we found an inactive dot, stop
      return;
    }
  }
}
void init_dot(dot d) {
  d.active = false;
  d.pos = 0;
  d.velocity = 0;
  d.dir = 1;
}

void setup() { 
  //randomSeed(analogRead(0));
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.show();
  //FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MA);

  hue = random8();

  for (int i = 0; i < NUM_COLORS; i++) {
    colors[i] = CHSV(0, 0, 255);
  }

  for (int i = 0; i < MAX_DOTS; i++) {
    init_dot(dots[i]);
  }

  spawn_dot();

  Serial.begin(115200);

  WiFi.begin(ssid, password);

  //WiFi.disconnect();
  Serial.print("connecting to wifi...");
  while(WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }

  Serial.println("\nconnected to wifi");

  // server address, port and URL
  webSocket.begin("192.168.1.101", 1234, "/");

  // event handler
  webSocket.onEvent(webSocketEvent);

  // try again every second if connection has failed
  webSocket.setReconnectInterval(1000);
}

void loop() {
  counter++;

  if (counter % 128 == 0) {
    //hue = (hue + 1) % 256;
    hue++;
  }

  if (random8() < 1) {
    spawn_dot();
  }

  // Clear out all leds
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  //uint8_t dither = random(DITHER_TRESHOLD);

  // Loop through each dot
  for (int i = 0; i < MAX_DOTS; i++) {
    if (!dots[i].active) {
      continue;
    }
    
    // Dot movement
    //if (counter % VELOCITY_DIVISOR == 0) {
      dots[i].pos += dots[i].dir * dots[i].velocity;
    //}
    
    // Convert subpixel pos to pixel pos
    int dot_pos = dots[i].pos / 256;

    if (dots[i].pos < 0) {
      // Because integer division truncates the decimal part,
      // we need to subtract one from negative numbers
      dot_pos--;
    }
    
    uint8_t sub_dot_pos = dots[i].pos;// % 256;
    
    // Subpixel pos reversed if dot travels left
    if (dots[i].dir == -1) {
      sub_dot_pos = 255 - sub_dot_pos;
    }

    // Draw dot and trail
    bool trail_was_in_bounds = false;
    for (int j = 0; j < TRAIL_LENGTH; j++) {
      int trail_pos = dot_pos + j * dots[i].dir * -1;

      // Bounds check
      if (trail_pos >= 0 && trail_pos < NUM_LEDS) {
        trail_was_in_bounds = true;

        uint8_t value;

        if (!j) {
          // Fade in first led
          value = sub_dot_pos;
        } else {
          // Fade out other leds in the trail
          value = 255 - ((j - 1) * 255 + sub_dot_pos) / TRAIL_LENGTH;
          value = ease8InOutApprox(value);
        }

/*
        if (value < 255) {
          value += dither;
        }
        */
        
        CHSV temp = CHSV(colors[dots[i].color]);

        float tempVal = (float) temp.v * (float) value / 255.0; 
        temp.v = tempVal;

        if (temp.v < DITHER_TRESHOLD) {
          temp.v = 0;
          //value = counter % (DITHER_TRESHOLD / 3) < value / 3 ? DITHER_TRESHOLD : 0;
        }
       
        leds[trail_pos] += temp;
      }
    }

    // If no part of the trail was in bounds, mark dot as inactive
    if (!trail_was_in_bounds) {
      dots[i].active = false;
    }
  }

  FastLED.show();
  webSocket.loop();
}

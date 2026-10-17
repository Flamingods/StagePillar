#pragma once
#include <stdint.h>

#if !__has_include("secrets.h")
#error "Missing include/secrets.h: copy include/secrets.example.h to include/secrets.h and fill it in"
#endif
#include "secrets.h"

#ifndef PILLAR_ID
#error "PILLAR_ID is not set: build with a pillar environment (pio run -e pillar1 / pillar2)"
#endif
static_assert(PILLAR_ID == 1 || PILLAR_ID == 2, "PILLAR_ID must be 1 or 2");

#define PILLAR_STR2(x) #x
#define PILLAR_STR(x) PILLAR_STR2(x)

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

constexpr uint8_t BUTTON_PIN = 12;  // internal pull-up
constexpr uint8_t BUTTON_PRESSED_LEVEL = 1;  // this board reads HIGH while the button is down
constexpr uint8_t LED_PIN = 2;      // onboard LED, active high
constexpr uint8_t PILLAR_LED_PIN = 4;     // 100 x WS2812B, GRB
constexpr uint8_t PILLAR_BRIGHTNESS = 80; // of 255; caps strip current
constexpr const char* DEVICE_HOSTNAME = "stage-pillar-" PILLAR_STR(PILLAR_ID);
constexpr uint16_t HTTP_TIMEOUT_MS = 2000;
constexpr uint32_t MDNS_TIMEOUT_MS = 1500;
constexpr uint8_t GESTURE_QUEUE_LEN = 4;
constexpr uint32_t SHOW_POLL_MS = 1000;

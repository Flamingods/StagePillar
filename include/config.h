#pragma once
#include <stdint.h>

#if !__has_include("secrets.h")
#error "Missing include/secrets.h: copy include/secrets.example.h to include/secrets.h and fill it in"
#endif
#include "secrets.h"

// Each pillar is a side, left or right as seen from the entrance (show-events contract).
#if defined(PILLAR_SIDE_L) == defined(PILLAR_SIDE_R)
#error "Pillar side is not set: build with a side environment (pio run -e pillarL / pillarR)"
#endif
#ifdef PILLAR_SIDE_L
constexpr char PILLAR_SIDE = 'L';
#define PILLAR_HOSTNAME "stage-pillar-l"
#else
constexpr char PILLAR_SIDE = 'R';
#define PILLAR_HOSTNAME "stage-pillar-r"
#endif

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

constexpr uint8_t BUTTON_PIN = 12;  // internal pull-up
constexpr uint8_t BUTTON_PRESSED_LEVEL = 1;  // this board reads HIGH while the button is down
constexpr uint8_t LED_PIN = 2;      // onboard LED, active high
constexpr uint8_t PILLAR_LED_PIN = 4;     // 100 x WS2812B; this strip wires its channels B, R, G (checked on pillar R 2026-10-17)
constexpr uint8_t PILLAR_BRIGHTNESS = 80; // of 255; caps strip current
constexpr const char* DEVICE_HOSTNAME = PILLAR_HOSTNAME;
constexpr uint16_t HTTP_TIMEOUT_MS = 2000;
constexpr uint32_t MDNS_TIMEOUT_MS = 1500;
constexpr uint8_t GESTURE_QUEUE_LEN = 4;
constexpr uint32_t SHOW_POLL_MS = 1000;

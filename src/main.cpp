#include <Arduino.h>
#include "actions.h"
#include "config.h"
#include "gesture.h"
#include "net.h"
#include "ota.h"
#include "pillar_leds.h"
#include "plan_store.h"
#include "sender.h"
#include "show_state.h"
#include "status_led.h"

static GestureDetector detector;

void setup() {
    Serial.begin(115200);
    Serial.printf("\nStagePillar %c (%s) firmware %s\n", PILLAR_SIDE, DEVICE_HOSTNAME, FIRMWARE_VERSION);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    statusLedBegin();
    planStoreBegin();
    pillarLedsBegin();
    netBegin();
    senderBegin();
    showStateBegin();
}

void loop() {
    uint32_t now = millis();
    bool wifi = netConnected();

    Gesture gesture = detector.update(digitalRead(BUTTON_PIN) == BUTTON_PRESSED_LEVEL, now);
    if (gesture != Gesture::None) {
        // Every gesture goes to the show director, which decides what it does.
        Serial.printf("Gesture -> %s (%u taps)\n", actionFor(gesture), detector.lastTaps());
        if (gesture == Gesture::Single) showAssumePlaying(now);
        if (gesture == Gesture::Long) showAssumeStopped(now);
        pillarLedsPlay(gesture, now);
        if (!senderEnqueue(gesture, detector.lastTaps(), detector.lastFirstPressMs())) {
            Serial.println("Drop: queue full");
        }
    }

    otaLoop(wifi);
    statusLedUpdate(now, wifi);
    pillarLedsUpdate(now, otaActive(), showPlaying(now));
    delay(1);
}

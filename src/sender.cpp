#include "sender.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include "actions.h"
#include "config.h"
#include "net.h"
#include "ota.h"
#include "pillar_leds.h"
#include "send_policy.h"
#include "status_led.h"
#include "urls.h"

struct QueuedGesture {
    Gesture gesture;
    uint32_t queuedAtMs;
    uint8_t taps;
    uint32_t firstPressMs;
};

static QueueHandle_t queue;

static void send(const QueuedGesture& item) {
    const char* action = actionFor(item.gesture);
    if (!action) return;
    if (otaActive()) {
        Serial.printf("Drop %s: OTA in progress\n", action);
        return;
    }
    if (!netConnected()) {
        Serial.printf("Drop %s: WiFi down\n", action);
        statusLedFlash(Flash::Fail);
        pillarLedsFail();
        return;
    }

    String url = "http://" + netServerIp().toString() + ":" + String(SERVER_PORT) + "/api/buttons/press";
    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);

    uint32_t start = millis();
    int code = -1;
    if (http.begin(url)) {
        // Measured just before sending so it includes the gap wait and queue time.
        char body[128];
        pressBody(body, sizeof(body), action, PILLAR_SIDE, item.taps, millis() - item.firstPressMs);
        http.addHeader("Content-Type", "application/json");
        code = http.POST(body);
        http.end();
    }
    Serial.printf("POST %s -> %d (%lu ms)\n", action, code, (unsigned long)(millis() - start));

    // No retry: a repeated start or claps on stage is worse than a miss.
    switch (classifySend(code)) {
        case SendResult::Ok:
            statusLedFlash(Flash::Ok);
            break;
        case SendResult::Failed:
            statusLedFlash(Flash::Fail);
            pillarLedsFail();
            break;
        case SendResult::Unreachable:
            statusLedFlash(Flash::Fail);
            pillarLedsFail();
            netForgetServerIp();  // the Pi may have a new address
            break;
    }
}

static void senderTask(void*) {
    QueuedGesture item;
    for (;;) {
        if (xQueueReceive(queue, &item, portMAX_DELAY) != pdTRUE) continue;
        if (gestureStale(item.queuedAtMs, millis())) {
            Serial.printf("Drop %s: waited too long in queue\n", actionFor(item.gesture));
            continue;
        }
        send(item);
    }
}

void senderBegin() {
    queue = xQueueCreate(GESTURE_QUEUE_LEN, sizeof(QueuedGesture));
    xTaskCreate(senderTask, "sender", 8192, nullptr, 1, nullptr);
}

bool senderEnqueue(Gesture gesture, uint8_t taps, uint32_t firstPressMs) {
    QueuedGesture item{gesture, millis(), taps, firstPressMs};
    return xQueueSend(queue, &item, 0) == pdTRUE;
}

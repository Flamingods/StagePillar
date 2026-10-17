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
};

static QueueHandle_t queue;

static void send(Gesture gesture) {
    const char* action = actionFor(gesture);
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

    char path[64];
    pressPath(path, sizeof(path), action, PILLAR_ID);
    String url = "http://" + netServerIp().toString() + ":" + String(SERVER_PORT) + path;
    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);

    uint32_t start = millis();
    int code = -1;
    if (http.begin(url)) {
        code = http.POST("");
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
        send(item.gesture);
    }
}

void senderBegin() {
    queue = xQueueCreate(GESTURE_QUEUE_LEN, sizeof(QueuedGesture));
    xTaskCreate(senderTask, "sender", 8192, nullptr, 1, nullptr);
}

bool senderEnqueue(Gesture gesture) {
    QueuedGesture item{gesture, millis()};
    return xQueueSend(queue, &item, 0) == pdTRUE;
}

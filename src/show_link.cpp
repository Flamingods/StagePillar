#include "show_link.h"
#include <Arduino.h>
#include <WebSocketsClient.h>
#include "config.h"
#include "net.h"

namespace {

const uint32_t kReconnectMs = 2000;
// If the socket stays down this long, reconnect to the (possibly re-resolved) server address.
const uint32_t kRestartMs = 30000;

WebSocketsClient ws;
QueueHandle_t events;    // latest ShowEvent only (length 1, overwritten)
QueueHandle_t commands;  // PillarCommand, from the socket and the poll backup
volatile bool connected = false;
uint32_t downSinceMs = 0;
bool begun = false;

void onEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            connected = true;
            Serial.println("Show: socket connected");
            break;
        case WStype_DISCONNECTED:
            if (connected) Serial.println("Show: socket disconnected");
            connected = false;
            downSinceMs = millis();
            break;
        case WStype_TEXT: {
            ShowEvent show;
            PillarCommand command;
            switch (parseWsMessage((const char*)payload, length, PILLAR_SIDE, show, command)) {
                case WsMessage::Show:
                    xQueueOverwrite(events, &show);
                    break;
                case WsMessage::Command:
                    xQueueSend(commands, &command, 0);
                    break;
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }
}

void start() {
    ws.begin(netServerIp().toString(), SERVER_PORT, "/api/buttons/ws");
    ws.onEvent(onEvent);
    ws.setReconnectInterval(kReconnectMs);
    // Ping every 5 s; two missed pongs (3 s each) close the socket, so a crashed or rebooted
    // Pi is noticed instead of leaving the last game drawn forever.
    ws.enableHeartbeat(5000, 3000, 2);
    begun = true;
    downSinceMs = millis();
}

void linkTask(void*) {
    for (;;) {
        if (netConnected()) {
            if (!begun) start();
            ws.loop();
            // Reconnect with whatever address the sender/poll currently use. The shared
            // address cache is not cleared here, so presses never pay for a lookup.
            if (!connected && millis() - downSinceMs > kRestartMs) {
                ws.disconnect();
                start();
            }
        } else if (begun) {
            // WiFi is gone: the socket is dead even if the library has not noticed yet.
            ws.disconnect();
            begun = false;
            if (connected) Serial.println("Show: socket dropped (WiFi down)");
            connected = false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

}  // namespace

void showLinkBegin() {
    events = xQueueCreate(1, sizeof(ShowEvent));
    commands = xQueueCreate(4, sizeof(PillarCommand));
    xTaskCreate(linkTask, "show-ws", 8192, nullptr, 1, nullptr);
}

bool showLinkConnected() {
    return connected;
}

bool showLinkTakeEvent(ShowEvent& out) {
    return xQueueReceive(events, &out, 0) == pdTRUE;
}

bool showLinkTakeCommand(PillarCommand& out) {
    return xQueueReceive(commands, &out, 0) == pdTRUE;
}

void showLinkOfferCommand(const PillarCommand& command) {
    xQueueSend(commands, &command, 0);
}

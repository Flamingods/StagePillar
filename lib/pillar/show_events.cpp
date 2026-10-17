#include "show_events.h"
#include <ArduinoJson.h>
#include <string.h>

static ShowState stateFrom(const char* s) {
    if (!s) return ShowState::Other;
    if (!strcmp(s, "idle")) return ShowState::Idle;
    if (!strcmp(s, "launching")) return ShowState::Launching;
    if (!strcmp(s, "intro")) return ShowState::Intro;
    if (!strcmp(s, "playing")) return ShowState::Playing;
    if (!strcmp(s, "failing")) return ShowState::Failing;
    return ShowState::Other;
}

static PoleMode modeFrom(const char* s) {
    if (!s) return PoleMode::Off;
    if (!strcmp(s, "glow")) return PoleMode::Glow;
    if (!strcmp(s, "solid")) return PoleMode::Solid;
    if (!strcmp(s, "pulse")) return PoleMode::Pulse;
    if (!strcmp(s, "blink")) return PoleMode::Blink;
    if (!strcmp(s, "drain")) return PoleMode::Drain;
    return PoleMode::Off;
}

static uint8_t percent(JsonVariantConst v, uint8_t fallback) {
    if (!v.is<int>()) return fallback;
    int n = v.as<int>();
    return uint8_t(n < 0 ? 0 : n > 100 ? 100 : n);
}

static bool readCommand(JsonVariantConst obj, PillarCommand& out) {
    if (!obj["seq"].is<uint32_t>() || !obj["program"].is<uint16_t>()) return false;
    out.seq = obj["seq"].as<uint32_t>();
    out.program = obj["program"].as<uint16_t>();
    return true;
}

WsMessage parseWsMessage(const char* json, size_t len, char side, ShowEvent& show, PillarCommand& cmd) {
    // Only the fields the pillar uses are kept, which keeps big show events cheap.
    JsonDocument filter;
    filter["action"] = true;
    filter["state"] = true;
    filter["side"] = true;
    filter["seq"] = true;
    filter["program"] = true;
    const char sideKey[2] = {side, '\0'};
    filter["poles"][sideKey] = true;

    JsonDocument doc;
    if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return WsMessage::Ignore;
    const char* action = doc["action"];
    if (!action) return WsMessage::Ignore;

    if (!strcmp(action, "pillar_command")) {
        const char* to = doc["side"];
        if (!to || to[0] != side || to[1] != '\0') return WsMessage::Ignore;
        return readCommand(doc.as<JsonVariantConst>(), cmd) ? WsMessage::Command : WsMessage::Ignore;
    }
    if (strcmp(action, "show") != 0) return WsMessage::Ignore;

    ShowEvent ev;
    ev.state = stateFrom(doc["state"]);
    JsonVariantConst pole = doc["poles"][sideKey];
    if (!pole.isNull()) {
        ev.pole.mode = modeFrom(pole["mode"]);
        ev.pole.pct = percent(pole["pct"], 0);
        ev.pole.level = percent(pole["level"], kDefaultGlowLevel);
        if (pole["ms"].is<uint32_t>()) {
            ev.pole.hasMs = true;
            ev.pole.ms = pole["ms"].as<uint32_t>();
        }
        const char* color = pole["color"];
        if (color) {
            strncpy(ev.pole.color, color, sizeof(ev.pole.color) - 1);
            ev.pole.color[sizeof(ev.pole.color) - 1] = '\0';
        }
    }
    show = ev;
    return WsMessage::Show;
}

bool parseStateCommand(const char* body, PillarCommand& out) {
    if (!body) return false;
    JsonDocument filter;
    filter["pillar_command"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
    JsonVariantConst c = doc["pillar_command"];
    if (c.isNull()) return false;
    return readCommand(c, out);
}

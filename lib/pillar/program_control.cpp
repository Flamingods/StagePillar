#include "program_control.h"
#include <ArduinoJson.h>

bool parseProgramIndex(const char* json, ProgramIndex& out) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;
    if (!doc["version"].is<uint32_t>() || !doc["ids"].is<JsonArrayConst>()) return false;
    JsonArrayConst ids = doc["ids"];
    if (ids.size() > kMaxPrograms) return false;
    ProgramIndex idx;
    idx.version = doc["version"];
    for (JsonVariantConst v : ids) {
        if (!v.is<int>() || v.as<int>() < 1 || v.as<int>() > 255) return false;
        idx.ids[idx.count++] = uint8_t(v.as<int>());
    }
    out = idx;
    return true;
}

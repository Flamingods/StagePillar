#include "pole_render.h"
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

// Emulator defaults when an event gives no rate.
static const float kPulseMs = 500;
static const float kBlinkMs = 250;
static const float kTau = 6.2831853f;

static PaletteColor color(const char* name, Rgb dim, Rgb base, Rgb bright, uint32_t driftMs) {
    PaletteColor c{};
    strncpy(c.name, name, sizeof(c.name) - 1);
    c.ramp[0] = dim;
    c.ramp[1] = base;
    c.ramp[2] = bright;
    c.gain = 1.0f;
    c.driftMs = driftMs;
    return c;
}

Palette defaultPalette() {
    Palette p;
    p.colors[0] = color("pink", {196, 0, 122}, {255, 20, 147}, {255, 106, 193}, 2000);
    p.colors[1] = color("lime", {70, 190, 0}, {125, 255, 0}, {205, 255, 130}, 2800);
    p.colors[2] = color("blue", {12, 50, 215}, {20, 80, 255}, {96, 150, 255}, 2800);
    p.colors[3] = color("white", {190, 190, 205}, {255, 255, 255}, {255, 255, 255}, 2800);
    p.count = 4;
    return p;
}

PaletteColor* paletteFind(Palette& palette, const char* name) {
    for (uint8_t i = 0; i < palette.count; i++) {
        if (!strcmp(palette.colors[i].name, name)) return &palette.colors[i];
    }
    return nullptr;
}

static bool readRgb(JsonVariantConst v, Rgb& out) {
    if (!v.is<JsonArrayConst>() || v.size() != 3) return false;
    out = Rgb{v[0].as<uint8_t>(), v[1].as<uint8_t>(), v[2].as<uint8_t>()};
    return true;
}

bool parsePalette(const char* json, Palette& out) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;
    JsonObjectConst colors = doc["colors"];
    if (colors.isNull() || !doc["version"].is<uint32_t>()) return false;
    Palette p;
    p.version = doc["version"];
    for (JsonPairConst kv : colors) {
        if (p.count == sizeof(p.colors) / sizeof(p.colors[0])) break;
        PaletteColor c{};
        strncpy(c.name, kv.key().c_str(), sizeof(c.name) - 1);
        JsonVariantConst ramp = kv.value()["ramp"];
        if (!readRgb(ramp[0], c.ramp[0]) || !readRgb(ramp[1], c.ramp[1]) || !readRgb(ramp[2], c.ramp[2])) return false;
        c.gain = kv.value()["gain"] | 1.0f;
        c.driftMs = kv.value()["drift_ms"] | 2800u;
        if (c.driftMs == 0) c.driftMs = 2800;
        p.colors[p.count++] = c;
    }
    if (p.count == 0) return false;
    out = p;
    return true;
}

static float mixf(uint8_t a, uint8_t b, float k) {
    return a + (b - a) * k;
}

void renderPole(const PoleState& pole, uint32_t elapsedMs, uint32_t nowMs, const Palette& palette, Rgb* frame,
                uint16_t count) {
    for (uint16_t i = 0; i < count; i++) frame[i] = Rgb{0, 0, 0};
    if (pole.mode == PoleMode::Off || pole.color[0] == '\0') return;

    float pct = pole.pct;
    float level = 1.0f;
    float ms = pole.hasMs && pole.ms ? float(pole.ms) : 0.0f;
    switch (pole.mode) {
        case PoleMode::Drain:
            if (ms > 0) pct *= fmaxf(0.0f, 1.0f - elapsedMs / ms);
            break;
        case PoleMode::Blink:
            level = (uint32_t(elapsedMs / kBlinkMs) % 2 == 0) ? 1.0f : 0.0f;
            break;
        case PoleMode::Glow:
            level = pole.level / 100.0f;
            break;
        case PoleMode::Pulse:
            level = 0.1f + 0.9f * (0.5f - 0.5f * cosf((elapsedMs / (ms > 0 ? ms : kPulseMs)) * kTau));
            break;
        default:
            break;
    }

    uint16_t lit = uint16_t(lroundf(pct / 100.0f * count));
    if (lit > count) lit = count;
    const PaletteColor* c = nullptr;
    for (uint8_t k = 0; k < palette.count; k++) {
        if (!strcmp(palette.colors[k].name, pole.color)) c = &palette.colors[k];
    }

    // The drift repeats every period, so use the phase within it: exact even after weeks of uptime.
    float phase = c ? float(nowMs % c->driftMs) / c->driftMs : 0.0f;
    for (uint16_t i = 0; i < lit; i++) {
        float rgb[3] = {255, 255, 255};  // unknown colour: white, as in the emulator
        if (c) {
            float k = 0.5f + 0.5f * sinf(i * 0.33f - kTau * phase);
            for (int ch = 0; ch < 3; ch++) {
                const uint8_t* lo = &c->ramp[k < 0.5f ? 0 : 1].r;
                const uint8_t* hi = &c->ramp[k < 0.5f ? 1 : 2].r;
                float v = mixf(lo[ch], hi[ch], k < 0.5f ? k * 2 : (k - 0.5f) * 2) * c->gain;
                rgb[ch] = fminf(255.0f, v);
            }
        }
        frame[i] = Rgb{uint8_t(lroundf(rgb[0] * level)), uint8_t(lroundf(rgb[1] * level)), uint8_t(lroundf(rgb[2] * level))};
    }
}

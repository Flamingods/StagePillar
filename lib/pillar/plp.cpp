#include "plp.h"
#include <string.h>

static const uint8_t kMagic[4] = {'P', 'L', 'P', '1'};
static const uint8_t kMaxEffectId = uint8_t(EffectId::Fade);

static uint16_t get16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
static uint32_t get32(const uint8_t* p) { return uint32_t(get16(p)) | uint32_t(get16(p + 2)) << 16; }
static void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
static void put32(uint8_t* p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc) {
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

bool MemorySource::read(uint32_t offset, uint8_t* dst, uint32_t len) const {
    if (offset > len_ || len > len_ - offset) return false;
    memcpy(dst, data_ + offset, len);
    return true;
}

bool planIndex(const ByteSource& src, PlanIndex& out) {
    uint32_t size = src.size();
    if (size < kPlanHeaderBytes || size > kPlanMaxBytes) return false;

    uint8_t header[kPlanHeaderBytes];
    if (!src.read(0, header, kPlanHeaderBytes) || memcmp(header, kMagic, 4) != 0) return false;
    out.version = get32(header + 4);
    out.loop = header[8] != 0;
    out.stepCount = header[9];
    if (out.stepCount == 0 || out.stepCount > kPlanMaxSteps) return false;

    // Walk the steps, checking each one and running the CRC over the body as we go.
    uint32_t crc = 0;
    uint32_t at = kPlanHeaderBytes;
    out.totalMs = 0;
    uint8_t buf[kFrameStepBytes];
    for (uint8_t i = 0; i < out.stepCount; i++) {
        uint8_t kind;
        if (!src.read(at, &kind, 1)) return false;
        uint32_t len;
        if (kind == uint8_t(StepKind::Effect)) {
            len = kEffectStepBytes;
        } else if (kind == uint8_t(StepKind::Frame)) {
            len = kFrameStepBytes;
        } else {
            return false;
        }
        if (!src.read(at, buf, len)) return false;
        if (kind == uint8_t(StepKind::Effect) && (buf[1] > kMaxEffectId || buf[5] > 2)) return false;
        uint16_t duration = get16(buf + (kind == uint8_t(StepKind::Effect) ? 2 : 1));
        if (duration == 0) return false;

        out.kind[i] = StepKind(kind);
        out.offset[i] = uint16_t(at);
        out.durationMs[i] = duration;
        out.totalMs += duration;
        crc = crc32(buf, len, crc);
        at += len;
    }
    out.crc = crc;
    return at == size && crc == get32(header + 12);
}

bool readEffectStep(const ByteSource& src, const PlanIndex& index, uint8_t step, EffectStep& out) {
    if (step >= index.stepCount || index.kind[step] != StepKind::Effect) return false;
    uint8_t b[kEffectStepBytes];
    if (!src.read(index.offset[step], b, kEffectStepBytes)) return false;
    out.effect = EffectId(b[1]);
    out.durationMs = get16(b + 2);
    out.brightness = b[4];
    out.direction = Direction(b[5]);
    out.speedX16 = b[6];
    for (int c = 0; c < 3; c++) out.colors[c] = Rgb{b[7 + c * 3], b[8 + c * 3], b[9 + c * 3]};
    return true;
}

bool readFramePixels(const ByteSource& src, const PlanIndex& index, uint8_t step, FramePixel* out) {
    if (step >= index.stepCount || index.kind[step] != StepKind::Frame) return false;
    uint8_t b[kFrameStepBytes];
    if (!src.read(index.offset[step], b, kFrameStepBytes)) return false;
    for (uint16_t i = 0; i < kPlanPixels; i++) {
        const uint8_t* p = b + 3 + i * 7;
        out[i] = FramePixel{Rgb{p[0], p[1], p[2]}, get16(p + 3), get16(p + 5)};
    }
    return true;
}

uint32_t encodeEffectPlan(uint32_t version, bool loop, const EffectStep* steps, uint8_t count,
                          uint8_t* out, uint32_t capacity) {
    uint32_t len = kPlanHeaderBytes + uint32_t(count) * kEffectStepBytes;
    if (count == 0 || count > kPlanMaxSteps || len > capacity) return 0;
    memcpy(out, kMagic, 4);
    put32(out + 4, version);
    out[8] = loop ? 1 : 0;
    out[9] = count;
    put16(out + 10, 0);
    for (uint8_t i = 0; i < count; i++) {
        uint8_t* b = out + kPlanHeaderBytes + i * kEffectStepBytes;
        const EffectStep& s = steps[i];
        b[0] = uint8_t(StepKind::Effect);
        b[1] = uint8_t(s.effect);
        put16(b + 2, s.durationMs);
        b[4] = s.brightness;
        b[5] = uint8_t(s.direction);
        b[6] = s.speedX16;
        for (int c = 0; c < 3; c++) {
            b[7 + c * 3] = s.colors[c].r;
            b[8 + c * 3] = s.colors[c].g;
            b[9 + c * 3] = s.colors[c].b;
        }
    }
    put32(out + 12, crc32(out + kPlanHeaderBytes, len - kPlanHeaderBytes));
    return len;
}

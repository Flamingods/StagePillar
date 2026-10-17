#include "plan_store.h"
#include <HTTPClient.h>
#include <LittleFS.h>
#include <string.h>
#include "config.h"
#include "net.h"
#include "program_control.h"

namespace {

// Reads a plan file on demand; each read opens, seeks and closes it.
class FileSource : public ByteSource {
public:
    void setPath(const String& path) { path_ = path; }
    uint32_t size() const override {
        File f = LittleFS.open(path_, "r");
        uint32_t n = f ? f.size() : 0;
        f.close();
        return n;
    }
    bool read(uint32_t offset, uint8_t* dst, uint32_t len) const override {
        File f = LittleFS.open(path_, "r");
        bool ok = f && f.seek(offset) && f.read(dst, len) == len;
        f.close();
        return ok;
    }

private:
    String path_;
};

const char* kDir = "/plans";
const char* kStaging = "/plans/new";
const char* kVersionFile = "/plans/version";
const char* kPreviewStaging = "/preview.new";
const char* kPreviewFile = "/preview.bin";
// The program library lives in one of two folders, /programs/a and /programs/b. A new
// library is downloaded into the inactive one and loop() just switches to it, so loop()
// does no file work at all; the download task then records the switch in kProgramsPointer.
const char* kProgramsRoot = "/programs";
const char* kProgramsPointer = "/programs/active";  // "<a|b> <version>"
const uint32_t kRetryMinMs = 5000;
const uint32_t kRetryMaxMs = 300000;

bool mounted = false;

// loop() only (and planStoreBegin before the task starts).
FileSource sources[kStoredSlots];
PlanIndex indexes[kStoredSlots];
bool valid[kStoredSlots];
FileSource previewSource;
PlanIndex previewIndex;

// Written by the download task while the matching staged flag is clear, then read by
// loop() while it is set.
PlanIndex stagedIndexes[kStoredSlots];
bool stagedValid[kStoredSlots];
PlanIndex stagedPreviewIndex;

// Program library: loop() owns the active set; the download task fills the staged set
// while stagedProgramsVersion is 0.
struct ProgramEntry {
    uint8_t id;
    FileSource source;
    PlanIndex index;
};
ProgramEntry programs[kMaxPrograms];
uint8_t programCount = 0;
volatile char activeLetter = 'a';
char stagedLetter = 'b';
volatile bool pointerDirty = false;
uint8_t stagedIds[kMaxPrograms];
PlanIndex stagedProgramIndexes[kMaxPrograms];
uint8_t stagedProgramCount = 0;
Palette stagedPalette;

// Hand-over flags between the tasks.
volatile uint32_t activeVersion = 0;
volatile uint32_t stagedVersion = 0;    // non-zero: a complete round waits in kStaging
volatile uint32_t stagedPreviewId = 0;  // non-zero: a preview waits in kPreviewStaging
volatile bool previewEnded = false;
volatile uint32_t targetVersion = 0;
volatile uint32_t targetPreviewId = 0;
volatile uint32_t activeProgramsVersion = 0;
volatile uint32_t stagedProgramsVersion = 0;  // non-zero: a complete library round waits
volatile uint32_t targetProgramsVersion = 0;
volatile uint32_t activePaletteVersion = 0;
volatile bool paletteStaged = false;
volatile uint32_t targetPaletteVersion = 0;

// Download task only.
uint32_t lastPreviewId = 0;
uint32_t failedVersion = 0;
uint32_t failedAtMs = 0;
uint32_t retryMs = kRetryMinMs;
uint32_t programsFailedVersion = 0;
uint32_t programsFailedAtMs = 0;
uint32_t programsRetryMs = kRetryMinMs;
uint32_t paletteFailedAtMs = 0;

String slotPath(const char* dir, Slot slot) { return String(dir) + "/" + slotName(slot) + ".bin"; }

int slotIndex(Slot slot) {
    for (int i = 0; i < kStoredSlots; i++) {
        if (kSlots[i] == slot) return i;
    }
    return -1;
}

uint32_t readVersionFile(const char* path = kVersionFile) {
    File f = LittleFS.open(path, "r");
    if (!f) return 0;
    uint32_t v = f.readString().toInt();
    f.close();
    return v;
}

// Downloads url to path. 1 = saved, 0 = 404 (not set), -1 = failed.
int download(const String& url, const char* path) {
    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    if (!http.begin(url)) return -1;
    int code = http.GET();
    int result = -1;
    if (code == 404) {
        result = 0;
    } else if (code == 200 && http.getSize() > 0 && uint32_t(http.getSize()) <= kPlanMaxBytes) {
        File f = LittleFS.open(path, "w");
        if (f) {
            int written = http.writeToStream(&f);
            f.close();
            if (written == http.getSize()) result = 1;
        }
    }
    http.end();
    if (result < 0) Serial.printf("Plans: GET %s -> %d\n", url.c_str(), code);
    return result;
}

// plans_version from a file's header, if it has a PLP1 header at all.
bool headerVersion(const char* path, uint32_t& out) {
    File f = LittleFS.open(path, "r");
    uint8_t h[8];
    bool ok = f && f.read(h, 8) == 8 && memcmp(h, "PLP1", 4) == 0;
    f.close();
    if (ok) out = uint32_t(h[4]) | uint32_t(h[5]) << 8 | uint32_t(h[6]) << 16 | uint32_t(h[7]) << 24;
    return ok;
}

bool indexFile(const char* path, PlanIndex& out) {
    FileSource src;
    src.setPath(path);
    return planIndex(src, out);
}

void clearDir(const char* path) {
    File dir = LittleFS.open(path);
    if (!dir) return;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        bool isDir = f.isDirectory();
        String child = String(path) + "/" + f.name();
        f.close();
        if (!isDir) LittleFS.remove(child);
    }
    dir.close();
}

void clearStaging() {
    File dir = LittleFS.open(kStaging);
    if (!dir) return;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String path = String(kStaging) + "/" + f.name();
        f.close();
        LittleFS.remove(path);
    }
    dir.close();
}

String apiBase() {
    return "http://" + netServerIp().toString() + ":" + String(SERVER_PORT) + "/api/pillar/";
}

// Fetches all six slots for `version` into staging. A slot that is missing (404) or
// that the ESP cannot play uses its default; only a version change fails the round.
bool downloadRound(uint32_t version) {
    clearStaging();
    String base = apiBase() + "plans/";
    for (int i = 0; i < kStoredSlots; i++) {
        Slot slot = kSlots[i];
        String path = slotPath(kStaging, slot);
        stagedValid[i] = false;
        int r = download(base + slotName(slot) + ".bin", path.c_str());
        if (r < 0) return false;
        if (r == 0) continue;
        uint32_t fileVersion;
        if (headerVersion(path.c_str(), fileVersion) && fileVersion != version) {
            Serial.printf("Plans: %s is v%u, wanted v%u; restarting\n", slotName(slot), (unsigned)fileVersion,
                          (unsigned)version);
            return false;
        }
        stagedValid[i] = indexFile(path.c_str(), stagedIndexes[i]);
        if (!stagedValid[i]) {
            Serial.printf("Plans: %s v%u is not playable, using default\n", slotName(slot), (unsigned)version);
            LittleFS.remove(path);
        }
    }
    return true;
}

// GET into a String; false on any failure or a body over maxBytes.
bool getText(const String& url, String& out, uint32_t maxBytes) {
    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    if (!http.begin(url)) return false;
    int code = http.GET();
    bool ok = code == 200 && http.getSize() > 0 && uint32_t(http.getSize()) <= maxBytes;
    if (ok) out = http.getString();
    http.end();
    if (!ok) Serial.printf("Plans: GET %s -> %d\n", url.c_str(), code);
    return ok;
}

String programDir(char letter) { return String(kProgramsRoot) + "/" + letter; }
String programPath(char letter, uint8_t id) { return programDir(letter) + "/" + String(id) + ".bin"; }

void writePointer() {
    File f = LittleFS.open(kProgramsPointer, "w");
    f.printf("%c %u", activeLetter, (unsigned)activeProgramsVersion);
    f.close();
    Serial.printf("Plans: %u of %u KB used\n", unsigned(LittleFS.usedBytes() / 1024), unsigned(LittleFS.totalBytes() / 1024));
}

// Fetches the whole library for `version` into staging. A program the ESP cannot play is
// left out; a version change or a failed download fails the round.
bool downloadPrograms(uint32_t version) {
    String text;
    ProgramIndex idx;
    if (!getText(apiBase() + "programs/index", text, 2048) || !parseProgramIndex(text.c_str(), idx)) return false;
    if (idx.version != version) return false;
    stagedLetter = activeLetter == 'a' ? 'b' : 'a';
    String dir = programDir(stagedLetter);
    LittleFS.mkdir(dir);
    clearDir(dir.c_str());
    stagedProgramCount = 0;
    for (uint8_t i = 0; i < idx.count; i++) {
        uint8_t id = idx.ids[i];
        String path = programPath(stagedLetter, id);
        if (download(apiBase() + "programs/" + String(id) + ".bin", path.c_str()) != 1) return false;
        uint32_t fileVersion;
        if (headerVersion(path.c_str(), fileVersion) && fileVersion != version) return false;
        if (!indexFile(path.c_str(), stagedProgramIndexes[stagedProgramCount])) {
            Serial.printf("Plans: program %u is not playable, skipped\n", id);
            LittleFS.remove(path);
            continue;
        }
        stagedIds[stagedProgramCount++] = id;
    }
    return true;
}

void syncPrograms() {
    uint32_t version = targetProgramsVersion;
    if (version == activeProgramsVersion || stagedProgramsVersion != 0) return;
    if (version == programsFailedVersion && millis() - programsFailedAtMs < programsRetryMs) return;
    Serial.printf("Plans: fetching program library %u\n", (unsigned)version);
    if (downloadPrograms(version)) {
        __sync_synchronize();  // staged library is complete before loop() can see the flag
        stagedProgramsVersion = version;
        programsFailedVersion = 0;
        programsRetryMs = kRetryMinMs;
    } else {
        programsRetryMs = version == programsFailedVersion ? min(programsRetryMs * 2, kRetryMaxMs) : kRetryMinMs;
        programsFailedVersion = version;
        programsFailedAtMs = millis();
    }
}

void syncPalette() {
    uint32_t version = targetPaletteVersion;
    if (version == 0 || version == activePaletteVersion || paletteStaged) return;
    if (millis() - paletteFailedAtMs < kRetryMinMs) return;
    String text;
    Palette p;
    if (getText("http://" + netServerIp().toString() + ":" + String(SERVER_PORT) + "/api/show/palette", text, 4096) &&
        parsePalette(text.c_str(), p)) {
        // Mark the version the state reply announced as done, even if the body carries a
        // different one, so a mismatch cannot cause a refetch every 200 ms.
        p.version = version;
        stagedPalette = p;
        __sync_synchronize();
        paletteStaged = true;
    } else {
        paletteFailedAtMs = millis();
    }
}

void syncPreview() {
    uint32_t id = targetPreviewId;
    if (id == lastPreviewId || stagedPreviewId != 0) return;
    if (id == 0) {
        previewEnded = true;
        lastPreviewId = 0;
        return;
    }
    // Retried on the next pass if it fails; preview ids expire on the server after 60 s.
    uint32_t fileVersion;
    if (download(apiBase() + "preview.bin", kPreviewStaging) == 1 && headerVersion(kPreviewStaging, fileVersion) &&
        fileVersion == id && indexFile(kPreviewStaging, stagedPreviewIndex)) {
        __sync_synchronize();  // staged index is complete before loop() can see the flag
        stagedPreviewId = id;
        lastPreviewId = id;
    }
}

void syncPlans() {
    uint32_t version = targetVersion;
    if (version == activeVersion || stagedVersion != 0) return;
    if (version == failedVersion && millis() - failedAtMs < retryMs) return;
    Serial.printf("Plans: fetching version %u\n", (unsigned)version);
    if (downloadRound(version)) {
        __sync_synchronize();  // staged indexes are complete before loop() can see the flag
        stagedVersion = version;
        failedVersion = 0;
        retryMs = kRetryMinMs;
    } else {
        retryMs = version == failedVersion ? min(retryMs * 2, kRetryMaxMs) : kRetryMinMs;
        failedVersion = version;
        failedAtMs = millis();
    }
}

void downloadTask(void*) {
    for (;;) {
        if (netConnected()) {
            if (pointerDirty) {
                pointerDirty = false;
                writePointer();
            }
            syncPreview();
            syncPalette();
            syncPlans();
            syncPrograms();
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

}  // namespace

void planStoreBegin() {
    mounted = LittleFS.begin(true);
    if (!mounted) {
        Serial.println("Plans: LittleFS mount failed, using defaults");
        return;
    }
    LittleFS.mkdir(kDir);
    LittleFS.mkdir(kStaging);
    for (int i = 0; i < kStoredSlots; i++) {
        String path = slotPath(kDir, kSlots[i]);
        sources[i].setPath(path);
        valid[i] = LittleFS.exists(path) && indexFile(path.c_str(), indexes[i]);
    }
    activeVersion = readVersionFile();
    LittleFS.mkdir(kProgramsRoot);
    uint32_t pointedVersion = 0;
    {
        File f = LittleFS.open(kProgramsPointer, "r");
        if (f) {
            String text = f.readString();
            if (text.length() >= 1 && (text[0] == 'a' || text[0] == 'b')) {
                activeLetter = text[0];
                pointedVersion = text.substring(2).toInt();
            }
            f.close();
        }
    }
    LittleFS.mkdir(programDir(activeLetter));
    File dir = LittleFS.open(programDir(activeLetter));
    for (File f = dir.openNextFile(); f && programCount < kMaxPrograms; f = dir.openNextFile()) {
        String name = f.name();
        bool isDir = f.isDirectory();
        f.close();
        if (isDir || !name.endsWith(".bin")) continue;
        int id = name.toInt();
        if (id < 1 || id > 255) continue;
        ProgramEntry& e = programs[programCount];
        String path = programPath(activeLetter, uint8_t(id));
        e.source.setPath(path);
        if (indexFile(path.c_str(), e.index)) {
            e.id = uint8_t(id);
            programCount++;
        }
    }
    dir.close();
    activeProgramsVersion = pointedVersion;
    Serial.printf("Plans: version %u, programs %u (%u stored)\n", (unsigned)activeVersion,
                  (unsigned)activeProgramsVersion, programCount);
    xTaskCreate(downloadTask, "plans", 8192, nullptr, 1, nullptr);
}

Plan planStoreLookup(Slot slot) {
    int i = slotIndex(slot);
    if (i < 0 || !valid[i]) return defaultPlan(slot);
    return Plan{&sources[i], &indexes[i]};
}

bool planStoreApplyPending() {
    uint32_t version = stagedVersion;
    if (version == 0) return false;
    for (int i = 0; i < kStoredSlots; i++) {
        String active = slotPath(kDir, kSlots[i]);
        LittleFS.remove(active);
        if (stagedValid[i]) LittleFS.rename(slotPath(kStaging, kSlots[i]), active);
        valid[i] = stagedValid[i];
        if (valid[i]) indexes[i] = stagedIndexes[i];
    }
    File f = LittleFS.open(kVersionFile, "w");
    f.print(version);
    f.close();
    activeVersion = version;
    stagedVersion = 0;
    Serial.printf("Plans: now on version %u\n", (unsigned)version);
    return true;
}

bool planStoreTakePreview(Plan& out) {
    if (stagedPreviewId == 0) return false;
    LittleFS.remove(kPreviewFile);
    LittleFS.rename(kPreviewStaging, kPreviewFile);
    previewSource.setPath(kPreviewFile);
    previewIndex = stagedPreviewIndex;
    stagedPreviewId = 0;
    out = Plan{&previewSource, &previewIndex};
    return true;
}

bool planStoreTakePreviewEnd() {
    if (!previewEnded) return false;
    previewEnded = false;
    return true;
}

bool planStoreProgram(uint16_t id, Plan& out) {
    for (uint8_t i = 0; i < programCount; i++) {
        if (programs[i].id == id) {
            out = Plan{&programs[i].source, &programs[i].index};
            return true;
        }
    }
    return false;
}

bool planStoreApplyPrograms() {
    uint32_t version = stagedProgramsVersion;
    if (version == 0) return false;
    // No file work here: the staged folder already holds the files; just point at it.
    programCount = 0;
    for (uint8_t i = 0; i < stagedProgramCount; i++) {
        ProgramEntry& e = programs[programCount++];
        e.id = stagedIds[i];
        e.source.setPath(programPath(stagedLetter, stagedIds[i]));
        e.index = stagedProgramIndexes[i];
    }
    activeLetter = stagedLetter;
    activeProgramsVersion = version;
    pointerDirty = true;
    stagedProgramsVersion = 0;
    Serial.printf("Plans: program library %u (%u programs)\n", (unsigned)version, programCount);
    return true;
}

bool planStoreTakePalette(Palette& out) {
    if (!paletteStaged) return false;
    out = stagedPalette;
    activePaletteVersion = out.version;
    paletteStaged = false;
    return true;
}

uint32_t planStoreVersion() {
    return activeVersion;
}

void planStoreSetTarget(uint32_t plansVersion, uint32_t previewId, uint32_t programsVersion,
                        uint32_t paletteVersion) {
    targetPaletteVersion = paletteVersion;  // RAM only, works without the file system
    if (!mounted) return;
    targetVersion = plansVersion;
    targetPreviewId = previewId;
    targetProgramsVersion = programsVersion;
}

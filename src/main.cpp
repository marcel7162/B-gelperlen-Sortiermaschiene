#include <Arduino.h>
#include <EEPROM.h>
#include "config.h"
#include "machine.h"
#include "stepper.h"
#include "color_sensor.h"
#include "shell.h"

// =============================================================
//  Globaler Zustand
// =============================================================
const char* colorNames[] = {
    "Weiss",     "Schwarz",    "Rot",        "Blau",
    "Gelb",      "Gruen",      "Orange",     "Pink",
    "Lila",      "Braun",      "Hellblau",   "Hellgruen",
    "Dunkelblau","Dunkelgruen","Grau",       "Beige",
    "Transparent","Unbekannt"
};

CalData     cal;
RuntimeCfg  cfg;
bool        calibrated = false;
bool        sorting    = false;
bool        debugMode  = false;
int32_t     sortPos    = 0;
uint32_t    beadCount  = 0;
static int32_t feedPos = 0;

// =============================================================
//  RuntimeCfg – Standardwerte / EEPROM
// =============================================================
void cfgDefaults() {
    cfg.magic            = CFG_MAGIC;
    cfg.beadThreshold    = BEAD_THRESHOLD;
    cfg.feedStepsPerBead = FEED_STEPS_PER_BEAD;
    cfg.feedPollSteps    = FEED_POLL_STEPS;
    cfg.rehomeInterval   = REHOME_INTERVAL;
    cfg.sortStepUs       = SORT_STEP_US;
    cfg.feedStepUs       = FEED_STEP_US;
    cfg.homeStepUs       = HOME_STEP_US;
    cfg.colorReadSamples = COLOR_READ_SAMPLES;
    cfg.tcsSampleMs      = TCS_SAMPLE_MS;
    cfg.maxColorDist     = MAX_COLOR_DISTANCE;
    cfg.pulseDark        = 80;
    cfg.pulseLight       = 5;
}

void saveCfg() {
    cfg.magic = CFG_MAGIC;
    EEPROM.put(sizeof(CalData), cfg);
}

bool loadCfg() {
    RuntimeCfg tmp;
    EEPROM.get(sizeof(CalData), tmp);
    if (tmp.magic != CFG_MAGIC) return false;
    cfg = tmp;
    return true;
}

// =============================================================
//  Summer
// =============================================================
void beep(uint16_t freq, uint16_t ms) {
    uint32_t halfPeriod = 500000UL / freq;
    uint32_t cycles     = (uint32_t)freq * ms / 1000;
    for (uint32_t i = 0; i < cycles; i++) {
        digitalWrite(PIN_BUZZER, HIGH);
        delayMicroseconds(halfPeriod);
        digitalWrite(PIN_BUZZER, LOW);
        delayMicroseconds(halfPeriod);
    }
}

void beepOK()    { beep(2000, 100); }
void beepLong()  { beep(1500, 400); }
void beepError() { beep(500,  800); }

// =============================================================
//  Taster (entprellt)
// =============================================================
static bool buttonDown() {
    return digitalRead(PIN_BUTTON) == LOW;
}

static bool buttonPressed() {
    if (!buttonDown()) return false;
    delay(50);
    if (!buttonDown()) return false;
    while (buttonDown()) delay(10);
    delay(50);
    return true;
}

// Wartet auf Taster ODER Enter. Gibt false bei 'q' (Abbruch).
static bool waitForInput() {
    Serial.println(F("  (Enter oder Taster, 'q' = Abbruch)"));
    while (true) {
        if (buttonPressed()) return true;
        if (Serial.available()) {
            char c = Serial.read();
            if (c == 'q' || c == 'Q') {
                while (Serial.available()) Serial.read();
                Serial.println(F("Abgebrochen."));
                return false;
            }
            if (c == '\n' || c == '\r') {
                while (Serial.available()) Serial.read();
                return true;
            }
        }
        delay(10);
    }
}

// Prueft ob Taster ODER Serial-Eingabe vorliegt (nicht-blockierend)
static bool inputAvailable() {
    if (buttonPressed()) return true;
    if (Serial.available()) {
        char c = Serial.peek();
        if (c == '\n' || c == '\r') {
            Serial.read();
            while (Serial.available() && (Serial.peek() == '\n' || Serial.peek() == '\r'))
                Serial.read();
            return true;
        }
    }
    return false;
}

// =============================================================
//  Standard-Kalibrierdaten (typische Buegelperlen-Farben)
// =============================================================
void calDefaults() {
    //                      H     S     V     R    G    B
    struct { int16_t h; uint16_t s,v,r,g,b; } def[] = {
        {   0,  100,  950, 240, 230, 220 }, // 0  Weiss
        {   0,  100,  100,  25,  25,  25 }, // 1  Schwarz
        {   0,  850,  800, 200,  30,  30 }, // 2  Rot
        { 220,  800,  700,  30,  50, 180 }, // 3  Blau
        {  50,  900,  900, 230, 210,  30 }, // 4  Gelb
        { 130,  750,  600,  30, 150,  40 }, // 5  Gruen
        {  25,  900,  850, 220, 120,  20 }, // 6  Orange
        { 330,  600,  800, 200, 100, 160 }, // 7  Pink
        { 280,  650,  550, 120,  50, 140 }, // 8  Lila
        {  20,  700,  400, 100,  50,  20 }, // 9  Braun
        { 195,  500,  800, 100, 180, 210 }, // 10 Hellblau
        { 110,  550,  750,  90, 190,  90 }, // 11 Hellgruen
        { 235,  850,  500,  20,  30, 130 }, // 12 Dunkelblau
        { 145,  800,  400,  20, 100,  30 }, // 13 Dunkelgruen
        {   0,  100,  500, 128, 125, 122 }, // 14 Grau
        {  35,  400,  800, 200, 180, 130 }, // 15 Beige
        {   0,   50,  900, 230, 225, 220 }, // 16 Transparent
    };
    const uint8_t n = sizeof(def) / sizeof(def[0]);
    cal.magic     = CAL_MAGIC;
    cal.numColors = n;
    cal.baseC     = 500;
    for (uint8_t i = 0; i < n; i++) {
        cal.h[i] = def[i].h;
        cal.s[i] = def[i].s;
        cal.v[i] = def[i].v;
        cal.r[i] = def[i].r;
        cal.g[i] = def[i].g;
        cal.b[i] = def[i].b;
    }
}

// =============================================================
//  EEPROM – Kalibrierdaten
// =============================================================
void saveCal() {
    cal.magic = CAL_MAGIC;
    EEPROM.put(0, cal);
}

bool loadCal() {
    EEPROM.get(0, cal);
    if (cal.magic != CAL_MAGIC) return false;
    if (cal.numColors == 0 || cal.numColors > MAX_COLORS)
        cal.numColors = MAX_COLORS;
    return true;
}

// =============================================================
//  Perlenerkennung
// =============================================================
bool beadDetected() {
    uint16_t c    = tcs_read_clear_fast();
    uint16_t diff = (c > cal.baseC) ? (c - cal.baseC) : (cal.baseC - c);
    if (debugMode) {
        Serial.print(F("  [det] C=")); Serial.print(c);
        Serial.print(F(" base=")); Serial.print(cal.baseC);
        Serial.print(F(" diff=")); Serial.print(diff);
        Serial.print(F(" thr=")); Serial.println(cfg.beadThreshold);
    }
    return diff > cfg.beadThreshold;
}

// =============================================================
//  Farblesung – Pulsdauer zu RGB zu HSV
// =============================================================
static void rgbToHsv(uint16_t r8, uint16_t g8, uint16_t b8,
                     int16_t* hue, uint16_t* sat, uint16_t* val) {
    int16_t maxC = r8; if (g8 > maxC) maxC = g8; if (b8 > maxC) maxC = b8;
    int16_t minC = r8; if (g8 < minC) minC = g8; if (b8 < minC) minC = b8;
    int16_t delta = maxC - minC;

    *val = (uint16_t)((int32_t)maxC * 1000 / 255);

    if (maxC == 0) { *sat = 0; *hue = 0; return; }
    *sat = (uint16_t)((int32_t)delta * 1000 / maxC);

    if (delta == 0) { *hue = 0; return; }

    int32_t h;
    if (maxC == (int16_t)r8)
        h = 60L * ((int32_t)g8 - b8) / delta;
    else if (maxC == (int16_t)g8)
        h = 120L + 60L * ((int32_t)b8 - r8) / delta;
    else
        h = 240L + 60L * ((int32_t)r8 - g8) / delta;

    if (h < 0) h += 360;
    *hue = (int16_t)h;
}

void readColorHSV(int16_t* hue, uint16_t* sat, uint16_t* val,
                  uint16_t* outR, uint16_t* outG, uint16_t* outB) {
    int32_t sumR = 0, sumG = 0, sumB = 0;
    uint8_t valid = 0;

    for (int s = 0; s < cfg.colorReadSamples; s++) {
        RGB raw = tcs_read();
        if (debugMode) {
            Serial.print(F("  [raw] R=")); Serial.print(raw.r);
            Serial.print(F(" G=")); Serial.print(raw.g);
            Serial.print(F(" B=")); Serial.print(raw.b);
            Serial.print(F(" C=")); Serial.println(raw.c);
        }
        if (raw.r == 0 && raw.g == 0 && raw.b == 0) continue;
        sumR += raw.r;
        sumG += raw.g;
        sumB += raw.b;
        valid++;
    }

    if (valid == 0) {
        *hue = 0; *sat = 0; *val = 0;
        *outR = 0; *outG = 0; *outB = 0;
        return;
    }

    uint16_t avgR = sumR / valid;
    uint16_t avgG = sumG / valid;
    uint16_t avgB = sumB / valid;

    uint16_t r8 = constrain(map(avgR, cfg.pulseDark, cfg.pulseLight, 0, 255), 0, 255);
    uint16_t g8 = constrain(map(avgG, cfg.pulseDark, cfg.pulseLight, 0, 255), 0, 255);
    uint16_t b8 = constrain(map(avgB, cfg.pulseDark, cfg.pulseLight, 0, 255), 0, 255);

    *outR = r8;
    *outG = g8;
    *outB = b8;

    rgbToHsv(r8, g8, b8, hue, sat, val);

    if (debugMode) {
        Serial.print(F("  [hsv] R=")); Serial.print(r8);
        Serial.print(F(" G=")); Serial.print(g8);
        Serial.print(F(" B=")); Serial.print(b8);
        Serial.print(F(" H=")); Serial.print(*hue);
        Serial.print(F(" S=")); Serial.print(*sat);
        Serial.print(F(" V=")); Serial.println(*val);
    }
}

// =============================================================
//  Farbklassifikation – HSV-Distanz
// =============================================================
uint8_t classifyColor() {
    int16_t  hue;
    uint16_t sat, val, r8, g8, b8;
    readColorHSV(&hue, &sat, &val, &r8, &g8, &b8);

    if (r8 == 0 && g8 == 0 && b8 == 0) return cal.numColors;

    uint32_t minDist = UINT32_MAX;
    uint8_t  bestBin = cal.numColors;

    for (uint8_t i = 0; i < cal.numColors; i++) {
        // Hue-Distanz mit Wraparound (0-360)
        int32_t dh = (int32_t)hue - cal.h[i];
        if (dh > 180) dh -= 360;
        if (dh < -180) dh += 360;
        int32_t ds = (int32_t)sat - cal.s[i];
        int32_t dv = (int32_t)val - cal.v[i];

        // Gewichtung: Hue x4, Sat x2, Val x1
        uint32_t dist = (uint32_t)(dh * dh * 4 + ds * ds * 2 + dv * dv);

        if (debugMode) {
            Serial.print(F("  [cls] ")); Serial.print(colorNames[i]);
            Serial.print(F(" dH=")); Serial.print(dh);
            Serial.print(F(" dS=")); Serial.print(ds);
            Serial.print(F(" dV=")); Serial.print(dv);
            Serial.print(F(" d=")); Serial.println(dist);
        }
        if (dist < minDist) {
            minDist = dist;
            bestBin = i;
        }
    }

    if (debugMode) {
        Serial.print(F("  [cls] H=")); Serial.print(hue);
        Serial.print(F(" S=")); Serial.print(sat);
        Serial.print(F(" V=")); Serial.print(val);
        Serial.print(F(" -> "));
        Serial.print(bestBin < MAX_COLORS ? colorNames[bestBin] : "Unbekannt");
        Serial.print(F(" dist=")); Serial.println(minDist);
    }

    if (minDist > cfg.maxColorDist) return cal.numColors;
    return bestBin;
}

// =============================================================
//  Sortierer
// =============================================================
void moveToBin(uint8_t bin) {
    int32_t target = (int32_t)bin * STEPS_PER_BIN;
    int32_t diff   = target - sortPos;
    stepper_move(PIN_SORT_STEP, PIN_SORT_DIR, diff, cfg.sortStepUs);
    sortPos = target;
}

// =============================================================
//  Homing
// =============================================================
bool homeSorter() {
    Serial.println(F("Homing Sortierer..."));
    if (!stepper_home(PIN_SORT_STEP, PIN_SORT_DIR, PIN_SORT_HOME, cfg.homeStepUs)) {
        Serial.println(F("FEHLER: Home nicht gefunden!"));
        return false;
    }
    sortPos = 0;
    Serial.println(F("Home OK"));
    return true;
}

// =============================================================
//  Baseline
// =============================================================
void measureBaseline() {
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) {
        sum += tcs_read_clear();
    }
    cal.baseC = sum / 8;
    Serial.print(F("Baseline Clear: "));
    Serial.println(cal.baseC);
}

// =============================================================
//  Feeder exakt ein Fach weiterdrehen (ohne Rundungsdrift)
// =============================================================
static void feedAdvance() {
    int32_t target = (int32_t)((uint32_t)(feedPos + 1) * STEPS_PER_REV / 6);
    int32_t current = (int32_t)((uint32_t)feedPos * STEPS_PER_REV / 6);
    int32_t steps = target - current;
    stepper_move(PIN_FEED_STEP, PIN_FEED_DIR, steps, cfg.feedStepUs);
    feedPos++;
    if (feedPos >= 6) feedPos = 0;
}

// =============================================================
//  Eine Perle sortieren
// =============================================================
void sortOneBead() {
    stepper_enable(true);

    feedAdvance();
    delay(200);

    uint8_t bin = classifyColor();

    Serial.print(F("Perle #")); Serial.print(beadCount);
    Serial.print(F(" -> "));
    Serial.print(bin < MAX_COLORS ? colorNames[bin] : "Unbekannt");
    Serial.print(F(" (Fach ")); Serial.print(bin);
    Serial.println(F(")"));

    moveToBin(bin);
    delay(50);

    beadCount++;
    beepOK();

    if (cfg.rehomeInterval > 0 && beadCount % cfg.rehomeInterval == 0) {
        homeSorter();
    }
}

// =============================================================
//  Kalibrierung
// =============================================================
void calibrateColor(uint8_t idx) {
    if (idx >= MAX_COLORS) {
        Serial.print(F("Ungueltig! Max ")); Serial.println(MAX_COLORS - 1);
        return;
    }

    Serial.print(F("\r\nKalibriere Farbe ")); Serial.print(idx);
    Serial.print(F(" ("));
    Serial.print(colorNames[idx]);
    Serial.println(F(")"));
    Serial.println(F("Perle einlegen, Enter druecken."));
    if (!waitForInput()) return;

    stepper_enable(true);
    Serial.println(F("Fahre zum Sensor..."));
    feedAdvance();

    delay(200);
    readColorHSV(&cal.h[idx], &cal.s[idx], &cal.v[idx],
                 &cal.r[idx], &cal.g[idx], &cal.b[idx]);

    Serial.print(F("  RGB=")); Serial.print(cal.r[idx]);
    Serial.print(F(",")); Serial.print(cal.g[idx]);
    Serial.print(F(",")); Serial.print(cal.b[idx]);
    Serial.print(F("  H=")); Serial.print(cal.h[idx]);
    Serial.print(F(" S=")); Serial.print(cal.s[idx]);
    Serial.print(F(" V=")); Serial.println(cal.v[idx]);

    if (idx >= cal.numColors) cal.numColors = idx + 1;
    saveCal();
    calibrated = true;
    beepOK();
    Serial.print(F("Farbe ")); Serial.print(idx);
    Serial.println(F(" gespeichert."));
    Serial.print(F("Kalibrierte Farben: ")); Serial.println(cal.numColors);
}

void runCalibration() {
    Serial.println(F("\r\n=== KALIBRIERUNG ==="));
    Serial.println(F("Wie viele Farben? (1-18, Enter = bisherige beibehalten)"));

    char numBuf[8];
    uint8_t numLen = 0;
    while (true) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == 'q' || c == 'Q') return;
            if (c == '\r' || c == '\n') break;
            if (c >= '0' && c <= '9' && numLen < 4) {
                numBuf[numLen++] = c;
                Serial.print(c);
            }
        }
    }
    numBuf[numLen] = '\0';
    Serial.println();

    uint8_t count = 0;
    if (numLen > 0) {
        count = atoi(numBuf);
        if (count > MAX_COLORS) count = MAX_COLORS;
        if (count == 0) count = 1;
    } else {
        count = cal.numColors > 0 ? cal.numColors : MAX_COLORS;
    }

    Serial.print(F("Kalibriere ")); Serial.print(count);
    Serial.println(F(" Farben.\r\n"));

    measureBaseline();
    stepper_enable(true);
    beepOK();

    for (uint8_t i = 0; i < count; i++) {
        Serial.print(F("\r\nFarbe ")); Serial.print(i);
        Serial.print(F(" / ")); Serial.print(count);
        Serial.print(F(" ("));
        Serial.print(colorNames[i]);
        Serial.println(F(")"));
        Serial.println(F("Perle einlegen, Enter = weiter, 'q' = fertig"));
        if (!waitForInput()) {
            Serial.println(F("Abgebrochen."));
            break;
        }

        Serial.println(F("Fahre zum Sensor..."));
        feedAdvance();

        delay(200);
        readColorHSV(&cal.h[i], &cal.s[i], &cal.v[i],
                     &cal.r[i], &cal.g[i], &cal.b[i]);

        Serial.print(F("  RGB=")); Serial.print(cal.r[i]);
        Serial.print(F(",")); Serial.print(cal.g[i]);
        Serial.print(F(",")); Serial.print(cal.b[i]);
        Serial.print(F("  H=")); Serial.print(cal.h[i]);
        Serial.print(F(" S=")); Serial.print(cal.s[i]);
        Serial.print(F(" V=")); Serial.println(cal.v[i]);
        beepOK();

        cal.numColors = i + 1;
    }

    saveCal();
    saveCfg();
    calibrated = true;
    Serial.print(F("\r\nKalibrierung gespeichert! Farben: "));
    Serial.println(cal.numColors);
    beepLong();
}

// =============================================================
//  Startup-Assistent
// =============================================================
void startupWizard() {
    Serial.println(F("\r\n--- HARDWARE-TEST ---\r\n"));

    // 1) Summer
    Serial.println(F("[1] Summer testen..."));
    beepOK();
    Serial.println(F("     Beep OK\r\n"));

    // 2) Farbsensor Rohwerte
    Serial.println(F("[2] Farbsensor Rohwerte:"));
    for (int i = 0; i < 3; i++) {
        RGB raw = tcs_read();
        Serial.print(F("     R=")); Serial.print(raw.r);
        Serial.print(F(" G=")); Serial.print(raw.g);
        Serial.print(F(" B=")); Serial.print(raw.b);
        Serial.print(F(" C=")); Serial.println(raw.c);
        delay(200);
    }

    // 3) Motoren
    Serial.println(F("\r\n[3] Motoren testen..."));
    stepper_enable(true);

    Serial.println(F("     Zufuehrrad 100 Schritte..."));
    stepper_move(PIN_FEED_STEP, PIN_FEED_DIR, 100, cfg.feedStepUs);
    delay(300);
    stepper_move(PIN_FEED_STEP, PIN_FEED_DIR, -100, cfg.feedStepUs);
    Serial.println(F("     OK"));

    Serial.println(F("     Sortierer 200 Schritte..."));
    stepper_move(PIN_SORT_STEP, PIN_SORT_DIR, 200, cfg.sortStepUs);
    delay(300);
    stepper_move(PIN_SORT_STEP, PIN_SORT_DIR, -200, cfg.sortStepUs);
    Serial.println(F("     OK"));

    // 4) Endstop
    Serial.print(F("\r\n[4] Lichtschranke: "));
    Serial.println(digitalRead(PIN_SORT_HOME) == HOME_ACTIVE_STATE
                   ? F("AUSGELOEST") : F("frei"));

    // 5) Homing
    Serial.println(F("\r\n[5] Homing..."));
    if (homeSorter()) {
        beepOK();
    } else {
        beepError();
        Serial.println(F("     Homing fehlgeschlagen!"));
        Serial.println(F("     Weiter mit Shell ('home' zum Wiederholen)"));
    }

    // 6) Baseline
    Serial.println(F("\r\n[6] Baseline messen..."));
    measureBaseline();

    Serial.println(F("\r\n--- TEST ABGESCHLOSSEN ---\r\n"));
}

// =============================================================
//  Setup
// =============================================================
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\r\n========================================"));
    Serial.println(F(" Buegelperlen-Sortiermaschine v2.0"));
    Serial.println(F(" 'help' fuer Befehle"));
    Serial.println(F("========================================"));

    pinMode(PIN_BUTTON,    INPUT_PULLUP);
    pinMode(PIN_BUZZER,    OUTPUT);
    pinMode(PIN_SORT_HOME, INPUT_PULLUP);

    stepper_init();
    tcs_init();

    cfgDefaults();
    bool fromEeprom = loadCal();
    if (!fromEeprom) calDefaults();
    calibrated = true;
    loadCfg();

    // Hardware-Test beim Start
    startupWizard();
    saveCal();

    Serial.print(fromEeprom ? F("Kalibrierdaten aus EEPROM") : F("Standard-Farben"));
    Serial.print(F(" geladen ("));
    Serial.print(cal.numColors);
    Serial.println(F(" Farben)."));
    Serial.println(F("'run' = Sortierung starten"));
    Serial.println(F("'cal' = Farben anpassen"));

    Serial.println(F("'help' = alle Befehle\r\n"));
    shell_init();
}

// =============================================================
//  Hauptschleife
// =============================================================
void loop() {
    shell_poll();

    if (!sorting) {
        if (buttonPressed() && calibrated) {
            sorting = true;
            stepper_enable(true);
            Serial.println(F("\r\nSortierung GESTARTET (Taster)"));
            beepOK();
        }
        return;
    }

    // --- Sortierbetrieb ---
    sortOneBead();

    if (buttonDown()) {
        delay(50);
        if (buttonDown()) {
            while (buttonDown()) delay(10);
            sorting = false;
            Serial.print(F("\r\nPAUSE – "));
            Serial.print(beadCount);
            Serial.println(F(" Perlen sortiert."));
            Serial.println(F("'run' oder Taster zum Weitermachen."));
            beepLong();
        }
    }
}

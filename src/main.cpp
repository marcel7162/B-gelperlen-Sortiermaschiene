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
//  Farblesung
// =============================================================
void readColorNorm(uint16_t* nr, uint16_t* ng, uint16_t* nb) {
    uint32_t sumR = 0, sumG = 0, sumB = 0;
    uint8_t  valid = 0;

    for (int s = 0; s < cfg.colorReadSamples; s++) {
        RGB raw = tcs_read();
        if (raw.c < MIN_CLEAR_READING) continue;
        sumR += (uint32_t)raw.r * 255 / raw.c;
        sumG += (uint32_t)raw.g * 255 / raw.c;
        sumB += (uint32_t)raw.b * 255 / raw.c;
        valid++;
    }

    if (valid == 0) {
        *nr = *ng = *nb = 0;
        return;
    }
    *nr = sumR / valid;
    *ng = sumG / valid;
    *nb = sumB / valid;
}

// =============================================================
//  Farbklassifikation
// =============================================================
uint8_t classifyColor() {
    uint16_t nr, ng, nb;
    readColorNorm(&nr, &ng, &nb);

    if (nr == 0 && ng == 0 && nb == 0) return cal.numColors;

    uint32_t minDist = UINT32_MAX;
    uint8_t  bestBin = cal.numColors;

    for (uint8_t i = 0; i < cal.numColors; i++) {
        int32_t dr = (int32_t)nr - cal.r[i];
        int32_t dg = (int32_t)ng - cal.g[i];
        int32_t db = (int32_t)nb - cal.b[i];
        uint32_t dist = (uint32_t)(dr * dr + dg * dg + db * db);
        if (debugMode) {
            Serial.print(F("  [cls] ")); Serial.print(colorNames[i]);
            Serial.print(F(" d=")); Serial.println(dist);
        }
        if (dist < minDist) {
            minDist = dist;
            bestBin = i;
        }
    }

    if (debugMode) {
        Serial.print(F("  [cls] nR=")); Serial.print(nr);
        Serial.print(F(" nG=")); Serial.print(ng);
        Serial.print(F(" nB=")); Serial.print(nb);
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
//  Eine Perle sortieren
// =============================================================
void sortOneBead() {
    Serial.println(F("Suche Perle..."));
    stepper_enable(true);

    bool found = false;
    for (int32_t s = 0; s < STEPS_PER_REV; s += cfg.feedPollSteps) {
        stepper_move(PIN_FEED_STEP, PIN_FEED_DIR, cfg.feedPollSteps, cfg.feedStepUs);
        if (beadDetected()) { found = true; break; }
    }

    if (!found) {
        Serial.println(F("Keine Perle gefunden."));
        return;
    }

    uint8_t bin = classifyColor();

    Serial.print(F("Perle #")); Serial.print(beadCount);
    Serial.print(F(" -> "));
    Serial.print(bin < MAX_COLORS ? colorNames[bin] : "Unbekannt");
    Serial.print(F(" (Fach ")); Serial.print(bin);
    Serial.println(F(")"));

    moveToBin(bin);
    delay(50);

    stepper_move(PIN_FEED_STEP, PIN_FEED_DIR, cfg.feedStepsPerBead, cfg.feedStepUs);
    delay(200);

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
    Serial.print(idx < MAX_COLORS ? colorNames[idx] : "?");
    Serial.println(F(")"));
    Serial.println(F("Perle auf Sensor legen, Enter druecken."));
    if (!waitForInput()) return;

    readColorNorm(&cal.r[idx], &cal.g[idx], &cal.b[idx]);

    Serial.print(F("  R=")); Serial.print(cal.r[idx]);
    Serial.print(F(" G=")); Serial.print(cal.g[idx]);
    Serial.print(F(" B=")); Serial.println(cal.b[idx]);

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
    beepOK();

    for (uint8_t i = 0; i < count; i++) {
        Serial.print(F("\r\nFarbe ")); Serial.print(i);
        Serial.print(F(" / ")); Serial.print(count);
        Serial.print(F(" ("));
        Serial.print(colorNames[i]);
        Serial.println(F(")"));
        Serial.println(F("Perle auf Sensor legen, Enter = messen, 'q' = fertig"));
        if (!waitForInput()) {
            Serial.println(F("Abgebrochen."));
            break;
        }

        readColorNorm(&cal.r[i], &cal.g[i], &cal.b[i]);

        Serial.print(F("  R=")); Serial.print(cal.r[i]);
        Serial.print(F(" G=")); Serial.print(cal.g[i]);
        Serial.print(F(" B=")); Serial.println(cal.b[i]);
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
    calibrated = loadCal();
    loadCfg();

    // Hardware-Test beim Start
    startupWizard();
    saveCal();

    if (calibrated) {
        Serial.println(F("Kalibrierdaten aus EEPROM geladen."));
        Serial.println(F("'run' = Sortierung starten"));
        Serial.println(F("'cal' = neu kalibrieren"));
    } else {
        Serial.println(F("Keine Kalibrierdaten vorhanden."));
        Serial.println(F("'cal' = Kalibrierung starten"));
    }

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
    stepper_move(PIN_FEED_STEP, PIN_FEED_DIR, cfg.feedPollSteps, cfg.feedStepUs);

    if (beadDetected()) {
        uint8_t bin = classifyColor();

        Serial.print(F("\r\nPerle #")); Serial.print(beadCount);
        Serial.print(F(" -> "));
        Serial.print(bin < MAX_COLORS ? colorNames[bin] : "Unbekannt");
        Serial.print(F(" (Fach ")); Serial.print(bin);
        Serial.println(F(")"));

        moveToBin(bin);
        delay(50);

        stepper_move(PIN_FEED_STEP, PIN_FEED_DIR, cfg.feedStepsPerBead, cfg.feedStepUs);
        delay(200);

        beadCount++;
        beepOK();

        if (cfg.rehomeInterval > 0 && beadCount % cfg.rehomeInterval == 0) {
            homeSorter();
        }
    }

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

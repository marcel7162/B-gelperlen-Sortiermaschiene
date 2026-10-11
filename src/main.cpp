#include <Arduino.h>
#include <EEPROM.h>
#include "config.h"
#include "stepper.h"
#include "color_sensor.h"

// =============================================================
//  Kalibrierdaten
// =============================================================
struct CalData {
    uint16_t magic;
    uint16_t baseC;                             // Baseline Clear (ohne Perle)
    uint16_t r[MAX_COLORS];                     // Normalisiertes R pro Farbe
    uint16_t g[MAX_COLORS];                     // Normalisiertes G
    uint16_t b[MAX_COLORS];                     // Normalisiertes B
};

static const char* colorNames[] = {
    "Weiss",     "Schwarz",    "Rot",        "Blau",
    "Gelb",      "Gruen",      "Orange",     "Pink",
    "Lila",      "Braun",      "Hellblau",   "Hellgruen",
    "Dunkelblau","Dunkelgruen","Grau",       "Beige",
    "Transparent","Unbekannt"
};

static CalData  cal;
static bool     calibrated = false;
static int32_t  sortPos    = 0;
static uint32_t beadCount  = 0;

// =============================================================
//  Summer
// =============================================================
static void beep(uint16_t freq, uint16_t ms) {
    uint32_t halfPeriod = 500000UL / freq;
    uint32_t cycles     = (uint32_t)freq * ms / 1000;
    for (uint32_t i = 0; i < cycles; i++) {
        digitalWrite(PIN_BUZZER, HIGH);
        delayMicroseconds(halfPeriod);
        digitalWrite(PIN_BUZZER, LOW);
        delayMicroseconds(halfPeriod);
    }
}

static void beepOK()    { beep(2000, 100); }
static void beepLong()  { beep(1500, 400); }
static void beepError() { beep(500,  800); }

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

// =============================================================
//  EEPROM – Kalibrierdaten speichern / laden
// =============================================================
static void saveCal() {
    cal.magic = CAL_MAGIC;
    EEPROM.put(0, cal);
}

static bool loadCal() {
    EEPROM.get(0, cal);
    return cal.magic == CAL_MAGIC;
}

// =============================================================
//  Perlenerkennung
// =============================================================
static bool beadDetected() {
    uint16_t c    = tcs_read_clear_fast();
    uint16_t diff = (c > cal.baseC) ? (c - cal.baseC) : (cal.baseC - c);
    return diff > BEAD_THRESHOLD;
}

// =============================================================
//  Farblesung – mehrfach messen und Mittelwert normalisieren
// =============================================================
static void readColorNorm(uint16_t* nr, uint16_t* ng, uint16_t* nb) {
    uint32_t sumR = 0, sumG = 0, sumB = 0;
    uint8_t  valid = 0;

    for (int s = 0; s < COLOR_READ_SAMPLES; s++) {
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
//  Farbklassifikation – naechster Nachbar (euklidisch)
// =============================================================
static uint8_t classifyColor() {
    uint16_t nr, ng, nb;
    readColorNorm(&nr, &ng, &nb);

    if (nr == 0 && ng == 0 && nb == 0) return MAX_COLORS - 1;

    uint32_t minDist  = UINT32_MAX;
    uint8_t  bestBin  = MAX_COLORS - 1;

    for (uint8_t i = 0; i < MAX_COLORS; i++) {
        int32_t dr = (int32_t)nr - cal.r[i];
        int32_t dg = (int32_t)ng - cal.g[i];
        int32_t db = (int32_t)nb - cal.b[i];
        uint32_t dist = (uint32_t)(dr * dr + dg * dg + db * db);
        if (dist < minDist) {
            minDist = dist;
            bestBin = i;
        }
    }

    if (minDist > MAX_COLOR_DISTANCE) return MAX_COLORS - 1;
    return bestBin;
}

// =============================================================
//  Sortierer – direkt zum Fach fahren (270°-Bogen, kein Umlauf)
// =============================================================
static void moveToBin(uint8_t bin) {
    int32_t target = (int32_t)bin * STEPS_PER_BIN;
    int32_t diff   = target - sortPos;
    stepper_move(PIN_SORT_STEP, PIN_SORT_DIR, diff, SORT_STEP_US);
    sortPos = target;
}

// =============================================================
//  Homing
// =============================================================
static bool homeSorter() {
    Serial.println(F("Homing Sortierer..."));
    if (!stepper_home(PIN_SORT_STEP, PIN_SORT_DIR, PIN_SORT_HOME, HOME_STEP_US)) {
        Serial.println(F("FEHLER: Home nicht gefunden!"));
        return false;
    }
    sortPos = 0;
    Serial.println(F("Home OK"));
    return true;
}

// =============================================================
//  Baseline messen (ohne Perle)
// =============================================================
static void measureBaseline() {
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) {
        sum += tcs_read_clear();
    }
    cal.baseC = sum / 8;
    Serial.print(F("Baseline Clear: "));
    Serial.println(cal.baseC);
}

// =============================================================
//  Kalibrierung
//  – Taster beim Einschalten halten → Kalibrierung starten
//  – 17 Perlen in Reihenfolge (Farbe 0-16) ins Rad legen
//  – Taster druecken → Maschine liest jede Perle automatisch
// =============================================================
static void runCalibration() {
    Serial.println(F("=== KALIBRIERUNG ==="));
    beepOK(); delay(150); beepOK(); delay(150); beepOK();

    // Baseline messen
    delay(500);
    measureBaseline();
    beepLong();

    Serial.println(F("17 Perlen einlegen, dann Taster druecken."));
    Serial.println(F("Reihenfolge:"));
    for (uint8_t i = 0; i < MAX_COLORS; i++) {
        Serial.print(F("  Fach "));
        Serial.print(i);
        Serial.print(F(": "));
        Serial.println(colorNames[i]);
    }

    while (!buttonPressed()) delay(10);
    beepOK();

    stepper_enable(true);
    for (uint8_t i = 0; i < MAX_COLORS; i++) {
        Serial.print(F("Warte auf Perle "));
        Serial.print(i);
        Serial.print(F(" ("));
        Serial.print(colorNames[i]);
        Serial.println(F(")..."));

        // Rad drehen bis Perle erkannt
        bool found = false;
        for (int32_t s = 0; s < STEPS_PER_REV; s++) {
            stepper_move(PIN_FEED_STEP, PIN_FEED_DIR, 1, FEED_STEP_US);
            if (beadDetected()) { found = true; break; }
        }

        if (!found) {
            Serial.println(F("  Keine Perle gefunden – nochmal!"));
            beepError();
            i--;
            continue;
        }

        // Farbe messen
        readColorNorm(&cal.r[i], &cal.g[i], &cal.b[i]);

        Serial.print(F("  R="));  Serial.print(cal.r[i]);
        Serial.print(F(" G="));   Serial.print(cal.g[i]);
        Serial.print(F(" B="));   Serial.println(cal.b[i]);
        beepOK();

        // Perle weiterfoerdern (faellt durch)
        stepper_move(PIN_FEED_STEP, PIN_FEED_DIR, FEED_STEPS_PER_BEAD, FEED_STEP_US);
        delay(300);
    }

    saveCal();
    calibrated = true;
    Serial.println(F("Kalibrierung gespeichert!"));
    beepLong(); delay(200); beepLong(); delay(200); beepLong();
}

// =============================================================
//  Setup
// =============================================================
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("Buegelperlen-Sortiermaschine v1.0"));

    pinMode(PIN_BUTTON,    INPUT_PULLUP);
    pinMode(PIN_BUZZER,    OUTPUT);
    pinMode(PIN_SORT_HOME, INPUT_PULLUP);
    pinMode(PIN_FEED_HOME, INPUT_PULLUP);

    stepper_init();
    tcs_init();

    beepOK();
    delay(500);

    // Taster beim Start gehalten → Kalibrierung
    bool wantCal = buttonDown();
    if (wantCal) {
        while (buttonDown()) delay(10);
    }

    calibrated = loadCal();

    if (wantCal || !calibrated) {
        runCalibration();
    }

    stepper_enable(true);

    if (!homeSorter()) {
        beepError(); beepError();
        while (true) delay(1000);
    }

    // Frische Baseline
    measureBaseline();
    saveCal();

    beepOK(); delay(100); beepOK();
    Serial.println(F("=== SORTIERUNG GESTARTET ==="));
}

// =============================================================
//  Hauptschleife – Perlen sortieren
// =============================================================
void loop() {
    // Rad ein Stueck drehen
    stepper_move(PIN_FEED_STEP, PIN_FEED_DIR, FEED_POLL_STEPS, FEED_STEP_US);

    if (beadDetected()) {
        uint8_t bin = classifyColor();

        Serial.print(F("Perle #"));    Serial.print(beadCount);
        Serial.print(F(" -> "));       Serial.print(colorNames[bin]);
        Serial.print(F(" (Fach "));    Serial.print(bin);
        Serial.println(F(")"));

        moveToBin(bin);
        delay(50);

        // Perle weiterfoerdern → faellt in den Trichter
        stepper_move(PIN_FEED_STEP, PIN_FEED_DIR, FEED_STEPS_PER_BEAD, FEED_STEP_US);
        delay(200);

        beadCount++;
        beepOK();

        if (beadCount % REHOME_INTERVAL == 0) {
            homeSorter();
        }
    }

    // Taster → Pause
    if (buttonDown()) {
        delay(50);
        if (buttonDown()) {
            while (buttonDown()) delay(10);
            Serial.print(F("PAUSE – "));
            Serial.print(beadCount);
            Serial.println(F(" Perlen sortiert."));
            beepLong();

            // Warten auf naechsten Tastendruck
            while (!buttonPressed()) delay(10);
            Serial.println(F("Weiter..."));
            beepOK();
        }
    }
}

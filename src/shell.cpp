#include <Arduino.h>
#include <string.h>
#include <stdlib.h>
#include "shell.h"
#include "machine.h"
#include "stepper.h"
#include "color_sensor.h"

static char    cmdBuf[80];
static uint8_t cmdLen = 0;

// ---------------------------------------------------------------
static void printHelp() {
    Serial.println(F(
        "\r\n=== BEFEHLE ===\r\n"
        "status          Maschinenstatus\r\n"
        "pins            Pin-Belegung\r\n"
        "\r\n--- Motor ---\r\n"
        "home            Sortierer homen\r\n"
        "move <n>        Sortierer n Schritte (+/-)\r\n"
        "bin <n>         Zu Fach 0-17 fahren\r\n"
        "feed <n>        Zufuehrrad n Schritte\r\n"
        "on              Motoren ein\r\n"
        "off             Motoren aus\r\n"
        "\r\n--- Sensor ---\r\n"
        "color           Farbe messen + klassifizieren\r\n"
        "raw             Rohwerte R/G/B/C\r\n"
        "clear           Clear-Kanal\r\n"
        "baseline        Baseline messen\r\n"
        "detect          Perle vorhanden?\r\n"
        "scan            Dauermessung (Enter stoppt)\r\n"
        "\r\n--- Kalibrierung ---\r\n"
        "cal             Kalibrierung starten\r\n"
        "dump            Kalibrierdaten anzeigen\r\n"
        "save            Einstellungen speichern\r\n"
        "load            Einstellungen laden\r\n"
        "reset           Standardwerte laden\r\n"
        "\r\n--- Test ---\r\n"
        "test            Alle Hardware-Tests\r\n"
        "test buzzer     Summer\r\n"
        "test sensor     Farbsensor Rohwerte\r\n"
        "test feed       Zufuehrrad vor/zurueck\r\n"
        "test sort       Sortierer vor/zurueck\r\n"
        "test endstop    Lichtschranke Status\r\n"
        "test baseline   Baseline messen\r\n"
        "\r\n--- Betrieb ---\r\n"
        "run             Sortierung starten\r\n"
        "stop            Sortierung stoppen\r\n"
        "sort            Eine Perle sortieren\r\n"
        "rainbow         Alle Faecher anfahren\r\n"
        "beep            Summer testen\r\n"
        "debug           Debug-Modus umschalten\r\n"
        "\r\n--- Konfiguration ---\r\n"
        "get             Alle Parameter anzeigen\r\n"
        "set <p> <w>     Parameter setzen\r\n"
        "  threshold feedsteps pollsteps rehome\r\n"
        "  sortspeed feedspeed homespeed samples\r\n"
        "  samplems maxdist"
    ));
}

// ---------------------------------------------------------------
static void printStatus() {
    Serial.println(F("\r\n=== STATUS ==="));
    Serial.print(F("Sortierung: "));
    Serial.println(sorting ? F("LAEUFT") : F("GESTOPPT"));
    Serial.print(F("Kalibriert: "));
    Serial.println(calibrated ? F("Ja") : F("Nein"));
    Serial.print(F("Perlen:     "));
    Serial.println(beadCount);
    Serial.print(F("Sort-Pos:   "));
    Serial.print(sortPos);
    Serial.print(F(" Schritte (Fach ~"));
    Serial.print(sortPos / STEPS_PER_BIN);
    Serial.println(F(")"));
    Serial.print(F("Baseline:   "));
    Serial.println(cal.baseC);
    Serial.print(F("Debug:      "));
    Serial.println(debugMode ? F("Ein") : F("Aus"));
}

// ---------------------------------------------------------------
static void printPins() {
    Serial.println(F(
        "\r\n=== PIN-BELEGUNG ===\r\n"
        "Feed Step:    PB9   (X-Step)\r\n"
        "Feed Dir:     PC2   (X-Dir)\r\n"
        "Sort Step:    PB7   (Y-Step)\r\n"
        "Sort Dir:     PB8   (Y-Dir)\r\n"
        "Enable:       PC3   (X/Y-EN)\r\n"
        "Lichtschranke:PA4\r\n"
        "TCS S2:       PB12  (LCD)\r\n"
        "TCS S3:       PB15  (LCD)\r\n"
        "TCS OUT:      PB13  (LCD)\r\n"
        "Taster:       PB10  (LCD)\r\n"
        "Summer:       PB11  (LCD)\r\n"
        "UART TX:      PA9\r\n"
        "UART RX:      PA10"
    ));
}

// ---------------------------------------------------------------
static void printGet() {
    Serial.println(F("\r\n=== KONFIGURATION ==="));
    Serial.print(F("threshold   = ")); Serial.println(cfg.beadThreshold);
    Serial.print(F("feedsteps   = ")); Serial.println(cfg.feedStepsPerBead);
    Serial.print(F("pollsteps   = ")); Serial.println(cfg.feedPollSteps);
    Serial.print(F("rehome      = ")); Serial.println(cfg.rehomeInterval);
    Serial.print(F("sortspeed   = ")); Serial.print(cfg.sortStepUs);
    Serial.println(F(" us"));
    Serial.print(F("feedspeed   = ")); Serial.print(cfg.feedStepUs);
    Serial.println(F(" us"));
    Serial.print(F("homespeed   = ")); Serial.print(cfg.homeStepUs);
    Serial.println(F(" us"));
    Serial.print(F("samples     = ")); Serial.println(cfg.colorReadSamples);
    Serial.print(F("samplems    = ")); Serial.print(cfg.tcsSampleMs);
    Serial.println(F(" ms"));
    Serial.print(F("maxdist     = ")); Serial.println(cfg.maxColorDist);
}

// ---------------------------------------------------------------
static void cmdSet(char* args) {
    char* param = strtok(args, " ");
    char* valStr = strtok(NULL, " ");
    if (!param || !valStr) {
        Serial.println(F("Syntax: set <param> <wert>"));
        return;
    }
    long val = atol(valStr);

    if      (strcmp(param, "threshold") == 0)  cfg.beadThreshold    = val;
    else if (strcmp(param, "feedsteps") == 0)  cfg.feedStepsPerBead = val;
    else if (strcmp(param, "pollsteps") == 0)  cfg.feedPollSteps    = val;
    else if (strcmp(param, "rehome") == 0)     cfg.rehomeInterval   = val;
    else if (strcmp(param, "sortspeed") == 0)  cfg.sortStepUs       = val;
    else if (strcmp(param, "feedspeed") == 0)  cfg.feedStepUs       = val;
    else if (strcmp(param, "homespeed") == 0)  cfg.homeStepUs       = val;
    else if (strcmp(param, "samples") == 0)    cfg.colorReadSamples = val;
    else if (strcmp(param, "samplems") == 0)   cfg.tcsSampleMs      = val;
    else if (strcmp(param, "maxdist") == 0)    cfg.maxColorDist     = val;
    else {
        Serial.print(F("Unbekannt: ")); Serial.println(param);
        return;
    }
    Serial.print(param); Serial.print(F(" = ")); Serial.println(val);
}

// ---------------------------------------------------------------
static void cmdDump() {
    Serial.println(F("\r\n=== KALIBRIERDATEN ==="));
    Serial.print(F("Magic:    0x")); Serial.println(cal.magic, HEX);
    Serial.print(F("Baseline: ")); Serial.println(cal.baseC);
    Serial.println(F("\r\nFach  Farbe          R     G     B"));
    Serial.println(F("----  -------------- ----- ----- -----"));
    for (uint8_t i = 0; i < MAX_COLORS; i++) {
        if (i < 10) Serial.print(' ');
        Serial.print(i);
        Serial.print(F("    "));
        Serial.print(colorNames[i]);
        uint8_t pad = 15 - strlen(colorNames[i]);
        while (pad--) Serial.print(' ');
        Serial.print(cal.r[i]); Serial.print(F("   "));
        Serial.print(cal.g[i]); Serial.print(F("   "));
        Serial.println(cal.b[i]);
    }
}

// ---------------------------------------------------------------
static void cmdColor() {
    uint16_t nr, ng, nb;
    readColorNorm(&nr, &ng, &nb);
    uint8_t bin = classifyColor();

    Serial.print(F("Norm: R=")); Serial.print(nr);
    Serial.print(F(" G=")); Serial.print(ng);
    Serial.print(F(" B=")); Serial.println(nb);
    Serial.print(F("Farbe: ")); Serial.print(colorNames[bin]);
    Serial.print(F(" (Fach ")); Serial.print(bin);
    Serial.println(F(")"));
}

// ---------------------------------------------------------------
static void cmdRaw() {
    RGB raw = tcs_read();
    Serial.print(F("R=")); Serial.print(raw.r);
    Serial.print(F(" G=")); Serial.print(raw.g);
    Serial.print(F(" B=")); Serial.print(raw.b);
    Serial.print(F(" C=")); Serial.println(raw.c);
}

// ---------------------------------------------------------------
static void cmdScan() {
    Serial.println(F("Dauermessung – Enter zum Stoppen"));
    while (!Serial.available()) {
        RGB raw = tcs_read();
        uint16_t nr, ng, nb;
        readColorNorm(&nr, &ng, &nb);

        Serial.print(F("R=")); Serial.print(raw.r);
        Serial.print(F(" G=")); Serial.print(raw.g);
        Serial.print(F(" B=")); Serial.print(raw.b);
        Serial.print(F(" C=")); Serial.print(raw.c);
        Serial.print(F("  | nR=")); Serial.print(nr);
        Serial.print(F(" nG=")); Serial.print(ng);
        Serial.print(F(" nB=")); Serial.println(nb);
        delay(300);
    }
    while (Serial.available()) Serial.read();
    Serial.println(F("Gestoppt."));
}

// ---------------------------------------------------------------
static void cmdRainbow() {
    Serial.println(F("Fahre alle 18 Faecher an..."));
    stepper_enable(true);
    for (uint8_t i = 0; i < NUM_BINS; i++) {
        Serial.print(F("Fach ")); Serial.print(i);
        Serial.print(F(": ")); Serial.println(colorNames[i]);
        moveToBin(i);
        delay(500);
    }
    Serial.println(F("Zurueck zu Home..."));
    homeSorter();
    Serial.println(F("Fertig."));
}

// ---------------------------------------------------------------
static void processCommand(char* cmd) {
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') return;

    char* args = strchr(cmd, ' ');
    if (args) {
        *args = '\0';
        args++;
        while (*args == ' ') args++;
    }

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "hilfe") == 0 || strcmp(cmd, "?") == 0) {
        printHelp();
    }
    else if (strcmp(cmd, "status") == 0) {
        printStatus();
    }
    else if (strcmp(cmd, "pins") == 0) {
        printPins();
    }
    else if (strcmp(cmd, "home") == 0) {
        stepper_enable(true);
        if (homeSorter()) beepOK(); else beepError();
    }
    else if (strcmp(cmd, "move") == 0) {
        if (!args) { Serial.println(F("Syntax: move <schritte>")); return; }
        int32_t steps = atol(args);
        stepper_enable(true);
        stepper_move(PIN_SORT_STEP, PIN_SORT_DIR, steps, cfg.sortStepUs);
        sortPos += steps;
        Serial.print(F("Pos: ")); Serial.println(sortPos);
    }
    else if (strcmp(cmd, "bin") == 0) {
        if (!args) { Serial.println(F("Syntax: bin <0-17>")); return; }
        uint8_t b = atoi(args);
        if (b >= NUM_BINS) { Serial.println(F("Ungueltig (0-17)")); return; }
        stepper_enable(true);
        moveToBin(b);
        Serial.print(F("Fach ")); Serial.print(b);
        Serial.print(F(": ")); Serial.println(colorNames[b]);
    }
    else if (strcmp(cmd, "feed") == 0) {
        if (!args) { Serial.println(F("Syntax: feed <schritte>")); return; }
        int32_t steps = atol(args);
        stepper_enable(true);
        stepper_move(PIN_FEED_STEP, PIN_FEED_DIR, steps, cfg.feedStepUs);
        Serial.println(F("OK"));
    }
    else if (strcmp(cmd, "on") == 0) {
        stepper_enable(true);
        Serial.println(F("Motoren EIN"));
    }
    else if (strcmp(cmd, "off") == 0) {
        stepper_enable(false);
        Serial.println(F("Motoren AUS"));
    }
    else if (strcmp(cmd, "color") == 0) {
        cmdColor();
    }
    else if (strcmp(cmd, "raw") == 0) {
        cmdRaw();
    }
    else if (strcmp(cmd, "clear") == 0) {
        uint16_t c = tcs_read_clear();
        Serial.print(F("Clear: ")); Serial.println(c);
    }
    else if (strcmp(cmd, "baseline") == 0) {
        measureBaseline();
    }
    else if (strcmp(cmd, "detect") == 0) {
        Serial.println(beadDetected() ? F("Perle erkannt!") : F("Keine Perle."));
    }
    else if (strcmp(cmd, "scan") == 0) {
        cmdScan();
    }
    else if (strcmp(cmd, "cal") == 0) {
        sorting = false;
        runCalibration();
    }
    else if (strcmp(cmd, "dump") == 0) {
        cmdDump();
    }
    else if (strcmp(cmd, "save") == 0) {
        saveCal();
        saveCfg();
        Serial.println(F("Gespeichert."));
    }
    else if (strcmp(cmd, "load") == 0) {
        calibrated = loadCal();
        loadCfg();
        Serial.println(calibrated ? F("Kalibrierdaten geladen.") : F("Keine Kalibrierdaten."));
    }
    else if (strcmp(cmd, "reset") == 0) {
        cfgDefaults();
        Serial.println(F("Standardwerte geladen."));
        printGet();
    }
    else if (strcmp(cmd, "run") == 0) {
        if (!calibrated) {
            Serial.println(F("Nicht kalibriert! Erst 'cal' ausfuehren."));
            return;
        }
        sorting = true;
        stepper_enable(true);
        Serial.println(F("Sortierung GESTARTET"));
    }
    else if (strcmp(cmd, "stop") == 0) {
        sorting = false;
        Serial.println(F("Sortierung GESTOPPT"));
    }
    else if (strcmp(cmd, "sort") == 0) {
        stepper_enable(true);
        sortOneBead();
    }
    else if (strcmp(cmd, "test") == 0) {
        if (!args) {
            startupWizard();
        }
        else if (strcmp(args, "buzzer") == 0) {
            Serial.println(F("Summer-Test..."));
            beepOK(); delay(200); beepLong(); delay(200); beepError();
            Serial.println(F("OK"));
        }
        else if (strcmp(args, "sensor") == 0) {
            Serial.println(F("Farbsensor Rohwerte (5 Messungen):"));
            for (int i = 0; i < 5; i++) {
                RGB raw = tcs_read();
                Serial.print(F("  R=")); Serial.print(raw.r);
                Serial.print(F(" G=")); Serial.print(raw.g);
                Serial.print(F(" B=")); Serial.print(raw.b);
                Serial.print(F(" C=")); Serial.println(raw.c);
                delay(200);
            }
        }
        else if (strcmp(args, "feed") == 0) {
            Serial.println(F("Zufuehrrad: 200 vor, 200 zurueck..."));
            Serial.print(F("  Step=PB9 Dir=PC2 Speed=")); Serial.print(cfg.feedStepUs); Serial.println(F("us"));
            stepper_enable(true);
            stepper_move(PIN_FEED_STEP, PIN_FEED_DIR, 200, cfg.feedStepUs);
            delay(500);
            stepper_move(PIN_FEED_STEP, PIN_FEED_DIR, -200, cfg.feedStepUs);
            Serial.println(F("OK"));
        }
        else if (strcmp(args, "sort") == 0) {
            Serial.println(F("Sortierer: 400 vor, 400 zurueck..."));
            Serial.print(F("  Step=PB7 Dir=PB8 Speed=")); Serial.print(cfg.sortStepUs); Serial.println(F("us"));
            stepper_enable(true);
            stepper_move(PIN_SORT_STEP, PIN_SORT_DIR, 400, cfg.sortStepUs);
            delay(500);
            stepper_move(PIN_SORT_STEP, PIN_SORT_DIR, -400, cfg.sortStepUs);
            Serial.println(F("OK"));
        }
        else if (strcmp(args, "step") == 0) {
            Serial.println(F("Einzelschritt-Test (10 Schritte, sehr langsam):"));
            stepper_enable(true);
            Serial.println(F("  Feed (X):"));
            for (int i = 0; i < 10; i++) {
                digitalWrite(PIN_FEED_DIR, HIGH);
                digitalWrite(PIN_FEED_STEP, HIGH);
                delay(50);
                digitalWrite(PIN_FEED_STEP, LOW);
                delay(50);
                Serial.print(F("  .")); Serial.println(i);
            }
            Serial.println(F("  Sort (Y):"));
            for (int i = 0; i < 10; i++) {
                digitalWrite(PIN_SORT_DIR, HIGH);
                digitalWrite(PIN_SORT_STEP, HIGH);
                delay(50);
                digitalWrite(PIN_SORT_STEP, LOW);
                delay(50);
                Serial.print(F("  .")); Serial.println(i);
            }
            Serial.println(F("OK (20 Schritte total, 100ms pro Schritt)"));
        }
        else if (strcmp(args, "endstop") == 0) {
            Serial.print(F("Lichtschranke (PA4): "));
            bool state = digitalRead(PIN_SORT_HOME) == HOME_ACTIVE_STATE;
            Serial.println(state ? F("AUSGELOEST (Home)") : F("frei"));
            Serial.print(F("  Rohwert: ")); Serial.println(digitalRead(PIN_SORT_HOME));
            Serial.print(F("  Erwartet bei Home: ")); Serial.println(HOME_ACTIVE_STATE);
        }
        else if (strcmp(args, "baseline") == 0) {
            measureBaseline();
        }
        else if (strcmp(args, "enable") == 0) {
            Serial.print(F("Enable-Pin PC3 Zustand: "));
            Serial.println(digitalRead(PIN_FEED_EN));
            Serial.println(F("  0=Motoren EIN, 1=Motoren AUS"));
            Serial.println(F("Setze Enable EIN..."));
            stepper_enable(true);
            Serial.print(F("Enable-Pin PC3 jetzt: "));
            Serial.println(digitalRead(PIN_FEED_EN));
        }
        else {
            Serial.println(F("test [buzzer|sensor|feed|sort|endstop|baseline|enable]"));
        }
    }
    else if (strcmp(cmd, "rainbow") == 0) {
        cmdRainbow();
    }
    else if (strcmp(cmd, "beep") == 0) {
        Serial.println(F("Beep!"));
        beepOK(); delay(200); beepLong(); delay(200); beepError();
    }
    else if (strcmp(cmd, "debug") == 0) {
        debugMode = !debugMode;
        Serial.print(F("Debug: "));
        Serial.println(debugMode ? F("EIN") : F("AUS"));
    }
    else if (strcmp(cmd, "get") == 0) {
        printGet();
    }
    else if (strcmp(cmd, "set") == 0) {
        if (!args) { Serial.println(F("Syntax: set <param> <wert>")); return; }
        cmdSet(args);
    }
    else {
        Serial.print(F("Unbekannt: ")); Serial.println(cmd);
        Serial.println(F("'help' fuer Befehle"));
    }

    Serial.print(F("\r\n> "));
}

// ---------------------------------------------------------------
void shell_init() {
    Serial.print(F("\r\n> "));
}

void shell_poll() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (cmdLen > 0) {
                cmdBuf[cmdLen] = '\0';
                Serial.println();
                processCommand(cmdBuf);
                cmdLen = 0;
            }
        } else if (c == '\b' || c == 127) {
            if (cmdLen > 0) {
                cmdLen--;
                Serial.print(F("\b \b"));
            }
        } else if (cmdLen < sizeof(cmdBuf) - 1) {
            cmdBuf[cmdLen++] = c;
            Serial.print(c);
        }
    }
}

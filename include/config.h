#pragma once
#include <Arduino.h>

// =============================================================
//  Pin-Belegung – Creality Ender v4.2.7 Board (STM32F103RET6)
// =============================================================

// Motor 1: Zufuehrrad – X-Stepper-Port
#define PIN_FEED_STEP       PB9
#define PIN_FEED_DIR        PC2
#define PIN_FEED_EN         PC3

// Motor 2: Sortierer – Y-Stepper-Port
#define PIN_SORT_STEP       PB7
#define PIN_SORT_DIR        PB8
#define PIN_SORT_EN         PC3      // gemeinsamer Enable mit Motor 1

// Gabellichtschranke
// HOME_ACTIVE_STATE: LOW = Schlitz frei (Fahne draussen bei Home)
//                    HIGH = Schlitz blockiert (Fahne drin bei Home)
#define PIN_SORT_HOME       PA4      // Gabellichtschranke Sortierer Home
#define HOME_ACTIVE_STATE   HIGH     // Fahne blockiert Schlitz an Home-Position

// TCS3200 Farbsensor – LCD-Stecker (EXP1)
// S0/S1 am TCS3200-Modul direkt auf VCC loeten (100 % Frequenz)
// TCS3200 VCC → LCD 5V,  GND → LCD GND
#define PIN_TCS_S2          PB12     // LCD-Stecker
#define PIN_TCS_S3          PB15     // LCD-Stecker
#define PIN_TCS_OUT         PB13     // LCD-Stecker (5V-tolerant, EXTI-faehig)

// Taster – LCD-Stecker (interner Pullup per Software)
#define PIN_BUTTON          PB10     // LCD-Stecker

// Summer – LCD-Stecker
#define PIN_BUZZER          PB11     // LCD-Stecker

// Unbenutzte Treiber abschalten
#define PIN_ZE_EN           PA15     // Enable Z+E Treiber (nicht benutzt)

// =============================================================
//  Stepper-Konfiguration
// =============================================================
#define STEPS_PER_REV           3200 // 200 Vollschritte * 16 Mikroschritte
#define NUM_BINS                18
#define SORT_ARC_DEG            270  // Boxen verteilt auf 270°
#define SORT_ARC_STEPS          2400 // 3200 * 270 / 360
#define STEPS_PER_BIN           133  // 2400 / 18 ≈ 133

#define FEED_STEP_US            2000 // Schrittperiode Zufuehrrad  (langsam)
#define SORT_STEP_US            600  // Schrittperiode Sortierer   (schnell)
#define HOME_STEP_US            1000 // Schrittperiode beim Homen

// Zufuehrrad – Schritte pro Perlenposition (je nach Rad-Geometrie anpassen!)
#define FEED_STEPS_PER_BEAD     533  // 3200 / 6 Loecher

// Schritte zwischen Sensor-Abfragen waehrend der Suche
#define FEED_POLL_STEPS         5

// =============================================================
//  Farbsensor-Konfiguration
// =============================================================
#define TCS_SAMPLE_MS           50   // Messzeit pro Kanal (Farblesung)
#define TCS_FAST_MS             10   // Schnellmessung (Perlenerkennung)
#define TCS_SETTLE_US           200  // Einschwingzeit nach Kanalwechsel
#define BEAD_THRESHOLD          40   // Min. Clear-Differenz fuer Perlenerkennung
#define MIN_CLEAR_READING       10   // Unter diesem Wert keine Klassifikation
#define COLOR_READ_SAMPLES      5    // Messwerte zum Mitteln
#define MAX_COLOR_DISTANCE      8000 // Max. Distanz fuer Farbzuordnung

#define MAX_COLORS              NUM_BINS

// =============================================================
//  Kalibrierung – EEPROM
// =============================================================
#define CAL_MAGIC               0xBEAD

// Periodisches Re-Homing alle N Perlen
#define REHOME_INTERVAL         50

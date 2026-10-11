#pragma once
#include <Arduino.h>

// =============================================================
//  Pin-Belegung – Creality Ender v4.2.7 Board (STM32F103RET6)
// =============================================================

// Motor 1: Zufuehrrad – X-Stepper-Port
#define PIN_FEED_STEP       PC2
#define PIN_FEED_DIR        PB9
#define PIN_FEED_EN         PC3

// Motor 2: Sortierer – Y-Stepper-Port
#define PIN_SORT_STEP       PB8
#define PIN_SORT_DIR        PB7
#define PIN_SORT_EN         PC3      // gemeinsamer Enable mit Motor 1

// Gabellichtschranke (active LOW)
#define PIN_SORT_HOME       PC5      // X-Endstop-Stecker – Sortierer Home

// TCS3200 Farbsensor – alles ueber Stecker erreichbar
// S0/S1 am TCS3200-Modul direkt auf VCC loeten (100 % Frequenz)
#define PIN_TCS_S2          PC4      // Y-Endstop-Stecker  (Signal-Pin)
#define PIN_TCS_S3          PA1      // Thermistor-Stecker T_Bed (Signal-Pin)
#define PIN_TCS_OUT         PA7      // Z-Endstop-Stecker  (Signal-Pin)
// TCS3200 Stromversorgung: VCC + GND vom Z-Endstop-Stecker

// Taster – Thermistor-Stecker T0 (hat Board-Pullup)
#define PIN_BUTTON          PA0

// Summer (onboard)
#define PIN_BUZZER          PB0

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
#define FEED_STEPS_PER_BEAD     40

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

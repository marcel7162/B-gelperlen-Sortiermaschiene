#include "stepper.h"

void stepper_init() {
    pinMode(PIN_FEED_STEP, OUTPUT);
    pinMode(PIN_FEED_DIR,  OUTPUT);
    pinMode(PIN_FEED_EN,   OUTPUT);
    pinMode(PIN_SORT_STEP, OUTPUT);
    pinMode(PIN_SORT_DIR,  OUTPUT);

    // Unbenutzte Z/E-Treiber deaktivieren
    pinMode(PIN_ZE_EN, OUTPUT);
    digitalWrite(PIN_ZE_EN, HIGH);

    stepper_enable(false);
}

void stepper_enable(bool on) {
    // Active LOW: LOW = bestromt, HIGH = stromlos
    digitalWrite(PIN_FEED_EN, on ? LOW : HIGH);
}

void stepper_move(uint32_t stepPin, uint32_t dirPin, int32_t steps, uint16_t delayUs) {
    if (steps == 0) return;
    digitalWrite(dirPin, steps > 0 ? HIGH : LOW);
    int32_t n = abs(steps);
    for (int32_t i = 0; i < n; i++) {
        digitalWrite(stepPin, HIGH);
        delayMicroseconds(delayUs);
        digitalWrite(stepPin, LOW);
        delayMicroseconds(delayUs);
    }
}

bool stepper_home(uint32_t stepPin, uint32_t dirPin, uint32_t endstopPin, uint16_t delayUs) {
    // Falls schon am Endstop: zurueckfahren
    if (digitalRead(endstopPin) == LOW) {
        stepper_move(stepPin, dirPin, 200, delayUs);
    }

    // Richtung Endstop fahren (negativ)
    for (int32_t i = 0; i < STEPS_PER_REV * 2; i++) {
        if (digitalRead(endstopPin) == LOW) {
            // Leicht zurueck
            stepper_move(stepPin, dirPin, 80, delayUs);
            // Langsam wieder ran
            while (digitalRead(endstopPin) != LOW) {
                stepper_move(stepPin, dirPin, -1, delayUs * 2);
            }
            return true;
        }
        stepper_move(stepPin, dirPin, -1, delayUs);
    }
    return false;
}

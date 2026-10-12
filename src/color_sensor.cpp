#include "color_sensor.h"

void tcs_init() {
    pinMode(PIN_TCS_S2,  OUTPUT);
    pinMode(PIN_TCS_S3,  OUTPUT);
    pinMode(PIN_TCS_OUT, INPUT);

#ifdef PIN_TCS_S0
    pinMode(PIN_TCS_S0, OUTPUT);
    digitalWrite(PIN_TCS_S0, HIGH);
#endif
#ifdef PIN_TCS_S1
    pinMode(PIN_TCS_S1, OUTPUT);
    digitalWrite(PIN_TCS_S1, HIGH);
#endif
}

// S2/S3 Belegung:  Red=LL  Blue=LH  Clear=HL  Green=HH
// Gibt Pulsdauer in Mikrosekunden zurueck (kleiner = mehr Licht)
static uint16_t _readChannel(bool s2, bool s3) {
    digitalWrite(PIN_TCS_S2, s2);
    digitalWrite(PIN_TCS_S3, s3);
    delay(20);

    uint32_t sum = 0;
    uint8_t  valid = 0;

    for (uint8_t i = 0; i < 8; i++) {
        uint32_t pw = pulseIn(PIN_TCS_OUT, LOW, 100000UL);
        if (pw == 0) continue;
        sum += pw;
        valid++;
    }

    if (valid == 0) return 0;
    return (uint16_t)(sum / valid);
}

RGB tcs_read() {
    RGB out;
    out.r = _readChannel(false, false);
    out.b = _readChannel(false, true);
    out.c = _readChannel(true,  false);
    out.g = _readChannel(true,  true);
    return out;
}

uint16_t tcs_read_clear() {
    return _readChannel(true, false);
}

uint16_t tcs_read_clear_fast() {
    // Schnellmessung mit weniger Samples
    digitalWrite(PIN_TCS_S2, HIGH);
    digitalWrite(PIN_TCS_S3, LOW);
    delay(10);
    uint32_t pw = pulseIn(PIN_TCS_OUT, LOW, 100000UL);
    return (uint16_t)pw;
}

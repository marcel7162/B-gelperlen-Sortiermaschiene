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
static uint32_t _readChannel(bool s2, bool s3, uint16_t ms) {
    digitalWrite(PIN_TCS_S2, s2);
    digitalWrite(PIN_TCS_S3, s3);
    delayMicroseconds(TCS_SETTLE_US);

    uint32_t sum = 0;
    uint8_t  valid = 0;
    uint8_t  samples = (ms < 20) ? 4 : 10;

    for (uint8_t i = 0; i < samples; i++) {
        uint32_t pw = pulseIn(PIN_TCS_OUT, LOW, 100000UL);
        if (pw == 0) continue;
        sum += pw;
        valid++;
    }

    if (valid == 0) return 0;
    uint32_t avgPeriod = sum / valid;
    if (avgPeriod == 0) return 0;
    return 1000000UL / avgPeriod;
}

RGB tcs_read() {
    RGB out;
    out.r = _readChannel(false, false, TCS_SAMPLE_MS);
    out.b = _readChannel(false, true,  TCS_SAMPLE_MS);
    out.c = _readChannel(true,  false, TCS_SAMPLE_MS);
    out.g = _readChannel(true,  true,  TCS_SAMPLE_MS);
    return out;
}

uint16_t tcs_read_clear() {
    return _readChannel(true, false, TCS_SAMPLE_MS);
}

uint16_t tcs_read_clear_fast() {
    return _readChannel(true, false, TCS_FAST_MS);
}

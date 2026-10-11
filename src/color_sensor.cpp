#include "color_sensor.h"

static volatile uint32_t _pulseCount;
static volatile bool     _counting;

static void _tcsISR() {
    if (_counting) _pulseCount++;
}

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

    _counting = false;
    attachInterrupt(digitalPinToInterrupt(PIN_TCS_OUT), _tcsISR, RISING);
}

// S2/S3 Belegung:  Red=LL  Blue=LH  Clear=HL  Green=HH
static uint32_t _readChannel(bool s2, bool s3, uint16_t ms) {
    digitalWrite(PIN_TCS_S2, s2);
    digitalWrite(PIN_TCS_S3, s3);
    delayMicroseconds(TCS_SETTLE_US);

    noInterrupts();
    _pulseCount = 0;
    _counting   = true;
    interrupts();

    delay(ms);

    noInterrupts();
    _counting = false;
    uint32_t count = _pulseCount;
    interrupts();

    return count;
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

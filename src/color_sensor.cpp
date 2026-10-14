#include "color_sensor.h"
#include <Wire.h>

#define AS_ADDR  AS7341_ADDR

// AS7341 Register
#define REG_ENABLE    0x80
#define REG_ATIME     0x81
#define REG_ASTEP_L   0xCA
#define REG_ASTEP_H   0xCB
#define REG_CFG0      0xA9
#define REG_CFG1      0xAA
#define REG_CFG6      0xAF
#define REG_STATUS2   0xA3
#define REG_CH0_L     0x95
#define REG_CFG20     0xD6

static bool _ok = false;

static void writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(AS_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

static uint8_t readReg(uint8_t reg) {
    Wire.beginTransmission(AS_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)AS_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
}

static void writeSMUX(const uint8_t* cfg, uint8_t len) {
    writeReg(REG_CFG6, 0x10);
    for (uint8_t i = 0; i < len; i++) {
        Wire.beginTransmission(AS_ADDR);
        Wire.write(i);
        Wire.write(cfg[i]);
        Wire.endTransmission();
    }
    writeReg(REG_ENABLE, 0x11);
    delay(5);
}

// SMUX config for F1-F4 + Clear + NIR
static const uint8_t smux_f1f4[] = {
    0x30, // 0x00: F3 left -> ADC2
    0x01, // 0x01: F1 left -> ADC0
    0x00, // 0x02: --
    0x00, // 0x03: --
    0x00, // 0x04: --
    0x42, // 0x05: F8 right -> NIR
    0x00, // 0x06: --
    0x00, // 0x07: --
    0x50, // 0x08: F6 left -> ADC3 (not used here but needed)
    0x00, // 0x09: --
    0x00, // 0x0A: --
    0x00, // 0x0B: --
    0x20, // 0x0C: F2 right -> ADC1
    0x04, // 0x0D: F4 right -> Clear
    0x00, // 0x0E: --
    0x30, // 0x0F: F6 right -> ADC2 (not used)
    0x01, // 0x10: F1 right -> ADC0 (connect)
    0x50, // 0x11: CLEAR right -> ADC4
    0x00, // 0x12: --
    0x06, // 0x13: F5 left to ADC3, NIR right -> ADC5
};

// SMUX config for F5-F8 + Clear + NIR
static const uint8_t smux_f5f8[] = {
    0x00, // 0x00: --
    0x00, // 0x01: --
    0x00, // 0x02: --
    0x40, // 0x03: F8 left -> NIR (ADC4-related)
    0x02, // 0x04: F6 left -> ADC1
    0x00, // 0x05: --
    0x10, // 0x06: F5 left -> ADC0
    0x03, // 0x07: F7 left -> ADC2
    0x50, // 0x08: --
    0x10, // 0x09: --
    0x03, // 0x0A: --
    0x00, // 0x0B: --
    0x00, // 0x0C: --
    0x00, // 0x0D: --
    0x24, // 0x0E: F8 right -> ADC3, Clear
    0x00, // 0x0F: --
    0x00, // 0x10: --
    0x50, // 0x11: CLEAR right -> ADC4
    0x00, // 0x12: --
    0x06, // 0x13: NIR right -> ADC5
};

static bool waitMeasure() {
    for (uint16_t i = 0; i < 500; i++) {
        uint8_t st = readReg(REG_STATUS2);
        if (st & 0x40) return true;
        delay(2);
    }
    return false;
}

static void readChannels(uint16_t* out) {
    Wire.beginTransmission(AS_ADDR);
    Wire.write(REG_CH0_L);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)AS_ADDR, (uint8_t)12);
    for (uint8_t i = 0; i < 6; i++) {
        uint8_t lo = Wire.available() ? Wire.read() : 0;
        uint8_t hi = Wire.available() ? Wire.read() : 0;
        out[i] = ((uint16_t)hi << 8) | lo;
    }
}

bool color_sensor_init() {
    Wire.setSDA(PIN_I2C_SDA);
    Wire.setSCL(PIN_I2C_SCL);
    Wire.begin();
    Wire.setClock(400000);

    // Check chip ID
    uint8_t id = readReg(0x92);
    if ((id & 0xFC) != 0x24) {
        _ok = false;
        return false;
    }

    writeReg(REG_ENABLE, 0x01);
    delay(5);

    // ATIME=29, ASTEP=599 -> ~50ms integration
    writeReg(REG_ATIME, 29);
    writeReg(REG_ASTEP_L, 0x57); // 599 low
    writeReg(REG_ASTEP_H, 0x02); // 599 high

    // Gain x16
    writeReg(REG_CFG1, 0x04);

    _ok = true;
    return true;
}

bool color_sensor_ok() {
    return _ok;
}

SpectralData color_sensor_read() {
    SpectralData sd;
    memset(&sd, 0, sizeof(sd));
    if (!_ok) return sd;

    uint16_t raw[6];

    // Messung 1: F1, F2, F3, F4, Clear, NIR
    writeSMUX(smux_f1f4, sizeof(smux_f1f4));
    writeReg(REG_ENABLE, 0x03);
    if (waitMeasure()) {
        readChannels(raw);
        sd.ch[0] = raw[0]; // F1
        sd.ch[1] = raw[1]; // F2
        sd.ch[2] = raw[2]; // F3
        sd.ch[3] = raw[3]; // F4
        sd.ch[8] = raw[4]; // Clear
        sd.ch[9] = raw[5]; // NIR
    }
    writeReg(REG_ENABLE, 0x01);

    // Messung 2: F5, F6, F7, F8, Clear, NIR
    writeSMUX(smux_f5f8, sizeof(smux_f5f8));
    writeReg(REG_ENABLE, 0x03);
    if (waitMeasure()) {
        readChannels(raw);
        sd.ch[4] = raw[0]; // F5
        sd.ch[5] = raw[1]; // F6
        sd.ch[6] = raw[2]; // F7
        sd.ch[7] = raw[3]; // F8
    }
    writeReg(REG_ENABLE, 0x01);

    return sd;
}

RGB color_sensor_to_rgb(const SpectralData& sd) {
    RGB out;
    // F6(590nm)+F7(630nm)+F8(680nm) -> Rot
    // F3(480nm)+F4(515nm)+F5(555nm) -> Gruen
    // F1(415nm)+F2(445nm)+F3(480nm) -> Blau
    uint32_t rr = (uint32_t)sd.ch[5] + sd.ch[6] + sd.ch[7];
    uint32_t gg = (uint32_t)sd.ch[2] + sd.ch[3] + sd.ch[4];
    uint32_t bb = (uint32_t)sd.ch[0] + sd.ch[1] + sd.ch[2];
    uint32_t cc = sd.ch[8];

    uint32_t maxVal = rr;
    if (gg > maxVal) maxVal = gg;
    if (bb > maxVal) maxVal = bb;

    if (maxVal == 0) {
        out.r = out.g = out.b = out.c = 0;
        return out;
    }

    out.r = (uint16_t)(rr * 255 / maxVal);
    out.g = (uint16_t)(gg * 255 / maxVal);
    out.b = (uint16_t)(bb * 255 / maxVal);
    out.c = (uint16_t)(cc > 65535 ? 65535 : cc);
    return out;
}

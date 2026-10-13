#pragma once
#include "config.h"

struct CalData {
    uint16_t magic;
    uint16_t baseC;
    uint8_t  numColors;
    int16_t  h[MAX_COLORS];    // Hue 0-360 (oder negativ)
    uint16_t s[MAX_COLORS];    // Saturation 0-1000
    uint16_t v[MAX_COLORS];    // Value 0-1000
    uint16_t r[MAX_COLORS];    // RGB zur Anzeige (map 0-255)
    uint16_t g[MAX_COLORS];
    uint16_t b[MAX_COLORS];
};

struct RuntimeCfg {
    uint16_t magic;
    uint16_t beadThreshold;
    uint16_t feedStepsPerBead;
    uint16_t feedPollSteps;
    uint16_t rehomeInterval;
    uint16_t sortStepUs;
    uint16_t feedStepUs;
    uint16_t homeStepUs;
    uint8_t  colorReadSamples;
    uint16_t tcsSampleMs;
    uint16_t maxColorDist;
    uint16_t pulseDark;
    uint16_t pulseLight;
};

#define CFG_MAGIC 0xCF61

extern CalData     cal;
extern RuntimeCfg  cfg;
extern const char* colorNames[];
extern bool        calibrated;
extern bool        sorting;
extern bool        debugMode;
extern int32_t     sortPos;
extern uint32_t    beadCount;

void    beep(uint16_t freq, uint16_t ms);
void    beepOK();
void    beepLong();
void    beepError();
bool    beadDetected();
void    readColorHSV(int16_t* hue, uint16_t* sat, uint16_t* val,
                     uint16_t* outR, uint16_t* outG, uint16_t* outB);
uint8_t classifyColor();
void    moveToBin(uint8_t bin);
bool    homeSorter();
void    measureBaseline();
void    calDefaults();
void    saveCal();
bool    loadCal();
void    saveCfg();
bool    loadCfg();
void    cfgDefaults();
void    runCalibration();
void    calibrateColor(uint8_t idx);
void    sortOneBead();
void    startupWizard();

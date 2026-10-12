#pragma once
#include "config.h"

struct CalData {
    uint16_t magic;
    uint16_t baseC;
    uint8_t  numColors;
    uint16_t r[MAX_COLORS];
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
};

#define CFG_MAGIC 0xCF60

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
void    readColorNorm(uint16_t* nr, uint16_t* ng, uint16_t* nb);
uint8_t classifyColor();
void    moveToBin(uint8_t bin);
bool    homeSorter();
void    measureBaseline();
void    saveCal();
bool    loadCal();
void    saveCfg();
bool    loadCfg();
void    cfgDefaults();
void    runCalibration();
void    calibrateColor(uint8_t idx);
void    sortOneBead();
void    startupWizard();

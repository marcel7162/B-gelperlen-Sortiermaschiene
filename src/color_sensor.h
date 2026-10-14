#pragma once
#include "config.h"

struct SpectralData {
    uint16_t ch[10];
    // ch[0]=F1(415nm) ch[1]=F2(445nm) ch[2]=F3(480nm) ch[3]=F4(515nm)
    // ch[4]=F5(555nm) ch[5]=F6(590nm) ch[6]=F7(630nm) ch[7]=F8(680nm)
    // ch[8]=Clear     ch[9]=NIR
};

struct RGB {
    uint16_t r;
    uint16_t g;
    uint16_t b;
    uint16_t c;
};

bool     color_sensor_init();
bool     color_sensor_ok();
SpectralData color_sensor_read();
RGB      color_sensor_to_rgb(const SpectralData& sd);

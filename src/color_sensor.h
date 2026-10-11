#pragma once
#include "config.h"

struct RGB {
    uint16_t r;
    uint16_t g;
    uint16_t b;
    uint16_t c;   // Clear (ungefiltert)
};

void     tcs_init();
RGB      tcs_read();
uint16_t tcs_read_clear();
uint16_t tcs_read_clear_fast();

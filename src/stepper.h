#pragma once
#include "config.h"

void stepper_init();
void stepper_enable(bool on);
void stepper_move(uint32_t stepPin, uint32_t dirPin, int32_t steps, uint16_t delayUs);
bool stepper_home(uint32_t stepPin, uint32_t dirPin, uint32_t endstopPin, uint16_t delayUs);

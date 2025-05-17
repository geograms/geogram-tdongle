#pragma once

#include <Arduino.h>
#include <time.h>

void initTime();
void updateTime();
time_t getCurrentTimestamp();

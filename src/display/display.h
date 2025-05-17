#pragma once

#ifndef DISPLAY_H
#define DISPLAY_H

void initDisplay();       // Initializes TFT and LVGL
void updateDisplay();     // To be called inside loop()

void writeLog(const char* line);

#endif

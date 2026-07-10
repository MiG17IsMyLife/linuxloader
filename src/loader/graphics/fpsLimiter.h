#pragma once

#include <stdbool.h>

typedef struct
{
    long targetFrameTime;
    long frameStart;
    long frameEnd;
    long sleepTime;
    long frameOverhead;
} FpsLimit;

void initFpsLimiter();
long clockNow();
void fpsLimiter(FpsLimit *stats);
double calculateFps();
void frameTiming();

void fpsOverlayUpdateFps(double fps);
void fpsOverlayToggleVisibility(void);
bool fpsOverlayIsVisible(void);
void fpsOverlaySetPosition(int pos);
int fpsOverlayGetSlot(void);

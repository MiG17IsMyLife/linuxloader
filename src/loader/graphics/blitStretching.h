#pragma once
#include <stdbool.h>

typedef struct
{
    int W;
    int H;
    int X;
    int Y;
} Dest;

extern Dest dest;
extern int drawableW;
extern int drawableH;
extern int blitWidth;
extern int blitHeight;
extern int renderWidth;
extern int renderHeight;
extern int fboInitialized;
extern unsigned int fboId;
extern int gameIsOutrunChihiroMode;

void initBlitting();
void blitSetWidthandHeightSize();
int blitInitializeFbo();
void blitStretch();

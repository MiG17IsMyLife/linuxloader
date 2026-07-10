#pragma once

#include <stdbool.h>

#define MAX_OVERLAY_SLOTS 8

typedef enum
{
    OVERLAY_TOP_LEFT,
    OVERLAY_TOP_RIGHT,
    OVERLAY_BOTTOM_LEFT,
    OVERLAY_BOTTOM_RIGHT
} OverlayPosition;

void overlayInit(void);
void overlayRender(void);
void overlayDestroy(void);

int  overlayShowMessage(const char *text, int durationMs, OverlayPosition pos);
void overlayHideMessage(int slotIndex);
void overlayUpdateMessageText(int slotIndex, const char *text);
void overlaySetMessageVisible(int slotIndex, bool visible);
bool overlayIsMessageVisible(int slotIndex);

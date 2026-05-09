#pragma once

#include <stdint.h>
#include <stddef.h>

namespace PthreadBridge
{
    void initBridges();

    int bridgePthreadOnce(int *once_control, void (*init_routine)(void));
}

extern "C" int bridgeSchedGetPriorityMax(int policy);
extern "C" int bridgeSchedGetPriorityMin(int policy);

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace PthreadBridge
{
    void initBridges();

}

extern "C" int bridgeSchedGetPriorityMax(int policy);
extern "C" int bridgeSchedGetPriorityMin(int policy);

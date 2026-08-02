#include "graphicsBridge.hpp"

#include "../graphics/gluBridge.h"
#include "../graphics/pacloaderGraphics.h"
#include "../log/log.h"
#include "symbolResolver.hpp"

#define MAP(name, function) \
    SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(function))
#define MAP_WITH_ORIGINAL(name, function, original) \
    SymbolResolver::GetInstance().RegisterVTable( \
        name, reinterpret_cast<void *>(function), reinterpret_cast<void **>(original))

namespace GraphicsBridge
{
void initBridges()
{
    log_info("Initializing pacloader graphics bridges");
    MAP("gluPerspective", bridgeGluPerspective);
    MAP("gluLookAt", bridgeGluLookAt);
    MAP("gluOrtho2D", bridgeGluOrtho2D);
    MAP("gluErrorString", bridgeGluErrorString);
    MAP_WITH_ORIGINAL("cgCreateProgram", bridgeCgCreateProgram, &real_cgCreateProgram);
    MAP_WITH_ORIGINAL("cgGetProgramString", bridgeCgGetProgramString, &real_cgGetProgramString);
}
} // namespace GraphicsBridge

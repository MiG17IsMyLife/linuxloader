#include "configIni.h"

#include <stdio.h>

#include "config.h"
#include "../log/log.h"

int createDefaultIni(const char *filePath)
{
    FILE *file = fopen(filePath, "w");
    if (!file)
    {
        log_error("Could not create default INI file at %s", filePath);
        return -1;
    }

    EmulatorConfig defaults = {0};
    setDefaultValues(&defaults);

    fprintf(file, "# pacloader configuration (Namco System N2 / ES1)\n\n");
    fprintf(file, "[Platform]\nSYSTEM = N2\n\n");

    fprintf(file, "[NamcoN2]\n");
    fprintf(file, "DONGLE_ID = \"%s\"\n", defaults.namcoN2.dongleId);
    fprintf(file, "DONGLE_ID_2 = \"%s\"\n", defaults.namcoN2.dongleId2);
    fprintf(file, "DEBUG_MODE = %s\n", defaults.namcoN2.debugMode ? "true" : "false");
    fprintf(file, "FFB_ENABLED = %s\n\n", defaults.namcoN2.forceFeedbackEnabled ? "true" : "false");
    fprintf(file, "FFB_DIAGNOSTICS = %s\n\n",
            defaults.namcoN2.forceFeedbackDiagnostics ? "true" : "false");

    fprintf(file, "# YaCardEmu remains an external process.\n");
    fprintf(file, "YACARDEMU_ENABLED = %s\n", defaults.namcoN2.card.enabled ? "true" : "false");
    fprintf(file, "YACARDEMU_AUTOSTART = %s\n", defaults.namcoN2.card.autoStart ? "true" : "false");
    fprintf(file, "YACARDEMU_PATH = \"%s\"\n", defaults.namcoN2.card.executablePath);
    fprintf(file, "YACARDEMU_PIPE = \"%s\"\n", defaults.namcoN2.card.pipeName);
    fprintf(file, "YACARDEMU_API_HOST = \"%s\"\n", defaults.namcoN2.card.apiHost);
    fprintf(file, "YACARDEMU_API_PORT = %d\n", defaults.namcoN2.card.apiPort);
    fprintf(file, "YACARDEMU_CARD_NAME = \"%s\"\n", defaults.namcoN2.card.cardName);
    fprintf(file, "YACARDEMU_DIAGNOSTICS = %s\n\n",
            defaults.namcoN2.card.diagnostics ? "true" : "false");

    fprintf(file, "# Advanced cabinet calibration; defaults match the emulated N2 board.\n");
    fprintf(file, "STEERING_RAW_MIN = %d\nSTEERING_RAW_MAX = %d\n",
            defaults.namcoN2.steering.minimum, defaults.namcoN2.steering.maximum);
    fprintf(file, "ACCELERATOR_RAW_MIN = %d\nACCELERATOR_RAW_MAX = %d\n",
            defaults.namcoN2.accelerator.minimum, defaults.namcoN2.accelerator.maximum);
    fprintf(file, "BRAKE_RAW_MIN = %d\nBRAKE_RAW_MAX = %d\n\n",
            defaults.namcoN2.brake.minimum, defaults.namcoN2.brake.maximum);

    fprintf(file, "[Display]\nWIDTH = AUTO\nHEIGHT = AUTO\n");
    fprintf(file, "FULLSCREEN = %s\n", defaults.fullscreen ? "true" : "false");
    fprintf(file, "KEEP_ASPECT_RATIO = %s\n", defaults.keepAspectRatio ? "true" : "false");
    fprintf(file, "HIDE_CURSOR = %s\n\n", defaults.hideCursor ? "true" : "false");

    fprintf(file, "[Emulation]\nREGION = JP\nFREEPLAY = none\nEMULATE_JVS = true\n\n");
    fprintf(file, "[Graphics]\nFPS_LIMITER_ENABLED = %s\nFPS_TARGET = %.1f\n\n",
            defaults.fpsLimiter ? "true" : "false", defaults.fpsTarget);
    fprintf(file, "[System]\nDEBUG_MSGS = %s\n",
            defaults.showDebugMessages ? "true" : "false");

    fclose(file);
    printf("Created pacloader configuration: %s\n", filePath);
    return 1;
}

#include "cardControl.h"

#include <string.h>

#include "../../log/log.h"

static CardControlBackend activeBackend;
static int insertActive;

void cardControlSetBackend(const CardControlBackend *backend)
{
    memset(&activeBackend, 0, sizeof(activeBackend));
    insertActive = 0;
    if (backend)
        activeBackend = *backend;

    log_debug("Card control backend: %s",
              activeBackend.name ? activeBackend.name : "none");
}

CardControlActionResult cardControlSetInsertState(int active)
{
    if (!activeBackend.setInsertState)
        return CARD_CONTROL_NOT_HANDLED;

    const CardControlActionResult result = activeBackend.setInsertState(active ? 1 : 0);
    if (result != CARD_CONTROL_NOT_HANDLED)
        insertActive = active ? 1 : 0;
    if (result == CARD_CONTROL_HANDLED_ONE_SHOT)
        insertActive = 0;
    return result;
}

CardControlActionResult cardControlRequestEject(void)
{
    if (!activeBackend.requestEject)
        return CARD_CONTROL_NOT_HANDLED;
    return activeBackend.requestEject();
}

int cardControlIsInsertActive(void)
{
    return insertActive;
}

CardControlConnectionState cardControlGetConnectionState(void)
{
    if (!activeBackend.getConnectionState)
        return CARD_CONTROL_UNAVAILABLE;
    return activeBackend.getConnectionState();
}

int cardControlIsConnected(void)
{
    return cardControlGetConnectionState() == CARD_CONTROL_CONNECTED;
}

const char *cardControlGetConnectionText(void)
{
    if (activeBackend.getConnectionText)
        return activeBackend.getConnectionText();

    switch (cardControlGetConnectionState())
    {
        case CARD_CONTROL_CONNECTED: return "Connected";
        case CARD_CONTROL_DISCONNECTED: return "Disconnected";
        default: return "Unavailable";
    }
}

void cardControlLogDiagnostics(void)
{
    if (activeBackend.logDiagnostics)
    {
        activeBackend.logDiagnostics();
        return;
    }
    log_info("Card control status: backend=%s state=%s",
             activeBackend.name ? activeBackend.name : "none",
             cardControlGetConnectionText());
}

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    CARD_CONTROL_UNAVAILABLE = 0,
    CARD_CONTROL_DISCONNECTED,
    CARD_CONTROL_CONNECTED
} CardControlConnectionState;

typedef enum
{
    CARD_CONTROL_NOT_HANDLED = 0,
    CARD_CONTROL_HANDLED,
    CARD_CONTROL_HANDLED_ONE_SHOT
} CardControlActionResult;

typedef struct
{
    const char *name;
    CardControlActionResult (*setInsertState)(int active);
    CardControlActionResult (*requestEject)(void);
    CardControlConnectionState (*getConnectionState)(void);
    const char *(*getConnectionText)(void);
    void (*logDiagnostics)(void);
} CardControlBackend;

void cardControlSetBackend(const CardControlBackend *backend);
CardControlActionResult cardControlSetInsertState(int active);
CardControlActionResult cardControlRequestEject(void);
int cardControlIsInsertActive(void);
CardControlConnectionState cardControlGetConnectionState(void);
int cardControlIsConnected(void);
const char *cardControlGetConnectionText(void);
void cardControlLogDiagnostics(void);

#ifdef __cplusplus
}
#endif

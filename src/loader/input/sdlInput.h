#pragma once

#include <SDL3/SDL.h>
#include <stdbool.h>

#include "../config/iniParser.h"
#include "../hardware/common/jvs.h"


// --- CORE DATA STRUCTURES ---
#define MAX_PLAYERS 4
#define MAX_ENTITIES (MAX_PLAYERS + 1)
#define MAX_JOYSTICKS 8
#define MAX_JOY_BUTTONS 32
#define MAX_JOY_AXES 32
#define MAX_JOY_HATS 4
#define MAX_MOUSE_BUTTONS 16

typedef enum
{
    LA_INVALID = -1,
    LA_Test ,
    LA_Coin,
    LA_GearUp,
    LA_GearDown,
    LA_Gear1,
    LA_Gear2,
    LA_Gear3,
    LA_Gear4,
    LA_Gear5,
    LA_Gear6,
    LA_ViewChange,
    LA_Intrude,
    LA_MusicChange,
    LA_Boost,
    LA_BoostRight,
    LA_Start,
    LA_Service,
    LA_Up,
    LA_Down,
    LA_Left,
    LA_Right,
    LA_Button1,
    LA_Button2,
    LA_Button3,
    LA_Button4,
    LA_Button5,
    LA_Button6,
    LA_Button7,
    LA_Button8,
    LA_Button9,
    LA_Button10,
    LA_Trigger,
    LA_OutOfScreen,
    LA_Reload,
    LA_GunButton,
    LA_ActionButton,
    LA_ChangeButton,
    LA_PedalLeft,
    LA_PedalRight,
    LA_Steer,
    LA_Gas,
    LA_Brake,
    LA_GunX,
    LA_GunY,
    LA_Steer_Left,
    LA_Steer_Right,
    LA_Gas_Digital,
    LA_Brake_Digital,
    LA_Flying_X,
    LA_Flying_Left,
    LA_Flying_Right,
    LA_Flying_Y,
    LA_Flying_Up,
    LA_Flying_Down,
    LA_Throttle,
    LA_Throttle_Accelerate,
    LA_Throttle_Slowdown,
    LA_GunTrigger,
    LA_MissileTrigger,
    LA_ClimaxSwitch,
    LA_CardInsert,
    LA_Card1Insert,
    LA_Card2Insert,
    LA_CardEject,
    LA_A,
    LA_B,
    LA_C,
    LA_D,
    LA_E,
    LA_F,
    LA_G,
    LA_H,
    LA_I,
    LA_J,
    LA_K,
    LA_L,
    LA_M,
    LA_N,
    LA_Reach,
    LA_Chi,
    LA_Pon,
    LA_Kan,
    LA_Agari,
    LA_Cancel,
    LA_ExitGame,
    NUM_LOGICAL_ACTIONS
} LogicalAction;

typedef enum
{
    INPUT_TYPE_NONE,
    INPUT_TYPE_KEY,
    INPUT_TYPE_JOY_AXIS,
    INPUT_TYPE_JOY_BUTTON,
    INPUT_TYPE_JOY_HAT,
    INPUT_TYPE_MOUSE_AXIS,
    INPUT_TYPE_MOUSE_BUTTON,
    INPUT_TYPE_GAMEPAD_AXIS,
    INPUT_TYPE_GAMEPAD_BUTTON
} SDLInputType;

typedef enum
{
    AXIS_MODE_FULL,
    AXIS_MODE_POSITIVE_HALF,
    AXIS_MODE_NEGATIVE_HALF,
    AXIS_MODE_DIGITAL
} AxisMode;

#define MAX_COMBINATION_GROUPS 64
#define MAX_INPUTS_PER_COMBO 4

typedef struct {
    LogicalAction action;
    JVSPlayer player;
    int numInputs;
    int activeCount;
    bool inputStates[MAX_INPUTS_PER_COMBO];
} ComboGroup;

typedef struct
{
    SDLInputType type;
    int deviceIndex;
    int sdlId;
    AxisMode axisMode;
    int axisThreshold;
    bool isInverted;
    JVSPlayer player;
    LogicalAction action;
    int comboGroupId;
    int comboInputIndex;
} ControlBinding;

typedef struct
{
    ControlBinding bindings[2];
} BindingPair;

typedef struct
{
    ControlBinding up;
    ControlBinding down;
    ControlBinding left;
    ControlBinding right;
} HatBinding;

typedef struct
{
    bool isActive;
    bool isPhysicalActive;
    float analogValue;
    float positiveContribution;
    float negativeContribution;
} ActionState;

/*
 * Where every logical action's current value lives. Force feedback reads
 * LA_Steer out of it so a wheel with no condition effects can still be given a
 * centring force worked out here instead - which is what the cabinet's own
 * board does against its encoder.
 */
extern ActionState gActionStates[MAX_ENTITIES][NUM_LOGICAL_ACTIONS];

typedef struct
{
    enum
    {
        JVS_CALL_SWITCH,
        JVS_CALL_ANALOGUE,
        JVS_CALL_COIN,
        JVS_CALL_NONE
    } call_type;
    JVSInput jvsInput;
} JVSActionMapping;

typedef struct
{
    JVSPlayer player;
    LogicalAction action;
} ChangedAction;

typedef struct
{
    bool isCentering;
    bool isCombinedAxis;
    bool isToggle;
    int deadzone; // Added to store per-action deadzone
} LogicalActionProperties;

typedef struct
{
    SDL_Joystick *joysticks[MAX_JOYSTICKS];
    SDL_Gamepad *controllers[MAX_JOYSTICKS];
    SDL_Haptic *haptics[MAX_JOYSTICKS];
    int joysticksCount;
    // int hapticsCount;
    // int numButtons[MAX_JOYSTICKS];
    // int numAxes[MAX_JOYSTICKS];
    // int numHats[MAX_JOYSTICKS];
} SDLControllers;

// Ini Creation
#define MAX_INI_KEYS_PER_SECTION 100
#define MAX_INI_VALUE_LENGTH 256

typedef struct
{
    char key[64];
    char value[MAX_INI_VALUE_LENGTH];
} IniFileEntry;

typedef struct
{
    const char *name;
    IniFileEntry entries[MAX_INI_KEYS_PER_SECTION];
    int num_entries;
} IniFileSection;

#ifdef __cplusplus
extern "C" {
#endif

int initSdlInput(const char *controlsPath);
int loadProfileFromIni(const IniSection *section);
void loadGlobalConfig(const IniConfig *ini);
void processChangedActions();
void processSdlEvent(const SDL_Event *e);
void initJvsMappings();
void setDefaultMappings();
void remapPerGame();
void initActionProperties();
void updateGunShake();
void updateCombinedAxes();
void detectCombinedAxes();
void getLogicalActionString(const ControlBinding *binding, char *out_str, size_t str_size, const char *name);
bool needsPlayer(LogicalAction action, const char *name);
int listSdlControllers(void);

/*
 * Gears the sequential GearUp/GearDown bindings walk through, from the
 * ShifterGears key in controls.ini.
 */
int getShifterGears(void);

/* Active WMMT H-pattern position, 1..6, or 0 when none is pressed. */
int getWmmtDirectGear(void);

#ifdef __cplusplus
}
#endif

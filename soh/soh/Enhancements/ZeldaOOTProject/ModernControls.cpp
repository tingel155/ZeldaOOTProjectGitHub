// ZeldaOOTProject: modernized controls for Link.
//
// CVars (gZOP.Controls.*):
//   Enabled          master switch for the hooks below (default 1)
//   DrawWithoutSlash the first press of the sword button only draws the sword, the second one slashes (default 1)
//   KeepNaviOut      Navi always flies next to Link; A no longer calls her in or out (default 1)
//   ForceFPS         frame rate forced on every start (default 20, the original rate; 0 = leave the setting alone)
//   ForceN64Mode     N64 graphics mode (4:3, 240p) forced on every start (default 1, 0 = leave the setting alone)
//   SwordCombos      repeated sword presses chain different swings, ending in a finisher (default 1)
//   ComboWindow      game frames after a swing in which the next press still continues the combo (default 8)
//   CameraSpeed      speed factor of the free camera on top of the menu sensitivity (default 1.5)
//   CameraSmoothing  share of the stick input the free camera follows per frame, 1 = instant (default 0.55)
//   TurnSpeed        factor on how fast Link turns while running and how fast his body follows (default 2.5)
//   Acceleration     factor on how fast Link speeds up when running off (default 1.5)
//   QuickTurn        sharp turns spin Link around in a few frames instead of braking to a stop first (default 1)
//   LockOnLeap       locked on, A with the stick forward only leaps toward the target; the sword button in the air
//                    makes it the jump attack (default 1)
//   LeapSpeed        forward speed of that leap (default 8)
//   LockOnFraming    the lock-on camera pulls back and widens for bosses and high targets (default 1)
//   FreeJump         A while moving (not locked on) jumps in the stick direction; A again in the air rolls on landing
//                    (default 1)
//   JumpHeight       upward speed of that jump (default 6.5)
//   JumpSpeed        minimum forward speed of that jump (default 6)
//   RollIFrames      frames a roll can't be hurt (default 10, 0 = off)
//   AnalogDodge      locked-on hops and flips go exactly where the stick points (default 1)
//   AirControl       steer in the air almost like on the ground (default 1)
//   CameraCurve      0..1, how much small stick deflections are slowed down for fine aiming (default 0.5)
//   PresetVersion    last applied version of the settings preset; the preset is applied once per version, so
//                    later changes in the menus are kept
//   ApplyPreset      set to 1 to apply the settings preset again (reset to 0 afterwards)
//   ApplyLayout      set to 1 to apply the modern gamepad layout to port 1 (reset to 0 afterwards). The layout is
//                    also applied once together with the preset.
//
// Settings preset: right-stick free camera (not inverted), right-stick aiming, chained/steerable rolls, camera drift
// and swing fixes, instant putaway (A puts the sword away right after stopping, without the vanilla cooldown during
// which A toggled Navi instead).
//
// Gamepad layout (Xbox names):  A = A (action/roll)  X = B (sword)  Y = C-Left  B = C-Down  RB = C-Right
//                               LB = C-Up (look/Navi)  LT = Z (lock-on)  RT = R (shield)  Start = Start  Back = L
//                               D-pad = D-pad  right stick = camera (no longer bound to the C buttons)

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/controller/controldeck/ControlDeck.h>
#include <ship/controller/controldevice/controller/mapping/sdl/SDLButtonToButtonMapping.h>
#include <ship/controller/controldevice/controller/mapping/sdl/SDLAxisDirectionToButtonMapping.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
#include "ZeldaOOTProjectHooks.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

extern "C" {
#include <z64.h>
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
void Player_ZopLandingRoll(Player* player, PlayState* play);
}

#define CVAR_ZOP_CONTROLS(name) "gZOP.Controls." name

namespace {

constexpr int kPresetVersion = 2;

bool ControlsEnabled() {
    return CVarGetInteger(CVAR_ZOP_CONTROLS("Enabled"), 1) != 0;
}

// CVars changed by the preset; their own init functions are re-run afterwards, so the options take effect right
// away and not only after the next start
std::vector<std::string> sChangedCVars;

void SetPresetInt(const char* name, int value) {
    if (CVarGetInteger(name, -1) != value) {
        CVarSetInteger(name, value);
        sChangedCVars.push_back(name);
    }
}

// Preset version 1
void ApplySettingsPresetV1() {
    SetPresetInt(CVAR_SETTING("FreeLook.Enabled"), 1);
    SetPresetInt(CVAR_SETTING("FreeLook.InvertXAxis"), 0);
    SetPresetInt(CVAR_SETTING("FreeLook.InvertYAxis"), 0);
    SetPresetInt(CVAR_SETTING("Controls.RightStickAim"), 1);
    SetPresetInt(CVAR_ENHANCEMENT("ImprovedRoll"), 1);
    SetPresetInt(CVAR_ENHANCEMENT("ImprovedRollSteering"), 1);
    SetPresetInt(CVAR_ENHANCEMENT("FixCameraDrift"), 1);
    SetPresetInt(CVAR_ENHANCEMENT("FixCameraSwing"), 1);
}

// Additions of preset version 2
void ApplySettingsPresetV2() {
    SetPresetInt(CVAR_ENHANCEMENT("InstantPutaway"), 1);
}

void ApplySettingsPreset() {
    ApplySettingsPresetV1();
    ApplySettingsPresetV2();
}

void ForceFrameRate() {
    int fps = CVarGetInteger(CVAR_ZOP_CONTROLS("ForceFPS"), 20);
    if (fps > 0) {
        SetPresetInt(CVAR_SETTING("MatchRefreshRate"), 0);
        SetPresetInt(CVAR_SETTING("InterpolationFPS"), fps);
    }
    if (CVarGetInteger(CVAR_ZOP_CONTROLS("ForceN64Mode"), 1)) {
        SetPresetInt(CVAR_LOW_RES_MODE, 1);
    }
}

struct ButtonBinding {
    CONTROLLERBUTTONS_T button;
    int32_t sdlButton;   // SDL_GameControllerButton, or -1
    int32_t sdlAxis;     // SDL_GameControllerAxis for triggers, or -1
};

const ButtonBinding kModernLayout[] = {
    { BTN_A, SDL_CONTROLLER_BUTTON_A, -1 },
    { BTN_B, SDL_CONTROLLER_BUTTON_X, -1 },
    { BTN_CLEFT, SDL_CONTROLLER_BUTTON_Y, -1 },
    { BTN_CDOWN, SDL_CONTROLLER_BUTTON_B, -1 },
    { BTN_CRIGHT, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, -1 },
    { BTN_CUP, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, -1 },
    { BTN_Z, -1, SDL_CONTROLLER_AXIS_TRIGGERLEFT },
    { BTN_R, -1, SDL_CONTROLLER_AXIS_TRIGGERRIGHT },
    { BTN_START, SDL_CONTROLLER_BUTTON_START, -1 },
    { BTN_L, SDL_CONTROLLER_BUTTON_BACK, -1 },
    { BTN_DUP, SDL_CONTROLLER_BUTTON_DPAD_UP, -1 },
    { BTN_DDOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN, -1 },
    { BTN_DLEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT, -1 },
    { BTN_DRIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, -1 },
};

// Replaces the gamepad bindings of the buttons above on port 1 (keyboard and mouse bindings stay untouched)
void ApplyModernLayout() {
    auto controlDeck = Ship::Context::GetRawInstance()->GetControlDeck();
    if (controlDeck == nullptr) {
        return;
    }
    auto controller = controlDeck->GetControllerByPort(0);
    if (controller == nullptr) {
        return;
    }

    for (const ButtonBinding& binding : kModernLayout) {
        auto button = controller->GetButton(binding.button);
        if (button == nullptr) {
            continue;
        }
        button->ClearAllButtonMappingsForDeviceType(Ship::PhysicalDeviceType::SDLGamepad);

        std::shared_ptr<Ship::ControllerButtonMapping> mapping;
        if (binding.sdlButton >= 0) {
            mapping = std::make_shared<Ship::SDLButtonToButtonMapping>(0, binding.button, binding.sdlButton);
        } else {
            mapping = std::make_shared<Ship::SDLAxisDirectionToButtonMapping>(0, binding.button, binding.sdlAxis, 1);
        }
        button->AddButtonMapping(mapping);
        mapping->SaveToConfig();
        button->SaveButtonMappingIdsToConfig();
    }
    CVarSetInteger("gSettings.Controllers.Port1.HasConfig", 1);
}

void ApplyPendingChanges() {
    bool changed = false;
    int presetVersion = CVarGetInteger(CVAR_ZOP_CONTROLS("PresetVersion"), 0);

    if (presetVersion < kPresetVersion) {
        if (presetVersion < 1) {
            ApplySettingsPresetV1();
            ApplyModernLayout();
        }
        ApplySettingsPresetV2();
        CVarSetInteger(CVAR_ZOP_CONTROLS("PresetVersion"), kPresetVersion);
        changed = true;
    }
    if (CVarGetInteger(CVAR_ZOP_CONTROLS("ApplyPreset"), 0)) {
        ApplySettingsPreset();
        CVarSetInteger(CVAR_ZOP_CONTROLS("ApplyPreset"), 0);
        changed = true;
    }
    if (CVarGetInteger(CVAR_ZOP_CONTROLS("ApplyLayout"), 0)) {
        ApplyModernLayout();
        CVarSetInteger(CVAR_ZOP_CONTROLS("ApplyLayout"), 0);
        changed = true;
    }

    ForceFrameRate();

    if (changed || !sChangedCVars.empty()) {
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    std::vector<std::string> changedCVars;
    changedCVars.swap(sChangedCVars);
    for (const std::string& name : changedCVars) {
        ShipInit::Init(name);
    }
}

bool IsSword(s32 itemId) {
    return itemId == ITEM_SWORD_KOKIRI || itemId == ITEM_SWORD_MASTER || itemId == ITEM_SWORD_BGS;
}

} // namespace

void ZOP_RegisterModernControls() {
    ApplyPendingChanges();

    // Drawing the sword: the press that draws it does not also slash (all swords, unlike the vanilla option)
    COND_VB_SHOULD(VB_USE_HELD_ITEM_AFTER_CHANGE,
                   ControlsEnabled() && CVarGetInteger(CVAR_ZOP_CONTROLS("DrawWithoutSlash"), 1), {
                       Player* player = va_arg(args, Player*);
                       if (IsSword(player->heldItemId)) {
                           *should = false;
                       }
                   });

    // Navi stays out: keep the flag set that makes her fly next to Link (the A button and her own timeout no longer
    // clear it, see ZOP_KeepNaviOut)
    COND_HOOK(OnPlayerUpdate, ZOP_KeepNaviOut(), []() {
        Player* player = GET_PLAYER(gPlayState);
        if (player != nullptr) {
            player->stateFlags2 |= PLAYER_STATE2_NAVI_ACTIVE;
        }
    });

    // Jump, then roll: A pressed in the air queues a roll that starts the moment Link lands
    COND_HOOK(OnPlayerUpdate, ZOP_FreeJump(), []() {
        static bool sRollQueued = false;
        Player* player = GET_PLAYER(gPlayState);
        if (player == nullptr) {
            return;
        }
        bool airborne = !(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND);
        bool jumping = (player->stateFlags1 & PLAYER_STATE1_JUMPING) != 0;
        if (airborne && jumping && CHECK_BTN_ALL(gPlayState->state.input[0].press.button, BTN_A)) {
            sRollQueued = true;
        }
        if (sRollQueued && !airborne) {
            sRollQueued = false;
            if (!(player->stateFlags1 & (PLAYER_STATE1_IN_WATER | PLAYER_STATE1_DAMAGED | PLAYER_STATE1_DEAD |
                                         PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_CARRYING_ACTOR))) {
                Player_ZopLandingRoll(player, gPlayState);
            }
        }
        if (!airborne && !jumping) {
            sRollQueued = false;
        }
    });
}

extern "C" int ZOP_FreeJump(void) {
    return ControlsEnabled() && CVarGetInteger(CVAR_ZOP_CONTROLS("FreeJump"), 1);
}

extern "C" float ZOP_FreeJumpHeight(void) {
    return CVarGetFloat(CVAR_ZOP_CONTROLS("JumpHeight"), 6.5f);
}

extern "C" float ZOP_FreeJumpSpeed(void) {
    return CVarGetFloat(CVAR_ZOP_CONTROLS("JumpSpeed"), 6.0f);
}

extern "C" int ZOP_RollIFrames(void) {
    return ControlsEnabled() ? CVarGetInteger(CVAR_ZOP_CONTROLS("RollIFrames"), 10) : 0;
}

extern "C" int ZOP_AnalogDodge(void) {
    return ControlsEnabled() && CVarGetInteger(CVAR_ZOP_CONTROLS("AnalogDodge"), 1);
}

extern "C" int ZOP_AirControl(void) {
    return ControlsEnabled() && CVarGetInteger(CVAR_ZOP_CONTROLS("AirControl"), 1);
}

extern "C" int ZOP_KeepNaviOut(void) {
    return ControlsEnabled() && CVarGetInteger(CVAR_ZOP_CONTROLS("KeepNaviOut"), 1);
}

// --- Sword combos ---------------------------------------------------------------------------------------------------
//
// Each swing of a combo is one of the vanilla sword swings, played out completely. A press during a swing is
// remembered and starts the next one when the swing ends; a press shortly after it (during the recovery) continues
// the combo too. Locked on: vertical, right, left, then the big vertical finisher. Free: right, left, right, then the
// spinning side finisher. Stabs, jump attacks, spin attacks and the hammer keep their vanilla behavior.

namespace {

constexpr int kComboLength = 4;
const s8 kComboTargeted[kComboLength] = { PLAYER_MWA_FORWARD_SLASH_1H, PLAYER_MWA_RIGHT_SLASH_1H, PLAYER_MWA_LEFT_SLASH_1H,
                                          PLAYER_MWA_FORWARD_COMBO_1H };
const s8 kComboFree[kComboLength] = { PLAYER_MWA_RIGHT_SLASH_1H, PLAYER_MWA_LEFT_SLASH_1H, PLAYER_MWA_RIGHT_SLASH_1H,
                                      PLAYER_MWA_RIGHT_COMBO_1H };

int sComboStep = 0;          // swing of the combo the next chained press plays
u32 sComboLastSwingEnd = 0;  // gameplay frame on which the last combo swing ended
bool sComboSwingActive = false;
bool sComboBuffered = false;

bool SwordCombosEnabled() {
    return ControlsEnabled() && CVarGetInteger(CVAR_ZOP_CONTROLS("SwordCombos"), 1);
}

// Basic one-handed slashes the combo is built from (two-handed variants are the odd entries right after them)
bool IsBasicSlash(int attack1H) {
    return attack1H == PLAYER_MWA_FORWARD_SLASH_1H || attack1H == PLAYER_MWA_RIGHT_SLASH_1H ||
           attack1H == PLAYER_MWA_LEFT_SLASH_1H;
}

} // namespace

extern "C" int ZOP_SwordCombo(PlayState* play, int attack, int zTargeting, int* comboCounter) {
    sComboBuffered = false;
    sComboSwingActive = false;
    if (!SwordCombosEnabled() || attack < 0 || attack > PLAYER_MWA_LEFT_COMBO_2H) {
        sComboStep = 0;
        return -1;
    }

    int twoHanded = attack & 1;
    int attack1H = attack & ~1;
    if (!IsBasicSlash(attack1H)) {
        sComboStep = 0;
        return -1;
    }

    u32 window = (u32)CVarGetInteger(CVAR_ZOP_CONTROLS("ComboWindow"), 8);
    bool chained = sComboStep > 0 && (u32)(play->gameplayFrames - sComboLastSwingEnd) <= window;
    if (!chained) {
        sComboStep = 0;
    }

    // The opening swing is the one vanilla picked (it follows the stick); the rest of the chain is fixed
    int swing = sComboStep == 0 ? attack1H : (zTargeting ? kComboTargeted : kComboFree)[sComboStep];
    bool finisher = sComboStep == kComboLength - 1;

    *comboCounter = finisher ? 3 : 1;
    sComboStep = finisher ? 0 : sComboStep + 1;
    sComboSwingActive = true;
    return swing + twoHanded;
}

extern "C" void ZOP_SwordComboBufferPress(void) {
    if (sComboSwingActive) {
        sComboBuffered = true;
    }
}

extern "C" int ZOP_SwordComboSwingEnded(PlayState* play) {
    if (!sComboSwingActive) {
        return 0;
    }
    sComboSwingActive = false;
    sComboLastSwingEnd = play->gameplayFrames;
    bool buffered = sComboBuffered && sComboStep > 0;
    sComboBuffered = false;
    return buffered;
}

// --- Snappier running ----------------------------------------------------------------------------------------------

extern "C" short ZOP_PlayerTurnRate(short vanillaRate) {
    if (!ControlsEnabled()) {
        return vanillaRate;
    }
    float rate = vanillaRate * CVarGetFloat(CVAR_ZOP_CONTROLS("TurnSpeed"), 2.5f);
    return (short)std::clamp(rate, 0.0f, 16000.0f);
}

extern "C" float ZOP_PlayerAccel(float vanillaAccel) {
    if (!ControlsEnabled()) {
        return vanillaAccel;
    }
    return vanillaAccel * CVarGetFloat(CVAR_ZOP_CONTROLS("Acceleration"), 1.5f);
}

extern "C" int ZOP_LockOnLeap(void) {
    return ControlsEnabled() && CVarGetInteger(CVAR_ZOP_CONTROLS("LockOnLeap"), 1);
}

extern "C" float ZOP_LockOnLeapSpeed(void) {
    return CVarGetFloat(CVAR_ZOP_CONTROLS("LeapSpeed"), 8.0f);
}

extern "C" int ZOP_PlayerQuickTurn(void) {
    return ControlsEnabled() && CVarGetInteger(CVAR_ZOP_CONTROLS("QuickTurn"), 1);
}

// --- Lock-on camera framing ----------------------------------------------------------------------------------------

extern "C" void ZOP_LockOnFraming(void* targetPtr, void* playerPosPtr, float playerHeight, float* distance,
                                  float* fov) {
    if (!ControlsEnabled() || !CVarGetInteger(CVAR_ZOP_CONTROLS("LockOnFraming"), 1) || targetPtr == nullptr) {
        return;
    }
    Actor* target = (Actor*)targetPtr;
    Vec3f* playerPos = (Vec3f*)playerPosPtr;

    // How far the target is above Link's head: full effect at 350 units (Gohma on the ceiling)
    float above = target->focus.pos.y - (playerPos->y + playerHeight);
    float high = std::clamp(above / 350.0f, 0.0f, 1.0f);

    float zoom = 1.0f + 0.7f * high;
    float widen = 12.0f * high;
    if (target->category == ACTORCAT_BOSS) {
        zoom += 0.25f;
        widen += 5.0f;
    }

    *distance *= zoom;
    *fov += widen;
}

// --- Free camera smoothing ------------------------------------------------------------------------------------------

extern "C" void ZOP_SmoothFreeLook(float* camX, float* camY, int frame) {
    static float sVelX = 0.0f;
    static float sVelY = 0.0f;
    static int sLastFrame = 0;

    if (!ControlsEnabled()) {
        return;
    }
    // The camera was driven by something else in between: start from rest
    if (frame - sLastFrame > 1) {
        sVelX = sVelY = 0.0f;
    }
    sLastFrame = frame;

    // Response curve: full stick (about 850 units) keeps its speed, small deflections get slower for fine control
    float curve = std::clamp(CVarGetFloat(CVAR_ZOP_CONTROLS("CameraCurve"), 0.5f), 0.0f, 1.0f);
    auto shape = [curve](float v) {
        float n = std::min(std::fabs(v) / 850.0f, 1.0f);
        return v * ((1.0f - curve) + curve * n);
    };
    float speed = CVarGetFloat(CVAR_ZOP_CONTROLS("CameraSpeed"), 1.5f);
    float targetX = shape(*camX) * speed;
    float targetY = shape(*camY) * speed;

    // Ease toward the stick instead of following it instantly
    float follow = std::clamp(CVarGetFloat(CVAR_ZOP_CONTROLS("CameraSmoothing"), 0.55f), 0.05f, 1.0f);
    sVelX += (targetX - sVelX) * follow;
    sVelY += (targetY - sVelY) * follow;

    *camX = sVelX;
    *camY = sVelY;
}

static RegisterShipInitFunc initFunc(ZOP_RegisterModernControls,
                                     { CVAR_ZOP_CONTROLS("Enabled"), CVAR_ZOP_CONTROLS("ApplyPreset"),
                                       CVAR_ZOP_CONTROLS("ApplyLayout"), CVAR_ZOP_CONTROLS("DrawWithoutSlash"),
                                       CVAR_ZOP_CONTROLS("KeepNaviOut"), CVAR_ZOP_CONTROLS("ForceFPS"),
                                       CVAR_ZOP_CONTROLS("ForceN64Mode"), CVAR_ZOP_CONTROLS("FreeJump") });

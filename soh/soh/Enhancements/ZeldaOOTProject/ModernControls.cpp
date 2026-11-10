// ZeldaOOTProject: modernized controls for Link.
//
// CVars (gZOP.Controls.*):
//   Enabled          master switch for the hooks below (default 1)
//   DrawWithoutSlash the first press of the sword button only draws the sword, the second one slashes (default 1)
//   KeepNaviOut      Navi always flies next to Link; A no longer calls her in or out (default 1)
//   ForceFPS         frame rate forced on every start (default 60, 0 = leave the setting alone)
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

#include <string>
#include <vector>

extern "C" {
#include <z64.h>
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
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
    int fps = CVarGetInteger(CVAR_ZOP_CONTROLS("ForceFPS"), 60);
    if (fps > 0) {
        SetPresetInt(CVAR_SETTING("MatchRefreshRate"), 0);
        SetPresetInt(CVAR_SETTING("InterpolationFPS"), fps);
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
}

extern "C" int ZOP_KeepNaviOut(void) {
    return ControlsEnabled() && CVarGetInteger(CVAR_ZOP_CONTROLS("KeepNaviOut"), 1);
}

static RegisterShipInitFunc initFunc(ZOP_RegisterModernControls,
                                     { CVAR_ZOP_CONTROLS("Enabled"), CVAR_ZOP_CONTROLS("ApplyPreset"),
                                       CVAR_ZOP_CONTROLS("ApplyLayout"), CVAR_ZOP_CONTROLS("DrawWithoutSlash"),
                                       CVAR_ZOP_CONTROLS("KeepNaviOut"), CVAR_ZOP_CONTROLS("ForceFPS") });

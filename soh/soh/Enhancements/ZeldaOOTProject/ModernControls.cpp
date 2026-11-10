// ZeldaOOTProject: modernized controls for Link.
//
// CVars (gZOP.Controls.*):
//   Enabled          master switch for the hooks below (default 1)
//   PresetVersion    last applied version of the settings preset; the preset is applied once per version, so
//                    later changes in the menus are kept
//   ApplyPreset      set to 1 to apply the settings preset again (reset to 0 afterwards)
//   ApplyLayout      set to 1 to apply the modern gamepad layout to port 1 (reset to 0 afterwards). The layout is
//                    also applied once together with the preset.
//
// Settings preset: right-stick free camera (not inverted), right-stick aiming, chained/steerable rolls, camera drift
// and swing fixes, and drawing the sword without slashing (extended here to the Biggoron Sword / Giant's Knife).
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
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

extern "C" {
#include "z64player.h"
}

#define CVAR_ZOP_CONTROLS(name) "gZOP.Controls." name

namespace {

constexpr int kPresetVersion = 1;

bool ControlsEnabled() {
    return CVarGetInteger(CVAR_ZOP_CONTROLS("Enabled"), 1) != 0;
}

void ApplySettingsPreset() {
    CVarSetInteger(CVAR_SETTING("FreeLook.Enabled"), 1);
    CVarSetInteger(CVAR_SETTING("FreeLook.InvertXAxis"), 0);
    CVarSetInteger(CVAR_SETTING("FreeLook.InvertYAxis"), 0);
    CVarSetInteger(CVAR_SETTING("Controls.RightStickAim"), 1);
    CVarSetInteger(CVAR_ENHANCEMENT("ImprovedRoll"), 1);
    CVarSetInteger(CVAR_ENHANCEMENT("ImprovedRollSteering"), 1);
    CVarSetInteger(CVAR_ENHANCEMENT("FixCameraDrift"), 1);
    CVarSetInteger(CVAR_ENHANCEMENT("FixCameraSwing"), 1);
    CVarSetInteger(CVAR_ENHANCEMENT("UnsheatheWithoutSlashing"), 1);
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

    if (CVarGetInteger(CVAR_ZOP_CONTROLS("PresetVersion"), 0) < kPresetVersion) {
        ApplySettingsPreset();
        ApplyModernLayout();
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

    if (changed) {
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
}

} // namespace

void ZOP_RegisterModernControls() {
    ApplyPendingChanges();

    // The vanilla "Unsheathe Sword Without Slashing" option only covers the Kokiri and Master Sword
    COND_VB_SHOULD(VB_USE_HELD_ITEM_AFTER_CHANGE,
                   ControlsEnabled() && CVarGetInteger(CVAR_ENHANCEMENT("UnsheatheWithoutSlashing"), 0), {
                       Player* player = va_arg(args, Player*);
                       if (player->heldItemId == ITEM_SWORD_BGS) {
                           *should = false;
                       }
                   });
}

static RegisterShipInitFunc initFunc(ZOP_RegisterModernControls,
                                     { CVAR_ZOP_CONTROLS("Enabled"), CVAR_ZOP_CONTROLS("ApplyPreset"),
                                       CVAR_ZOP_CONTROLS("ApplyLayout"), CVAR_ENHANCEMENT("UnsheatheWithoutSlashing") });

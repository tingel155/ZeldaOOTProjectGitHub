// ZeldaOOTProject: Phantom Ganon's energy ball rallies light up the room.
//
// Every hit of a rally (Link's and Phantom Ganon's) brightens the room a little more with the light bluish haze of the
// Gohma fight; when the ball is gone it fades back. Driven by z_en_fhg_fire.c through ZOP_GndRallyGlow.

#include <libultraship/bridge/consolevariablebridge.h>

#include <algorithm>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "ZeldaOOTProjectHooks.h"

extern "C" {
#include <z64.h>
#include "variables.h"
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace {

constexpr int kFullGlowHits = 16; // hits until the room is fully lit
float sTarget = 0.0f;
float sGlow = 0.0f;
float sFlash = 0.0f; // bright flicker on top of the glow, decays fast
bool sApplied = false;

// Same haze as Gohma's (BossGoma_SetHaze): brightened bluish fog, a touch of blue ambient light, fog pulled in
void ApplyHaze(PlayState* play, float intensity) {
    EnvironmentContext* env = &play->envCtx;
    float fogNear = env->lightSettings.fogNear;

    env->adjFogColor[0] = (s16)(35.0f * intensity);
    env->adjFogColor[1] = (s16)(55.0f * intensity);
    env->adjFogColor[2] = (s16)(85.0f * intensity);
    env->adjAmbientColor[0] = (s16)(12.0f * intensity);
    env->adjAmbientColor[1] = (s16)(18.0f * intensity);
    env->adjAmbientColor[2] = (s16)(30.0f * intensity);
    env->adjFogNear = (fogNear > 900.0f) ? (s16)((900.0f - fogNear) * intensity) : 0;
}

void Tick() {
    PlayState* play = gPlayState;
    if (play == nullptr) {
        return;
    }
    if (play->sceneNum != SCENE_FOREST_TEMPLE_BOSS) {
        sTarget = sGlow = 0.0f;
        sApplied = false;
        return;
    }
    // Eases in quickly with every hit, fades out slowly once the rally is over
    float step = (sTarget > sGlow) ? 0.08f : 0.02f;
    Math_ApproachF(&sGlow, sTarget, 1.0f, step);
    // The flash flickers while it fades
    float flash = 0.0f;
    if (sFlash > 0.0f) {
        flash = sFlash * (0.6f + 0.4f * Rand_ZeroOne()) * 2.5f;
        sFlash = std::max(0.0f, sFlash - 0.12f);
    }
    float intensity = sGlow + flash;
    if (intensity > 0.0f || sApplied) {
        ApplyHaze(play, intensity);
        sApplied = intensity > 0.0f;
    }
}

} // namespace

extern "C" void ZOP_GndRallyFlash(void) {
    sFlash = 1.0f;
}

extern "C" void ZOP_GndRallyGlow(int hits) {
    sTarget = std::clamp((float)hits / kFullGlowHits, 0.0f, 1.0f);
}

static void RegisterGanondrofRally() {
    COND_HOOK(OnPlayerUpdate, true, Tick);
}

static RegisterShipInitFunc initFunc(RegisterGanondrofRally, {});

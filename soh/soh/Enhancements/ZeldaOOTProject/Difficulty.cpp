// Difficulty: enemies hit harder, take more punishment and act more aggressively.
//
// Scoped to a list of actors so it can be tried out enemy by enemy; currently Gohma and her brood.
//   gZOP.Difficulty.Enabled      master switch
//   gZOP.Difficulty.DamageMult   damage enemies deal to Link (default 2)
//   gZOP.Difficulty.HealthMult   enemy health (default 2)
//   gZOP.Difficulty.Aggressive   aggressive AI tuning, see ZOP_AGGRO in the enemies' source (default 1)
//   gZOP.Difficulty.StunMult     how long a stunned boss stays down, see ZOP_STUN (default 1.5)
//   gZOP.Difficulty.BossPhase2   bosses get a second phase (default 1)
//   gZOP.Difficulty.Phase2At     share of their starting health at which it begins (default 0.5)

#include <libultraship/bridge/consolevariablebridge.h>

#include <algorithm>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "ZeldaOOTProjectHooks.h"

extern "C" {
#include <z64.h>
#include "variables.h"
#include "functions.h"
#include "macros.h"
}

#define CVAR_ZOP_DIFFICULTY(var) "gZOP.Difficulty." var
#define CVAR_DIFFICULTY_NAME CVAR_ZOP_DIFFICULTY("Enabled")
#define CVAR_DIFFICULTY_VALUE CVarGetInteger(CVAR_DIFFICULTY_NAME, 0)

static bool IsAffectedEnemy(s16 actorId) {
    switch (actorId) {
        case ACTOR_BOSS_GOMA:
        case ACTOR_EN_GOMA: // Gohma Larvae and their eggs
            return true;
        default:
            return ZOP_IsPearlArrow(actorId) || ZOP_IsGohmaLaser(actorId); // Gohma's phase 2 attacks
    }
}

static void ScaleEnemyHealth(Actor* actor) {
    float mult = CVarGetFloat(CVAR_ZOP_DIFFICULTY("HealthMult"), 2.0f);
    // health is an s8 in practice (enemies test (s8)health <= 0)
    int health = (int)(actor->colChkInfo.health * mult + 0.5f);
    actor->colChkInfo.health = (u8)std::clamp(health, 1, 127);
}

extern "C" float ZOP_ModifyAcDamage(PlayState* play, void* colliderRef, ColliderInfo* info, float damage) {
    Collider* collider = (Collider*)colliderRef;
    if (!CVAR_DIFFICULTY_VALUE || collider->actor != &GET_PLAYER(play)->actor || info->acHit == nullptr ||
        info->acHit->actor == nullptr || !IsAffectedEnemy(info->acHit->actor->id)) {
        return damage;
    }
    return damage * CVarGetFloat(CVAR_ZOP_DIFFICULTY("DamageMult"), 2.0f);
}

extern "C" int ZOP_EnemyAggressive(void) {
    return CVAR_DIFFICULTY_VALUE && CVarGetInteger(CVAR_ZOP_DIFFICULTY("Aggressive"), 1);
}

extern "C" int ZOP_DifficultyEnabled(void) {
    return CVAR_DIFFICULTY_VALUE;
}

extern "C" int ZOP_BossPhase2(void* bossRef, int baseHealth) {
    Actor* boss = (Actor*)bossRef;
    if (!CVAR_DIFFICULTY_VALUE || !CVarGetInteger(CVAR_ZOP_DIFFICULTY("BossPhase2"), 1)) {
        return false;
    }
    float startHealth = baseHealth * CVarGetFloat(CVAR_ZOP_DIFFICULTY("HealthMult"), 2.0f);
    s8 health = (s8)boss->colChkInfo.health;
    return health > 0 && health <= startHealth * ZOP_BossPhase2At();
}

extern "C" float ZOP_BossPhase2At(void) {
    return CVarGetFloat(CVAR_ZOP_DIFFICULTY("Phase2At"), 0.5f);
}

extern "C" int ZOP_TestPhase2Intro(void) {
    return CVAR_DIFFICULTY_VALUE && CVarGetInteger(CVAR_ZOP_DIFFICULTY("TestPhase2Intro"), 0);
}

extern "C" float ZOP_EnemyStunMult(void) {
    return CVAR_DIFFICULTY_VALUE ? CVarGetFloat(CVAR_ZOP_DIFFICULTY("StunMult"), 1.5f) : 1.0f;
}

static void RegisterDifficulty() {
    COND_HOOK(OnActorInit, CVAR_DIFFICULTY_VALUE, [](void* actorRef) {
        Actor* actor = (Actor*)actorRef;
        if (IsAffectedEnemy(actor->id)) {
            ScaleEnemyHealth(actor);
        }
        // Testing aid: bosses start with just enough health to already be in their second phase
        if (actor->category == ACTORCAT_BOSS && CVarGetInteger(CVAR_ZOP_DIFFICULTY("TestStartInPhase2"), 0)) {
            actor->colChkInfo.health = (u8)std::max(1, (int)(actor->colChkInfo.health * ZOP_BossPhase2At()));
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterDifficulty, { CVAR_DIFFICULTY_NAME });

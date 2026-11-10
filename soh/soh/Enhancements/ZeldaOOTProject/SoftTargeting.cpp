// ZeldaOOTProject: soft targeting for boss fights.
//
// Between the free camera and the hard lock-on (Z): while a boss is around and the camera roughly faces it, the free
// camera keeps turning toward the boss, so it stays in view, but Link moves, turns and jumps freely. Turning the camera
// far away from the boss leaves soft targeting (e.g. to cut grass at the edge of the arena); turning back toward it
// picks it up again. Z still locks on hard as usual. A small, quiet marker above the boss shows that it is soft
// targeted.
//
// CVars (gZOP.Controls.*):
//   SoftTarget        on/off (default 1)
//   SoftTargetPull    how fast the camera turns toward the boss per frame while the player leaves it alone
//                     (default 0.1)

#include <libultraship/bridge/consolevariablebridge.h>

#include <algorithm>
#include <cmath>

#include "soh/ActorDB.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
#include "NayrusLove.h"
#include "ZeldaOOTProjectHooks.h"

extern "C" {
#include <z64.h>
#include "variables.h"
#include "functions.h"
#include "macros.h"
#include "objects/gameplay_keep/gameplay_keep.h"
extern PlayState* gPlayState;
}

#define CVAR_ZOP_CONTROLS(name) "gZOP.Controls." name

using namespace NayrusLove;

namespace {

constexpr float kMaxBossDist = 1600.0f;
constexpr s16 kEnterAngle = 0x2C00;  // camera within ~60 degrees of the boss: soft targeting picks up
constexpr s16 kLeaveAngle = 0x5800;  // turned more than ~125 degrees away: it lets go
constexpr float kStickIdle = 20.0f;  // right stick deltas below this count as "not turning the camera"

Actor* sTarget = nullptr;
bool sActive = false;
float sMarkerFade = 0.0f;
s16 sMarkerActorId = -1;

bool Enabled() {
    return CVarGetInteger(CVAR_ZOP_CONTROLS("Enabled"), 1) && CVarGetInteger(CVAR_ZOP_CONTROLS("SoftTarget"), 1) &&
           CVarGetInteger(CVAR_SETTING("FreeLook.Enabled"), 0);
}

// Nearest living, targetable boss
Actor* FindBoss(PlayState* play, Player* player) {
    Actor* best = nullptr;
    float bestDist = kMaxBossDist;
    for (Actor* actor = play->actorCtx.actorLists[ACTORCAT_BOSS].head; actor != nullptr; actor = actor->next) {
        if (actor->update == nullptr || !(actor->flags & ACTOR_FLAG_ATTENTION_ENABLED) ||
            (s8)actor->colChkInfo.health <= 0) {
            continue;
        }
        float dist = Math_Vec3f_DistXZ(&player->actor.world.pos, &actor->world.pos);
        if (dist < bestDist) {
            best = actor;
            bestDist = dist;
        }
    }
    return best;
}

// Camera yaw (from the eye toward what it looks at)
s16 CameraYaw(PlayState* play) {
    Camera* cam = GET_ACTIVE_CAM(play);
    return Math_Vec3f_Yaw(&cam->eye, &cam->at);
}

void UpdateSoftTarget() {
    PlayState* play = gPlayState;
    if (play == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(play);
    bool blocked = !Enabled() || player == nullptr ||
                   (player->stateFlags1 & (PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_DEAD | PLAYER_STATE1_FIRST_PERSON)) ||
                   player->csAction != 0 || Player_InCsMode(play) || (play->activeCamera != 0) ||
                   (player->stateFlags1 & PLAYER_STATE1_PARALLEL) || (player->focusActor != nullptr);

    Actor* boss = blocked ? nullptr : FindBoss(play, player);
    if (boss == nullptr) {
        sActive = false;
        sTarget = nullptr;
    } else {
        s16 bossYaw = Math_Vec3f_Yaw(&player->actor.world.pos, &boss->world.pos);
        s16 diff = (s16)(bossYaw - CameraYaw(play));
        int absDiff = std::abs((int)diff);
        if (boss != sTarget) {
            sActive = false;
            sTarget = boss;
        }
        if (!sActive && absDiff < kEnterAngle) {
            sActive = true;
        } else if (sActive && absDiff > kLeaveAngle) {
            sActive = false;
        }
        if (sActive) {
            // Keep the free camera in charge, so Camera_Free can steer toward the boss
            play->manualCamera = true;
        }
    }

    Math_ApproachF(&sMarkerFade, sActive ? 1.0f : 0.0f, 1.0f, 0.08f);

    // The marker is drawn by a small helper actor; make sure one exists while needed
    if (sMarkerFade > 0.0f && sMarkerActorId >= 0) {
        bool found = false;
        for (Actor* actor = play->actorCtx.actorLists[ACTORCAT_MISC].head; actor != nullptr; actor = actor->next) {
            if (actor->id == sMarkerActorId) {
                found = true;
                break;
            }
        }
        if (!found) {
            Actor_Spawn(&play->actorCtx, play, sMarkerActorId, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0);
        }
    }
}

void Marker_Init(Actor* thisx, PlayState* play) {
    thisx->room = -1;
}

void Marker_Update(Actor* thisx, PlayState* play) {
    if (sMarkerFade <= 0.0f) {
        Actor_Kill(thisx);
    }
}

} // namespace

// Quiet marker: a small soft-blue glint hovering above the boss, gently pulsing
extern "C" void SoftTargetMarker_Draw(Actor* thisx, PlayState* play) {
    if (sTarget == nullptr || sMarkerFade <= 0.0f || sTarget->update == nullptr) {
        return;
    }
    float t = play->gameplayFrames * 0.15f;
    float pulse = 0.75f + 0.25f * sinf(t);
    Vec3f pos = sTarget->focus.pos;
    pos.y += 55.0f + 4.0f * sinf(t * 0.5f);

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gDPSetEnvColor(POLY_XLU_DISP++, 90, 160, 255, 255);
    for (int i = 0; i < 2; i++) {
        float scale = (i == 0) ? 0.022f * pulse : 0.009f;
        float alpha = (i == 0) ? 0.45f : 0.85f;
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x80, 220, 240, 255, ToU8(alpha * sMarkerFade));
        Matrix_Translate(pos.x, pos.y, pos.z, MTXMODE_NEW);
        Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
        Matrix_Scale(scale, scale, 1.0f, MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gEffFlash1DL);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void ZOP_SoftTargetSteerCamera(float* camX, float* camY, float inputX, float inputY) {
    if (!sActive || sTarget == nullptr || gPlayState == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    // The camera sits behind Link, opposite the boss: its yaw (from what it looks at to the eye) points away from it
    s16 wantYaw = (s16)(Math_Vec3f_Yaw(&player->actor.world.pos, &sTarget->world.pos) + 0x8000);
    s16 yawDiff = (s16)(wantYaw - (s16)*camX);

    // A high boss (Gohma on the ceiling) pulls the camera down a little to look up at it
    float dist = Math_Vec3f_DistXZ(&player->actor.world.pos, &sTarget->focus.pos);
    float above = sTarget->focus.pos.y - (player->actor.world.pos.y + 50.0f);
    float wantPitch = std::clamp(-atan2f(above, std::max(dist, 1.0f)) * (0x8000 / kPi) * 0.5f, -2500.0f, 2500.0f);

    // Full pull while the player leaves the camera alone, a light one while he looks around himself
    bool turning = std::fabs(inputX) > kStickIdle || std::fabs(inputY) > kStickIdle;
    float pull = CVarGetFloat(CVAR_ZOP_CONTROLS("SoftTargetPull"), 0.1f) * (turning ? 0.25f : 1.0f);
    *camX += yawDiff * pull;
    if (!turning) {
        *camY += (wantPitch - *camY) * pull * 0.5f;
    }
}

static void RegisterSoftTargeting() {
    COND_HOOK(OnPlayerUpdate, true, UpdateSoftTarget);

    if (sMarkerActorId < 0) {
        ActorDBInit entry = {};
        entry.name = "ZOP_SoftTargetMarker";
        entry.desc = "Marker above a soft targeted boss";
        entry.category = ACTORCAT_MISC;
        entry.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        entry.objectId = OBJECT_GAMEPLAY_KEEP;
        entry.instanceSize = sizeof(Actor);
        entry.init = Marker_Init;
        entry.update = Marker_Update;
        entry.draw = SoftTargetMarker_Draw;
        sMarkerActorId = (s16)ActorDB::Instance->AddEntry(entry).entry.id;
    }
}

static RegisterShipInitFunc initFunc(RegisterSoftTargeting, {});

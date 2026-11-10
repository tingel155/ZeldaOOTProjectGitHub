// Gohma phase 2: tracking laser.
//
// Fired from Gohma's eye (the spawning actor's focus point); her eye may only be a slit while she fires. It aims first (thin flickering line, Beamos aim sound),
// then fires: the beam's impact point starts a bit in front of Link and crawls toward him slower than he can run, so
// it can be outrun or side-stepped. The damaging part is the impact point on the floor/wall.

#include <libultraship/bridge/consolevariablebridge.h>

#include <algorithm>

#include "soh/ActorDB.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
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

using namespace NayrusLove;

namespace {

constexpr int kAimFrames = 22;
constexpr int kFireFrames = 64;
constexpr int kFadeFrames = 8;
static_assert(kAimFrames + kFireFrames + kFadeFrames == ZOP_GOHMA_LASER_FRAMES, "keep the header in sync");
constexpr float kStartAhead = 130.0f; // impact starts this far in front of Link, toward Gohma
constexpr float kAimSpeed = 3.0f;
constexpr float kTrackSpeed = 5.6f;   // just below Link's running speed: sprinting or side-stepping still escapes it
constexpr int kHitCooldown = 20;

struct GohmaLaser {
    Actor actor;
    ColliderCylinder collider;
    V3 origin;
    V3 target; // where the beam is aimed (on Link's floor level)
    V3 impact; // where it actually hits geometry
    s16 timer;
    s16 hitCooldown;
    s16 fireFrames; // how long the beam fires (actor params, 0 = kFireFrames)
    float glow;     // how strongly it lights up the room (0..1)
};

// The room takes on the beam's color while a laser fires: added to the ambient light and the fog
// Blue, like the beam
constexpr float kGlowAmbient[3] = { 26.0f, 50.0f, 110.0f };
constexpr float kGlowFog[3] = { 36.0f, 75.0f, 150.0f };
float sRoomGlow = 0.0f;
bool sRoomGlowApplied = false;

s16 sLaserActorId = -1;

ColliderCylinderInit sCylinderInit = {
    { COLTYPE_NONE, AT_ON | AT_TYPE_ENEMY, AC_NONE, OC1_NONE, OC2_NONE, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK0, { 0xFFCFFFFF, 0x00, 0x08 }, { 0x00000000, 0x00, 0x00 }, TOUCH_ON | TOUCH_SFX_NORMAL, BUMP_NONE,
      OCELEM_NONE },
    { 18, 50, -5, { 0, 0, 0 } },
};

V3 ToV3(const Vec3f& v) {
    return { v.x, v.y, v.z };
}

V3 MoveToward(V3 from, V3 to, float step) {
    V3 d = to - from;
    float len = sqrtf(Dot(d, d));
    return (len <= step) ? to : from + d * (step / len);
}

void UpdateOrigin(GohmaLaser* self) {
    if (self->actor.parent != nullptr && self->actor.parent->update != nullptr) {
        self->origin = ToV3(self->actor.parent->focus.pos);
    }
}

void UpdateImpact(GohmaLaser* self, PlayState* play) {
    V3 dir = Normalize(self->target - self->origin);
    Vec3f a = { self->origin.x, self->origin.y, self->origin.z };
    V3 far = self->origin + dir * 2500.0f;
    Vec3f b = { far.x, far.y, far.z };
    Vec3f hit;
    CollisionPoly* poly = nullptr;
    s32 bgId;
    self->impact = BgCheck_EntityLineTest1(&play->colCtx, &a, &b, &hit, &poly, true, true, true, true, &bgId)
                       ? ToV3(hit)
                       : self->target;
}

void GohmaLaser_Init(Actor* thisx, PlayState* play) {
    GohmaLaser* self = (GohmaLaser*)thisx;
    Player* player = GET_PLAYER(play);

    thisx->room = -1;
    self->timer = 0;
    self->hitCooldown = 0;
    self->fireFrames = (thisx->params > 0) ? thisx->params : kFireFrames;
    self->glow = 0.0f;
    self->origin = ToV3(thisx->world.pos);
    UpdateOrigin(self);

    V3 linkPos = ToV3(player->actor.world.pos);
    V3 toShooter = self->origin - linkPos;
    toShooter.y = 0.0f;
    self->target = linkPos + Normalize(toShooter) * kStartAhead;
    self->target.y = linkPos.y;
    UpdateImpact(self, play);

    Collider_InitCylinder(play, &self->collider);
    Collider_SetCylinder(play, &self->collider, thisx, &sCylinderInit);
    Audio_PlayActorSound2(thisx, NA_SE_EN_BIMOS_AIM);
}

void GohmaLaser_Destroy(Actor* thisx, PlayState* play) {
    GohmaLaser* self = (GohmaLaser*)thisx;
    Audio_StopSfxByPos(&thisx->projectedPos);
    Collider_DestroyCylinder(play, &self->collider);
}

void GohmaLaser_Update(Actor* thisx, PlayState* play) {
    GohmaLaser* self = (GohmaLaser*)thisx;
    Player* player = GET_PLAYER(play);
    V3 linkPos = ToV3(player->actor.world.pos);

    self->timer++;
    if (self->timer > kAimFrames + self->fireFrames + kFadeFrames) {
        Actor_Kill(thisx);
        return;
    }

    UpdateOrigin(self);
    bool firing = self->timer > kAimFrames && self->timer <= kAimFrames + self->fireFrames;
    // A faint tint while aiming, the full glow while firing, fading with the beam
    if (self->timer <= kAimFrames) {
        self->glow = 0.2f * self->timer / kAimFrames;
    } else if (firing) {
        self->glow = 1.0f;
    } else {
        self->glow = 1.0f - (float)(self->timer - kAimFrames - self->fireFrames) / kFadeFrames;
    }
    self->target = MoveToward(self->target, linkPos, firing ? kTrackSpeed : kAimSpeed);
    UpdateImpact(self, play);
    thisx->world.pos = { self->impact.x, self->impact.y, self->impact.z };

    if (!firing) {
        return;
    }

    Actor_PlaySfx_Flagged(thisx, NA_SE_EN_BIMOS_LAZER_GND - SFX_FLAG);
    if (self->timer == kAimFrames + 1) {
        Audio_PlayActorSound2(thisx, NA_SE_EN_BIMOS_LAZER);
    }

    // Sparks and smoke where the beam hits
    if (self->timer % 2 == 0) {
        Vec3f pos = thisx->world.pos;
        Vec3f vel = { Rand_CenteredFloat(5.0f), 3.0f + Rand_ZeroOne() * 3.0f, Rand_CenteredFloat(5.0f) };
        Vec3f accel = { 0.0f, -0.4f, 0.0f };
        Color_RGBA8 prim = { 255, 255, 220, 255 };
        Color_RGBA8 env = { 70, 130, 255, 255 };
        EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 2200, 10);
    }
    if (self->timer % 6 == 0) {
        Vec3f pos = thisx->world.pos;
        Vec3f vel = { 0.0f, 1.2f, 0.0f };
        Vec3f accel = { 0.0f, 0.1f, 0.0f };
        Color_RGBA8 prim = { 120, 90, 90, 255 };
        Color_RGBA8 env = { 60, 40, 40, 255 };
        EffectSsDust_Spawn(play, 0, &pos, &vel, &accel, &prim, &env, 120, 20, 14, 0);
    }

    if (self->hitCooldown > 0) {
        self->hitCooldown--;
    } else if (self->collider.base.atFlags & (AT_HIT | AT_BOUNCED)) {
        self->hitCooldown = kHitCooldown;
    }
    if (self->hitCooldown == 0) {
        Collider_UpdateCylinder(thisx, &self->collider);
        CollisionCheck_SetAT(play, &play->colChkCtx, &self->collider.base);
    }
}

// One camera-facing quad from a to b
void DrawBeamQuad(Gfx*& dl, PlayState* play, V3 a, V3 b, V3 eyeLocal, float width, V3 color, float alpha) {
    V3 dir = Normalize(b - a);
    V3 mid = (a + b) * 0.5f;
    V3 side = Normalize(Cross(dir, eyeLocal - mid)) * width;
    V3 corners[4] = { a - side, a + side, b - side, b + side };
    Vtx* vtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, 4 * sizeof(Vtx));
    for (int i = 0; i < 4; i++) {
        Vtx_t& out = vtx[i].v;
        out.ob[0] = (s16)corners[i].x;
        out.ob[1] = (s16)corners[i].y;
        out.ob[2] = (s16)corners[i].z;
        out.flag = 0;
        out.tc[0] = out.tc[1] = 0;
        out.cn[0] = ToU8(color.x);
        out.cn[1] = ToU8(color.y);
        out.cn[2] = ToU8(color.z);
        out.cn[3] = ToU8(alpha);
    }
    gSPVertex(dl++, (uintptr_t)vtx, 4, 0);
    gSP2Triangles(dl++, 0, 1, 3, 0, 0, 3, 2, 0);
}

void DrawFlash(PlayState* play, V3 pos, float scale, float alpha) {
    gDPSetPrimColor(play->state.gfxCtx->polyXlu.p++, 0, 0x80, 255, 255, 255, ToU8(alpha));
    Matrix_Translate(pos.x, pos.y, pos.z, MTXMODE_NEW);
    Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
    Matrix_Scale(scale, scale, 1.0f, MTXMODE_APPLY);
    gSPMatrix(play->state.gfxCtx->polyXlu.p++, MATRIX_NEWMTX(play->state.gfxCtx),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(play->state.gfxCtx->polyXlu.p++, (Gfx*)gEffFlash1DL);
}

} // namespace

// extern "C": OPEN_DISPS declares C-linkage helpers at block scope
extern "C" void GohmaLaser_Draw(Actor* thisx, PlayState* play) {
    GohmaLaser* self = (GohmaLaser*)thisx;
    const s16 t = self->timer;
    const bool aiming = t <= kAimFrames;
    float strength;
    if (aiming) {
        strength = (play->gameplayFrames & 1) ? 0.35f : 0.6f; // flickering aim line
    } else if (t <= kAimFrames + self->fireFrames) {
        strength = 0.9f + 0.1f * sinf(t * 1.7f);
    } else {
        strength = 1.0f - (float)(t - kAimFrames - self->fireFrames) / kFadeFrames;
    }

    OPEN_DISPS(play->state.gfxCtx);

    // Glow at the eye and at the impact point
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gDPSetEnvColor(POLY_XLU_DISP++, 60, 120, 255, 255);
    DrawFlash(play, self->origin, aiming ? 0.015f + t * 0.0012f : 0.05f, strength);
    if (!aiming) {
        DrawFlash(play, self->impact, 0.06f + 0.01f * sinf(t * 2.3f), strength);
    }

    Gfx* dl = POLY_XLU_DISP;
    gDPPipeSync(dl++);
    gDPSetCycleType(dl++, G_CYC_1CYCLE);
    gDPSetRenderMode(dl++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
    gDPSetCombineLERP(dl++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE);
    gSPTexture(dl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gSPClearGeometryMode(dl++, G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_FOG | G_CULL_BOTH);
    gSPSetGeometryMode(dl++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
    Matrix_Translate(self->origin.x, self->origin.y, self->origin.z, MTXMODE_NEW);
    gSPMatrix(dl++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    const V3 a = { 0.0f, 0.0f, 0.0f };
    const V3 b = self->impact - self->origin;
    const V3 eyeLocal = V3{ play->view.eye.x, play->view.eye.y, play->view.eye.z } - self->origin;
    if (aiming) {
        DrawBeamQuad(dl, play, a, b, eyeLocal, 1.2f, { 0.4f, 0.6f, 1.0f }, strength);
    } else {
        float pulse = 1.0f + 0.15f * sinf(t * 2.9f);
        DrawBeamQuad(dl, play, a, b, eyeLocal, 14.0f * pulse, { 0.2f, 0.4f, 1.0f }, 0.3f * strength);
        DrawBeamQuad(dl, play, a, b, eyeLocal, 6.0f * pulse, { 0.55f, 0.75f, 1.0f }, 0.7f * strength);
        DrawBeamQuad(dl, play, a, b, eyeLocal, 2.2f, { 1.0f, 1.0f, 1.0f }, strength);
    }
    POLY_XLU_DISP = dl;

    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void ZOP_SpawnGohmaLaserTimed(PlayState* play, void* shooterRef, int fireFrames) {
    Actor* shooter = (Actor*)shooterRef;
    Actor_SpawnAsChild(&play->actorCtx, shooter, play, sLaserActorId, shooter->focus.pos.x, shooter->focus.pos.y,
                       shooter->focus.pos.z, 0, 0, 0, (s16)fireFrames);
}

extern "C" void ZOP_SpawnGohmaLaser(PlayState* play, void* shooterRef) {
    ZOP_SpawnGohmaLaserTimed(play, shooterRef, kFireFrames);
}

extern "C" void ZOP_StopGohmaLasers(PlayState* play) {
    for (Actor* actor = play->actorCtx.actorLists[ACTORCAT_ITEMACTION].head; actor != nullptr; actor = actor->next) {
        if (actor->id == sLaserActorId) {
            // Stop its sounds right away: the actor itself may no longer update (e.g. once Link has died)
            Audio_StopSfxByPos(&actor->projectedPos);
            Actor_Kill(actor);
        }
    }
}

// Once per frame (OnPlayerUpdate):
// - Link died: the other actors freeze, so a laser would never reach its end and its beam sound would keep looping
// - the room glow follows the strongest laser, eased so it doesn't flicker
static void LaserFrameTick() {
    if (gPlayState == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player != nullptr && ((player->stateFlags1 & PLAYER_STATE1_DEAD) || gSaveContext.health <= 0)) {
        ZOP_StopGohmaLasers(gPlayState);
    }

    float target = 0.0f;
    for (Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_ITEMACTION].head; actor != nullptr;
         actor = actor->next) {
        if (actor->id == sLaserActorId && actor->update != nullptr) {
            target = std::max(target, ((GohmaLaser*)actor)->glow);
        }
    }
    sRoomGlow += (target - sRoomGlow) * 0.25f;
    if (target == 0.0f && sRoomGlow < 0.01f) {
        sRoomGlow = 0.0f;
    }

    if (sRoomGlow > 0.0f || sRoomGlowApplied) {
        EnvironmentContext* env = &gPlayState->envCtx;
        // Unsteady like the beam itself: a fast jitter plus a slower pulse, never fully dark
        u32 f = gPlayState->gameplayFrames;
        float flicker = 0.7f + 0.2f * sinf(f * 1.9f) + 0.1f * sinf(f * 0.55f) + Rand_CenteredFloat(0.25f);
        flicker = std::clamp(flicker, 0.35f, 1.15f);
        float glow = sRoomGlow * flicker;
        for (int i = 0; i < 3; i++) {
            env->adjAmbientColor[i] = (s16)(kGlowAmbient[i] * glow);
            env->adjFogColor[i] = (s16)(kGlowFog[i] * glow);
        }
        sRoomGlowApplied = sRoomGlow > 0.0f;
    }
}

extern "C" int ZOP_GohmaLaserActive(PlayState* play) {
    for (Actor* actor = play->actorCtx.actorLists[ACTORCAT_ITEMACTION].head; actor != nullptr; actor = actor->next) {
        if (actor->id == sLaserActorId && actor->update != nullptr) {
            return true;
        }
    }
    return false;
}

extern "C" int ZOP_IsGohmaLaser(short actorId) {
    return sLaserActorId >= 0 && actorId == sLaserActorId;
}

// Testing aid (gZOP.TestEnv.LaserTest): a laser from a point in front of and above Link every 7 seconds, in any scene
static void LaserTestTick() {
    static int sFrames = 0;
    if (++sFrames % 140 != 20) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    s16 yaw = player->actor.shape.rot.y;
    Actor_Spawn(&gPlayState->actorCtx, gPlayState, sLaserActorId,
                player->actor.world.pos.x + Math_SinS(yaw) * 260.0f, player->actor.world.pos.y + 130.0f,
                player->actor.world.pos.z + Math_CosS(yaw) * 260.0f, 0, 0, 0, 0);
}

static void RegisterGohmaLaser() {
    COND_HOOK(OnPlayerUpdate, CVarGetInteger("gZOP.TestEnv.LaserTest", 0), LaserTestTick);
    COND_HOOK(OnPlayerUpdate, true, LaserFrameTick);

    if (sLaserActorId < 0) {
        ActorDBInit entry = {};
        entry.name = "ZOP_GohmaLaser";
        entry.desc = "Gohma phase 2 tracking laser";
        entry.category = ACTORCAT_ITEMACTION;
        entry.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        entry.objectId = OBJECT_GAMEPLAY_KEEP;
        entry.instanceSize = sizeof(GohmaLaser);
        entry.init = GohmaLaser_Init;
        entry.destroy = GohmaLaser_Destroy;
        entry.update = GohmaLaser_Update;
        entry.draw = GohmaLaser_Draw;
        sLaserActorId = (s16)ActorDB::Instance->AddEntry(entry).entry.id;
    }
}

static RegisterShipInitFunc initFunc(RegisterGohmaLaser, { "gZOP.TestEnv.LaserTest" });

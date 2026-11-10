// Gohma phase 2: bright rifts racing over the floor.
//
// When Gohma slams the floor during her barrage, a few glowing lanes shoot out from the impact point across the floor
// toward Link. The head of each lane hurts; it stops at walls (or after a while) and the trail fades out. Link can
// side-step them or jump over them.

#include <libultraship/bridge/consolevariablebridge.h>

#include <algorithm>

#include "soh/ActorDB.h"
#include "soh/ShipInit.hpp"
#include "NayrusLove.h"
#include "ZeldaOOTProjectHooks.h"

extern "C" {
#include <z64.h>
#include "variables.h"
#include "functions.h"
#include "macros.h"
#include "objects/gameplay_keep/gameplay_keep.h"
}

using namespace NayrusLove;

namespace {

constexpr float kSpeed = 15.0f;
constexpr float kMaxLength = 950.0f;
constexpr float kTrailLength = 280.0f; // visible glowing part behind the head
constexpr int kFadeFrames = 14;
constexpr int kHitCooldown = 20;
const V3 kColorOuter = { 0.35f, 0.6f, 1.0f };
const V3 kColorCore = { 0.85f, 0.95f, 1.0f };

struct FloorLane {
    Actor actor;
    ColliderCylinder collider;
    V3 start;
    V3 head;
    V3 dir;
    float length;
    s16 timer;
    s16 stoppedFrames; // -1 while racing, then counts the fade
    s16 hitCooldown;
};

s16 sLaneActorId = -1;

ColliderCylinderInit sCylinderInit = {
    { COLTYPE_NONE, AT_ON | AT_TYPE_ENEMY, AC_NONE, OC1_NONE, OC2_NONE, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK0, { 0xFFCFFFFF, 0x00, 0x08 }, { 0x00000000, 0x00, 0x00 }, TOUCH_ON | TOUCH_SFX_NORMAL, BUMP_NONE,
      OCELEM_NONE },
    { 22, 28, 0, { 0, 0, 0 } },
};

V3 ToV3(const Vec3f& v) {
    return { v.x, v.y, v.z };
}

void FloorLane_Init(Actor* thisx, PlayState* play) {
    FloorLane* self = (FloorLane*)thisx;
    float yaw = thisx->world.rot.y * (kPi / 0x8000);

    thisx->room = -1;
    self->start = ToV3(thisx->world.pos);
    self->head = self->start;
    self->dir = { sinf(yaw), 0.0f, cosf(yaw) };
    self->length = 0.0f;
    self->timer = 0;
    self->stoppedFrames = -1;
    self->hitCooldown = 0;
    Collider_InitCylinder(play, &self->collider);
    Collider_SetCylinder(play, &self->collider, thisx, &sCylinderInit);
}

void FloorLane_Destroy(Actor* thisx, PlayState* play) {
    FloorLane* self = (FloorLane*)thisx;
    Audio_StopSfxByPos(&thisx->projectedPos);
    Collider_DestroyCylinder(play, &self->collider);
}

void FloorLane_Update(Actor* thisx, PlayState* play) {
    FloorLane* self = (FloorLane*)thisx;
    self->timer++;

    if (self->stoppedFrames >= 0) {
        if (++self->stoppedFrames > kFadeFrames) {
            Actor_Kill(thisx);
        }
        return;
    }

    // Race forward until a wall is in the way
    V3 next = self->head + self->dir * kSpeed;
    Vec3f a = { self->head.x, self->head.y + 12.0f, self->head.z };
    Vec3f b = { next.x, next.y + 12.0f, next.z };
    Vec3f hit;
    CollisionPoly* poly = nullptr;
    s32 bgId;
    if (BgCheck_EntityLineTest1(&play->colCtx, &a, &b, &hit, &poly, true, false, false, true, &bgId)) {
        self->head = { hit.x, self->head.y, hit.z };
        self->stoppedFrames = 0;
    } else {
        self->head = next;
        self->length += kSpeed;
        if (self->length >= kMaxLength) {
            self->stoppedFrames = 0;
        }
    }
    thisx->world.pos = { self->head.x, self->head.y, self->head.z };

    Actor_PlaySfx_Flagged(thisx, NA_SE_EN_BIMOS_LAZER_GND - SFX_FLAG);

    // Sparks and a little dust at the head
    if (self->timer % 2 == 0) {
        Vec3f pos = thisx->world.pos;
        Vec3f vel = { Rand_CenteredFloat(4.0f), 2.5f + Rand_ZeroOne() * 3.0f, Rand_CenteredFloat(4.0f) };
        Vec3f accel = { 0.0f, -0.4f, 0.0f };
        Color_RGBA8 prim = { 255, 255, 255, 255 };
        Color_RGBA8 env = { 90, 150, 255, 255 };
        EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 2000, 10);
    }
    if (self->timer % 5 == 0) {
        Vec3f pos = thisx->world.pos;
        Vec3f vel = { 0.0f, 1.0f, 0.0f };
        Vec3f accel = { 0.0f, 0.1f, 0.0f };
        Color_RGBA8 prim = { 170, 180, 200, 255 };
        Color_RGBA8 env = { 90, 100, 120, 255 };
        EffectSsDust_Spawn(play, 0, &pos, &vel, &accel, &prim, &env, 100, 15, 12, 0);
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

// A flat quad on the floor from a to b; alpha fades from aAlpha at a to bAlpha at b
void DrawFloorQuad(Gfx*& dl, PlayState* play, V3 a, V3 b, float width, V3 color, float aAlpha, float bAlpha) {
    V3 dir = Normalize(b - a);
    V3 side = { -dir.z * width, 0.0f, dir.x * width };
    V3 corners[4] = { a - side, a + side, b - side, b + side };
    float alphas[4] = { aAlpha, aAlpha, bAlpha, bAlpha };
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
        out.cn[3] = ToU8(alphas[i]);
    }
    gSPVertex(dl++, (uintptr_t)vtx, 4, 0);
    gSP2Triangles(dl++, 0, 1, 3, 0, 0, 3, 2, 0);
}

} // namespace

// extern "C": OPEN_DISPS declares C-linkage helpers at block scope
extern "C" void FloorLane_Draw(Actor* thisx, PlayState* play) {
    FloorLane* self = (FloorLane*)thisx;
    float fade = (self->stoppedFrames < 0) ? 1.0f : 1.0f - (float)self->stoppedFrames / kFadeFrames;
    if (fade <= 0.0f) {
        return;
    }
    float flicker = 0.85f + 0.15f * sinf(self->timer * 2.3f);

    OPEN_DISPS(play->state.gfxCtx);

    // Glowing trail just above the floor, brightest at the head
    Gfx* dl = POLY_XLU_DISP;
    gDPPipeSync(dl++);
    gDPSetCycleType(dl++, G_CYC_1CYCLE);
    gDPSetRenderMode(dl++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
    gDPSetCombineLERP(dl++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE);
    gSPTexture(dl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gSPClearGeometryMode(dl++, G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_FOG | G_CULL_BOTH);
    gSPSetGeometryMode(dl++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);

    float trail = std::min(self->length, kTrailLength);
    V3 tail = self->head - self->dir * trail;
    Matrix_Translate(self->start.x, self->start.y + 2.0f, self->start.z, MTXMODE_NEW);
    gSPMatrix(dl++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    V3 a = tail - self->start;
    V3 b = self->head - self->start;
    if (trail > 1.0f) {
        DrawFloorQuad(dl, play, a, b, 26.0f, kColorOuter, 0.0f, 0.45f * fade * flicker);
        DrawFloorQuad(dl, play, a, b, 8.0f, kColorCore, 0.0f, 0.95f * fade);
    }
    POLY_XLU_DISP = dl;

    // Bright flare at the head while it races
    if (self->stoppedFrames < 0) {
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x80, 255, 255, 255, ToU8(0.9f * flicker));
        gDPSetEnvColor(POLY_XLU_DISP++, 80, 140, 255, 255);
        Matrix_Translate(self->head.x, self->head.y + 10.0f, self->head.z, MTXMODE_NEW);
        Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
        Matrix_Scale(0.05f, 0.05f, 1.0f, MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gEffFlash1DL);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void ZOP_SpawnGohmaFloorLanes(PlayState* play, void* shooterRef, float x, float y, float z, short yaw,
                                         int count) {
    Actor* shooter = (Actor*)shooterRef;
    // Fanned out around the given direction, the middle one straight at it
    for (int i = 0; i < count; i++) {
        float spread = (count > 1) ? ((float)i / (count - 1) - 0.5f) : 0.0f;
        s16 laneYaw = (s16)(yaw + spread * 0x2400 + Rand_CenteredFloat(0x300));
        Actor_SpawnAsChild(&play->actorCtx, shooter, play, sLaneActorId, x, y, z, 0, laneYaw, 0, 0);
    }
}

extern "C" int ZOP_IsGohmaFloorLane(short actorId) {
    return sLaneActorId >= 0 && actorId == sLaneActorId;
}

static void RegisterGohmaFloorLanes() {
    if (sLaneActorId < 0) {
        ActorDBInit entry = {};
        entry.name = "ZOP_GohmaFloorLane";
        entry.desc = "Gohma phase 2 glowing floor rift";
        entry.category = ACTORCAT_ITEMACTION;
        entry.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        entry.objectId = OBJECT_GAMEPLAY_KEEP;
        entry.instanceSize = sizeof(FloorLane);
        entry.init = FloorLane_Init;
        entry.destroy = FloorLane_Destroy;
        entry.update = FloorLane_Update;
        entry.draw = FloorLane_Draw;
        sLaneActorId = (s16)ActorDB::Instance->AddEntry(entry).entry.id;
    }
}

static RegisterShipInitFunc initFunc(RegisterGohmaFloorLanes, {});

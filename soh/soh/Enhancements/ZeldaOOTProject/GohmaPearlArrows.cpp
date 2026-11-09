// Gohma phase 2: pearl magic arrows.
//
// While brooding on the ceiling in her second phase, Gohma fires volleys of pearl shards (same look as the Nayru's
// Love burst). Each arrow fans out first, then homes in on Link, but stops steering once it is close so it can be
// dodged. Arrows stick into floor and walls, shatter on Link or his shield.

#include <libultraship/bridge/consolevariablebridge.h>

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

constexpr int kLaunchFrames = 8;     // flies straight along its fan direction before homing
constexpr float kStopHomingDist = 110.0f;
constexpr float kTurnRate = 0.13f;
constexpr float kMaxSpeed = 15.0f;
constexpr int kFlyFrames = 100;
constexpr int kStuckFrames = 50;
constexpr int kStuckFadeFrames = 15;
constexpr float kLength = 30.0f;
constexpr float kWidth = 6.0f;

enum ArrowState : u8 { ARROW_FLYING, ARROW_STUCK };

struct PearlArrow {
    Actor actor;
    ColliderCylinder collider;
    V3 pos;
    V3 dir;
    V3 trail[3];
    float speed;
    float hue;
    s16 roll;
    s16 timer;
    ArrowState state;
    bool homing;
};

s16 sArrowActorId = -1;

ColliderCylinderInit sCylinderInit = {
    { COLTYPE_NONE, AT_ON | AT_TYPE_ENEMY, AC_NONE, OC1_NONE, OC2_NONE, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK0, { 0xFFCFFFFF, 0x00, 0x08 }, { 0x00000000, 0x00, 0x00 }, TOUCH_ON | TOUCH_SFX_NORMAL, BUMP_NONE,
      OCELEM_NONE },
    { 9, 18, -9, { 0, 0, 0 } },
};

V3 ToV3(const Vec3f& v) {
    return { v.x, v.y, v.z };
}

void SpawnPearlSparkles(PlayState* play, V3 pos, int count, float speed) {
    Vec3f p = { pos.x, pos.y, pos.z };
    for (int i = 0; i < count; i++) {
        Vec3f vel = { Rand_CenteredFloat(speed), Rand_ZeroOne() * speed * 0.6f, Rand_CenteredFloat(speed) };
        Vec3f accel = { 0.0f, -0.3f, 0.0f };
        V3 tint = PearlPalette(Rand_ZeroOne());
        Color_RGBA8 prim = { 255, 255, 255, 255 };
        Color_RGBA8 env = { ToU8(tint.x), ToU8(tint.y), ToU8(tint.z), 255 };
        EffectSsKiraKira_SpawnDispersed(play, &p, &vel, &accel, &prim, &env, 1600, 12);
    }
}

void Shatter(PearlArrow* self, PlayState* play) {
    Vec3f pos = { self->pos.x, self->pos.y, self->pos.z };
    SoundSource_PlaySfxAtFixedWorldPos(play, &pos, 15, NA_SE_EV_ICE_BROKEN);
    SpawnPearlSparkles(play, self->pos, 8, 6.0f);
    Actor_Kill(&self->actor);
}

void PearlArrow_Init(Actor* thisx, PlayState* play) {
    PearlArrow* self = (PearlArrow*)thisx;
    // Launch direction is passed in world.rot (x = pitch, positive = downward; y = yaw)
    float pitch = thisx->world.rot.x * (kPi / 0x8000);
    float yaw = thisx->world.rot.y * (kPi / 0x8000);

    thisx->room = -1;
    self->pos = ToV3(thisx->world.pos);
    self->dir = { cosf(pitch) * sinf(yaw), -sinf(pitch), cosf(pitch) * cosf(yaw) };
    for (V3& t : self->trail) {
        t = self->pos;
    }
    self->speed = 7.0f;
    self->hue = Rand_ZeroOne();
    self->roll = (s16)(Rand_ZeroOne() * 0xFFFF);
    self->timer = 0;
    self->state = ARROW_FLYING;
    self->homing = true;

    Collider_InitCylinder(play, &self->collider);
    Collider_SetCylinder(play, &self->collider, thisx, &sCylinderInit);
}

void PearlArrow_Destroy(Actor* thisx, PlayState* play) {
    PearlArrow* self = (PearlArrow*)thisx;
    Collider_DestroyCylinder(play, &self->collider);
}

void PearlArrow_Update(Actor* thisx, PlayState* play) {
    PearlArrow* self = (PearlArrow*)thisx;
    Player* player = GET_PLAYER(play);
    self->timer++;

    if (self->state == ARROW_STUCK) {
        if (self->timer > kStuckFrames + kStuckFadeFrames) {
            Actor_Kill(thisx);
        }
        return;
    }

    // Hit Link or got blocked by his shield last frame
    if (self->collider.base.atFlags & (AT_HIT | AT_BOUNCED)) {
        Shatter(self, play);
        return;
    }

    V3 prev = self->pos;
    V3 target = ToV3(player->bodyPartsPos[PLAYER_BODYPART_WAIST]);
    V3 toTarget = target - self->pos;
    float dist = sqrtf(Dot(toTarget, toTarget));

    if (self->homing && self->timer > kLaunchFrames) {
        self->dir = Normalize(self->dir + (Normalize(toTarget) - self->dir) * kTurnRate);
        if (dist < kStopHomingDist) {
            // Commit to the current heading: close enough to be dodged by side-stepping or rolling
            self->homing = false;
        }
    }

    self->speed = (self->speed + 0.6f < kMaxSpeed) ? self->speed + 0.6f : kMaxSpeed;
    self->pos = self->pos + self->dir * self->speed;
    self->roll += 0x1800;
    self->trail[2] = self->trail[1];
    self->trail[1] = self->trail[0];
    self->trail[0] = prev;

    // Embed into floor or walls
    Vec3f a = { prev.x, prev.y, prev.z };
    V3 tipV = self->pos + self->dir * (kLength * 0.5f);
    Vec3f b = { tipV.x, tipV.y, tipV.z };
    Vec3f hit;
    CollisionPoly* poly = nullptr;
    s32 bgId;
    if (BgCheck_EntityLineTest1(&play->colCtx, &a, &b, &hit, &poly, true, true, true, true, &bgId)) {
        self->pos = ToV3(hit) - self->dir * (kLength * 0.2f);
        self->state = ARROW_STUCK;
        self->timer = 0;
        SoundSource_PlaySfxAtFixedWorldPos(play, &hit, 15, NA_SE_IT_ARROW_STICK_OBJ);
        SpawnPearlSparkles(play, ToV3(hit), 3, 3.0f);
        return;
    }
    if (self->timer > kFlyFrames) {
        Shatter(self, play);
        return;
    }

    thisx->world.pos = { self->pos.x, self->pos.y, self->pos.z };
    Collider_UpdateCylinder(thisx, &self->collider);
    CollisionCheck_SetAT(play, &play->colChkCtx, &self->collider.base);
}

} // namespace

// extern "C": OPEN_DISPS declares C-linkage helpers at block scope
extern "C" void PearlArrow_Draw(Actor* thisx, PlayState* play) {
    PearlArrow* self = (PearlArrow*)thisx;
    const float time = (float)(play->gameplayFrames % 100000);
    float scale = 1.0f;
    if (self->state == ARROW_STUCK && self->timer > kStuckFrames) {
        scale = 1.0f - (float)(self->timer - kStuckFrames) / (float)kStuckFadeFrames;
    }

    OPEN_DISPS(play->state.gfxCtx);

    // Glowing head and a short fading trail while in flight
    if (self->state == ARROW_FLYING) {
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gDPSetEnvColor(POLY_XLU_DISP++, 255, 140, 220, 255);
        V3 points[4] = { self->pos, self->trail[0], self->trail[1], self->trail[2] };
        for (int i = 0; i < 4; i++) {
            gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x80, 255, 255, 255, ToU8(0.9f - i * 0.25f));
            Matrix_Translate(points[i].x, points[i].y, points[i].z, MTXMODE_NEW);
            Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
            float s = 0.018f - i * 0.004f;
            Matrix_Scale(s, s, 1.0f, MTXMODE_APPLY);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gEffFlash1DL);
        }
    }

    Gfx* dl = POLY_XLU_DISP;
    SetupPearlShardMaterial(dl);
    DrawPearlShard(play, dl, self->pos, self->dir, self->roll, kLength, kWidth, self->hue, scale, time);
    POLY_XLU_DISP = dl;

    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void ZOP_SpawnPearlVolley(PlayState* play, void* shooterRef, int count) {
    Actor* shooter = (Actor*)shooterRef;
    Vec3f origin = shooter->focus.pos;
    s16 yawToPlayer = Actor_WorldYawTowardActor(shooter, &GET_PLAYER(play)->actor);

    // Fan out to the sides and slightly downward, then the arrows curve in on Link
    for (int i = 0; i < count; i++) {
        float spread = (count > 1) ? ((float)i / (count - 1) - 0.5f) : 0.0f;
        s16 yaw = (s16)(yawToPlayer + spread * 0x6000 + Rand_CenteredFloat(0x600));
        s16 pitch = (s16)(0x1000 + Rand_CenteredFloat(0x800));
        Actor_Spawn(&play->actorCtx, play, sArrowActorId, origin.x, origin.y, origin.z, pitch, yaw, 0, i);
    }
    SoundSource_PlaySfxAtFixedWorldPos(play, &origin, 20, NA_SE_IT_MAGIC_ARROW_SHOT);
    SpawnPearlSparkles(play, ToV3(origin), 6, 5.0f);
}

extern "C" int ZOP_IsPearlArrow(short actorId) {
    return sArrowActorId >= 0 && actorId == sArrowActorId;
}

static void RegisterGohmaPearlArrows() {
    if (sArrowActorId < 0) {
        ActorDBInit entry = {};
        entry.name = "ZOP_PearlArrow";
        entry.desc = "Gohma phase 2 pearl magic arrow";
        entry.category = ACTORCAT_ITEMACTION;
        entry.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        entry.objectId = OBJECT_GAMEPLAY_KEEP;
        entry.instanceSize = sizeof(PearlArrow);
        entry.init = PearlArrow_Init;
        entry.destroy = PearlArrow_Destroy;
        entry.update = PearlArrow_Update;
        entry.draw = PearlArrow_Draw;
        sArrowActorId = (s16)ActorDB::Instance->AddEntry(entry).entry.id;
    }
}

static RegisterShipInitFunc initFunc(RegisterGohmaPearlArrows, {});

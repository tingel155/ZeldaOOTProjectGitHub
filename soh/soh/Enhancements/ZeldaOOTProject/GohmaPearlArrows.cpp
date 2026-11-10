// Gohma phase 2: pearl magic arrows.
//
// In her second phase Gohma fires volleys of pearl shards (same look as the Nayru's Love burst). Each volley picks
// one way to fly:
//   homing    fan out, then curve in on Link, but stop steering once close so they can be dodged
//   straight  a tight bundle shot right at Link, fast and without steering
//   arc       lobbed in a high arc, coming down around Link
//   hover     rise above Gohma's head and hang there aiming, then dart at Link one after another
// Arrows stick into floor and walls, shatter on Link or his shield.

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
constexpr int kFlyFrames = 100;
constexpr int kStuckFrames = 50;
constexpr int kStuckFadeFrames = 15;
constexpr float kLength = 30.0f;
constexpr float kWidth = 6.0f;

enum ArrowState : u8 { ARROW_FLYING, ARROW_STUCK };
enum ArrowMode : u8 { MODE_HOMING, MODE_STRAIGHT, MODE_ARC, MODE_HOVER, MODE_COUNT };

constexpr float kArcGravity = 0.7f;
constexpr int kHoverRiseFrames = 14;
constexpr float kHoverHeight = 110.0f;
constexpr float kHoverRadius = 55.0f;
constexpr float kDashSpeed = 24.0f;

struct PearlArrow {
    Actor actor;
    ColliderCylinder collider;
    V3 pos;
    V3 dir;
    V3 trail[3];
    float speed;
    float maxSpeed;
    float accel;
    float hue;
    s16 roll;
    s16 timer;
    ArrowState state;
    bool homing;
    ArrowMode mode;
    V3 vel;      // arc: ballistic velocity
    V3 hoverPos; // hover: where it waits above Gohma
    s16 dashAt;  // hover: frame on which it darts off
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
    // Every arrow gets its own pace: some drift in slowly, some streak at Link
    self->speed = 5.0f + Rand_ZeroOne() * 4.0f;
    self->maxSpeed = 10.0f + Rand_ZeroOne() * 13.0f;
    self->accel = 0.35f + Rand_ZeroOne() * 0.75f;
    self->hue = Rand_ZeroOne();
    self->roll = (s16)(Rand_ZeroOne() * 0xFFFF);
    self->timer = 0;
    self->state = ARROW_FLYING;
    self->homing = true;

    // params: bits 0-3 index in the volley, 4-7 volley size, 8-11 flight mode
    int index = thisx->params & 0xF;
    int count = (thisx->params >> 4) & 0xF;
    self->mode = (ArrowMode)((thisx->params >> 8) & 0xF);
    if (count == 0) {
        count = 1;
    }
    Player* player = GET_PLAYER(play);
    V3 linkPos = ToV3(player->actor.world.pos);

    switch (self->mode) {
        case MODE_STRAIGHT:
            self->homing = false;
            self->speed = 9.0f;
            self->maxSpeed = 17.0f + Rand_ZeroOne() * 3.0f;
            self->accel = 0.8f;
            break;

        case MODE_ARC: {
            // Ballistic throw that lands near Link after T frames
            float t = 28.0f + Rand_ZeroOne() * 12.0f;
            float angle = Rand_ZeroOne() * 2.0f * kPi;
            float off = Rand_ZeroOne() * 50.0f;
            V3 target = { linkPos.x + sinf(angle) * off, linkPos.y + 10.0f, linkPos.z + cosf(angle) * off };
            V3 d = target - self->pos;
            self->vel = { d.x / t, (d.y + 0.5f * kArcGravity * t * t) / t, d.z / t };
            self->dir = Normalize(self->vel);
            self->homing = false;
            break;
        }

        case MODE_HOVER: {
            // Spread on a ring above her head; each one darts off a little after the previous
            float angle = (float)index / count * 2.0f * kPi;
            // Above her head on the floor; below her when she hangs on the ceiling
            float up = (self->pos.y - linkPos.y > 250.0f) ? -kHoverHeight * 0.8f : kHoverHeight;
            self->hoverPos = { self->pos.x + sinf(angle) * kHoverRadius, self->pos.y + up,
                               self->pos.z + cosf(angle) * kHoverRadius };
            self->dashAt = (s16)(kHoverRiseFrames + 10 + index * 6);
            self->homing = false;
            break;
        }

        default:
            break;
    }

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

    switch (self->mode) {
        case MODE_ARC:
            self->vel.y -= kArcGravity;
            self->pos = self->pos + self->vel;
            self->dir = Normalize(self->vel);
            break;

        case MODE_HOVER:
            if (self->timer < self->dashAt) {
                // Rise to its spot above Gohma, then hang there with a slight bob, pointing at Link
                V3 spot = self->hoverPos;
                spot.y += sinf(self->timer * 0.5f) * 3.0f;
                self->pos = self->pos + (spot - self->pos) * ((self->timer <= kHoverRiseFrames) ? 0.25f : 0.5f);
                self->dir = Normalize(self->dir + (Normalize(toTarget) - self->dir) * 0.35f);
            } else {
                if (self->timer == self->dashAt) {
                    self->dir = Normalize(toTarget);
                    Vec3f pos = { self->pos.x, self->pos.y, self->pos.z };
                    SoundSource_PlaySfxAtFixedWorldPos(play, &pos, 15, NA_SE_IT_MAGIC_ARROW_SHOT);
                }
                self->pos = self->pos + self->dir * kDashSpeed;
            }
            break;

        default: // homing and straight
            if (self->homing && self->timer > kLaunchFrames) {
                self->dir = Normalize(self->dir + (Normalize(toTarget) - self->dir) * kTurnRate);
                if (dist < kStopHomingDist) {
                    // Commit to the current heading: close enough to be dodged by side-stepping or rolling
                    self->homing = false;
                }
            }
            self->speed = (self->speed + self->accel < self->maxSpeed) ? self->speed + self->accel : self->maxSpeed;
            self->pos = self->pos + self->dir * self->speed;
            break;
    }
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
    if (self->timer > kFlyFrames + ((self->mode == MODE_HOVER) ? self->dashAt : 0)) {
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
    Player* player = GET_PLAYER(play);
    s16 yawToPlayer = Actor_WorldYawTowardActor(shooter, &player->actor);

    // Launch pitch aims at Link's waist (positive = downward): steeply down from the ceiling, almost level from the
    // floor, so the arrows no longer dive into the ground before they start homing
    Vec3f* waist = &player->bodyPartsPos[PLAYER_BODYPART_WAIST];
    float dx = waist->x - origin.x;
    float dz = waist->z - origin.z;
    float drop = origin.y - waist->y;
    s16 pitchToPlayer = (s16)(atan2f(drop, sqrtf(dx * dx + dz * dz)) * (0x8000 / kPi));

    // Each volley flies one way (see the top of the file)
    int mode = (int)(Rand_ZeroOne() * (float)MODE_COUNT);
    if (mode >= MODE_COUNT) {
        mode = MODE_HOMING;
    }
    count = CLAMP(count, 1, 15);

    for (int i = 0; i < count; i++) {
        float spread = (count > 1) ? ((float)i / (count - 1) - 0.5f) : 0.0f;
        // Homing arrows fan out wide, a straight bundle stays tight
        float fan = (mode == MODE_STRAIGHT) ? (float)0x1400 : (float)0x6000;
        s16 yaw = (s16)(yawToPlayer + spread * fan + Rand_CenteredFloat(0x600));
        s16 pitch = (s16)(pitchToPlayer - 0x200 + Rand_CenteredFloat(0x500)); // a touch upward, never into the floor
        s16 params = (s16)(i | (count << 4) | (mode << 8));
        Actor_Spawn(&play->actorCtx, play, sArrowActorId, origin.x, origin.y, origin.z, pitch, yaw, 0, params);
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

// Nayru's Love: the crystal bursts.
//
// The crystal shatters when
//  - its time runs out,
//  - enemies have hit it often enough (gZOP.NayrusLove.HitsToShatter), or
//  - the player casts Nayru's Love again while it is still active.
// It winds up for a few frames (swelling, whitening, trembling), then explodes with a flash, shockwaves, a camera
// quake and rumble. Pearl shards fly out, damage enemies they touch and stick into the floor and walls.

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/ActorDB.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "NayrusLove.h"

extern "C" {
#include <z64.h>
#include "variables.h"
#include "functions.h"
#include "macros.h"
#include "objects/gameplay_keep/gameplay_keep.h"
extern PlayState* gPlayState;
void MagicDark_DiamondDraw(Actor* thisx, PlayState* play);
void NayrusCrystal_Draw(Actor* thisx, PlayState* play);
}

#define CVAR_SHATTER_NAME CVAR_ZOP_NAYRU("Shatter")
#define CVAR_SHATTER_VALUE CVarGetInteger(CVAR_SHATTER_NAME, 1)
#define CVAR_HITS_TO_SHATTER_VALUE CVarGetInteger(CVAR_ZOP_NAYRU("HitsToShatter"), 6)

using namespace NayrusLove;

namespace {

constexpr int kChargeFrames = 7;     // wind-up before the burst
constexpr s16 kTimeShatterAt = 1170; // vanilla fades the crystal out from 1180 and ends it at 1200
constexpr int kHitCooldown = 8;      // one enemy attack stays in contact for several frames

CrystalFx sFx;
Actor* sCrystal = nullptr;
int sHits = 0;
int sHitCooldown = 0;
int sChargeTimer = -1;
s16 sShardsActorId = -1;

bool IsCrystalPhase(Actor* actor) {
    return (actor->draw == MagicDark_DiamondDraw) || (actor->draw == NayrusCrystal_Draw);
}

void ResetState() {
    sFx = {};
    sHits = 0;
    sHitCooldown = 0;
    sChargeTimer = -1;
}

Vec3f CrystalCenter(Actor* crystal) {
    Vec3f center = crystal->world.pos;
    center.y += GetCrystalYOffset(crystal->scale.y / 0.6f) + 18.0f; // 18 = middle of the gem shape above its origin
    return center;
}

void StartCharge() {
    if (sChargeTimer < 0 && sCrystal != nullptr) {
        sChargeTimer = 0;
        Vec3f center = CrystalCenter(sCrystal);
        SoundSource_PlaySfxAtFixedWorldPos(gPlayState, &center, 20, NA_SE_EV_ICE_FREEZE);
    }
}

void Shatter(Actor* crystal) {
    Vec3f center = CrystalCenter(crystal);
    Player* player = GET_PLAYER(gPlayState);

    Actor_Spawn(&gPlayState->actorCtx, gPlayState, sShardsActorId, center.x, center.y, center.z, 0, 0, 0, 0);

    // End the spell; MagicDark_Destroy resets the magic meter state once the timer is 0
    gSaveContext.nayrusLoveTimer = 0;
    player->invincibilityTimer = 0;
    Actor_Kill(crystal);
    sCrystal = nullptr;
    ResetState();
}

void OnCrystalUpdate(Actor* crystal) {
    if (!IsCrystalPhase(crystal) || crystal->update == nullptr) {
        return;
    }
    if (sCrystal != crystal) {
        sCrystal = crystal;
        ResetState();
    }

    sFx.hitFlash *= 0.72f;
    if (sHitCooldown > 0) {
        sHitCooldown--;
    }

    const int hitsNeeded = CVAR_HITS_TO_SHATTER_VALUE;
    sFx.damage = (hitsNeeded > 0) ? Clamp01((float)sHits / (float)hitsNeeded) : 0.0f;

    if (gSaveContext.nayrusLoveTimer >= kTimeShatterAt || (hitsNeeded > 0 && sHits >= hitsNeeded)) {
        StartCharge();
    }

    if (sChargeTimer >= 0) {
        // Hold the vanilla timer so the crystal stays fully visible and is not removed during the wind-up
        gSaveContext.nayrusLoveTimer = 1000;
        sChargeTimer++;
        sFx.charge = (float)sChargeTimer / (float)kChargeFrames;
        if (sChargeTimer >= kChargeFrames) {
            Shatter(crystal);
        }
    }
}

// Runs before Player_Update, while the AC result of last frame's collision check is still on the player's colliders
void OnPlayerPreUpdate(Player* player) {
    if (sCrystal == nullptr || sChargeTimer >= 0) {
        return;
    }

    // Blocked hits: under Nayru's Love the player keeps registering AC hits but takes no damage
    bool wasHit = (player->cylinder.base.acFlags & AC_HIT) || (player->shieldQuad.base.acFlags & AC_BOUNCED);
    if (wasHit && player->invincibilityTimer < 0 && sHitCooldown == 0) {
        sHits++;
        sHitCooldown = kHitCooldown;
        sFx.hitFlash = 1.0f;

        Vec3f hitPos = { (f32)player->cylinder.info.bumper.hitPos.x, (f32)player->cylinder.info.bumper.hitPos.y,
                         (f32)player->cylinder.info.bumper.hitPos.z };
        if (!(player->cylinder.base.acFlags & AC_HIT)) {
            hitPos = CrystalCenter(sCrystal);
        }
        SoundSource_PlaySfxAtFixedWorldPos(gPlayState, &hitPos, 20, NA_SE_IT_SHIELD_REFLECT_SW);
        for (int i = 0; i < 6; i++) {
            Vec3f vel = { Rand_CenteredFloat(6.0f), Rand_ZeroOne() * 4.0f, Rand_CenteredFloat(6.0f) };
            Vec3f accel = { 0.0f, -0.3f, 0.0f };
            V3 tint = PearlPalette(Rand_ZeroOne());
            Color_RGBA8 prim = { 255, 255, 255, 255 };
            Color_RGBA8 env = { ToU8(tint.x), ToU8(tint.y), ToU8(tint.z), 255 };
            EffectSsKiraKira_SpawnDispersed(gPlayState, &hitPos, &vel, &accel, &prim, &env, 1800, 14);
        }
    }

    // Casting Nayru's Love again while it is active bursts the crystal
    static const u16 kCButtons[3] = { BTN_CLEFT, BTN_CDOWN, BTN_CRIGHT };
    Input* input = &gPlayState->state.input[0];
    if (gPlayState->pauseCtx.state != 0 || gPlayState->msgCtx.msgMode != MSGMODE_NONE || player->csAction != 0) {
        return;
    }
    for (int i = 0; i < 3; i++) {
        if (gSaveContext.equips.buttonItems[i + 1] == ITEM_NAYRUS_LOVE &&
            CHECK_BTN_ALL(input->press.button, kCButtons[i])) {
            input->press.button &= ~kCButtons[i];
            StartCharge();
            break;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Shards actor: one actor runs the burst effects and all shards (physics, AT collider, drawing)

constexpr int kShardCount = 32;
constexpr int kShardFlyFrames = 60;
constexpr int kShardStuckFrames = 160;
constexpr int kShardFadeFrames = 20;
constexpr int kBlastFrames = 3;
constexpr float kBlastRadius = 120.0f;
constexpr float kShardVtxScale = 8.0f;

enum ShardState : u8 { SHARD_GONE, SHARD_FLYING, SHARD_STUCK };

struct Shard {
    V3 pos;
    V3 vel;
    V3 dir;
    float length;
    float width;
    float hue;
    s16 roll;
    s16 rollSpeed;
    s16 timer;
    ShardState state;
};

struct NayruShards {
    Actor actor;
    ColliderJntSph collider;
    ColliderJntSphElement elements[kShardCount + 1]; // [0] = blast sphere, [1..] = shards
    Shard shards[kShardCount];
    s16 timer;
};

ColliderJntSphElementInit sElementsInit[kShardCount + 1];
ColliderJntSphInit sColliderInit = {
    { COLTYPE_NONE, AT_ON | AT_TYPE_PLAYER, AC_NONE, OC1_NONE, OC2_NONE, COLSHAPE_JNTSPH },
    kShardCount + 1,
    sElementsInit,
};

void InitColliderData() {
    // Blast: hits like a spin attack, shards: like arrows
    sElementsInit[0] = { { ELEMTYPE_UNK0, { DMG_SPIN_MASTER, 0x00, 0x04 }, { 0, 0, 0 }, TOUCH_ON | TOUCH_SFX_NONE,
                           BUMP_NONE, OCELEM_NONE },
                         { 0, { { 0, 0, 0 }, (s16)kBlastRadius }, 100 } };
    for (int i = 1; i <= kShardCount; i++) {
        sElementsInit[i] = { { ELEMTYPE_UNK0, { DMG_ARROW_NORMAL, 0x00, 0x02 }, { 0, 0, 0 },
                               TOUCH_ON | TOUCH_SFX_NORMAL, BUMP_NONE, OCELEM_NONE },
                             { 0, { { 0, 0, 0 }, 10 }, 100 } };
    }
}

V3 ToV3(const Vec3f& v) {
    return { v.x, v.y, v.z };
}
Vec3f ToVec3f(const V3& v) {
    return { v.x, v.y, v.z };
}

void SpawnPearlSparkles(PlayState* play, Vec3f pos, int count, float speed, s16 scale, s32 life) {
    for (int i = 0; i < count; i++) {
        V3 dir = Normalize({ Rand_CenteredFloat(2.0f), Rand_CenteredFloat(2.0f), Rand_CenteredFloat(2.0f) });
        Vec3f vel = ToVec3f(dir * (speed * (0.4f + Rand_ZeroOne() * 0.6f)));
        Vec3f accel = { 0.0f, -0.15f, 0.0f };
        V3 tint = PearlPalette(Rand_ZeroOne());
        Color_RGBA8 prim = { 255, 255, 255, 255 };
        Color_RGBA8 env = { ToU8(tint.x), ToU8(tint.y), ToU8(tint.z), 255 };
        EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, scale, life);
    }
}

void NayruShards_Init(Actor* thisx, PlayState* play) {
    NayruShards* self = (NayruShards*)thisx;
    Vec3f center = thisx->world.pos;
    Vec3f zero = { 0.0f, 0.0f, 0.0f };

    thisx->room = -1;
    self->timer = 0;
    Collider_InitJntSph(play, &self->collider);
    Collider_SetJntSph(play, &self->collider, thisx, &sColliderInit, self->elements);

    for (int i = 0; i < kShardCount; i++) {
        Shard& shard = self->shards[i];
        // Mostly outward and upward arcs; some aim down and bury themselves right next to Link
        float yaw = Rand_ZeroOne() * 2.0f * kPi;
        float pitch = -0.45f + Rand_ZeroOne() * 1.25f;
        shard.dir = { cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw) };
        shard.vel = shard.dir * (15.0f + Rand_ZeroOne() * 13.0f);
        shard.pos = ToV3(center) + shard.dir * 18.0f;
        shard.length = 24.0f + Rand_ZeroOne() * 20.0f;
        shard.width = 5.5f + Rand_ZeroOne() * 4.5f;
        shard.hue = Rand_ZeroOne();
        shard.roll = (s16)(Rand_ZeroOne() * 0xFFFF);
        shard.rollSpeed = (s16)Rand_CenteredFloat(0x2800);
        shard.timer = 0;
        shard.state = SHARD_FLYING;
    }

    // Boom: bomb blast + breaking ice, flash, shockwaves, camera quake and rumble
    SoundSource_PlaySfxAtFixedWorldPos(play, &center, 40, NA_SE_IT_BOMB_EXPLOSION);
    SoundSource_PlaySfxAtFixedWorldPos(play, &center, 40, NA_SE_EV_ICE_BROKEN);
    SoundSource_PlaySfxAtFixedWorldPos(play, &center, 40, NA_SE_IT_EXPLOSION_ICE);

    s16 quake = Quake_Add(GET_ACTIVE_CAM(play), 3);
    Quake_SetSpeed(quake, 28000);
    Quake_SetQuakeValues(quake, 9, 0, 0, 0);
    Quake_SetCountdown(quake, 16);
    Rumble_Request(0.0f, 255, 24, 140);

    Color_RGBA8 ringPrim = { 255, 235, 250, 255 };
    Color_RGBA8 ringEnvPink = { 255, 110, 200, 0 };
    Color_RGBA8 ringEnvAqua = { 90, 255, 230, 0 };
    EffectSsBlast_SpawnWhiteShockwave(play, &center, &zero, &zero);
    EffectSsBlast_Spawn(play, &center, &zero, &zero, &ringPrim, &ringEnvPink, 120, 500, 40, 12);
    EffectSsBlast_Spawn(play, &center, &zero, &zero, &ringPrim, &ringEnvAqua, 60, 320, 30, 16);
    SpawnPearlSparkles(play, center, 36, 14.0f, 3000, 26);
}

void NayruShards_Destroy(Actor* thisx, PlayState* play) {
    NayruShards* self = (NayruShards*)thisx;
    Collider_DestroyJntSph(play, &self->collider);
}

void StickShard(PlayState* play, Shard& shard, const Vec3f& hitPos, int index) {
    // Bury the tip about a third of the way into the surface
    shard.pos = ToV3(hitPos) - shard.dir * (shard.length * 0.2f);
    shard.state = SHARD_STUCK;
    shard.timer = 0;
    if (index % 3 == 0) {
        Vec3f sfxPos = hitPos;
        SoundSource_PlaySfxAtFixedWorldPos(play, &sfxPos, 15, NA_SE_IT_ARROW_STICK_OBJ);
    }
    SpawnPearlSparkles(play, hitPos, 2, 3.0f, 1500, 10);
}

void NayruShards_Update(Actor* thisx, PlayState* play) {
    NayruShards* self = (NayruShards*)thisx;
    bool anyAlive = false;
    bool anyToucher = false;

    self->timer++;

    ColliderJntSphElement& blast = self->elements[0];
    if (self->timer <= kBlastFrames) {
        blast.info.toucherFlags |= TOUCH_ON;
        blast.dim.worldSphere.center = { (s16)thisx->world.pos.x, (s16)thisx->world.pos.y, (s16)thisx->world.pos.z };
        blast.dim.worldSphere.radius = (s16)kBlastRadius;
        anyToucher = true;
    } else {
        blast.info.toucherFlags &= ~TOUCH_ON;
    }

    for (int i = 0; i < kShardCount; i++) {
        Shard& shard = self->shards[i];
        ColliderJntSphElement& element = self->elements[i + 1];

        if (shard.state == SHARD_FLYING) {
            if (element.info.toucherFlags & TOUCH_HIT) {
                // Hit an enemy: the shard bursts into sparkles
                SpawnPearlSparkles(play, ToVec3f(shard.pos), 6, 5.0f, 2000, 14);
                shard.state = SHARD_GONE;
            } else {
                Vec3f prev = ToVec3f(shard.pos);
                shard.vel.y -= 1.1f;
                shard.pos = shard.pos + shard.vel;
                shard.dir = Normalize(shard.vel);
                shard.roll += shard.rollSpeed;
                shard.timer++;

                Vec3f tip = ToVec3f(shard.pos + shard.dir * (shard.length * 0.5f));
                Vec3f hitPos;
                CollisionPoly* poly = nullptr;
                s32 bgId;
                if (BgCheck_EntityLineTest1(&play->colCtx, &prev, &tip, &hitPos, &poly, true, true, true, true,
                                            &bgId)) {
                    StickShard(play, shard, hitPos, i);
                } else if (shard.timer > kShardFlyFrames) {
                    shard.state = SHARD_GONE;
                }
            }
        } else if (shard.state == SHARD_STUCK) {
            if (++shard.timer > kShardStuckFrames + kShardFadeFrames) {
                shard.state = SHARD_GONE;
            }
        }

        if (shard.state == SHARD_FLYING) {
            element.info.toucherFlags |= TOUCH_ON;
            element.dim.worldSphere.center = { (s16)shard.pos.x, (s16)shard.pos.y, (s16)shard.pos.z };
            element.dim.worldSphere.radius = 10;
            anyToucher = true;
        } else {
            element.info.toucherFlags &= ~TOUCH_ON;
            element.dim.worldSphere.radius = 0;
        }
        anyAlive |= (shard.state != SHARD_GONE);
    }

    if (anyToucher) {
        CollisionCheck_SetAT(play, &play->colChkCtx, &self->collider.base);
    }
    if (!anyAlive) {
        Actor_Kill(thisx);
    }
}

// Elongated 4-sided bipyramid along +Z: long tip forward, short tip back
constexpr int kShardTris = 8;

void DrawShard(PlayState* play, Gfx*& dl, const Shard& shard, int index, float time) {
    float fadeScale = 1.0f;
    if (shard.state == SHARD_STUCK && shard.timer > kShardStuckFrames) {
        fadeScale = 1.0f - (float)(shard.timer - kShardStuckFrames) / (float)kShardFadeFrames;
    }
    const float len = shard.length;
    const float w = shard.width;
    const V3 tipFront = { 0.0f, 0.0f, len * 0.5f };
    const V3 tipBack = { 0.0f, 0.0f, -len * 0.35f };
    const V3 ring[4] = { { w, 0.0f, len * 0.05f }, { 0.0f, w * 0.6f, len * 0.05f }, { -w, 0.0f, len * 0.05f },
                         { 0.0f, -w * 0.6f, len * 0.05f } };

    Vtx* vtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, kShardTris * 3 * sizeof(Vtx));
    int v = 0;
    auto emit = [&](const V3& p, int face, bool isTip) {
        V3 c = PearlPalette(shard.hue + face * 0.13f + time * 0.02f);
        c = Lerp(c, { 1.0f, 1.0f, 1.0f }, isTip ? 0.55f : 0.15f);
        Vtx_t& out = vtx[v++].v;
        out.ob[0] = (s16)(p.x * kShardVtxScale);
        out.ob[1] = (s16)(p.y * kShardVtxScale);
        out.ob[2] = (s16)(p.z * kShardVtxScale);
        out.flag = 0;
        out.tc[0] = out.tc[1] = 0;
        out.cn[0] = ToU8(c.x);
        out.cn[1] = ToU8(c.y);
        out.cn[2] = ToU8(c.z);
        out.cn[3] = ToU8(isTip ? 0.95f : 0.7f);
    };
    for (int k = 0; k < 4; k++) {
        emit(tipFront, k, true);
        emit(ring[k], k, false);
        emit(ring[(k + 1) % 4], k, false);
        emit(tipBack, k + 4, true);
        emit(ring[(k + 1) % 4], k + 4, false);
        emit(ring[k], k + 4, false);
    }

    // Align local +Z with the flight direction
    float yaw = atan2f(shard.dir.x, shard.dir.z);
    float pitch = -asinf(shard.dir.y < -1.0f ? -1.0f : (shard.dir.y > 1.0f ? 1.0f : shard.dir.y));
    Matrix_Translate(shard.pos.x, shard.pos.y, shard.pos.z, MTXMODE_NEW);
    Matrix_RotateY(yaw, MTXMODE_APPLY);
    Matrix_RotateX(pitch, MTXMODE_APPLY);
    Matrix_RotateZ(shard.roll * (kPi / 0x8000), MTXMODE_APPLY);
    float s = fadeScale / kShardVtxScale;
    Matrix_Scale(s, s, s, MTXMODE_APPLY);
    gSPMatrix(dl++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPVertex(dl++, (uintptr_t)vtx, kShardTris * 3, 0);
    for (int t = 0; t < kShardTris; t += 2) {
        gSP2Triangles(dl++, t * 3, t * 3 + 1, t * 3 + 2, 0, t * 3 + 3, t * 3 + 4, t * 3 + 5, 0);
    }
}

} // namespace

// extern "C": OPEN_DISPS declares C-linkage helpers at block scope
extern "C" void NayruShards_Draw(Actor* thisx, PlayState* play) {
    NayruShards* self = (NayruShards*)thisx;
    const float time = (float)(play->gameplayFrames % 100000);

    OPEN_DISPS(play->state.gfxCtx);

    // Blinding flash at the moment of the burst
    if (self->timer < 8) {
        float t = (float)self->timer / 8.0f;
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x80, 255, 255, 255, ToU8(1.0f - t));
        gDPSetEnvColor(POLY_XLU_DISP++, 255, 160, 225, 255);
        Matrix_Translate(thisx->world.pos.x, thisx->world.pos.y, thisx->world.pos.z, MTXMODE_NEW);
        Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
        float scale = 0.12f + t * 0.25f;
        Matrix_Scale(scale, scale, 1.0f, MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gEffFlash1DL);
    }

    Gfx* dl = POLY_XLU_DISP;
    gDPPipeSync(dl++);
    gDPSetCycleType(dl++, G_CYC_1CYCLE);
    gDPSetRenderMode(dl++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
    gDPSetCombineLERP(dl++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE);
    gSPTexture(dl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gSPClearGeometryMode(dl++, G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_FOG | G_CULL_BOTH);
    gSPSetGeometryMode(dl++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
    for (int i = 0; i < kShardCount; i++) {
        if (self->shards[i].state != SHARD_GONE) {
            DrawShard(play, dl, self->shards[i], i, time);
        }
    }
    POLY_XLU_DISP = dl;

    CLOSE_DISPS(play->state.gfxCtx);
}

float NayrusLove::GetCrystalYOffset(float scaleY) {
    // Lowered so Link sits in the vertical middle of the gem (the lower tip sinks into the ground)
    return CVarGetFloat(CVAR_ZOP_NAYRU("CrystalYOffset"), -30.0f) * scaleY;
}

const CrystalFx& NayrusLove::GetCrystalFx() {
    return sFx;
}

static void RegisterNayrusShatter() {
    if (sShardsActorId < 0) {
        InitColliderData();
        ActorDBInit entry = {};
        entry.name = "ZOP_NayruShards";
        entry.desc = "Nayru's Love crystal shards";
        entry.category = ACTORCAT_ITEMACTION;
        entry.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        entry.objectId = OBJECT_GAMEPLAY_KEEP;
        entry.instanceSize = sizeof(NayruShards);
        entry.init = NayruShards_Init;
        entry.destroy = NayruShards_Destroy;
        entry.update = NayruShards_Update;
        entry.draw = NayruShards_Draw;
        sShardsActorId = (s16)ActorDB::Instance->AddEntry(entry).entry.id;
    }

    COND_ID_HOOK(OnActorUpdate, ACTOR_MAGIC_DARK, CVAR_SHATTER_VALUE,
                 [](void* actor) { OnCrystalUpdate((Actor*)actor); });
    COND_ID_HOOK(OnActorDestroy, ACTOR_MAGIC_DARK, CVAR_SHATTER_VALUE, [](void* actor) {
        if (actor == sCrystal) {
            sCrystal = nullptr;
            ResetState();
        }
    });
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_PLAYER, CVAR_SHATTER_VALUE,
                 [](void* actor, bool* should) { OnPlayerPreUpdate((Player*)actor); });
}

static RegisterShipInitFunc initFunc(RegisterNayrusShatter, { CVAR_SHATTER_NAME });

// Nayru's Love: offensive side.
//
// While Nayru's Love is active
//  - Link's blade is wrapped in a flickering pearl aura,
//  - sword hits deal more damage (gZOP.NayrusLove.SwordDamageMult, default 2), and
//  - every sword hit on an enemy throws a burst of lightning with a thunder crack.

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
extern PlayState* gPlayState;
}

#define CVAR_SWORD_NAME CVAR_ZOP_NAYRU("SwordAura")
#define CVAR_SWORD_VALUE CVarGetInteger(CVAR_SWORD_NAME, 1)
#define CVAR_SWORD_DAMAGE_MULT_VALUE CVarGetFloat(CVAR_ZOP_NAYRU("SwordDamageMult"), 2.0f)

using namespace NayrusLove;

namespace {

s16 sBoltActorId = -1;
// Bolts per target are rate limited: one swing can touch several collider elements over a few frames
Actor* sLastBoltTarget = nullptr;
u32 sLastBoltFrame = 0;

bool NayrusLoveActive() {
    return CVAR_SWORD_VALUE && gSaveContext.nayrusLoveTimer != 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Blade aura, drawn in the left hand's limb space (the blade runs along +X, see sMeleeWeaponLengths)

constexpr int kAuraSegments = 8;
constexpr int kAuraPlanes = 3;
constexpr int kAuraVtxPerPlane = (kAuraSegments + 1) * 3;

void DrawBladeAura(PlayState* play, Player* player) {
    static const float kBladeLengths[] = { 0.0f, 4000.0f, 3000.0f, 5500.0f, 0.0f, 2500.0f };
    s32 weapon = Player_GetMeleeWeaponHeld(player);
    if (weapon <= 0 || weapon >= (s32)ARRAY_COUNT(kBladeLengths) || kBladeLengths[weapon] <= 0.0f) {
        return;
    }
    if (player->leftHandType != PLAYER_MODELTYPE_LH_SWORD && player->leftHandType != PLAYER_MODELTYPE_LH_BGS) {
        return;
    }

    const float time = (float)(play->gameplayFrames % 100000);
    const float bladeStart = 700.0f;
    const float bladeEnd = kBladeLengths[weapon] * 1.08f;
    const float baseWidth = 520.0f;
    // A fresh hit (bolt within the last frames) makes the aura flare up
    const float flare = (play->gameplayFrames - sLastBoltFrame < 6) ? 1.0f : 0.0f;

    // Written straight into the XLU list: OPEN_DISPS cannot be used inside this namespace (C-linkage helpers)
    Gfx* dl = play->state.gfxCtx->polyXlu.p;
    gDPPipeSync(dl++);
    gDPSetCycleType(dl++, G_CYC_1CYCLE);
    gDPSetRenderMode(dl++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
    gDPSetCombineLERP(dl++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE);
    gSPTexture(dl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gSPClearGeometryMode(dl++, G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_FOG | G_CULL_BOTH);
    gSPSetGeometryMode(dl++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
    gSPMatrix(dl++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    // Three crossed planes along the blade so the aura reads from every angle. Each plane is a strip of
    // edge/center/edge vertices: opaque glowing center line, fully transparent outer edges.
    for (int plane = 0; plane < kAuraPlanes; plane++) {
        float angle = plane * (kPi / kAuraPlanes);
        float dirY = cosf(angle);
        float dirZ = sinf(angle);
        Vtx* vtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, kAuraVtxPerPlane * sizeof(Vtx));

        for (int i = 0; i <= kAuraSegments; i++) {
            float t = (float)i / kAuraSegments;
            float x = bladeStart + (bladeEnd - bladeStart) * t;
            // Flame-like flicker: width breathes along the blade and over time, tapering to a point at the tip
            float wobble = 0.75f + 0.25f * sinf(time * 1.7f + i * 1.3f + plane * 2.1f) +
                           0.15f * sinf(time * 3.1f - i * 0.7f);
            float taper = (t < 0.85f) ? 1.0f : (1.0f - (t - 0.85f) / 0.15f * 0.8f);
            float width = baseWidth * wobble * taper * (1.0f + flare * 0.6f);
            V3 color = Lerp(PearlPalette(t * 0.8f - time * 0.04f + plane * 0.33f), { 1.0f, 1.0f, 1.0f },
                            0.35f + flare * 0.4f);
            float coreAlpha = (0.55f + 0.25f * sinf(time * 2.3f + i)) * (0.4f + 0.6f * taper) + flare * 0.3f;

            for (int side = -1; side <= 1; side++) {
                Vtx_t& out = vtx[i * 3 + side + 1].v;
                out.ob[0] = (s16)x;
                out.ob[1] = (s16)(400.0f + dirY * width * side);
                out.ob[2] = (s16)(dirZ * width * side);
                out.flag = 0;
                out.tc[0] = out.tc[1] = 0;
                out.cn[0] = ToU8(color.x);
                out.cn[1] = ToU8(color.y);
                out.cn[2] = ToU8(color.z);
                out.cn[3] = (side == 0) ? ToU8(coreAlpha) : 0;
            }
        }

        gSPVertex(dl++, (uintptr_t)vtx, kAuraVtxPerPlane, 0);
        for (int i = 0; i < kAuraSegments; i++) {
            int a = i * 3;
            int b = (i + 1) * 3;
            gSP2Triangles(dl++, a, a + 1, b + 1, 0, a, b + 1, b, 0);
            gSP2Triangles(dl++, a + 1, a + 2, b + 2, 0, a + 1, b + 2, b + 1, 0);
        }
    }

    play->state.gfxCtx->polyXlu.p = dl;
}

// ---------------------------------------------------------------------------------------------------------------------
// Lightning burst actor, spawned at the point where the sword hit

constexpr int kBoltCount = 6;
constexpr int kBoltSegments = 7;
constexpr int kBoltLife = 9;

struct NayruBolt {
    Actor actor;
    V3 dirs[kBoltCount];
    float lengths[kBoltCount];
    s16 timer;
};

void NayruBolt_Init(Actor* thisx, PlayState* play) {
    NayruBolt* self = (NayruBolt*)thisx;
    thisx->room = -1;
    self->timer = 0;
    for (int i = 0; i < kBoltCount; i++) {
        self->dirs[i] = Normalize({ Rand_CenteredFloat(2.0f), Rand_CenteredFloat(1.6f) + 0.2f, Rand_CenteredFloat(2.0f) });
        self->lengths[i] = 45.0f + Rand_ZeroOne() * 55.0f;
    }

    Vec3f pos = thisx->world.pos;
    SoundSource_PlaySfxAtFixedWorldPos(play, &pos, 20, NA_SE_EN_FANTOM_THUNDER);
    SoundSource_PlaySfxAtFixedWorldPos(play, &pos, 20, NA_SE_IT_SHIELD_REFLECT_SW);
    Rumble_Request(0.0f, 180, 6, 120);

    for (int i = 0; i < 8; i++) {
        Vec3f vel = { Rand_CenteredFloat(9.0f), Rand_ZeroOne() * 6.0f, Rand_CenteredFloat(9.0f) };
        Vec3f accel = { 0.0f, -0.4f, 0.0f };
        V3 tint = PearlPalette(Rand_ZeroOne());
        Color_RGBA8 prim = { 255, 255, 255, 255 };
        Color_RGBA8 env = { ToU8(tint.x), ToU8(tint.y), ToU8(tint.z), 255 };
        EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 2200, 12);
    }
}

void NayruBolt_Update(Actor* thisx, PlayState* play) {
    NayruBolt* self = (NayruBolt*)thisx;
    if (++self->timer > kBoltLife) {
        Actor_Kill(thisx);
    }
}

// Appends one camera-facing ribbon (a quad per segment) through the given points
void DrawRibbon(Gfx*& dl, PlayState* play, const V3* points, int count, float width, V3 color, float alpha,
                V3 eyeLocal) {
    Vtx* vtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, count * 2 * sizeof(Vtx));
    for (int i = 0; i < count; i++) {
        V3 along = Normalize(points[(i + 1 < count) ? i + 1 : i] - points[(i > 0) ? i - 1 : i]);
        V3 side = Normalize(Cross(along, eyeLocal - points[i])) * (width * (1.0f - 0.6f * i / (count - 1)));
        for (int s = 0; s < 2; s++) {
            V3 p = points[i] + side * (s == 0 ? -1.0f : 1.0f);
            Vtx_t& out = vtx[i * 2 + s].v;
            out.ob[0] = (s16)p.x;
            out.ob[1] = (s16)p.y;
            out.ob[2] = (s16)p.z;
            out.flag = 0;
            out.tc[0] = out.tc[1] = 0;
            out.cn[0] = ToU8(color.x);
            out.cn[1] = ToU8(color.y);
            out.cn[2] = ToU8(color.z);
            out.cn[3] = ToU8(alpha);
        }
    }
    gSPVertex(dl++, (uintptr_t)vtx, count * 2, 0);
    for (int i = 0; i + 1 < count; i++) {
        int a = i * 2;
        gSP2Triangles(dl++, a, a + 1, a + 3, 0, a, a + 3, a + 2, 0);
    }
}

} // namespace

// extern "C": OPEN_DISPS declares C-linkage helpers at block scope
extern "C" void NayruBolt_Draw(Actor* thisx, PlayState* play) {
    NayruBolt* self = (NayruBolt*)thisx;
    const V3 origin = { thisx->world.pos.x, thisx->world.pos.y, thisx->world.pos.z };
    const V3 eyeLocal = V3{ play->view.eye.x, play->view.eye.y, play->view.eye.z } - origin;
    const float life = 1.0f - (float)self->timer / (float)kBoltLife;
    // Bolts strike out fast, then flicker while fading
    const float reach = Clamp01((float)(self->timer + 1) / 3.0f);
    const float flicker = (self->timer > 2 && (play->gameplayFrames & 1)) ? 0.55f : 1.0f;

    OPEN_DISPS(play->state.gfxCtx);

    // Flash at the impact point
    if (self->timer < 5) {
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x80, 255, 255, 255, ToU8(1.0f - self->timer / 5.0f));
        gDPSetEnvColor(POLY_XLU_DISP++, 120, 220, 255, 255);
        Matrix_Translate(origin.x, origin.y, origin.z, MTXMODE_NEW);
        Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
        float s = 0.03f + self->timer * 0.012f;
        Matrix_Scale(s, s, 1.0f, MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gEffFlash1DL);
    }

    Gfx* dl = POLY_XLU_DISP;
    gDPPipeSync(dl++);
    gDPSetCycleType(dl++, G_CYC_1CYCLE);
    gDPSetRenderMode(dl++, G_RM_XLU_SURF, G_RM_XLU_SURF2); // no z test: lightning stays visible through the enemy
    gDPSetCombineLERP(dl++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE);
    gSPTexture(dl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gSPClearGeometryMode(dl++, G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_FOG | G_CULL_BOTH | G_ZBUFFER);
    gSPSetGeometryMode(dl++, G_SHADE | G_SHADING_SMOOTH);
    Matrix_Translate(origin.x, origin.y, origin.z, MTXMODE_NEW);
    gSPMatrix(dl++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    for (int b = 0; b < kBoltCount; b++) {
        // Jagged path, re-randomized every frame for the crackle
        V3 points[kBoltSegments + 1];
        V3 dir = self->dirs[b];
        V3 sideA = Normalize(Cross(dir, { 0.0f, 1.0f, 0.0f }));
        V3 sideB = Cross(dir, sideA);
        float len = self->lengths[b] * reach;
        for (int i = 0; i <= kBoltSegments; i++) {
            float t = (float)i / kBoltSegments;
            float jag = (i == 0) ? 0.0f : len * 0.12f;
            points[i] = dir * (len * t) + sideA * Rand_CenteredFloat(jag) + sideB * Rand_CenteredFloat(jag);
        }
        V3 tint = PearlPalette((float)b / kBoltCount + play->gameplayFrames * 0.05f);
        V3 glow = Lerp(tint, { 0.55f, 0.85f, 1.0f }, 0.5f);
        DrawRibbon(dl, play, points, kBoltSegments + 1, 5.0f, glow, 0.45f * life * flicker, eyeLocal);
        DrawRibbon(dl, play, points, kBoltSegments + 1, 1.6f, { 1.0f, 1.0f, 1.0f }, 0.95f * life * flicker, eyeLocal);
    }
    gSPSetGeometryMode(dl++, G_ZBUFFER);

    POLY_XLU_DISP = dl;

    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void ZOP_OnPlayerLeftHandDraw(PlayState* play, Player* player) {
    if (NayrusLoveActive()) {
        DrawBladeAura(play, player);
    }
}

extern "C" float ZOP_ModifyAcDamage(PlayState* play, void* colliderRef, ColliderInfo* info, float damage) {
    Collider* collider = (Collider*)colliderRef;
    if (!NayrusLoveActive() || info->acHit == nullptr || info->acHitInfo == nullptr) {
        return damage;
    }
    Player* player = GET_PLAYER(play);
    if (info->acHit->actor != &player->actor || !(info->acHitInfo->toucher.dmgFlags & DMG_SWORD)) {
        return damage;
    }

    Actor* target = collider->actor;
    bool isEnemy = (target->category == ACTORCAT_ENEMY) || (target->category == ACTORCAT_BOSS);
    if (isEnemy && (target != sLastBoltTarget || play->gameplayFrames - sLastBoltFrame > 8)) {
        sLastBoltTarget = target;
        sLastBoltFrame = play->gameplayFrames;
        Vec3s hit = info->bumper.hitPos;
        Actor_Spawn(&play->actorCtx, play, sBoltActorId, hit.x, hit.y, hit.z, 0, 0, 0, 0);
    }

    return damage * CVAR_SWORD_DAMAGE_MULT_VALUE;
}

static void RegisterNayrusSword() {
    if (sBoltActorId < 0) {
        ActorDBInit entry = {};
        entry.name = "ZOP_NayruBolt";
        entry.desc = "Nayru's Love sword lightning";
        entry.category = ACTORCAT_ITEMACTION;
        entry.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        entry.objectId = OBJECT_GAMEPLAY_KEEP;
        entry.instanceSize = sizeof(NayruBolt);
        entry.init = NayruBolt_Init;
        entry.update = NayruBolt_Update;
        entry.draw = NayruBolt_Draw;
        sBoltActorId = (s16)ActorDB::Instance->AddEntry(entry).entry.id;
    }
}

static RegisterShipInitFunc initFunc(RegisterNayrusSword, { CVAR_SWORD_NAME });

// Phantom Ganon phase 2: ring-shaped shockwaves racing over the floor.
//
// When he slams lightning into the floor (end of a charge, his staff slam, a dodged burst ball hitting the floor), a
// glowing ring spreads out from the impact point. Link has to jump over it like over a skipping rope: the ring hurts
// him when it passes under him while he stands on the floor.

#include <libultraship/bridge/consolevariablebridge.h>

#include <algorithm>
#include <cmath>

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

constexpr int kSegments = 48;
constexpr float kHitBand = 22.0f; // how close to the ring Link has to be to get hit
constexpr int kFadeFrames = 10;
const V3 kColorOuter = { 0.35f, 0.75f, 1.0f };
const V3 kColorCore = { 0.9f, 1.0f, 1.0f };

struct GndRing {
    Actor actor;
    float radius;
    float maxRadius;
    float speed;
    s16 timer;
    s16 fadeFrames; // -1 while spreading, then counts the fade
    bool hitLink;
};

s16 sRingActorId = -1;

void GndRing_Init(Actor* thisx, PlayState* play) {
    GndRing* self = (GndRing*)thisx;

    thisx->room = -1;
    self->radius = 10.0f;
    self->timer = 0;
    self->fadeFrames = -1;
    self->hitLink = false;
    // maxRadius and speed are set by the spawner (ZOP_SpawnGndRing)
    self->maxRadius = 500.0f;
    self->speed = 9.0f;
}

void GndRing_Destroy(Actor* thisx, PlayState* play) {
    Audio_StopSfxByPos(&thisx->projectedPos);
}

void GndRing_Update(Actor* thisx, PlayState* play) {
    GndRing* self = (GndRing*)thisx;
    Player* player = GET_PLAYER(play);
    self->timer++;

    if (self->fadeFrames >= 0) {
        if (++self->fadeFrames > kFadeFrames) {
            Actor_Kill(thisx);
        }
        return;
    }

    self->radius += self->speed;
    if (self->radius >= self->maxRadius) {
        self->fadeFrames = 0;
    }
    Actor_PlaySfx_Flagged(thisx, NA_SE_EN_BIMOS_LAZER_GND - SFX_FLAG);

    // Sparks along the ring
    for (int i = 0; i < 3; i++) {
        float ang = Rand_ZeroOne() * 2.0f * kPi;
        Vec3f pos = { thisx->world.pos.x + sinf(ang) * self->radius, thisx->world.pos.y + 4.0f,
                      thisx->world.pos.z + cosf(ang) * self->radius };
        Vec3f vel = { Rand_CenteredFloat(3.0f), 2.5f + Rand_ZeroOne() * 3.0f, Rand_CenteredFloat(3.0f) };
        Vec3f accel = { 0.0f, -0.4f, 0.0f };
        Color_RGBA8 prim = { 255, 255, 255, 255 };
        Color_RGBA8 env = { 90, 180, 255, 255 };
        EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 2000, 10);
    }

    // Hits Link once if it passes under him while he is on the floor
    if (!self->hitLink) {
        float dx = player->actor.world.pos.x - thisx->world.pos.x;
        float dz = player->actor.world.pos.z - thisx->world.pos.z;
        float dist = sqrtf(dx * dx + dz * dz);
        bool onFloor = (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) &&
                       (player->actor.world.pos.y < thisx->world.pos.y + 30.0f);

        if (onFloor && (fabsf(dist - self->radius) < kHitBand) && (player->invincibilityTimer == 0) &&
            (gSaveContext.nayrusLoveTimer == 0)) {
            self->hitLink = true;
            Actor_SetPlayerKnockback(play, thisx, 4.0f, (s16)(atan2f(dx, dz) * (0x8000 / kPi)), 6.0f, 3, 0x18);
            Audio_PlayActorSound2(&player->actor, NA_SE_EN_FANTOM_HIT_THUNDER);
        }
    }
}

// The ring as a flat band on the floor; alpha fades from the inner to the outer edge as given
void DrawRingBand(Gfx*& dl, PlayState* play, float radius, float width, V3 color, float innerAlpha, float midAlpha,
                  float outerAlpha) {
    // three circles of vertices: inner edge, middle, outer edge
    const int n = kSegments;
    Vtx* vtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, 3 * (n + 1) * sizeof(Vtx));
    float radii[3] = { std::max(0.0f, radius - width), radius, radius + width };
    float alphas[3] = { innerAlpha, midAlpha, outerAlpha };

    for (int ring = 0; ring < 3; ring++) {
        for (int i = 0; i <= n; i++) {
            float ang = (float)i / n * 2.0f * kPi;
            Vtx_t& out = vtx[ring * (n + 1) + i].v;
            out.ob[0] = (s16)(sinf(ang) * radii[ring]);
            out.ob[1] = 0;
            out.ob[2] = (s16)(cosf(ang) * radii[ring]);
            out.flag = 0;
            out.tc[0] = out.tc[1] = 0;
            out.cn[0] = ToU8(color.x);
            out.cn[1] = ToU8(color.y);
            out.cn[2] = ToU8(color.z);
            out.cn[3] = ToU8(alphas[ring]);
        }
    }
    // Vertex cache holds 32 at a time: draw in chunks of 8 segments (2 bands x 9 vertices x 3 rings = 27)
    for (int start = 0; start < n; start += 8) {
        int count = std::min(8, n - start);
        for (int ring = 0; ring < 3; ring++) {
            gSPVertex(dl++, (uintptr_t)&vtx[ring * (n + 1) + start], count + 1, ring * 9);
        }
        for (int i = 0; i < count; i++) {
            for (int band = 0; band < 2; band++) {
                int a = band * 9 + i;
                int b = (band + 1) * 9 + i;
                gSP2Triangles(dl++, a, b, b + 1, 0, a, b + 1, a + 1, 0);
            }
        }
    }
}

} // namespace

// extern "C": OPEN_DISPS declares C-linkage helpers at block scope
extern "C" void GndRing_Draw(Actor* thisx, PlayState* play) {
    GndRing* self = (GndRing*)thisx;
    float fade = (self->fadeFrames < 0) ? 1.0f : 1.0f - (float)self->fadeFrames / kFadeFrames;
    if (fade <= 0.0f) {
        return;
    }
    float flicker = 0.85f + 0.15f * sinf(self->timer * 2.3f);

    OPEN_DISPS(play->state.gfxCtx);

    Gfx* dl = POLY_XLU_DISP;
    gDPPipeSync(dl++);
    gDPSetCycleType(dl++, G_CYC_1CYCLE);
    gDPSetRenderMode(dl++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
    gDPSetCombineLERP(dl++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE);
    gSPTexture(dl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gSPClearGeometryMode(dl++, G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_FOG | G_CULL_BOTH);
    gSPSetGeometryMode(dl++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);

    Matrix_Translate(thisx->world.pos.x, thisx->world.pos.y + 2.0f, thisx->world.pos.z, MTXMODE_NEW);
    gSPMatrix(dl++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    // wide soft glow, a fading wake on the inside, and a bright core line
    DrawRingBand(dl, play, self->radius, 28.0f, kColorOuter, 0.0f, 0.5f * fade * flicker, 0.0f);
    DrawRingBand(dl, play, self->radius - 30.0f, 30.0f, kColorOuter, 0.0f, 0.12f * fade, 0.25f * fade);
    DrawRingBand(dl, play, self->radius, 6.0f, kColorCore, 0.0f, 0.95f * fade, 0.0f);
    POLY_XLU_DISP = dl;

    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void ZOP_SpawnGndRing(PlayState* play, float x, float y, float z, float maxRadius, float speed) {
    Actor* ring = Actor_Spawn(&play->actorCtx, play, sRingActorId, x, y, z, 0, 0, 0, 0);
    if (ring != nullptr) {
        GndRing* self = (GndRing*)ring;
        self->maxRadius = maxRadius;
        self->speed = speed;
    }
}

static void RegisterGanondrofRings() {
    if (sRingActorId < 0) {
        ActorDBInit entry = {};
        entry.name = "ZOP_GndRing";
        entry.desc = "Phantom Ganon phase 2 ring shockwave";
        entry.category = ACTORCAT_ITEMACTION;
        entry.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        entry.objectId = OBJECT_GAMEPLAY_KEEP;
        entry.instanceSize = sizeof(GndRing);
        entry.init = GndRing_Init;
        entry.destroy = GndRing_Destroy;
        entry.update = GndRing_Update;
        entry.draw = GndRing_Draw;
        sRingActorId = (s16)ActorDB::Instance->AddEntry(entry).entry.id;
    }
}

static RegisterShipInitFunc initFunc(RegisterGanondrofRings, {});

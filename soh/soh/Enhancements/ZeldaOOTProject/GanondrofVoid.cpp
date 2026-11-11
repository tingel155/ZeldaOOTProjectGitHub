// Phantom Ganon phase 2b: the arena turns into a platform over an abyss, bordered by a ring of fire.
//
// After his spell (see BossGanondrof_ZopTransition) everything beyond the platform is pitch black: an opaque black
// floor over the lower ring of the room, a black wall in front of the room's walls and a black ceiling. They face the
// arena only (back faces culled), so a camera pushed between them and the real walls still sees the arena. The edge
// of the platform burns with a ring of fire in the style of the final battle against Ganon (his fire texture).

#include <algorithm>
#include <cmath>

#include "NayrusLove.h"
#include "ZeldaOOTProjectHooks.h"

extern "C" {
#include <z64.h>
#include "variables.h"
#include "functions.h"
#include "macros.h"
}
#include "overlays/ovl_Boss_Ganon2/ovl_Boss_Ganon2.h"

using namespace NayrusLove;

namespace {

constexpr int kSegments = 32;
// Arena of the Forest Temple boss room (measured): platform at y -33 up to r ~330 (railing at ~323), lower ring at
// y -193 out to the walls at ~410-440, ceiling at ~786
constexpr float kCenterX = 14.0f;
constexpr float kCenterZ = -3315.0f;
constexpr float kPlatformY = -33.0f;
constexpr float kVoidFloorY = -190.0f;
constexpr float kVoidInner = 300.0f; // under the platform edge, hidden by it
constexpr float kVoidWall = 405.0f;
constexpr float kVoidTop = 780.0f;
constexpr float kFireHeight = 110.0f;

// One vertex, position relative to the arena center
void SetVtx(Vtx& v, float x, float y, float z, s16 s, s16 t, u8 alpha) {
    Vtx_t& out = v.v;
    out.ob[0] = (s16)x;
    out.ob[1] = (s16)y;
    out.ob[2] = (s16)z;
    out.flag = 0;
    out.tc[0] = s;
    out.tc[1] = t;
    out.cn[0] = out.cn[1] = out.cn[2] = 255;
    out.cn[3] = alpha;
}

// A band of quads around the center between (r0, y0) and (r1, y1) for every segment, drawn in chunks that fit the
// vertex cache. Quads are wound so that their front faces look toward the inside / up / down as the rings are given.
// With fadeEye: when the camera is outside the ring of fire (Link near the edge, camera behind him), the part of the
// ring between the camera and the platform fades out, so the flames don't cover the view; the rest stays
float CameraFade(const Vec3f* eye, float x, float z) {
    if (eye == nullptr) {
        return 1.0f;
    }
    float ex = eye->x - kCenterX;
    float ez = eye->z - kCenterZ;
    float camR = sqrtf(ex * ex + ez * ez);
    float outside = Clamp01((camR - (ZOP_GND_FIRE_RADIUS - 80.0f)) / 80.0f);
    if (outside <= 0.0f) {
        return 1.0f;
    }
    float vr = sqrtf(x * x + z * z);
    float dot = (vr > 1.0f && camR > 1.0f) ? (x * ex + z * ez) / (vr * camR) : 0.0f; // 1: right in front of it
    float angular = Clamp01((0.55f - dot) / 0.4f);
    return 1.0f - outside * (1.0f - angular);
}

void DrawBand(Gfx*& dl, PlayState* play, float r0, float y0, float r1, float y1, int texRepeat, s16 tScroll,
              u8 alpha0, u8 alpha1, const Vec3f* fadeEye = nullptr) {
    Vtx* vtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, 2 * (kSegments + 1) * sizeof(Vtx));
    for (int i = 0; i <= kSegments; i++) {
        float ang = (float)i / kSegments * 2.0f * kPi;
        float sn = sinf(ang);
        float cs = cosf(ang);
        s16 s = (s16)((i * texRepeat * 32 * 32) / kSegments); // 10.5 fixed point, 32 texels wide
        float fade = CameraFade(fadeEye, sn * r0, cs * r0);
        SetVtx(vtx[i], sn * r0, y0, cs * r0, s, (s16)(64 * 32 + tScroll), (u8)(alpha0 * fade));
        SetVtx(vtx[kSegments + 1 + i], sn * r1, y1, cs * r1, s, (s16)(tScroll), (u8)(alpha1 * fade));
    }
    for (int start = 0; start < kSegments; start += 15) {
        int count = std::min(15, kSegments - start);
        gSPVertex(dl++, (uintptr_t)&vtx[start], count + 1, 0);
        gSPVertex(dl++, (uintptr_t)&vtx[kSegments + 1 + start], count + 1, 16);
        for (int i = 0; i < count; i++) {
            int a = i, b = i + 1, c = 16 + i, d = 16 + i + 1;
            gSP2Triangles(dl++, a, c, d, 0, a, d, b, 0);
        }
    }
}

} // namespace

// extern "C": OPEN_DISPS declares C-linkage helpers at block scope
extern "C" void ZOP_GndDrawVoid(PlayState* play, float voidOn, float fireAlpha) {
    OPEN_DISPS(play->state.gfxCtx);

    Matrix_Translate(kCenterX, 0.0f, kCenterZ, MTXMODE_NEW);

    if (voidOn > 0.0f) {
        // Pitch black, opaque, no fog
        Gfx* dl = POLY_OPA_DISP;
        gDPPipeSync(dl++);
        gDPSetCycleType(dl++, G_CYC_1CYCLE);
        gDPSetRenderMode(dl++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
        gDPSetCombineLERP(dl++, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE);
        gDPSetPrimColor(dl++, 0, 0, 0, 0, 0, 255);
        gSPTexture(dl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
        gSPClearGeometryMode(dl++, G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_FOG | G_CULL_BOTH);
        gSPSetGeometryMode(dl++, G_ZBUFFER | G_CULL_BACK);
        gSPMatrix(dl++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        // floor of the abyss (faces up), wall (faces in), ceiling (faces down)
        DrawBand(dl, play, kVoidInner, kVoidFloorY, kVoidWall + 30.0f, kVoidFloorY, 1, 0, 255, 255);
        DrawBand(dl, play, kVoidWall, kVoidFloorY - 50.0f, kVoidWall, kVoidTop + 50.0f, 1, 0, 255, 255);
        DrawBand(dl, play, kVoidWall + 30.0f, kVoidTop, 0.0f, kVoidTop, 1, 0, 255, 255);
        gSPClearGeometryMode(dl++, G_CULL_BACK);
        POLY_OPA_DISP = dl;
    }

    if (fireAlpha > 0.0f) {
        // Ring of flames at the edge of the platform, Ganon's fire texture scrolling upward
        Gfx* dl = POLY_XLU_DISP;
        float flicker = 0.85f + 0.15f * sinf(play->gameplayFrames * 1.7f);
        gDPPipeSync(dl++);
        gDPSetCycleType(dl++, G_CYC_1CYCLE);
        gDPSetRenderMode(dl++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
        // color: fire texture between red and yellow; alpha: texture x vertex alpha (fade-in, flicker, camera fade)
        gDPSetCombineLERP(dl++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, SHADE, 0, PRIMITIVE,
                          ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, SHADE, 0);
        gDPSetPrimColor(dl++, 0, 0, 255, 220, 60, 255);
        gDPSetEnvColor(dl++, 255, 30, 0, 255);
        gDPLoadTextureBlock(dl++, gGanonFireRingTex, G_IM_FMT_I, G_IM_SIZ_8b, 32, 64, 0, G_TX_NOMIRROR | G_TX_WRAP,
                            G_TX_NOMIRROR | G_TX_WRAP, 5, 6, G_TX_NOLOD, G_TX_NOLOD);
        gSPTexture(dl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        gSPClearGeometryMode(dl++, G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_FOG | G_CULL_BOTH);
        gSPSetGeometryMode(dl++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
        u8 a = ToU8(fireAlpha * flicker);
        const Vec3f* eye = &play->view.eye;
        gSPMatrix(dl++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        s16 scroll = (s16)((play->gameplayFrames * 6 * 32) % (64 * 32));
        // two layers, the inner one a bit lower and faster, for some depth
        DrawBand(dl, play, ZOP_GND_FIRE_RADIUS, kPlatformY, ZOP_GND_FIRE_RADIUS, kPlatformY + kFireHeight, 24, scroll,
                 a, a, eye);
        s16 scroll2 = (s16)((play->gameplayFrames * 9 * 32 + 700) % (64 * 32));
        DrawBand(dl, play, ZOP_GND_FIRE_RADIUS - 6.0f, kPlatformY, ZOP_GND_FIRE_RADIUS - 6.0f,
                 kPlatformY + kFireHeight * 0.7f, 18, scroll2, a, a, eye);
        POLY_XLU_DISP = dl;
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

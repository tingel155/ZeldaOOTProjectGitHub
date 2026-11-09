// Nayru's Love: pearlescent crystal.
//
// Replaces the draw function of the Nayru's Love diamond (ACTOR_MAGIC_DARK) with a faceted gem whose vertex colors
// are recomputed every frame from the view angle (thin-film style iridescence), plus a specular highlight, a light
// band sweeping over the facets, a glowing inner core and small sparkles on the surface.

#include <libultraship/bridge/consolevariablebridge.h>

#include <array>
#include <utility>
#include <cmath>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
#include "variables.h"
#include "functions.h"
#include "macros.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include "src/overlays/actors/ovl_Magic_Dark/z_magic_dark.h"
void MagicDark_DiamondDraw(Actor* thisx, PlayState* play);
}

#define CVAR_ZOP_NAYRU(var) "gZOP.NayrusLove." var
#define CVAR_PEARL_CRYSTAL_NAME CVAR_ZOP_NAYRU("PearlCrystal")
#define CVAR_PEARL_CRYSTAL_VALUE CVarGetInteger(CVAR_PEARL_CRYSTAL_NAME, 1)

namespace {

constexpr float kPi = 3.14159265f;

struct V3 {
    float x, y, z;
};

V3 operator+(V3 a, V3 b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}
V3 operator-(V3 a, V3 b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}
V3 operator*(V3 a, float s) {
    return { a.x * s, a.y * s, a.z * s };
}
float Dot(V3 a, V3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
V3 Cross(V3 a, V3 b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
V3 Normalize(V3 a) {
    float len = sqrtf(Dot(a, a));
    return (len > 0.0001f) ? a * (1.0f / len) : V3{ 0.0f, 1.0f, 0.0f };
}
float Clamp01(float f) {
    return (f < 0.0f) ? 0.0f : ((f > 1.0f) ? 1.0f : f);
}
V3 Lerp(V3 a, V3 b, float t) {
    return a + (b - a) * t;
}

// Gem shape in world units for adult Link, origin at Link's waist. Eight sides: a pointed crown, a girdle band and a
// pointed pavilion. Rings marked "offset" are rotated by half a side, which turns the crown/pavilion into kite facets.
constexpr int kSides = 8;
constexpr float kTopY = 82.0f;
constexpr float kBottomY = -46.0f;
struct Ring {
    float radius;
    float y;
    bool offset;
};
constexpr Ring kCrown = { 22.0f, 52.0f, true };
constexpr Ring kGirdleTop = { 36.0f, 12.0f, false };
constexpr Ring kGirdleBottom = { 35.0f, 2.0f, false };
constexpr Ring kPavilion = { 17.0f, -24.0f, true };

constexpr int kTriCount = kSides * 8;
// Every facet is split into a fan around its centroid: bright, opaque corners and a clear center give glowing edges
constexpr int kVtxCount = kTriCount * 9;
constexpr int kVtxPerBatch = 30; // 10 triangles per gSPVertex load
constexpr float kVtxScale = 8.0f; // model is stored at 8x in the s16 vertex buffer for sub-unit precision

struct Tri {
    V3 p[3];
    V3 n;
};

V3 RingPoint(const Ring& ring, int i) {
    float angle = (i + (ring.offset ? 0.5f : 0.0f)) * (2.0f * kPi / kSides);
    return { cosf(angle) * ring.radius, ring.y, sinf(angle) * ring.radius };
}

std::array<Tri, kTriCount> BuildGem() {
    std::array<Tri, kTriCount> tris{};
    const V3 top = { 0.0f, kTopY, 0.0f };
    const V3 bottom = { 0.0f, kBottomY, 0.0f };
    const V3 center = { 0.0f, (kTopY + kBottomY) * 0.5f, 0.0f };
    int t = 0;

    auto add = [&](V3 a, V3 b, V3 c) {
        V3 n = Normalize(Cross(b - a, c - a));
        V3 centroid = (a + b + c) * (1.0f / 3.0f);
        // Wind every triangle outward so back-face culling separates the inner and outer pass
        if (Dot(n, centroid - center) < 0.0f) {
            std::swap(b, c);
            n = n * -1.0f;
        }
        tris[t++] = { { a, b, c }, n };
    };

    for (int i = 0; i < kSides; i++) {
        int j = (i + 1) % kSides;
        V3 a0 = RingPoint(kCrown, i), a1 = RingPoint(kCrown, j);
        V3 b0 = RingPoint(kGirdleTop, i), b1 = RingPoint(kGirdleTop, j);
        V3 c0 = RingPoint(kGirdleBottom, i), c1 = RingPoint(kGirdleBottom, j);
        V3 d0 = RingPoint(kPavilion, i), d1 = RingPoint(kPavilion, j);
        // Offset rings (crown, pavilion) sit halfway between i and j
        add(top, a0, a1);
        add(a0, b1, a1);
        add(a0, b0, b1);
        add(b0, c0, c1);
        add(b0, c1, b1);
        add(d0, c1, c0);
        add(d0, d1, c1);
        add(bottom, d1, d0);
    }
    return tris;
}

const std::array<Tri, kTriCount>& Gem() {
    static const std::array<Tri, kTriCount> sGem = BuildGem();
    return sGem;
}

// Mother-of-pearl palette, cycled by view angle and time
constexpr std::array<V3, 5> kPearlPalette = { {
    { 1.00f, 0.52f, 0.80f }, // rose
    { 0.42f, 1.00f, 0.88f }, // aqua
    { 1.00f, 0.86f, 0.42f }, // gold
    { 0.68f, 0.52f, 1.00f }, // lavender
    { 0.45f, 0.78f, 1.00f }, // sky blue
} };

V3 PearlPalette(float t) {
    t -= floorf(t);
    float f = t * (float)kPearlPalette.size();
    int i = (int)f;
    float k = f - i;
    k = k * k * (3.0f - 2.0f * k);
    return Lerp(kPearlPalette[i % kPearlPalette.size()], kPearlPalette[(i + 1) % kPearlPalette.size()], k);
}

u32 Hash(u32 x) {
    x ^= x >> 16;
    x *= 0x7FEB352D;
    x ^= x >> 15;
    x *= 0x846CA68B;
    x ^= x >> 16;
    return x;
}

Vtx* BuildVertices(PlayState* play, V3 worldPos, s16 yaw, float scaleXZ, float scaleY) {
    Vtx* vtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, kVtxCount * sizeof(Vtx));
    const float time = (float)(play->gameplayFrames % 100000);
    const float sinYaw = sinf(yaw * (kPi / 0x8000));
    const float cosYaw = cosf(yaw * (kPi / 0x8000));
    const V3 eye = { play->view.eye.x, play->view.eye.y, play->view.eye.z };
    const V3 light = Normalize({ 0.35f, 1.0f, 0.25f });
    const V3 white = { 1.0f, 1.0f, 1.0f };
    const V3 pearlBase = { 0.97f, 0.95f, 1.0f };
    // Vertical light band, 0 = bottom tip, 1 = top tip; sweeps upward every ~3 seconds
    const float sweep = fmodf(time * 0.018f, 1.6f) - 0.3f;

    // Same transform as the display list matrix (scale, then rotate Y), applied on the CPU for lighting
    auto toWorld = [&](V3 p) -> V3 {
        p = { p.x * scaleXZ, p.y * scaleY, p.z * scaleXZ };
        return { worldPos.x + p.x * cosYaw + p.z * sinYaw, worldPos.y + p.y, worldPos.z - p.x * sinYaw + p.z * cosYaw };
    };
    auto rotNormal = [&](V3 n) -> V3 { return { n.x * cosYaw + n.z * sinYaw, n.y, -n.x * sinYaw + n.z * cosYaw }; };

    int v = 0;
    int face = 0;
    for (const Tri& tri : Gem()) {
        V3 n = rotNormal(tri.n);
        // Each facet gets its own phase in the palette so neighbouring facets catch different colors
        float faceShift = (float)(Hash(face++) & 0xFF) / 255.0f * 0.6f;
        // Soft directional shading keeps the facets distinguishable
        float facetLight = 0.78f + 0.3f * Dot(n, light);

        auto emit = [&](const V3& local, bool isEdge) {
            V3 p = toWorld(local);
            V3 view = Normalize(eye - p);
            float ndv = fabsf(Dot(n, view));
            float fresnel = 1.0f - ndv;

            float hue = ndv * 2.2f + time * 0.015f + local.y * 0.006f + faceShift;
            V3 color = Lerp(pearlBase, PearlPalette(hue), 0.55f + 0.4f * fresnel) * facetLight;

            float spec = powf(Clamp01(Dot(n, Normalize(view + light))), 24.0f);
            float heightNorm = (local.y - kBottomY) / (kTopY - kBottomY);
            float band = expf(-((heightNorm - sweep) * (heightNorm - sweep)) / 0.006f) * (0.4f + 0.6f * fresnel);
            float edge = isEdge ? 0.35f : 0.0f;
            color = Lerp(color, white, Clamp01(spec * 0.9f + band * 0.7f + edge * 0.6f));

            float alpha = (isEdge ? 0.42f : 0.06f) + 0.5f * powf(fresnel, 1.5f) + 0.45f * spec + 0.35f * band;

            Vtx_t& out = vtx[v++].v;
            out.ob[0] = (s16)(local.x * kVtxScale);
            out.ob[1] = (s16)(local.y * kVtxScale);
            out.ob[2] = (s16)(local.z * kVtxScale);
            out.flag = 0;
            out.tc[0] = out.tc[1] = 0;
            out.cn[0] = (u8)(Clamp01(color.x) * 255.0f);
            out.cn[1] = (u8)(Clamp01(color.y) * 255.0f);
            out.cn[2] = (u8)(Clamp01(color.z) * 255.0f);
            out.cn[3] = (u8)(Clamp01(alpha) * 255.0f);
        };

        V3 centroid = (tri.p[0] + tri.p[1] + tri.p[2]) * (1.0f / 3.0f);
        for (int k = 0; k < 3; k++) {
            emit(centroid, false);
            emit(tri.p[k], true);
            emit(tri.p[(k + 1) % 3], true);
        }
    }
    return vtx;
}

void DrawGemTriangles(Gfx*& dl, Vtx* vtx) {
    for (int base = 0; base < kVtxCount; base += kVtxPerBatch) {
        int count = (kVtxCount - base < kVtxPerBatch) ? (kVtxCount - base) : kVtxPerBatch;
        gSPVertex(dl++, (uintptr_t)&vtx[base], count, 0);
        for (int i = 0; i + 5 < count; i += 6) {
            gSP2Triangles(dl++, i, i + 1, i + 2, 0, i + 3, i + 4, i + 5, 0);
        }
        if (count % 6 == 3) {
            gSP1Triangle(dl++, count - 3, count - 2, count - 1, 0);
        }
    }
}

void LoadGemMatrix(PlayState* play, Gfx*& dl, V3 pos, s16 yaw, float scaleXZ, float scaleY) {
    Matrix_Translate(pos.x, pos.y, pos.z, MTXMODE_NEW);
    Matrix_RotateY(yaw * (kPi / 0x8000), MTXMODE_APPLY);
    Matrix_Scale(scaleXZ / kVtxScale, scaleY / kVtxScale, scaleXZ / kVtxScale, MTXMODE_APPLY);
    gSPMatrix(dl++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
}

void SetupGemMaterial(Gfx*& dl) {
    gDPPipeSync(dl++);
    gDPSetCycleType(dl++, G_CYC_1CYCLE);
    gDPSetRenderMode(dl++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
    // color = SHADE * PRIM, alpha = SHADE * PRIM (prim tints a pass and carries the fade in/out)
    gDPSetCombineLERP(dl++, SHADE, 0, PRIMITIVE, 0, SHADE, 0, PRIMITIVE, 0, SHADE, 0, PRIMITIVE, 0, SHADE, 0, PRIMITIVE,
                      0);
    gSPTexture(dl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gSPClearGeometryMode(dl++, G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_FOG | G_CULL_BOTH);
    gSPSetGeometryMode(dl++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
}

void DrawSparkles(PlayState* play, Gfx*& dl, V3 pos, s16 yaw, float scaleXZ, float scaleY, float fade) {
    constexpr int kSparkleCount = 4;
    constexpr int kSparkleLife = 9;
    const u32 frames = play->gameplayFrames;
    const float sinYaw = sinf(yaw * (kPi / 0x8000));
    const float cosYaw = cosf(yaw * (kPi / 0x8000));

    play->state.gfxCtx->polyXlu.p = dl;
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    dl = play->state.gfxCtx->polyXlu.p;

    for (int k = 0; k < kSparkleCount; k++) {
        u32 shifted = frames + k * 3;
        u32 seed = Hash(shifted / kSparkleLife * 31 + k * 7919);
        float life = (float)(shifted % kSparkleLife) / (float)(kSparkleLife - 1);
        float size = sinf(life * kPi);

        // Random point on a random outer facet
        const Tri& tri = Gem()[seed % kTriCount];
        float u = (float)((seed >> 8) & 0xFF) / 255.0f;
        float w = (float)((seed >> 16) & 0xFF) / 255.0f;
        if (u + w > 1.0f) {
            u = 1.0f - u;
            w = 1.0f - w;
        }
        V3 local = tri.p[0] + (tri.p[1] - tri.p[0]) * u + (tri.p[2] - tri.p[0]) * w;
        local = { local.x * scaleXZ, local.y * scaleY, local.z * scaleXZ };
        V3 world = { pos.x + local.x * cosYaw + local.z * sinYaw, pos.y + local.y,
                     pos.z - local.x * sinYaw + local.z * cosYaw };

        V3 tint = PearlPalette((float)(seed & 0xFFFF) / 4096.0f);
        gDPSetPrimColor(dl++, 0, 0x80, 255, 255, 255, (u8)(255 * fade));
        gDPSetEnvColor(dl++, (u8)(tint.x * 255), (u8)(tint.y * 255), (u8)(tint.z * 255), 255);
        Matrix_Translate(world.x, world.y, world.z, MTXMODE_NEW);
        Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
        Matrix_Scale(0.012f * size * scaleY, 0.012f * size * scaleY, 1.0f, MTXMODE_APPLY);
        Matrix_RotateZ((float)((frames + k * 11) % 1000) * 0.35f, MTXMODE_APPLY);
        gSPMatrix(dl++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(dl++, (Gfx*)gEffFlash1DL);
    }
}

} // namespace

// extern "C": OPEN_DISPS declares C-linkage helpers at block scope
extern "C" void NayrusCrystal_Draw(Actor* thisx, PlayState* play) {
    MagicDark* magic = (MagicDark*)thisx;

    if (!CVAR_PEARL_CRYSTAL_VALUE) {
        MagicDark_DiamondDraw(thisx, play);
        return;
    }

    // Follow Link's waist, like the vanilla draw
    Player* player = GET_PLAYER(play);
    Vec3f* waist = &player->bodyPartsPos[PLAYER_BODYPART_WAIST];
    thisx->world.pos.x = waist->x;
    thisx->world.pos.z = waist->z;
    float heightDiff = waist->y - thisx->world.pos.y;
    if (heightDiff < -2.0f) {
        thisx->world.pos.y = waist->y + 2.0f;
    } else if (heightDiff > 2.0f) {
        thisx->world.pos.y = waist->y - 2.0f;
    }

    // Vanilla steady-state scale is (scale * 1.3, scale, scale * 1.3) with scale 0.6 for adults; normalize to that so
    // the spawn "pop" animation and the child size carry over
    const float scaleXZ = thisx->scale.x / (0.6f * 1.3f);
    const float scaleY = thisx->scale.y / 0.6f;
    const float fade = magic->primAlpha / 255.0f;
    const V3 pos = { thisx->world.pos.x, thisx->world.pos.y, thisx->world.pos.z };
    // Slow independent spin; vanilla's shape.rot.y is tied to the camera which looks wrong on a gem
    const s16 yaw = (s16)(play->gameplayFrames * 0x90);

    OPEN_DISPS(play->state.gfxCtx);

    Vtx* vtx = BuildVertices(play, pos, yaw, scaleXZ, scaleY);
    Gfx* dl = POLY_XLU_DISP;
    SetupGemMaterial(dl);

    // 1. Inner core: a small warm copy of the gem glowing from inside
    LoadGemMatrix(play, dl, pos, -yaw * 2, scaleXZ * 0.38f, scaleY * 0.42f);
    gSPSetGeometryMode(dl++, G_CULL_BACK);
    gDPSetPrimColor(dl++, 0, 0, 255, 215, 240, (u8)(200 * fade));
    DrawGemTriangles(dl, vtx);

    // 2. Back facets, seen through the front: dimmer and slightly tinted
    LoadGemMatrix(play, dl, pos, yaw, scaleXZ, scaleY);
    gSPClearGeometryMode(dl++, G_CULL_BOTH);
    gSPSetGeometryMode(dl++, G_CULL_FRONT);
    gDPSetPrimColor(dl++, 0, 0, 225, 230, 255, (u8)(150 * fade));
    DrawGemTriangles(dl, vtx);

    // 3. Front facets
    gSPClearGeometryMode(dl++, G_CULL_BOTH);
    gSPSetGeometryMode(dl++, G_CULL_BACK);
    gDPSetPrimColor(dl++, 0, 0, 255, 255, 255, (u8)(255 * fade));
    DrawGemTriangles(dl, vtx);
    gSPClearGeometryMode(dl++, G_CULL_BOTH);

    POLY_XLU_DISP = dl;

    // 4. Sparkles glinting on the surface
    DrawSparkles(play, dl, pos, yaw, scaleXZ, scaleY, fade);
    POLY_XLU_DISP = dl;

    CLOSE_DISPS(play->state.gfxCtx);
}

static void RegisterNayrusCrystal() {
    COND_ID_HOOK(OnActorUpdate, ACTOR_MAGIC_DARK, CVAR_PEARL_CRYSTAL_VALUE, [](void* actorRef) {
        Actor* actor = (Actor*)actorRef;
        if (actor->draw == MagicDark_DiamondDraw) {
            actor->draw = NayrusCrystal_Draw;
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterNayrusCrystal, { CVAR_PEARL_CRYSTAL_NAME });

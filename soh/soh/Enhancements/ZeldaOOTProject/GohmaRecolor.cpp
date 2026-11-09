// Gohma phase 2 recolor.
//
// Rewrites Gohma's skin and shell textures (RGBA16) in place: every texel's hue is mapped into a red set, from crimson
// (former yellows/browns) over red (former greens) to orange-red (former cyans), with a bit more saturation. Amount 0
// restores the original texels, values in between blend (used to fade the colors in during the phase 2 cutscene).

#include <cmath>
#include <vector>

#include "soh/ResourceManagerHelpers.h"
#include "ZeldaOOTProjectHooks.h"

extern "C" {
#include <z64.h>
#include "objects/object_goma/object_goma.h"
}

namespace {

struct RecolorTexture {
    const char* path;
    int texels;
    u8* data = nullptr;          // texel data inside the loaded resource
    std::vector<u8> original;    // big-endian RGBA5551, as stored
    std::vector<u8> recolored;
};

RecolorTexture sTextures[] = {
    { gGohmaBodyTex, 16 * 16 },
    { gGohmaShellUndersideTex, 16 * 16 },
    { gGohmaDarkShellTex, 16 * 16 },
    { gGohmaShellTex, 32 * 32 },
};

float sAppliedAmount = 0.0f;

void RgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
    float max = fmaxf(r, fmaxf(g, b));
    float min = fminf(r, fminf(g, b));
    float d = max - min;
    v = max;
    s = (max > 0.0f) ? d / max : 0.0f;
    if (d <= 0.0f) {
        h = 0.0f;
    } else if (max == r) {
        h = 60.0f * fmodf((g - b) / d, 6.0f);
    } else if (max == g) {
        h = 60.0f * ((b - r) / d + 2.0f);
    } else {
        h = 60.0f * ((r - g) / d + 4.0f);
    }
    if (h < 0.0f) {
        h += 360.0f;
    }
}

void HsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    int sector = (int)(h / 60.0f) % 6;
    float rgb[6][3] = { { c, x, 0 }, { x, c, 0 }, { 0, c, x }, { 0, x, c }, { x, 0, c }, { c, 0, x } };
    r = rgb[sector][0] + m;
    g = rgb[sector][1] + m;
    b = rgb[sector][2] + m;
}

u16 RecolorTexel(u16 texel) {
    float r = ((texel >> 11) & 0x1F) / 31.0f;
    float g = ((texel >> 6) & 0x1F) / 31.0f;
    float b = ((texel >> 1) & 0x1F) / 31.0f;
    float h, s, v;
    RgbToHsv(r, g, b, h, s, v);

    // Hue 90 (yellow-green) lands on pure red; +-90 degrees around it spread from crimson (-18) to orange-red (+18).
    // Reds and magentas (above 270) count as negative so they stay on the crimson side instead of wrapping to orange.
    if (h > 270.0f) {
        h -= 360.0f;
    }
    float rel = (h - 90.0f) / 90.0f;
    rel = rel < -1.0f ? -1.0f : (rel > 1.0f ? 1.0f : rel);
    h = rel * 18.0f;
    if (h < 0.0f) {
        h += 360.0f;
    }
    s = fminf(1.0f, s * 1.35f); // neutral greys stay grey
    v = v * 0.95f;

    HsvToRgb(h, s, v, r, g, b);
    return (u16)(((int)(r * 31.0f + 0.5f) << 11) | ((int)(g * 31.0f + 0.5f) << 6) | ((int)(b * 31.0f + 0.5f) << 1) |
                 (texel & 1));
}

u16 BlendTexel(u16 a, u16 b, float t) {
    auto channel = [&](int shift) {
        float ca = (float)((a >> shift) & 0x1F);
        float cb = (float)((b >> shift) & 0x1F);
        return ((int)(ca + (cb - ca) * t + 0.5f) & 0x1F) << shift;
    };
    return (u16)(channel(11) | channel(6) | channel(1) | (a & 1));
}

// (Re)binds a texture to the currently loaded resource; a reloaded resource gets fresh originals
bool Bind(RecolorTexture& tex) {
    u8* data = (u8*)ResourceMgr_LoadTexOrDListByName(tex.path);
    if (data == nullptr) {
        return false;
    }
    if (data != tex.data) {
        tex.data = data;
        tex.original.assign(data, data + tex.texels * 2);
        tex.recolored.resize(tex.texels * 2);
        for (int i = 0; i < tex.texels; i++) {
            u16 texel = (u16)((tex.original[i * 2] << 8) | tex.original[i * 2 + 1]);
            u16 red = RecolorTexel(texel);
            tex.recolored[i * 2] = (u8)(red >> 8);
            tex.recolored[i * 2 + 1] = (u8)(red & 0xFF);
        }
        sAppliedAmount = 0.0f;
    }
    return true;
}

} // namespace

extern "C" int ZOP_GohmaSetRecolor(float amount) {
    bool rebound = false;
    for (RecolorTexture& tex : sTextures) {
        u8* before = tex.data;
        if (!Bind(tex)) {
            return false;
        }
        rebound |= (before != tex.data);
    }
    if (!rebound && fabsf(amount - sAppliedAmount) < 0.01f) {
        return false;
    }

    for (RecolorTexture& tex : sTextures) {
        for (int i = 0; i < tex.texels; i++) {
            u16 a = (u16)((tex.original[i * 2] << 8) | tex.original[i * 2 + 1]);
            u16 b = (u16)((tex.recolored[i * 2] << 8) | tex.recolored[i * 2 + 1]);
            u16 out = (amount >= 1.0f) ? b : ((amount <= 0.0f) ? a : BlendTexel(a, b, amount));
            tex.data[i * 2] = (u8)(out >> 8);
            tex.data[i * 2 + 1] = (u8)(out & 0xFF);
        }
    }
    sAppliedAmount = amount;
    return true;
}

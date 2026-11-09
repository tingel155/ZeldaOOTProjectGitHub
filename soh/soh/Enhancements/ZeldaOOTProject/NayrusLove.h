#pragma once

// Shared pieces of the Nayru's Love rework (NayrusCrystal.cpp draws the crystal, NayrusShatter.cpp handles hits,
// bursting and the shards).

#include <array>
#include <cmath>

#include <libultraship/libultra.h>

#define CVAR_ZOP_NAYRU(var) "gZOP.NayrusLove." var

namespace NayrusLove {

// Visual state of the crystal, driven by NayrusShatter.cpp and read by the crystal draw
struct CrystalFx {
    float hitFlash; // 1 on a blocked hit, decays to 0
    float damage;   // hits taken / hits needed to shatter, 0..1
    float charge;   // 0..1 during the short wind-up right before the crystal bursts
};
const CrystalFx& GetCrystalFx();

// Vertical offset of the crystal relative to Link's waist (scaled for child Link)
float GetCrystalYOffset(float scaleY);

constexpr float kPi = 3.14159265f;

struct V3 {
    float x, y, z;
};

inline V3 operator+(V3 a, V3 b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}
inline V3 operator-(V3 a, V3 b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}
inline V3 operator*(V3 a, float s) {
    return { a.x * s, a.y * s, a.z * s };
}
inline float Dot(V3 a, V3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline V3 Cross(V3 a, V3 b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
inline V3 Normalize(V3 a) {
    float len = sqrtf(Dot(a, a));
    return (len > 0.0001f) ? a * (1.0f / len) : V3{ 0.0f, 1.0f, 0.0f };
}
inline float Clamp01(float f) {
    return (f < 0.0f) ? 0.0f : ((f > 1.0f) ? 1.0f : f);
}
inline V3 Lerp(V3 a, V3 b, float t) {
    return a + (b - a) * t;
}

// Mother-of-pearl palette, cycled by view angle and time
inline V3 PearlPalette(float t) {
    static constexpr std::array<V3, 5> kPalette = { {
        { 1.00f, 0.52f, 0.80f }, // rose
        { 0.42f, 1.00f, 0.88f }, // aqua
        { 1.00f, 0.86f, 0.42f }, // gold
        { 0.68f, 0.52f, 1.00f }, // lavender
        { 0.45f, 0.78f, 1.00f }, // sky blue
    } };
    t -= floorf(t);
    float f = t * (float)kPalette.size();
    int i = (int)f;
    float k = f - (float)i;
    k = k * k * (3.0f - 2.0f * k);
    return Lerp(kPalette[i % kPalette.size()], kPalette[(i + 1) % kPalette.size()], k);
}

inline u32 Hash(u32 x) {
    x ^= x >> 16;
    x *= 0x7FEB352D;
    x ^= x >> 15;
    x *= 0x846CA68B;
    x ^= x >> 16;
    return x;
}

inline u8 ToU8(float f) {
    return (u8)(Clamp01(f) * 255.0f);
}

} // namespace NayrusLove

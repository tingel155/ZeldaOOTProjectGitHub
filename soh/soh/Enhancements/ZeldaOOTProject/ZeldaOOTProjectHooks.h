#pragma once

// Call sites inside the vanilla C code for ZeldaOOTProject mods that cannot be built on GameInteractor hooks alone.
// Each one is a single call placed in the original source; the logic lives in the mod's C++ file.

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;
struct ColliderInfo;

// z_collision_check.c, CollisionCheck_ApplyDamage: may change the damage an AC collider receives (Difficulty.cpp).
// collider is a Collider* (an anonymous struct, so it cannot be forward declared)
float ZOP_ModifyAcDamage(struct PlayState* play, void* collider, struct ColliderInfo* info, float damage);

// Enemy AI code: nonzero when enemies should use their more aggressive tuning (Difficulty.cpp)
int ZOP_EnemyAggressive(void);

#ifdef __cplusplus
}
#endif

// Picks the aggressive value when the difficulty option is on, the vanilla one otherwise
#define ZOP_AGGRO(vanilla, aggressive) (ZOP_EnemyAggressive() ? (aggressive) : (vanilla))

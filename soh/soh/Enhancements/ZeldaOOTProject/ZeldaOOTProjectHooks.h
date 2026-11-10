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

// Enemy AI code: factor for how long a stunned boss stays down (Difficulty.cpp)
float ZOP_EnemyStunMult(void);

// Nonzero when the difficulty options are on (Difficulty.cpp)
int ZOP_DifficultyEnabled(void);

// Boss AI code: nonzero once the boss (an Actor*) is in its second phase, i.e. has lost 70% of the health it started
// with. baseHealth is the boss's vanilla health (Difficulty.cpp)
int ZOP_BossPhase2(void* boss, int baseHealth);

// Gohma phase 2: fires a volley of homing pearl arrows from the shooter's (an Actor*) focus point (GohmaPearlArrows.cpp)
void ZOP_SpawnPearlVolley(struct PlayState* play, void* shooter, int count);
int ZOP_IsPearlArrow(short actorId);

// Gohma phase 2: blends her skin/shell textures toward a red set (0 = original, 1 = fully red). Returns nonzero when
// texture data changed, so the caller must invalidate the texture cache for them (GohmaRecolor.cpp)
int ZOP_GohmaSetRecolor(float amount);

// Gohma phase 2: a tracking laser from the shooter's (an Actor*) eye, lasting ZOP_GOHMA_LASER_FRAMES (GohmaLaser.cpp)
void ZOP_SpawnGohmaLaser(struct PlayState* play, void* shooter);
void ZOP_StopGohmaLasers(struct PlayState* play);
int ZOP_IsGohmaLaser(short actorId);

// Testing aid: bosses skip straight to their phase 2 intro after the encounter cutscene (Difficulty.cpp)
int ZOP_TestPhase2Intro(void);

#ifdef __cplusplus
}
#endif

// Aim + fire + fade frames of a Gohma laser
#define ZOP_GOHMA_LASER_FRAMES 94

// Picks the aggressive value when the difficulty option is on, the vanilla one otherwise
#define ZOP_AGGRO(vanilla, aggressive) (ZOP_EnemyAggressive() ? (aggressive) : (vanilla))

// Picks the hard value when the difficulty options are on, the vanilla one otherwise
#define ZOP_HARD(vanilla, hard) (ZOP_DifficultyEnabled() ? (hard) : (vanilla))

// Scales a stun duration in frames
#define ZOP_STUN(frames) ((s16)((frames) * ZOP_EnemyStunMult()))

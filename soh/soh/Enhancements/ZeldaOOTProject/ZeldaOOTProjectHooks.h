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

// Boss AI code: nonzero once the boss (an Actor*) is in its second phase, i.e. is down to ZOP_BossPhase2At() of the
// health it started with. baseHealth is the boss's vanilla health (Difficulty.cpp)
int ZOP_BossPhase2(void* boss, int baseHealth);
float ZOP_BossPhase2At(void);

// Gohma phase 2: fires a volley of homing pearl arrows from the shooter's (an Actor*) focus point (GohmaPearlArrows.cpp)
void ZOP_SpawnPearlVolley(struct PlayState* play, void* shooter, int count);
int ZOP_IsPearlArrow(short actorId);

// Gohma phase 2: blends her skin/shell textures toward a red set (0 = original, 1 = fully red). Returns nonzero when
// texture data changed, so the caller must invalidate the texture cache for them (GohmaRecolor.cpp)
int ZOP_GohmaSetRecolor(float amount);

// Gohma phase 2: a tracking laser from the shooter's (an Actor*) eye, lasting ZOP_GOHMA_LASER_FRAMES (GohmaLaser.cpp)
void ZOP_SpawnGohmaLaser(struct PlayState* play, void* shooter);
// Same with a custom firing time in frames (the full laser lasts 22 + fireFrames + 8 frames)
void ZOP_SpawnGohmaLaserTimed(struct PlayState* play, void* shooter, int fireFrames);
void ZOP_StopGohmaLasers(struct PlayState* play);
int ZOP_IsGohmaLaser(short actorId);
int ZOP_GohmaLaserActive(struct PlayState* play);

// Gohma phase 2: glowing rifts racing over the floor from (x, y, z), fanned around yaw (GohmaFloorLanes.cpp)
void ZOP_SpawnGohmaFloorLanes(struct PlayState* play, void* shooter, float x, float y, float z, short yaw, int count);
int ZOP_IsGohmaFloorLane(short actorId);
// Phantom Ganon phase 2: a glowing ring spreading over the floor from (x, y, z) that Link has to jump over
// (GanondrofRings.cpp)
void ZOP_SpawnGndRing(struct PlayState* play, float x, float y, float z, float maxRadius, float speed);

// z_player.c / z_en_elf.c: nonzero when Navi always flies next to Link and the A button no longer calls her in or
// out (ModernControls.cpp)
int ZOP_KeepNaviOut(void);

// z_player.c, func_80837948 (starting a sword swing): picks the next swing of a combo. attack is the swing vanilla
// chose. Returns -1 when vanilla handles it, otherwise the swing to use and its combo counter (3 = finisher)
// (ModernControls.cpp)
int ZOP_SwordCombo(struct PlayState* play, int attack, int zTargeting, int* comboCounter);
// z_player.c, sword swing action: a sword press during the swing / the swing animation ended. The latter returns
// nonzero when a remembered press should start the next swing right away
void ZOP_SwordComboBufferPress(void);
int ZOP_SwordComboSwingEnded(struct PlayState* play);

// z_player.c: snappier running. Scales a turn rate (binary angle per frame) / an acceleration step; quick turn is
// nonzero when sharp turns should spin Link around instead of braking to a stop first (ModernControls.cpp)
short ZOP_PlayerTurnRate(short vanillaRate);
float ZOP_PlayerAccel(float vanillaAccel);
int ZOP_PlayerQuickTurn(void);
// z_player.c, locked-on movement: scales a turn step (binary angle) like ZOP_PlayerTurnRate
float ZOP_LockOnTurnStep(float vanillaStep);

// z_player.c, A while locked on with the stick forward/neutral: nonzero when Link only leaps toward the target
// instead of doing the jump attack right away; the jump attack then comes from the sword button in the air
// (ModernControls.cpp)
int ZOP_LockOnLeap(void);
float ZOP_LockOnLeapSpeed(void);
// Jump height of the side hops while Z-targeting (as high as forward/back with modern controls)
float ZOP_SideHopHeight(float vanillaHeight);
// Air control in hops / Z-targeting jumps (push per frame, 0 = off)
float ZOP_AirControl(void);
// Link faces `face` and hops to `spot`: backflip away or leap toward it (cutscenes, z_player.c)
void ZOP_PlayerHopTo(struct PlayState* play, Vec3f* spot, Vec3f* face);
// Lock-on camera without a jolt when Link jumps
int ZOP_SmoothJumpCamera(void);

// z_camera.c, Camera_Battle1 (lock-on camera): scales the camera distance and field of view for the target (an
// Actor*); playerPos is a Vec3f* (ModernControls.cpp)
void ZOP_LockOnFraming(void* target, void* playerPos, float playerHeight, float* distance, float* fov);
// z_camera.c, Camera_Battle1: adjusts the camera pitch (binary angle, positive = eye above) for the target (an Actor*)
short ZOP_LockOnPitch(void* target, short pitch);

// z_camera.c, Camera_Free: softens the right-stick camera input (yaw/pitch deltas of this frame)
void ZOP_SmoothFreeLook(float* camX, float* camY, int frame);

// Phase 2 intro cutscene style: 0 = slam and roar, 1 = fake death (Difficulty.cpp)
int ZOP_Phase2IntroStyle(void);

// Phantom Ganon rallies (GanondrofRally.cpp): the room lights up with the number of hits so far (0 = fade out)
void ZOP_GndRallyGlow(int hits);
// ... or to a given haze level (1 = full rally, more is brighter, 0 = fade back)
void ZOP_GndRallyLight(float level);
// ... and flickers up brightly for a moment (hits in the lightning part of a rally, Phantom Ganon hit); strength 1 =
// normal flash
void ZOP_GndRallyFlash(float strength);
// ... and it starts / stops raining in the arena
void ZOP_GndRallyRain(int on);

// Testing aid: bosses skip straight to their phase 2 intro after the encounter cutscene (Difficulty.cpp)
int ZOP_TestPhase2Intro(void);
// Testing aid: Gohma stands in front of Link and cycles through eyelid poses
int ZOP_TestGohmaEye(void);
// Testing aid: Phantom Ganon skips his intro and painting phase and starts floating
int ZOP_TestGanondrofPhase2(void);
// Testing aid: Link is put into the arena and Phantom Ganon only does his staff slam
int ZOP_TestGanondrofSlam(void);

#ifdef __cplusplus
}
#endif

// Aim + fire + fade frames of a Gohma laser with the default firing time
#define ZOP_GOHMA_LASER_FRAMES 94

// Picks the aggressive value when the difficulty option is on, the vanilla one otherwise
#define ZOP_AGGRO(vanilla, aggressive) (ZOP_EnemyAggressive() ? (aggressive) : (vanilla))

// Picks the hard value when the difficulty options are on, the vanilla one otherwise
#define ZOP_HARD(vanilla, hard) (ZOP_DifficultyEnabled() ? (hard) : (vanilla))

// Scales a stun duration in frames
#define ZOP_STUN(frames) ((s16)((frames) * ZOP_EnemyStunMult()))

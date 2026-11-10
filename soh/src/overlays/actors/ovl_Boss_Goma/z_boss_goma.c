#include "z_boss_goma.h"
#include "textures/boss_title_cards/object_goma.h"
#include "objects/object_goma/object_goma.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include "overlays/ovl_Boss_Ganon/ovl_Boss_Ganon.h" // gGanondorfShockwaveDL for the barrage slam
#include "overlays/actors/ovl_En_Goma/z_en_goma.h"
#include "overlays/actors/ovl_Door_Shutter/z_door_shutter.h"
#include "overlays/actors/ovl_Door_Warp1/z_door_warp1.h"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/ZeldaOOTProject/ZeldaOOTProjectHooks.h"
#include <libultraship/bridge/consolevariablebridge.h>

#define FLAGS                                                                                 \
    (ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE | ACTOR_FLAG_UPDATE_CULLING_DISABLED | \
     ACTOR_FLAG_DRAW_CULLING_DISABLED)

// IRIS_FOLLOW: gohma looks towards the player (iris rotation)
// BONUS_IFRAMES: gain invincibility frames when the player does something (throwing things?), or
// randomly (see BossGoma_UpdateEye)
typedef enum {
    EYESTATE_IRIS_FOLLOW_BONUS_IFRAMES, // default, allows not drawing lens and iris when eye is closed
    EYESTATE_IRIS_NO_FOLLOW_NO_IFRAMES,
    EYESTATE_IRIS_FOLLOW_NO_IFRAMES
} GohmaEyeState;

typedef enum {
    VISUALSTATE_RED,         // main/eye: red
    VISUALSTATE_DEFAULT,     // main: greenish cyan, blinks with dark gray every 16 frames; eye: white
    VISUALSTATE_DEFEATED,    // main/eye: dark gray
    VISUALSTATE_STUNNED = 4, // main: greenish cyan, alternates with blue; eye: greenish cyan
    VISUALSTATE_HIT          // main: greenish cyan, alternates with red; eye: greenish cyan
} GohmaVisualState;

void BossGoma_Init(Actor* thisx, PlayState* play);
void BossGoma_Destroy(Actor* thisx, PlayState* play);
void BossGoma_Update(Actor* thisx, PlayState* play);
void BossGoma_Draw(Actor* thisx, PlayState* play);

void BossGoma_SetupEncounter(BossGoma* this, PlayState* play);
void BossGoma_Encounter(BossGoma* this, PlayState* play);
void BossGoma_Defeated(BossGoma* this, PlayState* play);
void BossGoma_FloorAttackPosture(BossGoma* this, PlayState* play);
void BossGoma_FloorPrepareAttack(BossGoma* this, PlayState* play);
void BossGoma_FloorAttack(BossGoma* this, PlayState* play);
void BossGoma_FloorDamaged(BossGoma* this, PlayState* play);
void BossGoma_FloorLandStruckDown(BossGoma* this, PlayState* play);
void BossGoma_FloorLand(BossGoma* this, PlayState* play);
void BossGoma_FloorStunned(BossGoma* this, PlayState* play);
void BossGoma_FallJump(BossGoma* this, PlayState* play);
void BossGoma_FallStruckDown(BossGoma* this, PlayState* play);
void BossGoma_CeilingSpawnGohmas(BossGoma* this, PlayState* play);
void BossGoma_CeilingPrepareSpawnGohmas(BossGoma* this, PlayState* play);
void BossGoma_FloorIdle(BossGoma* this, PlayState* play);
void BossGoma_CeilingIdle(BossGoma* this, PlayState* play);
void BossGoma_FloorMain(BossGoma* this, PlayState* play);
void BossGoma_WallClimb(BossGoma* this, PlayState* play);
void BossGoma_CeilingMoveToCenter(BossGoma* this, PlayState* play);
void BossGoma_SpawnChildGohma(BossGoma* this, PlayState* play, s16 i);

// ZeldaOOTProject: second phase once Gohma is down to half her health (gZOP.Difficulty.Phase2At). She retreats to the ceiling more often and,
// instead of laying eggs, broods there and fires volleys of homing pearl arrows.
#define BOSSGOMA_PHASE2_VOLLEY_FRAMES 110
#define BOSSGOMA_PHASE2_ROAR_FRAMES (BOSSGOMA_PHASE2_LINK_SHOT_FRAME + 12) // ends right after Link's startle
// Roar cutscene: orbit around Gohma, then a close-up of her eye opening, then a shot on Link
#define BOSSGOMA_PHASE2_EYE_SHOT_FRAME 70
#define BOSSGOMA_PHASE2_EYE_OPEN_FRAME 84
#define BOSSGOMA_PHASE2_LINK_SHOT_FRAME 102
static u8 sZopPhase2Announced;
static u8 sZopRecolorDirty;
static s16 sZopRoarCamAngle;
// Phase 2 shooting: which attacks the current salvo uses (both can run at the same time)
#define BOSSGOMA_ATTACK_LASER (1 << 0)
#define BOSSGOMA_ATTACK_VOLLEYS (1 << 1)
static u8 sZopAttack;

/**
 * Picks the next salvo: laser alone, arrow volleys alone, or both at once (about a third each)
 */
static u8 BossGoma_PickAttack(void) {
    f32 r = Rand_ZeroOne();

    if (r < 0.33f) {
        return BOSSGOMA_ATTACK_LASER;
    } else if (r < 0.66f) {
        return BOSSGOMA_ATTACK_VOLLEYS;
    }
    return BOSSGOMA_ATTACK_LASER | BOSSGOMA_ATTACK_VOLLEYS;
}
static s16 sZopFloorShootCooldown;

// Phase 2 on the floor: the eye stays shut for stretches of time (no stun, no damage), then opens again with a chime
static u8 sZopEyeCycleClosed;
static s16 sZopEyeCycleTimer;
static s16 sZopEyeClosedFrames; // how long the eye has been shut, so short blinks don't chime
#define BOSSGOMA_EYE_CHIME_MIN_CLOSED 15

// How long the eye has been open; phase 2 floor salvos wait until it has been open for a moment
static s16 sZopEyeOpenFrames;
#define BOSSGOMA_SHOOT_DELAY_AFTER_OPEN 20

// Lid offsets for a wide open eye: in the stand pose the bottom lid covers half the eye (checked on screenshots)
#define BOSSGOMA_EYE_LID_BOTTOM_WIDE 0x1400
#define BOSSGOMA_EYE_LID_TOP_WIDE (-0x300)
// Narrow slit: the eye is shut for hits, but the laser can come out of it
#define BOSSGOMA_EYE_LID_BOTTOM_SLIT (-0x700)
#define BOSSGOMA_EYE_LID_TOP_SLIT 0x1000

// Phase 2 floor cycle: entering it (after the ceiling, a stun...) starts with the eye shut, except right after the
// intro cutscene, which ends on the eye opening
static u8 sZopEyeWasInCycle;
static u8 sZopEyeCycleFromRoar;
static s16 sZopEyeCycleOpenStart; // > 0: entering the cycle opens the eye for this long (with chime) instead of shut

// The animations move the eyelids on their own, so an "open" eye often looked half shut. Instead the lids always
// start from their pose in the stand animation, plus eyeLidBottomRotX / eyeLidTopRotX.
void SkelAnime_GetFrameData(AnimationHeader* animation, s32 frame, s32 limbCount, Vec3s* frameTable);
static s16 sZopLidBaseBottom;
static s16 sZopLidBaseTop;
static u8 sZopLidBaseValid;

// Phase 2 barrage attack: three quick eye flashes as a warning, a hard floor slam that throws Link far back, then she
// fires lasers and arrow volleys at him while he fights his way back to her
static s16 sZopBarrageCooldown;
static u8 sZopSlamIntoBarrage;
static s16 sZopBarrageShotTimer;
static u8 sZopBarrageShots;
static s16 sZopBarrageSlamTimer; // frames until the floor slam of the barrage lands (0 = none)
static u8 sZopBarrageSlamAnim;   // the slam animation is playing (back to standing when it ends)
#define BOSSGOMA_BARRAGE_SLAM_IMPACT 10 // frame of gGohmaAttackAnim where her claws hit the floor
#define BOSSGOMA_SIGNAL_FRAMES 28
#define BOSSGOMA_BARRAGE_TRIGGER_DIST 380.0f // she starts it when Link is closer than this
#define BOSSGOMA_SLAM_WAVE_RADIUS 520.0f     // the shockwave throws Link back within this distance
#define BOSSGOMA_BARRAGE_FRAMES 300
#define BOSSGOMA_BARRAGE_REACHED_DIST 170.0f

// Shockwave of the barrage slam: Ganondorf's ground shockwave ring, white and misty
static Vec3f sZopWavePos;
static f32 sZopWaveScale;
static s16 sZopWaveAlpha;
static s16 sZopWaveTimer;

static u8 sZopWaveCanHit; // the shockwave throws Link back when its ring reaches him on the ground
// Shape of the current shockwave: final size, where its ring starts catching Link (fraction of the 300x band), how far
// it reaches, and damage (0 = only a push)
static f32 sZopWaveMaxScale = 3.4f;
static f32 sZopWaveInner = 0.5f;
static f32 sZopWaveRadius = 520.0f;
static s16 sZopWaveDamage = 0;

// Small shockwave when she lands (from the ceiling, or her hops); it hurts, so standing right under her doesn't pay off
static void BossGoma_SetupLandingWave(void) {
    sZopWaveMaxScale = 0.75f;
    sZopWaveInner = 0.0f;
    sZopWaveRadius = 200.0f;
    sZopWaveDamage = 16;
    sZopWaveCanHit = true;
}

// Hop attack landings: smaller still, so a roll away gets Link out of reach
static void BossGoma_SetupHopWave(void) {
    sZopWaveMaxScale = 0.45f;
    sZopWaveInner = 0.0f;
    sZopWaveRadius = 120.0f;
    sZopWaveDamage = 16;
    sZopWaveCanHit = true;
}

// The big barrage / intro shockwave: only pushes
static void BossGoma_SetupSlamWave(void) {
    sZopWaveMaxScale = 3.4f;
    sZopWaveInner = 0.5f;
    sZopWaveRadius = BOSSGOMA_SLAM_WAVE_RADIUS;
    sZopWaveDamage = 0;
}

static void BossGoma_SpawnShockwave(BossGoma* this, f32 floorY) {
    sZopWavePos = this->actor.world.pos;
    sZopWavePos.y = floorY + 2.0f;
    sZopWaveScale = 0.2f;
    sZopWaveAlpha = 255;
    sZopWaveTimer = 0;
}

// Expands and fades the shockwave, once per game frame
static void BossGoma_UpdateShockwave(void) {
    if (sZopWaveAlpha > 0) {
        sZopWaveTimer++;
        Math_ApproachF(&sZopWaveScale, sZopWaveMaxScale, 1.0f, 0.24f * sZopWaveMaxScale / 3.4f + 0.06f);
        sZopWaveAlpha -= (sZopWaveTimer < 6) ? 8 : 22;
        if (sZopWaveAlpha < 0) {
            sZopWaveAlpha = 0;
        }
    }
}

// Hop attack: she leaps from spot to spot close to Link, and every landing sends a small damaging shockwave, so he has
// to keep moving
static s16 sZopHopCooldown;
static s16 sZopHopsLeft;
static s16 sZopHopTimer;   // wind-up / pause frames before the next leap (0 while in the air)
static u8 sZopHopAirborne;
static f32 sZopHopFloorY; // floor height at takeoff: she lands there at the latest (the floor check can miss her)
#define BOSSGOMA_HOP_FLIGHT_FRAMES 20
#define BOSSGOMA_HOP_WINDUP_FRAMES 8
#define BOSSGOMA_HOP_PAUSE_FRAMES 10

// Flash of light on the eye when it opens (frames left)
static s16 sZopEyeFlashTimer;
#define BOSSGOMA_EYE_FLASH_FRAMES 12

static s32 BossGoma_InPhase2(BossGoma* this) {
    return ZOP_BossPhase2(&this->actor, 10);
}

// Phase 2: everything she does with her legs is 20% faster
#define BOSSGOMA_P2_SPEED(x) ((x) * (BossGoma_InPhase2(this) ? 1.2f : 1.0f))
// Phase 2: heading for the wall and climbing it goes twice as fast, so she spends less time getting to the ceiling
#define BOSSGOMA_P2_CLIMB(x) ((x) * (BossGoma_InPhase2(this) ? 2.0f : 1.0f))

static void BossGoma_Phase2Roar(BossGoma* this, PlayState* play);
static void BossGoma_Phase2Slam(BossGoma* this, PlayState* play);
static void BossGoma_SetupPhase2Roar(BossGoma* this, PlayState* play);
static void BossGoma_Phase2FloorShoot(BossGoma* this, PlayState* play);
static void BossGoma_Phase2Signal(BossGoma* this, PlayState* play);
static void BossGoma_Phase2Barrage(BossGoma* this, PlayState* play);
static void BossGoma_SetupPhase2Signal(BossGoma* this);
static void BossGoma_SetupPhase2Barrage(BossGoma* this);
static void BossGoma_Phase2FakeDeath(BossGoma* this, PlayState* play);
static void BossGoma_SetupPhase2FakeDeath(BossGoma* this, PlayState* play);
static s32 BossGoma_TryBarrage(BossGoma* this);
static void BossGoma_Hop(BossGoma* this, PlayState* play);
static s32 BossGoma_TryHop(BossGoma* this, PlayState* play);
void BossGoma_PlayEffectsAndSfx(BossGoma* this, PlayState* play, s16 arg2, s16 amountMinus1);
void BossGoma_SetupFloorMain(BossGoma* this);

/**
 * Phase 2 intro, part 1: Gohma rears up and slams the floor. The shockwave throws Link back without hurting him,
 * which opens up some distance before the cutscene.
 */
static void BossGoma_SetupPhase2Slam(BossGoma* this) {
    Animation_Change(&this->skelanime, &gGohmaAttackAnim, 1.0f, 0.0f, Animation_GetLastFrame(&gGohmaAttackAnim),
                     ANIMMODE_ONCE, -5.0f);
    this->actionFunc = BossGoma_Phase2Slam;
    this->actionState = 0;
    this->disableGameplayLogic = true; // no hits and no damaging contact meanwhile
    this->actor.speedXZ = 0.0f;
}

static void BossGoma_Phase2Slam(BossGoma* this, PlayState* play) {
    if (ZOP_TestPhase2Intro() && !sZopPhase2Announced) {
        // Testing aid (see BossGoma_Init): health is only scaled after Init, so drop it to phase 2 here
        this->actor.colChkInfo.health = (u8)CLAMP_MIN(this->actor.colChkInfo.health * ZOP_BossPhase2At(), 1);
        sZopPhase2Announced = true;
    }

    SkelAnime_Update(&this->skelanime);
    Math_ApproachS(&this->actor.shape.rot.x, 0, 2, 0xBB8);
    Math_ApproachS(&this->actor.world.rot.y, this->actor.yawTowardsPlayer, 3, 0x7D0);
    this->visualState = VISUALSTATE_RED;

    switch (this->actionState) {
        case 0:
            if (Animation_OnFrame(&this->skelanime, 10.0f)) {
                // Impact: harder than her normal lunge
                BossGoma_PlayEffectsAndSfx(this, play, 3, 8);
                Actor_RequestQuakeAndRumble(&this->actor, play, 14, 22);
                Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_CRY1);
                Actor_SpawnFloorDustRing(play, &this->actor, &this->actor.world.pos, 120.0f, 12, 10.0f, 800, 14, true);
                if (sZopSlamIntoBarrage) {
                    // Barrage attack: a shockwave rolls over the floor; within its reach Link flies a lot further
                    // (no damage), and has to fight his way back
                    BossGoma_SetupSlamWave();
                    BossGoma_SpawnShockwave(this, this->actor.world.pos.y);
                    Actor_SpawnFloorDustRing(play, &this->actor, &sZopWavePos, 200.0f, 20, 12.0f, 1000, 20, true);
                    // The ring itself throws Link back once it reaches him, unless he jumps over it
                    sZopWaveCanHit = true;
                    // Reward window: her eye opens right with the impact (chime and flare) and she can be hit and
                    // stunned from now on, so a player who jumps the wave gets a chance at her eye
                    this->disableGameplayLogic = false;
                    this->eyeClosedTimer = 0;
                    this->patienceTimer = 200;
                    sZopEyeClosedFrames = 0;
                    Audio_PlayActorSound2(&this->actor, NA_SE_EV_DIAMOND_SWITCH);
                    sZopEyeFlashTimer = BOSSGOMA_EYE_FLASH_FRAMES;
                } else {
                    // Phase 2 intro: the same shockwave opens some distance before the cutscene
                    BossGoma_SetupSlamWave();
                    BossGoma_SpawnShockwave(this, this->actor.world.pos.y);
                    Actor_SpawnFloorDustRing(play, &this->actor, &sZopWavePos, 200.0f, 20, 12.0f, 1000, 20, true);
                    Actor_SetPlayerKnockbackLargeNoDamage(play, &this->actor, 13.0f, this->actor.yawTowardsPlayer,
                                                          6.0f);
                }
                this->actionState = 1;
                this->framesUntilNextAction = 30; // Link lands and gets back up
            }
            break;

        case 1:
            if (sZopSlamIntoBarrage) {
                this->eyeState = EYESTATE_IRIS_FOLLOW_NO_IFRAMES; // no blinking in the reward window
            }
            if (this->framesUntilNextAction == 0) {
                if (sZopSlamIntoBarrage) {
                    sZopSlamIntoBarrage = false;
                    this->disableGameplayLogic = false;
                    BossGoma_SetupPhase2Barrage(this);
                } else if (ZOP_Phase2IntroStyle() == 1) {
                    BossGoma_SetupPhase2FakeDeath(this, play);
                } else {
                    BossGoma_SetupPhase2Roar(this, play);
                }
            }
            break;
    }
}

/**
 * Phase 2 intro, part 2: a short cutscene. The music stops, Gohma screams while the camera circles her and the room
 * fades into the fog of her death scene; then a telephoto shot on Link, startled. Afterwards the boss music starts again and she heads for
 * the ceiling.
 */
static void BossGoma_SetupPhase2Roar(BossGoma* this, PlayState* play) {
    Camera* camera = Play_GetCamera(play, 0);

    Animation_Change(&this->skelanime, &gGohmaStandAnim, 1.0f, 0.0f, Animation_GetLastFrame(&gGohmaStandAnim),
                     ANIMMODE_LOOP, -10.0f);
    this->actionFunc = BossGoma_Phase2Roar;
    this->framesUntilNextAction = BOSSGOMA_PHASE2_ROAR_FRAMES;
    this->disableGameplayLogic = true;
    this->actor.speedXZ = 0.0f;

    func_80064520(play, &play->csCtx);
    Player_SetCsActionWithHaltedActors(play, &this->actor, 1);
    this->subCameraId = Play_CreateSubCamera(play);
    Play_ChangeCameraStatus(play, 0, 3);
    Play_ChangeCameraStatus(play, this->subCameraId, 7);
    this->subCameraEye = camera->eye;
    this->subCameraAt = camera->at;
    sZopRoarCamAngle = Math_Vec3f_Yaw(&this->actor.world.pos, &camera->eye);

    // Stop the boss music
    Audio_QueueSeqCmd(0x1 << 28 | SEQ_PLAYER_BGM_MAIN << 24 | 0x100FF);
}

/**
 * Light bluish haze over the room (fog brightened, tinted blue and pulled in a little, a touch of blue ambient light).
 * intensity 0..1, 0 restores the room's own lighting.
 */
// Phase 2: the bluish haze of the intro flashes up for a moment whenever Link lands a hit (frames left)
static s16 sZopHitFlash;
#define BOSSGOMA_HIT_FLASH_FRAMES 9

static void BossGoma_HitFlash(void) {
    if (sZopPhase2Announced) {
        sZopHitFlash = BOSSGOMA_HIT_FLASH_FRAMES;
    }
}

static void BossGoma_SetHaze(PlayState* play, f32 intensity) {
    f32 fogNear = play->envCtx.lightSettings.fogNear;

    play->envCtx.adjFogColor[0] = (s16)(35.0f * intensity);
    play->envCtx.adjFogColor[1] = (s16)(55.0f * intensity);
    play->envCtx.adjFogColor[2] = (s16)(85.0f * intensity);
    play->envCtx.adjAmbientColor[0] = (s16)(12.0f * intensity);
    play->envCtx.adjAmbientColor[1] = (s16)(18.0f * intensity);
    play->envCtx.adjAmbientColor[2] = (s16)(30.0f * intensity);
    play->envCtx.adjFogNear = (fogNear > 900.0f) ? (s16)((900.0f - fogNear) * intensity) : 0;
}

/**
 * Phase 2 cutscenes: hard cut to Link's face, he flinches in shock (shotFrame counts from the cut). Telephoto: far away
 * with a narrow field of view.
 */
static void BossGoma_CsLinkStartle(BossGoma* this, PlayState* play, s16 shotFrame) {
    Player* player = GET_PLAYER(play);
    Vec3f* head = &player->bodyPartsPos[PLAYER_BODYPART_HEAD];
    s16 linkYaw = Math_Vec3f_Yaw(&player->actor.world.pos, &this->actor.world.pos);

    if (shotFrame == 0) {
        player->actor.shape.rot.y = player->actor.world.rot.y = player->yaw = linkYaw;
        // One startled step back: the backstep hop, plus his "surprised" voice
        LinkAnimation_Change(play, &player->skelAnime, (LinkAnimationHeader*)gPlayerAnim_link_normal_backspace, 1.0f,
                             0.0f, Animation_GetLastFrame(gPlayerAnim_link_normal_backspace), ANIMMODE_ONCE, -3.0f);
        Audio_PlayActorSound2(&player->actor, LINK_IS_ADULT ? NA_SE_VO_LI_SURPRISE : NA_SE_VO_LI_SURPRISE_KID);
        Play_CameraSetFov(play, this->subCameraId, 16.0f);
    }
    if (shotFrame < 7) {
        // Slide back about 20 units along the step
        player->actor.world.pos.x -= Math_SinS(linkYaw) * 3.0f;
        player->actor.world.pos.z -= Math_CosS(linkYaw) * 3.0f;
    }
    player->actor.shape.face = 7; // wide eyes, grimacing mouth (PLAYER_FACE_SURPRISED in the decomp)

    this->subCameraEye.x = head->x + Math_SinS(linkYaw) * 180.0f + Math_SinS(linkYaw + 0x4000) * 40.0f;
    this->subCameraEye.y = head->y + 6.0f;
    this->subCameraEye.z = head->z + Math_CosS(linkYaw) * 180.0f + Math_CosS(linkYaw + 0x4000) * 40.0f;
    this->subCameraAt.x = head->x;
    this->subCameraAt.y = head->y + 1.0f;
    this->subCameraAt.z = head->z;
}

/**
 * Phase 2 cutscenes: hands control back. The camera goes behind Link looking at Gohma; she is out of patience, so she
 * backs off and climbs the nearest wall to start shooting.
 */
static void BossGoma_EndPhase2Cutscene(BossGoma* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    Camera* cam = Play_GetCamera(play, 0);
    // Hand back a camera behind Link, looking at Gohma
    s16 toGohma = Math_Vec3f_Yaw(&player->actor.world.pos, &this->actor.world.pos);
    Vec3f eye;
    Vec3f at;

    eye.x = player->actor.world.pos.x - Math_SinS(toGohma) * 200.0f;
    eye.y = player->actor.world.pos.y + 80.0f;
    eye.z = player->actor.world.pos.z - Math_CosS(toGohma) * 200.0f;
    at.x = (player->actor.world.pos.x + this->actor.focus.pos.x) * 0.5f;
    at.y = (player->actor.world.pos.y + 40.0f + this->actor.focus.pos.y) * 0.5f;
    at.z = (player->actor.world.pos.z + this->actor.focus.pos.z) * 0.5f;
    player->actor.shape.rot.y = player->actor.world.rot.y = player->yaw = toGohma;
    cam->eye = eye;
    cam->eyeNext = eye;
    cam->at = at;
    this->subCameraEye = eye;
    this->subCameraAt = at;
    play->manualCamera = false;
    func_800C08AC(play, this->subCameraId, 0);
    this->subCameraId = 0;
    func_80064534(play, &play->csCtx);
    Player_SetCsActionWithHaltedActors(play, &this->actor, 7);
    BossGoma_SetHaze(play, 0.0f);
    this->disableGameplayLogic = false;
    this->invincibilityFrames = 10;
    sZopFloorShootCooldown = 60;

    sZopEyeCycleFromRoar = true;
    sZopBarrageCooldown = 120;

    // Out of patience: she backs off, climbs the nearest wall and starts shooting
    BossGoma_SetupFloorMain(this);
    this->patienceTimer = 0;
}

static void BossGoma_Phase2Roar(BossGoma* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    s16 elapsed = BOSSGOMA_PHASE2_ROAR_FRAMES - this->framesUntilNextAction;

    SkelAnime_Update(&this->skelanime);
    Math_ApproachS(&this->actor.shape.rot.x, 0, 2, 0xBB8);

    if (elapsed == 6 || elapsed == 30 || elapsed == 54) {
        Audio_PlayActorSound2(&this->actor, (elapsed == 30) ? NA_SE_EN_GOMA_CRY2 : NA_SE_EN_GOMA_CRY1);
        Actor_RequestQuakeAndRumble(&this->actor, play, 8, 14);
    }

    // Eye wide open and twitching while she screams
    Math_ApproachF(&this->eyeIrisScaleX, 1.3f + 0.25f * Math_SinS(elapsed * 0x1800), 0.5f, 0.3f);
    Math_ApproachF(&this->eyeIrisScaleY, 1.3f + 0.25f * Math_CosS(elapsed * 0x1500), 0.5f, 0.3f);
    this->visualState = VISUALSTATE_RED;

    // A subtle, light bluish haze: swells in with the first scream and clears during the shot on Link
    {
        f32 haze;

        if (elapsed < 8) {
            haze = 0.0f;
        } else if (elapsed < 30) {
            haze = (elapsed - 8) / 22.0f;
        } else if (elapsed < BOSSGOMA_PHASE2_ROAR_FRAMES - 25) {
            haze = 1.0f;
        } else {
            haze = this->framesUntilNextAction / 25.0f;
        }
        BossGoma_SetHaze(play, haze);
    }

    if (elapsed >= BOSSGOMA_PHASE2_EYE_SHOT_FRAME && elapsed < BOSSGOMA_PHASE2_LINK_SHOT_FRAME) {
        // Shot 2: close-up of her eye. It is shut, then opens slowly with the crystal chime and a lens flare
        s16 yaw = this->actor.shape.rot.y;

        if (elapsed == BOSSGOMA_PHASE2_EYE_SHOT_FRAME) {
            this->eyeLidBottomRotX = -0xA98;
            this->eyeLidTopRotX = 0x1600;
            Play_CameraSetFov(play, this->subCameraId, 38.0f);
        }
        if (elapsed >= BOSSGOMA_PHASE2_EYE_OPEN_FRAME) {
            if (elapsed == BOSSGOMA_PHASE2_EYE_OPEN_FRAME) {
                Audio_PlayActorSound2(&this->actor, NA_SE_EV_DIAMOND_SWITCH);
                // The boss music kicks back in as her eye opens
                Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_BOSS);
                sZopEyeFlashTimer = BOSSGOMA_EYE_FLASH_FRAMES;
            }
            Math_ApproachS(&this->eyeLidBottomRotX, BOSSGOMA_EYE_LID_BOTTOM_WIDE, 1, 0x500);
            Math_ApproachS(&this->eyeLidTopRotX, BOSSGOMA_EYE_LID_TOP_WIDE, 1, 0x400);
        }

        // In front of the eye, a little below it, slowly pushing in
        {
            f32 push = (f32)(elapsed - BOSSGOMA_PHASE2_EYE_SHOT_FRAME) /
                       (BOSSGOMA_PHASE2_LINK_SHOT_FRAME - BOSSGOMA_PHASE2_EYE_SHOT_FRAME);
            f32 dist = 150.0f - 30.0f * push;

            this->subCameraEye.x = this->actor.focus.pos.x + Math_SinS(yaw) * dist;
            this->subCameraEye.y = this->actor.focus.pos.y - 12.0f;
            this->subCameraEye.z = this->actor.focus.pos.z + Math_CosS(yaw) * dist;
            this->subCameraAt = this->actor.focus.pos;
        }
    } else if (elapsed < BOSSGOMA_PHASE2_LINK_SHOT_FRAME) {
        // Shot 1: the camera circles Gohma, slightly below her eye line
        sZopRoarCamAngle += 0x2C0;
        Math_ApproachF(&this->subCameraEye.x, this->actor.world.pos.x + Math_SinS(sZopRoarCamAngle) * 260.0f, 0.3f,
                       60.0f);
        Math_ApproachF(&this->subCameraEye.y, this->actor.world.pos.y + 70.0f, 0.3f, 30.0f);
        Math_ApproachF(&this->subCameraEye.z, this->actor.world.pos.z + Math_CosS(sZopRoarCamAngle) * 260.0f, 0.3f,
                       60.0f);
        Math_ApproachF(&this->subCameraAt.x, this->actor.focus.pos.x, 0.3f, 40.0f);
        Math_ApproachF(&this->subCameraAt.y, this->actor.focus.pos.y, 0.3f, 40.0f);
        Math_ApproachF(&this->subCameraAt.z, this->actor.focus.pos.z, 0.3f, 40.0f);
    } else {
        // Shot 3: hard cut to Link's face, he flinches in shock
        BossGoma_CsLinkStartle(this, play, elapsed - BOSSGOMA_PHASE2_LINK_SHOT_FRAME);
    }
    Play_CameraSetAtEye(play, this->subCameraId, &this->subCameraAt, &this->subCameraEye);

    if (this->framesUntilNextAction == 0) {
        BossGoma_EndPhase2Cutscene(this, play);
    }
}

// Phase 2 intro, alternative (gZOP.Difficulty.Phase2Intro = 1): she collapses as if beaten, the room goes quiet, then
// her eye snaps open, Link startles, and she rises again (her death animation played backwards) and screams.
// Timing follows the length of her collapse: she lies still for 2 s before the eye opens
static s16 sZopFdCollapseFrames;
#define BOSSGOMA_FD_EYE_OPEN (sZopFdCollapseFrames + 40)
#define BOSSGOMA_FD_EYE_SHOT (BOSSGOMA_FD_EYE_OPEN - 12)
#define BOSSGOMA_FD_LINK_SHOT (BOSSGOMA_FD_EYE_OPEN + 14)
#define BOSSGOMA_FD_RISE (BOSSGOMA_FD_LINK_SHOT + 12)
#define BOSSGOMA_FD_RISE_SPEED 1.6f
#define BOSSGOMA_FD_SCREAM_FRAMES 34
static s16 sZopFdElapsed;
static Vec3f sZopEyeAheadPos; // a point straight in front of her eye (BossGoma_PostLimbDraw)
static s16 sZopFdRiseFrames;

static void BossGoma_SetupPhase2FakeDeath(BossGoma* this, PlayState* play) {
    Camera* camera = Play_GetCamera(play, 0);

    Animation_Change(&this->skelanime, &gGohmaDeathAnim, 1.0f, 0.0f, Animation_GetLastFrame(&gGohmaDeathAnim),
                     ANIMMODE_ONCE, -2.0f);
    this->actionFunc = BossGoma_Phase2FakeDeath;
    this->disableGameplayLogic = true;
    this->actor.speedXZ = 0.0f;
    sZopFdElapsed = 0;
    sZopFdRiseFrames = (s16)(Animation_GetLastFrame(&gGohmaDeathAnim) / BOSSGOMA_FD_RISE_SPEED) + 1;
    sZopFdCollapseFrames = (s16)Animation_GetLastFrame(&gGohmaDeathAnim);
    ZOP_StopGohmaLasers(play);

    func_80064520(play, &play->csCtx);
    Player_SetCsActionWithHaltedActors(play, &this->actor, 1);
    this->subCameraId = Play_CreateSubCamera(play);
    Play_ChangeCameraStatus(play, 0, 3);
    Play_ChangeCameraStatus(play, this->subCameraId, 7);
    this->subCameraEye = camera->eye;
    this->subCameraAt = camera->at;
    sZopRoarCamAngle = Math_Vec3f_Yaw(&this->actor.world.pos, &camera->eye);

    // As if the fight were over: the music stops, her death cry
    Audio_QueueSeqCmd(0x1 << 28 | SEQ_PLAYER_BGM_MAIN << 24 | 0x100FF);
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_DEAD);
}

static void BossGoma_Phase2FakeDeath(BossGoma* this, PlayState* play) {
    s16 e = sZopFdElapsed++;
    s16 riseEnd = BOSSGOMA_FD_RISE + sZopFdRiseFrames;
    s16 yaw = this->actor.shape.rot.y;

    SkelAnime_Update(&this->skelanime);
    this->eyeState = EYESTATE_IRIS_NO_FOLLOW_NO_IFRAMES;

    if (e < BOSSGOMA_FD_EYE_SHOT) {
        // Shot 1: from the side she slumps down; the eye slowly closes, then nothing moves
        f32 dist = (e < 40) ? 300.0f : CLAMP_MIN(300.0f - (e - 40) * 1.2f, 190.0f);

        // She hits the floor: same dust, thud and rumble as in her real death (BossGoma_Defeated)
        if (Animation_OnFrame(&this->skelanime, 107.0f)) {
            BossGoma_PlayEffectsAndSfx(this, play, 0, 8);
            func_800A9F6C(0.0f, 0x96, 0x14, 0x14);
            Actor_RequestQuakeAndRumble(&this->actor, play, 6, 10);
        }
        if (e >= 8) {
            Math_ApproachS(&this->eyeLidBottomRotX, -0xA98, 1, 0x200);
            Math_ApproachS(&this->eyeLidTopRotX, 0x1600, 1, 0x300);
        }
        Math_ApproachF(&this->eyeIrisScaleX, 0.6f, 0.2f, 0.05f);
        Math_ApproachF(&this->eyeIrisScaleY, 0.6f, 0.2f, 0.05f);
        Math_ApproachF(&this->subCameraEye.x, this->actor.world.pos.x + Math_SinS(yaw + 0x5000) * dist, 0.2f, 40.0f);
        Math_ApproachF(&this->subCameraEye.y, this->actor.world.pos.y + 90.0f, 0.2f, 20.0f);
        Math_ApproachF(&this->subCameraEye.z, this->actor.world.pos.z + Math_CosS(yaw + 0x5000) * dist, 0.2f, 40.0f);
        Math_ApproachF(&this->subCameraAt.x, this->actor.world.pos.x, 0.2f, 30.0f);
        Math_ApproachF(&this->subCameraAt.y, this->actor.world.pos.y + 40.0f, 0.2f, 30.0f);
        Math_ApproachF(&this->subCameraAt.z, this->actor.world.pos.z, 0.2f, 30.0f);
    } else if (e < BOSSGOMA_FD_LINK_SHOT) {
        // Shot 2: close-up of the shut eye ... which suddenly snaps wide open
        if (e == BOSSGOMA_FD_EYE_SHOT) {
            this->eyeLidBottomRotX = -0xA98;
            this->eyeLidTopRotX = 0x1600;
            Play_CameraSetFov(play, this->subCameraId, 38.0f);
        }
        if (e >= BOSSGOMA_FD_EYE_OPEN) {
            if (e == BOSSGOMA_FD_EYE_OPEN) {
                Audio_PlayActorSound2(&this->actor, NA_SE_EV_DIAMOND_SWITCH);
                // The boss music kicks back in as her eye opens
                Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_BOSS);
                sZopEyeFlashTimer = BOSSGOMA_EYE_FLASH_FRAMES;
                this->eyeIrisScaleX = this->eyeIrisScaleY = 1.6f;
            }
            Math_ApproachS(&this->eyeLidBottomRotX, BOSSGOMA_EYE_LID_BOTTOM_WIDE, 1, 0x1400);
            Math_ApproachS(&this->eyeLidTopRotX, BOSSGOMA_EYE_LID_TOP_WIDE, 1, 0x1000);
            Math_ApproachF(&this->eyeIrisScaleX, 1.2f, 0.3f, 0.1f);
            Math_ApproachF(&this->eyeIrisScaleY, 1.2f, 0.3f, 0.1f);
        }
        {
            // Straight in front of the eye, wherever it points as she lies there
            Vec3f ahead;
            f32 len;

            Math_Vec3f_Diff(&sZopEyeAheadPos, &this->actor.focus.pos, &ahead);
            len = sqrtf(SQ(ahead.x) + SQ(ahead.y) + SQ(ahead.z));
            if (len < 1.0f) {
                ahead.x = Math_SinS(yaw);
                ahead.y = 0.0f;
                ahead.z = Math_CosS(yaw);
                len = 1.0f;
            }
            this->subCameraEye.x = this->actor.focus.pos.x + ahead.x / len * 130.0f;
            this->subCameraEye.y = this->actor.focus.pos.y + ahead.y / len * 130.0f + 12.0f;
            this->subCameraEye.z = this->actor.focus.pos.z + ahead.z / len * 130.0f;
            this->subCameraAt = this->actor.focus.pos;
        }
    } else if (e < BOSSGOMA_FD_RISE) {
        // Shot 3: Link startles
        BossGoma_CsLinkStartle(this, play, e - BOSSGOMA_FD_LINK_SHOT);
    } else {
        // Shot 4: low and in front of her: she rises (death animation backwards), turns red, screams
        if (e == BOSSGOMA_FD_RISE) {
            Animation_Change(&this->skelanime, &gGohmaDeathAnim, -BOSSGOMA_FD_RISE_SPEED,
                             Animation_GetLastFrame(&gGohmaDeathAnim), 0.0f, ANIMMODE_ONCE, 0.0f);
            Play_CameraSetFov(play, this->subCameraId, 60.0f);
            Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_CRY1);
        }
        if (e == riseEnd) {
            Animation_Change(&this->skelanime, &gGohmaStandAnim, 1.0f, 0.0f, Animation_GetLastFrame(&gGohmaStandAnim),
                             ANIMMODE_LOOP, -6.0f);
            Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_CRY2);
            Actor_RequestQuakeAndRumble(&this->actor, play, 12, 20);
        }
        if (e >= riseEnd) {
            // Eye wide open and twitching while she screams
            Math_ApproachF(&this->eyeIrisScaleX, 1.3f + 0.25f * Math_SinS(e * 0x1800), 0.5f, 0.3f);
            Math_ApproachF(&this->eyeIrisScaleY, 1.3f + 0.25f * Math_CosS(e * 0x1500), 0.5f, 0.3f);
            this->visualState = VISUALSTATE_RED;
        }
        BossGoma_SetHaze(play, CLAMP((f32)(e - BOSSGOMA_FD_RISE) / 20.0f, 0.0f, 1.0f) *
                                   CLAMP((f32)(riseEnd + BOSSGOMA_FD_SCREAM_FRAMES - e) / 12.0f, 0.0f, 1.0f));

        this->subCameraEye.x = this->actor.world.pos.x + Math_SinS(yaw + 0x1000) * 330.0f;
        this->subCameraEye.y = this->actor.world.pos.y + 25.0f;
        this->subCameraEye.z = this->actor.world.pos.z + Math_CosS(yaw + 0x1000) * 330.0f;
        Math_ApproachF(&this->subCameraAt.x, this->actor.focus.pos.x, 0.3f, 40.0f);
        Math_ApproachF(&this->subCameraAt.y, this->actor.focus.pos.y, 0.3f, 40.0f);
        Math_ApproachF(&this->subCameraAt.z, this->actor.focus.pos.z, 0.3f, 40.0f);
    }
    Play_CameraSetAtEye(play, this->subCameraId, &this->subCameraAt, &this->subCameraEye);

    if (e >= riseEnd + BOSSGOMA_FD_SCREAM_FRAMES) {
        BossGoma_EndPhase2Cutscene(this, play);
    }
}

// Lasers from the floor fire shorter than from the ceiling
#define BOSSGOMA_FLOOR_LASER_FIRE_FRAMES 30
#define BOSSGOMA_FLOOR_SHOOT_FRAMES (22 + BOSSGOMA_FLOOR_LASER_FIRE_FRAMES + 8 + 12)

/**
 * Phase 2: Gohma stops on the floor, rears up and fires a tracking laser, three pearl arrow volleys or both.
 * Her eye stays open, so a slingshot hit can still stun her out of it.
 */
static void BossGoma_SetupPhase2FloorShoot(BossGoma* this) {
    Animation_Change(&this->skelanime, &gGohmaStandAnim, BOSSGOMA_P2_SPEED(1.0f), 0.0f,
                     Animation_GetLastFrame(&gGohmaStandAnim), ANIMMODE_LOOP, -8.0f);
    this->actionFunc = BossGoma_Phase2FloorShoot;
    this->framesUntilNextAction = BOSSGOMA_FLOOR_SHOOT_FRAMES;
    sZopAttack = BossGoma_PickAttack();
}

static void BossGoma_Phase2FloorShoot(BossGoma* this, PlayState* play) {
    s16 elapsed = BOSSGOMA_FLOOR_SHOOT_FRAMES - this->framesUntilNextAction;

    SkelAnime_Update(&this->skelanime);
    Math_ApproachZeroF(&this->actor.speedXZ, 0.5f, 2.0f);
    Math_ApproachS(&this->actor.shape.rot.x, 0, 2, 0xBB8);
    Math_ApproachS(&this->actor.world.rot.y, this->actor.yawTowardsPlayer, 4, 0x5DC);

    // If her eye has just opened, the first shot waits a moment, so stunning her right after it opens works. The eye
    // keeps its usual open/shut rhythm while she shoots; while it is shut the laser comes out of a narrow slit.
    if (elapsed == 4 && sZopEyeOpenFrames > 0 && sZopEyeOpenFrames < BOSSGOMA_SHOOT_DELAY_AFTER_OPEN) {
        this->framesUntilNextAction++;
        elapsed--;
    }

    if (elapsed == 4) {
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_CRY1);
        if (sZopAttack & BOSSGOMA_ATTACK_LASER) {
            ZOP_SpawnGohmaLaserTimed(play, &this->actor, BOSSGOMA_FLOOR_LASER_FIRE_FRAMES);
        }
    }
    if ((sZopAttack & BOSSGOMA_ATTACK_VOLLEYS) && (elapsed == 14 || elapsed == 38 || elapsed == 62)) {
        ZOP_SpawnPearlVolley(play, &this->actor, 5);
    }
    if (this->framesUntilNextAction == 0 || (!(sZopAttack & BOSSGOMA_ATTACK_LASER) && elapsed >= 68)) {
        BossGoma_SetupFloorMain(this);
        sZopFloorShootCooldown = (s16)Rand_S16Offset(90, 70);
    }

    this->eyeState = EYESTATE_IRIS_FOLLOW_NO_IFRAMES;
    this->visualState = VISUALSTATE_RED;
}

/**
 * Phase 2 barrage, part 1: she stops and flashes her eye open three times in quick succession (three chimes) as a
 * warning, then slams the floor (BossGoma_Phase2Slam, which continues with the barrage).
 */
/**
 * Starts the barrage attack once its cooldown is over and Link is close enough (phase 2, on the floor)
 */
static s32 BossGoma_TryBarrage(BossGoma* this) {
    if (sZopPhase2Announced && BossGoma_InPhase2(this) && sZopBarrageCooldown == 0 &&
        this->actor.xzDistToPlayer < BOSSGOMA_BARRAGE_TRIGGER_DIST && (this->actor.bgCheckFlags & BGCHECKFLAG_GROUND)) {
        BossGoma_SetupPhase2Signal(this);
        return true;
    }
    return false;
}

static void BossGoma_SetupPhase2Signal(BossGoma* this) {
    Animation_Change(&this->skelanime, &gGohmaStandAnim, 1.0f, 0.0f, Animation_GetLastFrame(&gGohmaStandAnim),
                     ANIMMODE_LOOP, -6.0f);
    this->actionFunc = BossGoma_Phase2Signal;
    this->framesUntilNextAction = BOSSGOMA_SIGNAL_FRAMES;
    // Next barrage in 25-35 s. Set here and not when it ends: a stun cuts the barrage short, and the attack then
    // started again right away
    sZopBarrageCooldown = (s16)Rand_S16Offset(500, 200);
    this->actor.speedXZ = 0.0f;
}

static void BossGoma_Phase2Signal(BossGoma* this, PlayState* play) {
    SkelAnime_Update(&this->skelanime);
    Math_ApproachZeroF(&this->actor.speedXZ, 0.5f, 2.0f);
    Math_ApproachS(&this->actor.shape.rot.x, 0, 2, 0xBB8);
    Math_ApproachS(&this->actor.world.rot.y, this->actor.yawTowardsPlayer, 3, 0x7D0);
    this->eyeState = EYESTATE_IRIS_FOLLOW_NO_IFRAMES;
    this->visualState = VISUALSTATE_RED;

    if (this->framesUntilNextAction == 0) {
        sZopSlamIntoBarrage = true;
        BossGoma_SetupPhase2Slam(this);
    }
}

/**
 * Phase 2 barrage, part 2: she stays where she is, turned toward Link, and keeps firing lasers and arrow volleys at
 * him. Her eye follows the usual short open/shut cycle, so he can stun her on the way. Ends once he is back at her
 * (or after a while).
 */
static void BossGoma_SetupPhase2Barrage(BossGoma* this) {
    Animation_Change(&this->skelanime, &gGohmaStandAnim, 1.0f, 0.0f, Animation_GetLastFrame(&gGohmaStandAnim),
                     ANIMMODE_LOOP, -8.0f);
    this->actionFunc = BossGoma_Phase2Barrage;
    this->framesUntilNextAction = BOSSGOMA_BARRAGE_FRAMES;
    this->actor.speedXZ = 0.0f;
    sZopBarrageShotTimer = 12;
    sZopBarrageShots = 0;
    // The reward window opened at the slam's impact (1.5 s after it) lasts a few frames into the barrage
    sZopEyeCycleOpenStart = 6;
    sZopBarrageSlamTimer = 0;
    sZopBarrageSlamAnim = false;
}

static void BossGoma_Phase2Barrage(BossGoma* this, PlayState* play) {
    SkelAnime_Update(&this->skelanime);
    Math_ApproachZeroF(&this->actor.speedXZ, 0.5f, 2.0f);
    Math_ApproachS(&this->actor.shape.rot.x, 0, 2, 0xBB8);
    Math_ApproachS(&this->actor.world.rot.y, this->actor.yawTowardsPlayer, 4, 0x5DC);
    this->eyeState = EYESTATE_IRIS_FOLLOW_NO_IFRAMES;
    this->visualState = VISUALSTATE_RED;
    this->patienceTimer = 200; // a slingshot hit in the open eye stuns her (BossGoma_UpdateHit)

    // Floor slam: her claws hit the floor at a spot in front of her and glowing rifts race toward Link from there
    if (sZopBarrageSlamTimer > 0 && --sZopBarrageSlamTimer == 0) {
        s16 yaw = this->actor.shape.rot.y;
        f32 side = Rand_CenteredFloat(260.0f);
        Vec3f spot;

        spot.x = this->actor.world.pos.x + Math_SinS(yaw) * 110.0f + Math_SinS(yaw + 0x4000) * side;
        spot.y = this->actor.world.pos.y;
        spot.z = this->actor.world.pos.z + Math_CosS(yaw) * 110.0f + Math_CosS(yaw + 0x4000) * side;
        BossGoma_PlayEffectsAndSfx(this, play, 3, 5);
        Actor_RequestQuakeAndRumble(&this->actor, play, 8, 12);
        Actor_SpawnFloorDustRing(play, &this->actor, &spot, 50.0f, 10, 6.0f, 500, 12, true);
        ZOP_SpawnGohmaFloorLanes(play, &this->actor, spot.x, spot.y, spot.z,
                                 Math_Vec3f_Yaw(&spot, &GET_PLAYER(play)->actor.world.pos), 3);
    }
    if (sZopBarrageSlamAnim && Animation_OnFrame(&this->skelanime, Animation_GetLastFrame(&gGohmaAttackAnim))) {
        sZopBarrageSlamAnim = false;
        Animation_Change(&this->skelanime, &gGohmaStandAnim, 1.0f, 0.0f, Animation_GetLastFrame(&gGohmaStandAnim),
                         ANIMMODE_LOOP, -6.0f);
    }

    if (--sZopBarrageShotTimer <= 0) {
        // A cycle of laser (if none is out), arrow volley and two floor slams
        u8 kind = sZopBarrageShots % 4;

        if (kind == 0 && !ZOP_GohmaLaserActive(play)) {
            ZOP_SpawnGohmaLaserTimed(play, &this->actor, BOSSGOMA_FLOOR_LASER_FIRE_FRAMES);
            Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_CRY1);
            sZopBarrageShotTimer = 40;
        } else if (kind == 2 || kind == 3) {
            Animation_Change(&this->skelanime, &gGohmaAttackAnim, 1.3f, 0.0f,
                             Animation_GetLastFrame(&gGohmaAttackAnim), ANIMMODE_ONCE, -4.0f);
            sZopBarrageSlamTimer = (s16)(BOSSGOMA_BARRAGE_SLAM_IMPACT / 1.3f) + 1;
            sZopBarrageSlamAnim = true;
            sZopBarrageShotTimer = 30;
        } else {
            ZOP_SpawnPearlVolley(play, &this->actor, 4);
            Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_CRY1);
            sZopBarrageShotTimer = 28;
        }
        sZopBarrageShots++;
    }

    if (this->actor.xzDistToPlayer < BOSSGOMA_BARRAGE_REACHED_DIST || this->framesUntilNextAction == 0) {
        ZOP_StopGohmaLasers(play);
        BossGoma_SetupFloorMain(this);
        this->patienceTimer = 120;
    }
}

/**
 * Hop attack: starts once its cooldown is over and Link is neither too close nor too far
 */
static s32 BossGoma_TryHop(BossGoma* this, PlayState* play) {
    if (!ZOP_DifficultyEnabled() || sZopHopCooldown > 0 || this->actor.xzDistToPlayer < 150.0f ||
        this->actor.xzDistToPlayer > 650.0f || !(this->actor.bgCheckFlags & BGCHECKFLAG_GROUND)) {
        return false;
    }
    Animation_Change(&this->skelanime, &gGohmaPrepareAttackAnim, 1.0f, 0.0f,
                     Animation_GetLastFrame(&gGohmaPrepareAttackAnim), ANIMMODE_ONCE, -5.0f);
    this->actionFunc = BossGoma_Hop;
    this->actor.speedXZ = 0.0f;
    this->actor.gravity = -2.0f;
    this->patienceTimer = 200; // her eye can be hit and stun her out of it
    // Phase 2: 3-4 hops, every 20-28 s; phase 1: 2-3 hops, every 30-40 s
    if (sZopPhase2Announced) {
        sZopHopsLeft = 3 + (Rand_ZeroOne() < 0.5f);
        sZopHopCooldown = (s16)Rand_S16Offset(400, 160);
    } else {
        sZopHopsLeft = 2 + (Rand_ZeroOne() < 0.5f);
        sZopHopCooldown = (s16)Rand_S16Offset(600, 200);
    }
    sZopHopTimer = BOSSGOMA_HOP_WINDUP_FRAMES;
    sZopHopAirborne = false;
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_CRY1);
    return true;
}

static void BossGoma_Hop(BossGoma* this, PlayState* play) {
    Player* player = GET_PLAYER(play);

    SkelAnime_Update(&this->skelanime);
    Math_ApproachS(&this->actor.shape.rot.x, 0, 2, 0xBB8);
    this->patienceTimer = 200;
    if (sZopPhase2Announced) {
        this->visualState = VISUALSTATE_RED;
    }

    if (sZopHopAirborne) {
        // In the air: stop at walls; land, then a short pause before the next leap
        if (this->actor.bgCheckFlags & BGCHECKFLAG_WALL) {
            this->actor.speedXZ = 0.0f;
        }
        if (this->actor.velocity.y <= 0.0f &&
            ((this->actor.bgCheckFlags & BGCHECKFLAG_GROUND) || this->actor.world.pos.y <= sZopHopFloorY)) {
            if (this->actor.world.pos.y < sZopHopFloorY) {
                this->actor.world.pos.y = sZopHopFloorY;
            }
            sZopHopAirborne = false;
            this->actor.speedXZ = 0.0f;
            this->actor.velocity.y = 0.0f;
            Animation_Change(&this->skelanime, &gGohmaLandAnim, 1.5f, 0.0f, Animation_GetLastFrame(&gGohmaLandAnim),
                             ANIMMODE_ONCE, -2.0f);
            BossGoma_PlayEffectsAndSfx(this, play, 0, 6);
            Actor_RequestQuakeAndRumble(&this->actor, play, 5, 10);
            BossGoma_SetupHopWave();
            BossGoma_SpawnShockwave(this, this->actor.world.pos.y);
            sZopHopsLeft--;
            sZopHopTimer = BOSSGOMA_HOP_PAUSE_FRAMES;
        }
        return;
    }

    Math_ApproachS(&this->actor.world.rot.y, this->actor.yawTowardsPlayer, 3, 0x1000);
    if (sZopHopTimer > 0) {
        sZopHopTimer--;
        return;
    }

    if (sZopHopsLeft <= 0) {
        BossGoma_SetupFloorMain(this);
        return;
    }

    // Leap to a spot close to Link (a bit around him, so he has to run off), landing after a fixed flight time
    {
        f32 angle = Rand_ZeroOne() * 2.0f * M_PI;
        f32 off = 40.0f + Rand_ZeroOne() * 90.0f;
        Vec3f spot;
        f32 dist;

        spot.x = player->actor.world.pos.x + sinf(angle) * off;
        spot.y = player->actor.world.pos.y;
        spot.z = player->actor.world.pos.z + cosf(angle) * off;
        dist = Math_Vec3f_DistXZ(&this->actor.world.pos, &spot);
        this->actor.world.rot.y = Math_Vec3f_Yaw(&this->actor.world.pos, &spot);
        this->actor.speedXZ = CLAMP_MAX(dist / BOSSGOMA_HOP_FLIGHT_FRAMES, 30.0f);
        this->actor.velocity.y = -this->actor.gravity * BOSSGOMA_HOP_FLIGHT_FRAMES * 0.5f;
        this->actor.bgCheckFlags &= ~BGCHECKFLAG_GROUND;
        sZopHopFloorY = this->actor.world.pos.y;
        sZopHopAirborne = true;
        Animation_Change(&this->skelanime, &gGohmaWalkAnim, 2.5f, 0.0f, Animation_GetLastFrame(&gGohmaWalkAnim),
                         ANIMMODE_LOOP, -4.0f);
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_HIGH);
    }
}

static s32 BossGoma_LidsFollowAnim(BossGoma* this) {
    return this->actionFunc == BossGoma_Encounter || this->actionFunc == BossGoma_Defeated;
}

const ActorInit Boss_Goma_InitVars = {
    ACTOR_BOSS_GOMA,
    ACTORCAT_BOSS,
    FLAGS,
    OBJECT_GOMA,
    sizeof(BossGoma),
    (ActorFunc)BossGoma_Init,
    (ActorFunc)BossGoma_Destroy,
    (ActorFunc)BossGoma_Update,
    (ActorFunc)BossGoma_Draw,
    NULL,
};

static ColliderJntSphElementInit sColliderJntSphElementInit[13] = {
    {
        {
            ELEMTYPE_UNK3,
            { 0xFFCFFFFF, 0x00, 0x08 },
            { 0xFFCFFFFF, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NORMAL,
            BUMP_ON,
            OCELEM_ON,
        },
        { BOSSGOMA_LIMB_EYE, { { 0, 0, 1200 }, 20 }, 100 },
    },
    {
        {
            ELEMTYPE_UNK2,
            { 0xFFCFFFFF, 0x00, 0x08 },
            { 0xFFCFFFFF, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NORMAL,
            BUMP_ON,
            OCELEM_ON,
        },
        { BOSSGOMA_LIMB_TAIL4, { { 0, 0, 0 }, 20 }, 100 },
    },
    {
        {
            ELEMTYPE_UNK2,
            { 0xFFCFFFFF, 0x00, 0x08 },
            { 0xFFCFFFFF, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NORMAL,
            BUMP_ON,
            OCELEM_ON,
        },
        { BOSSGOMA_LIMB_TAIL3, { { 0, 0, 0 }, 15 }, 100 },
    },
    {
        {
            ELEMTYPE_UNK2,
            { 0xFFCFFFFF, 0x00, 0x08 },
            { 0xFFCFFFFF, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NORMAL,
            BUMP_ON,
            OCELEM_ON,
        },
        { BOSSGOMA_LIMB_TAIL2, { { 0, 0, 0 }, 12 }, 100 },
    },
    {
        {
            ELEMTYPE_UNK2,
            { 0xFFCFFFFF, 0x00, 0x08 },
            { 0xFFCFFFFF, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NORMAL,
            BUMP_ON,
            OCELEM_ON,
        },
        { BOSSGOMA_LIMB_TAIL1, { { 0, 0, 0 }, 25 }, 100 },
    },
    {
        {
            ELEMTYPE_UNK2,
            { 0xFFCFFFFF, 0x00, 0x08 },
            { 0xFFCFFFFF, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NORMAL,
            BUMP_ON,
            OCELEM_ON,
        },
        { BOSSGOMA_LIMB_R_FEET, { { 0, 0, 0 }, 30 }, 100 },
    },
    {
        {
            ELEMTYPE_UNK2,
            { 0xFFCFFFFF, 0x00, 0x08 },
            { 0xFFCFFFFF, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NORMAL,
            BUMP_ON,
            OCELEM_ON,
        },
        { BOSSGOMA_LIMB_R_SHIN, { { 0, 0, 0 }, 15 }, 100 },
    },
    {
        {
            ELEMTYPE_UNK2,
            { 0xFFCFFFFF, 0x00, 0x08 },
            { 0xFFCFFFFF, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NORMAL,
            BUMP_ON,
            OCELEM_ON,
        },
        { BOSSGOMA_LIMB_R_THIGH_SHELL, { { 0, 0, 0 }, 15 }, 100 },
    },
    {
        {
            ELEMTYPE_UNK2,
            { 0xFFCFFFFF, 0x00, 0x08 },
            { 0xFFCFFFFF, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NORMAL,
            BUMP_ON,
            OCELEM_ON,
        },
        { BOSSGOMA_LIMB_L_ANTENNA_CLAW, { { 0, 0, 0 }, 20 }, 100 },
    },
    {
        {
            ELEMTYPE_UNK2,
            { 0xFFCFFFFF, 0x00, 0x08 },
            { 0xFFCFFFFF, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NORMAL,
            BUMP_ON,
            OCELEM_ON,
        },
        { BOSSGOMA_LIMB_R_ANTENNA_CLAW, { { 0, 0, 0 }, 20 }, 100 },
    },
    {
        {
            ELEMTYPE_UNK2,
            { 0xFFCFFFFF, 0x00, 0x08 },
            { 0xFFCFFFFF, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NORMAL,
            BUMP_ON,
            OCELEM_ON,
        },
        { BOSSGOMA_LIMB_L_FEET, { { 0, 0, 0 }, 30 }, 100 },
    },
    {
        {
            ELEMTYPE_UNK2,
            { 0xFFCFFFFF, 0x00, 0x08 },
            { 0xFFCFFFFF, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NORMAL,
            BUMP_ON,
            OCELEM_ON,
        },
        { BOSSGOMA_LIMB_L_SHIN, { { 0, 0, 0 }, 15 }, 100 },
    },
    {
        {
            ELEMTYPE_UNK2,
            { 0xFFCFFFFF, 0x00, 0x08 },
            { 0xFFCFFFFF, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NORMAL,
            BUMP_ON,
            OCELEM_ON,
        },
        { BOSSGOMA_LIMB_L_THIGH_SHELL, { { 0, 0, 0 }, 15 }, 100 },
    },
};

static ColliderJntSphInit sColliderJntSphInit = {
    {
        COLTYPE_HIT3,
        AT_ON | AT_TYPE_ENEMY,
        AC_ON | AC_TYPE_PLAYER,
        OC1_ON | OC1_TYPE_PLAYER,
        OC2_TYPE_1,
        COLSHAPE_JNTSPH,
    },
    13,
    sColliderJntSphElementInit,
};

static u8 sClearPixelTableFirstPass[16 * 16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00,
    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00
};

static u8 sClearPixelTableSecondPass[16 * 16] = {
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01
};

static u8 sClearPixelTex16[16 * 16] = { { 0 } };
static u8 sClearPixelTex32[32 * 32] = { { 0 } };

// indexed by limb (where the root limb is 1)
static u8 sDeadLimbLifetime[] = {
    0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    30, // tail end/last part
    40, // tail 2nd to last part
    0,  0, 0, 0, 0, 0, 0, 0,
    10, // back of right claw/hand
    15, // front of right claw/hand
    21, // part of right arm (inner)
    0,  0,
    25, // part of right arm (shell)
    0,  0,
    31, // part of right arm (shell on shoulder)
    35, // part of right arm (shoulder)
    0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    43, // end of left antenna
    48, // middle of left antenna
    53, // start of left antenna
    0,  0, 0, 0,
    42, // end of right antenna
    45, // middle of right antenna
    53, // start of right antenna
    0,  0, 0, 0, 0, 0,
    11, // back of left claw/hand
    15, // front of left claw/hand
    21, // part of left arm (inner)
    0,  0,
    25, // part of left arm (shell)
    0,  0,
    30, // part of left arm (shell on shoulder)
    35, // part of left arm (shoulder)
    0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

/**
 * Clear pixels from Gohma's textures
 */
void BossGoma_ClearPixels(u8* clearPixelTable, s16 i) {
    if (clearPixelTable[i]) {
        sClearPixelTex16[i] = 1;

        u8* targetPixel = sClearPixelTex32 + ((i & 0xF) * 2 + (i & 0xF0) * 4);
        // set the 2x2 block of pixels to 0
        targetPixel[0] = 1;
        targetPixel[1] = 1;
        targetPixel[32 + 0] = 1;
        targetPixel[32 + 1] = 1;
    }
}

static InitChainEntry sInitChain[] = {
    ICHAIN_U8(targetMode, 2, ICHAIN_CONTINUE),
    ICHAIN_S8(naviEnemyId, 0x01, ICHAIN_CONTINUE),
    ICHAIN_F32_DIV1000(gravity, -2000, ICHAIN_STOP),
};

void BossGoma_Init(Actor* thisx, PlayState* play) {
    s32 pad;
    BossGoma* this = (BossGoma*)thisx;

    Actor_ProcessInitChain(&this->actor, sInitChain);
    // ZeldaOOTProject: lock-on reaches much further (1000 instead of 280), also up to the ceiling
    if (ZOP_DifficultyEnabled()) {
        this->actor.targetMode = 5;
    }
    ActorShape_Init(&this->actor.shape, 4000.0f, ActorShadow_DrawCircle, 150.0f);
    SkelAnime_Init(play, &this->skelanime, &gGohmaSkel, &gGohmaIdleCrouchedAnim, NULL, NULL, 0);
    {
        Vec3s standPose[BOSSGOMA_LIMB_MAX];

        sZopLidBaseValid = false;
        if (this->skelanime.limbCount <= BOSSGOMA_LIMB_MAX) {
            SkelAnime_GetFrameData(&gGohmaStandAnim, 0, this->skelanime.limbCount, standPose);
            sZopLidBaseBottom = standPose[BOSSGOMA_LIMB_EYE_LID_BOTTOM_ROOT2].x;
            sZopLidBaseTop = standPose[BOSSGOMA_LIMB_EYE_LID_TOP_ROOT2].x;
            sZopLidBaseValid = true;
        }
    }
    Animation_PlayLoop(&this->skelanime, &gGohmaIdleCrouchedAnim);
    this->actor.shape.rot.x = -0x8000; // upside-down
    this->eyeIrisScaleX = 1.0f;
    this->eyeIrisScaleY = 1.0f;
    this->unusedInitX = this->actor.world.pos.x;
    this->unusedInitZ = this->actor.world.pos.z;
    this->actor.world.pos.y = -300.0f; // ceiling
    this->actor.gravity = 0.0f;
    BossGoma_SetupEncounter(this, play);
    this->actor.colChkInfo.health = 10;

    if (ZOP_TestPhase2Intro()) {
        // Testing aid: skip the encounter, Gohma starts on the floor and goes straight into her phase 2 intro
        this->actor.world.pos.y = -640.0f;
        this->actor.shape.rot.x = 0;
        this->actor.gravity = -2.0f;
        this->disableGameplayLogic = false;
        play->envCtx.unk_BF = 4;
        BossGoma_SetupPhase2Slam(this);
    }
    this->actor.colChkInfo.mass = MASS_IMMOVABLE;
    sZopPhase2Announced = false;
    sZopEyeCycleClosed = false;
    sZopEyeCycleTimer = 60;
    sZopEyeClosedFrames = 0;
    sZopEyeFlashTimer = 0;
    sZopEyeOpenFrames = 0;
    sZopEyeWasInCycle = false;
    sZopEyeCycleFromRoar = false;
    sZopEyeCycleOpenStart = 0;
    sZopBarrageCooldown = 0;
    sZopHopCooldown = 200;
    sZopSlamIntoBarrage = false;
    sZopWaveAlpha = 0;
    sZopWaveCanHit = false;
    sZopHitFlash = 0;
    // Undo a recolor left over from an earlier fight; the texture cache is flushed on the next draw
    ZOP_GohmaSetRecolor(0.0f);
    sZopRecolorDirty = true;
    Collider_InitJntSph(play, &this->collider);
    Collider_SetJntSph(play, &this->collider, &this->actor, &sColliderJntSphInit, this->colliderItems);

    if (Flags_GetClear(play, play->roomCtx.curRoom.num)) {
        Actor_Kill(&this->actor);
        if (GameInteractor_Should(VB_SPAWN_BLUE_WARP, true, this)) {
            Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_DOOR_WARP1, 0.0f, -640.0f, 0.0f, 0, 0, 0,
                               WARP_DUNGEON_CHILD);
        }
        if (GameInteractor_Should(VB_SPAWN_HEART_CONTAINER, true)) {
            Actor_Spawn(&play->actorCtx, play, ACTOR_ITEM_B_HEART, 141.0f, -640.0f, -84.0f, 0, 0, 0, 0);
        }
    }

    for (int i = 0; i < ARRAY_COUNT(sClearPixelTex16); i++) {
        sClearPixelTex16[i] = 0;
    }

    for (int i = 0; i < ARRAY_COUNT(sClearPixelTex32); i++) {
        sClearPixelTex32[i] = 0;
    }

    Gfx_RegisterBlendedTexture(gGohmaBodyTex, sClearPixelTex16, NULL);
    Gfx_RegisterBlendedTexture(gGohmaShellUndersideTex, sClearPixelTex16, NULL);
    Gfx_RegisterBlendedTexture(gGohmaDarkShellTex, sClearPixelTex16, NULL);
    Gfx_RegisterBlendedTexture(gGohmaEyeTex, sClearPixelTex16, NULL);
    Gfx_RegisterBlendedTexture(gGohmaShellTex, sClearPixelTex32, NULL);
    Gfx_RegisterBlendedTexture(gGohmaIrisTex, sClearPixelTex32, NULL);
}

void BossGoma_PlayEffectsAndSfx(BossGoma* this, PlayState* play, s16 arg2, s16 amountMinus1) {
    if (arg2 == 0 || arg2 == 1 || arg2 == 3) {
        Actor_SpawnFloorDustRing(play, &this->actor, &this->rightHandBackLimbWorldPos, 25.0f, amountMinus1, 8.0f, 500,
                                 10, true);
    }

    if (arg2 == 0 || arg2 == 2 || arg2 == 3) {
        Actor_SpawnFloorDustRing(play, &this->actor, &this->leftHandBackLimbWorldPos, 25.0f, amountMinus1, 8.0f, 500,
                                 10, true);
    }

    if (arg2 == 0) {
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_DOWN);
    } else {
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_WALK);
    }
}

void BossGoma_Destroy(Actor* thisx, PlayState* play) {
    BossGoma* this = (BossGoma*)thisx;

    SkelAnime_Free(&this->skelanime, play);
    Collider_DestroyJntSph(play, &this->collider);
    // The textures are shared resources: hand them back in their original colors
    ZOP_GohmaSetRecolor(0.0f);
}

/**
 * When Gohma is hit and its health drops to 0
 */
void BossGoma_SetupDefeated(BossGoma* this, PlayState* play) {
    Animation_Change(&this->skelanime, &gGohmaDeathAnim, 1.0f, 0.0f, Animation_GetLastFrame(&gGohmaDeathAnim),
                     ANIMMODE_ONCE, -2.0f);
    this->actionFunc = BossGoma_Defeated;
    this->disableGameplayLogic = true;
    this->decayingProgress = 0;
    this->noBackfaceCulling = false;
    this->framesUntilNextAction = 1200;
    this->actionState = 0;
    this->actor.flags &= ~(ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE);
    this->actor.speedXZ = 0.0f;
    this->actor.shape.shadowScale = 0.0f;
    Audio_QueueSeqCmd(0x1 << 28 | SEQ_PLAYER_BGM_MAIN << 24 | 0x100FF);
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_DEAD);
}

/**
 * Initial action setup, with Gohma waiting on the ceiling for the fight to start.
 */
void BossGoma_SetupEncounter(BossGoma* this, PlayState* play) {
    f32 lastFrame = Animation_GetLastFrame(&gGohmaWalkAnim);

    Animation_Change(&this->skelanime, &gGohmaWalkAnim, 1.0f, 0.0f, lastFrame, ANIMMODE_LOOP, -15.0f);
    this->actionFunc = BossGoma_Encounter;
    this->actionState = 0;
    this->disableGameplayLogic = true;
    play->envCtx.unk_BF = 4;
    play->envCtx.unk_D6 = 0xFF;
}

/**
 * On the floor and not doing anything for 20-30 frames, before going back to BossGoma_FloorMain
 */
void BossGoma_SetupFloorIdle(BossGoma* this) {
    f32 lastFrame = Animation_GetLastFrame(&gGohmaIdleCrouchedAnim);

    this->framesUntilNextAction = ZOP_AGGRO(Rand_S16Offset(20, 30), Rand_S16Offset(4, 8));
    Animation_Change(&this->skelanime, &gGohmaIdleCrouchedAnim, 1.0f, 0.0f, lastFrame, ANIMMODE_LOOP, -5.0f);
    this->actionFunc = BossGoma_FloorIdle;
}

/**
 * On the ceiling and not doing anything for 20-30 frames, leads to spawning children gohmas
 */
void BossGoma_SetupCeilingIdle(BossGoma* this) {
    this->framesUntilNextAction = ZOP_AGGRO(Rand_S16Offset(20, 30), Rand_S16Offset(8, 12));
    Animation_Change(&this->skelanime, &gGohmaHangAnim, 1.0f, 0.0f, Animation_GetLastFrame(&gGohmaHangAnim),
                     ANIMMODE_LOOP, -5.0f);
    this->actionFunc = BossGoma_CeilingIdle;
}

/**
 * When the player killed all children gohmas
 */
void BossGoma_SetupFallJump(BossGoma* this) {
    // When in Enemy Randomizer, reset the state of the spawned Gohma Larva because it's not done
    // by the (non-existent) Larva themselves.
    if (CVarGetInteger(CVAR_ENHANCEMENT("RandomizedEnemies"), 0)) {
        this->childrenGohmaState[0] = this->childrenGohmaState[1] = this->childrenGohmaState[2] = 0;
    }
    Animation_Change(&this->skelanime, &gGohmaLandAnim, 1.0f, 0.0f, 0.0f, ANIMMODE_ONCE, -5.0f);
    this->actionFunc = BossGoma_FallJump;
    this->actor.speedXZ = 0.0f;
    this->actor.velocity.y = 0.0f;
    this->actor.gravity = -2.0f;
}

/**
 * When the player successfully hits Gohma on the ceiling
 */
void BossGoma_SetupFallStruckDown(BossGoma* this) {
    Animation_Change(&this->skelanime, &gGohmaCrashAnim, 1.0f, 0.0f, 0.0f, ANIMMODE_ONCE, -5.0f);
    this->actionFunc = BossGoma_FallStruckDown;
    this->actor.speedXZ = 0.0f;
    this->actor.velocity.y = 0.0f;
    this->actor.gravity = -2.0f;
}

void BossGoma_SetupCeilingSpawnGohmas(BossGoma* this) {
    Animation_Change(&this->skelanime, &gGohmaLayEggsAnim, 1.0f, 0.0f, Animation_GetLastFrame(&gGohmaLayEggsAnim),
                     ANIMMODE_LOOP, -15.0f);
    this->actionFunc = BossGoma_CeilingSpawnGohmas;
    this->spawnGohmasActionTimer = 0;
}

void BossGoma_SetupCeilingPrepareSpawnGohmas(BossGoma* this) {
    Animation_Change(&this->skelanime, &gGohmaPrepareEggsAnim, 1.0f, 0.0f,
                     Animation_GetLastFrame(&gGohmaPrepareEggsAnim), ANIMMODE_LOOP, -10.0f);
    this->actionFunc = BossGoma_CeilingPrepareSpawnGohmas;
    this->framesUntilNextAction = BossGoma_InPhase2(this) ? BOSSGOMA_PHASE2_VOLLEY_FRAMES : ZOP_AGGRO(70, 40);
    sZopAttack = BossGoma_PickAttack();
}

void BossGoma_SetupWallClimb(BossGoma* this) {
    Animation_Change(&this->skelanime, &gGohmaClimbAnim, BOSSGOMA_P2_CLIMB(1.0f), 0.0f, Animation_GetLastFrame(&gGohmaClimbAnim),
                     ANIMMODE_LOOP, -10.0f);
    this->actionFunc = BossGoma_WallClimb;
    this->actor.speedXZ = 0.0f;
    this->actor.velocity.y = 0.0f;
    this->actor.gravity = 0.0f;
}

/**
 * Gohma either reached the ceiling after climbing a wall, or is waiting for the player to kill the (children) Gohmas.
 */
void BossGoma_SetupCeilingMoveToCenter(BossGoma* this) {
    Animation_Change(&this->skelanime, &gGohmaWalkAnim, BOSSGOMA_P2_SPEED(1.0f), 0.0f,
                     Animation_GetLastFrame(&gGohmaWalkAnim),
                     ANIMMODE_LOOP, -5.0f);
    this->actionFunc = BossGoma_CeilingMoveToCenter;
    this->actor.speedXZ = 0.0f;
    this->actor.velocity.y = 0.0f;
    this->actor.gravity = 0.0f;
    this->framesUntilNextAction = ZOP_AGGRO(Rand_S16Offset(30, 60), Rand_S16Offset(10, 20));
}

/**
 * Root action when on the floor, leads to attacking or climbing.
 */
void BossGoma_SetupFloorMain(BossGoma* this) {
    Animation_Change(&this->skelanime, &gGohmaWalkCrouchedAnim, BOSSGOMA_P2_SPEED(ZOP_AGGRO(1.0f, 1.6f)), 0.0f,
                     Animation_GetLastFrame(&gGohmaWalkCrouchedAnim), ANIMMODE_LOOP, -5.0f);
    this->actionFunc = BossGoma_FloorMain;
    this->framesUntilNextAction = Rand_S16Offset(70, 110);
}

/**
 * Gohma jumped to the floor on its own, after the player has killed its children Gohmas.
 */
void BossGoma_SetupFloorLand(BossGoma* this) {
    Animation_Change(&this->skelanime, &gGohmaLandAnim, 1.0f, 0.0f, Animation_GetLastFrame(&gGohmaLandAnim),
                     ANIMMODE_ONCE, -2.0f);
    this->actionFunc = BossGoma_FloorLand;
    this->currentAnimFrameCount = Animation_GetLastFrame(&gGohmaLandAnim);
}

/**
 * Gohma was shot by the player down from the ceiling.
 */
void BossGoma_SetupFloorLandStruckDown(BossGoma* this) {
    Animation_Change(&this->skelanime, &gGohmaCrashAnim, 1.0f, 0.0f, Animation_GetLastFrame(&gGohmaCrashAnim),
                     ANIMMODE_ONCE, -2.0f);
    this->currentAnimFrameCount = Animation_GetLastFrame(&gGohmaCrashAnim);
    this->actionFunc = BossGoma_FloorLandStruckDown;
    this->currentAnimFrameCount = Animation_GetLastFrame(&gGohmaCrashAnim);
}

/**
 * Gohma is vulnerable, from being struck down from the ceiling or on the ground.
 */
void BossGoma_SetupFloorStunned(BossGoma* this) {
    Animation_Change(&this->skelanime, &gGohmaStunnedAnim, 1.0f, 0.0f, Animation_GetLastFrame(&gGohmaStunnedAnim),
                     ANIMMODE_LOOP, -2.0f);
    this->actionFunc = BossGoma_FloorStunned;
}

/**
 * Take an attack posture, when the player is close enough.
 */
void BossGoma_SetupFloorAttackPosture(BossGoma* this) {
    Animation_Change(&this->skelanime, &gGohmaPrepareAttackAnim, BOSSGOMA_P2_SPEED(ZOP_AGGRO(1.0f, 1.5f)), 0.0f,
                     Animation_GetLastFrame(&gGohmaPrepareAttackAnim), ANIMMODE_ONCE, -10.0f);
    this->actionFunc = BossGoma_FloorAttackPosture;
}

/**
 * Leads to BossGoma_FloorAttack after 1 frame
 */
void BossGoma_SetupFloorPrepareAttack(BossGoma* this) {
    Animation_Change(&this->skelanime, &gGohmaStandAnim, 1.0f, 0.0f, Animation_GetLastFrame(&gGohmaStandAnim),
                     ANIMMODE_LOOP, -10.0f);
    this->actionFunc = BossGoma_FloorPrepareAttack;
    this->framesUntilNextAction = 0;
}

void BossGoma_SetupFloorAttack(BossGoma* this) {
    Animation_Change(&this->skelanime, &gGohmaAttackAnim, BOSSGOMA_P2_SPEED(ZOP_AGGRO(1.0f, 1.3f)), 0.0f,
                     Animation_GetLastFrame(&gGohmaAttackAnim),
                     ANIMMODE_ONCE, -10.0f);
    this->actionFunc = BossGoma_FloorAttack;
    this->actionState = 0;
    this->framesUntilNextAction = 0;
}

/**
 * Plays an animation for Gohma being hit (while stunned)
 * The setup and the action preserve timers apart from the patience one, notably `framesUntilNextAction` which is used
 * as the stun duration
 */
void BossGoma_SetupFloorDamaged(BossGoma* this) {
    Animation_Change(&this->skelanime, &gGohmaDamageAnim, 1.0f, 0.0f, Animation_GetLastFrame(&gGohmaDamageAnim),
                     ANIMMODE_ONCE, -2.0f);
    this->actionFunc = BossGoma_FloorDamaged;
}

void BossGoma_UpdateCeilingMovement(BossGoma* this, PlayState* play, f32 dz, f32 targetSpeedXZ,
                                    s16 rotateTowardsCenter) {
    static Vec3f velInit = { 0.0f, 0.0f, 0.0f };
    static Vec3f accelInit = { 0.0f, -0.5f, 0.0f };
    static Vec3f roomCenter = { -150.0f, 0.0f, -350.0f };
    Vec3f* basePos = NULL;
    s16 i;
    Vec3f vel;
    Vec3f accel;
    Vec3f pos;

    roomCenter.z += dz; // dz is always 0
    SkelAnime_Update(&this->skelanime);
    Math_ApproachF(&this->actor.speedXZ, BOSSGOMA_P2_SPEED(targetSpeedXZ), 0.5f, 2.0f);

    if (rotateTowardsCenter) {
        Math_ApproachS(&this->actor.world.rot.y, Math_Vec3f_Yaw(&this->actor.world.pos, &roomCenter) + 0x8000, 3,
                       0x3E8);
    }

    if (Animation_OnFrame(&this->skelanime, 9.0f)) {
        basePos = &this->rightHandBackLimbWorldPos;
    } else if (Animation_OnFrame(&this->skelanime, 1.0f)) {
        basePos = &this->leftHandBackLimbWorldPos;
    }

    if (basePos != NULL) {
        for (i = 0; i < 5; i++) {
            vel = velInit;
            accel = accelInit;
            pos.x = Rand_CenteredFloat(70.0f) + basePos->x;
            pos.y = Rand_ZeroFloat(30.0f) + basePos->y;
            pos.z = Rand_CenteredFloat(70.0f) + basePos->z;
            EffectSsHahen_Spawn(play, &pos, &vel, &accel, 0, (s16)(Rand_ZeroOne() * 5.0f) + 10, -1, 10, NULL);
        }
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_HIGH);
    }
}

void BossGoma_SetupEncounterState4(BossGoma* this, PlayState* play) {
    Player* player;
    Camera* camera;

    camera = Play_GetCamera(play, 0);
    player = GET_PLAYER(play);
    this->actionState = 4;
    this->actor.flags |= ACTOR_FLAG_ATTENTION_ENABLED;
    func_80064520(play, &play->csCtx);
    Player_SetCsActionWithHaltedActors(play, &this->actor, 1);
    this->subCameraId = Play_CreateSubCamera(play);
    Play_ChangeCameraStatus(play, 0, 3);
    Play_ChangeCameraStatus(play, this->subCameraId, 7);
    Animation_Change(&this->skelanime, &gGohmaEyeRollAnim, 1.0f, 0.0f, Animation_GetLastFrame(&gGohmaEyeRollAnim),
                     ANIMMODE_ONCE, 0.0f);
    this->currentAnimFrameCount = Animation_GetLastFrame(&gGohmaEyeRollAnim);

    // room center (todo: defines for hardcoded positions relative to room center)
    this->actor.world.pos.x = -150.0f;
    this->actor.world.pos.z = -350.0f;

    // room entrance, towards center
    player->actor.world.pos.x = 150.0f;
    player->actor.world.pos.z = 300.0f;

    player->actor.world.rot.y = player->actor.shape.rot.y = -0x705C;
    this->actor.world.rot.y = Actor_WorldYawTowardActor(&this->actor, &GET_PLAYER(play)->actor) + 0x8000;

    // room entrance, closer to room center
    this->subCameraEye.x = 90.0f;
    this->subCameraEye.z = 170.0f;
    this->subCameraEye.y = camera->eye.y + 20.0f;

    this->framesUntilNextAction = 50;

    this->subCameraAt.x = this->actor.world.pos.x;
    this->subCameraAt.y = this->actor.world.pos.y;
    this->subCameraAt.z = this->actor.world.pos.z;

    Audio_QueueSeqCmd(0x1 << 28 | SEQ_PLAYER_BGM_MAIN << 24 | 0x100FF);
}

/**
 * Spawns the door once the player entered
 * Wait for the player to look at Gohma on the ceiling
 * Handles the "meeting Gohma" cutscene, including boss card
 *
 * Skips the door and look-at-Gohma puzzle if the player already reached the boss card part before
 */
void BossGoma_Encounter(BossGoma* this, PlayState* play) {
    Camera* cam;
    Player* player = GET_PLAYER(play);
    s32 pad[2];

    Math_ApproachZeroF(&this->actor.speedXZ, 0.5f, 2.0f);

    switch (this->actionState) {
        case 0: // wait for the player to enter the room
            // entrance of the boss room
            if (fabsf(player->actor.world.pos.x - 150.0f) < 60.0f &&
                fabsf(player->actor.world.pos.z - 350.0f) < 60.0f) {
                if (Flags_GetEventChkInf(EVENTCHKINF_BEGAN_GOHMA_BATTLE)) {
                    BossGoma_SetupEncounterState4(this, play);
                    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_DOOR_SHUTTER, 164.72f, -480.0f,
                                       397.68002f, 0, -0x705C, 0, 0x180);
                } else {
                    Player_SetCsActionWithHaltedActors(play, &this->actor, 8);
                    this->actionState = 1;
                }
            }
            break;

        case 1: // player entered the room
            func_80064520(play, &play->csCtx);
            this->subCameraId = Play_CreateSubCamera(play);
            osSyncPrintf("MAKE CAMERA !!!   1   !!!!!!!!!!!!!!!!!!!!!!!!!!\n");
            Play_ChangeCameraStatus(play, 0, 1);
            Play_ChangeCameraStatus(play, this->subCameraId, 7);
            this->actionState = 2;
            // ceiling center
            this->actor.world.pos.x = -150.0f;
            this->actor.world.pos.y = -320.0f;
            this->actor.world.pos.z = -350.0f;
            // room entrance
            player->actor.world.pos.x = 150.0f;
            player->actor.world.pos.z = 300.0f;
            // near ceiling center
            this->subCameraEye.x = -350.0f;
            this->subCameraEye.y = -310.0f;
            this->subCameraEye.z = -350.0f;
            // below room entrance
            this->subCameraAt.x = player->actor.world.pos.x;
            this->subCameraAt.y = player->actor.world.pos.y - 200.0f + 25.0f;
            this->subCameraAt.z = player->actor.world.pos.z;
            this->framesUntilNextAction = 50;
            this->timer = 80;
            this->frameCount = 0;
            this->actor.flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
            // fall-through
        case 2: // zoom on player from room center
            // room entrance, towards center
            player->actor.shape.rot.y = -0x705C;
            player->actor.world.pos.x = 150.0f;
            player->actor.world.pos.z = 300.0f;
            player->actor.world.rot.y = player->actor.shape.rot.y;
            player->actor.speedXZ = 0.0f;

            if (this->framesUntilNextAction == 0) {
                // (-20, 25, -65) is towards room center
                Math_ApproachF(&this->subCameraEye.x, player->actor.world.pos.x - 20.0f, 0.049999997f,
                               this->subCameraFollowSpeed * 50.0f);
                Math_ApproachF(&this->subCameraEye.y, player->actor.world.pos.y + 25.0f, 0.099999994f,
                               this->subCameraFollowSpeed * 130.0f);
                Math_ApproachF(&this->subCameraEye.z, player->actor.world.pos.z - 65.0f, 0.049999997f,
                               this->subCameraFollowSpeed * 30.0f);
                Math_ApproachF(&this->subCameraFollowSpeed, 0.29999998f, 1.0f, 0.0050000004f);
                if (this->timer == 0) {
                    Math_ApproachF(&this->subCameraAt.y, player->actor.world.pos.y + 35.0f, 0.099999994f,
                                   this->subCameraFollowSpeed * 30.0f);
                }
                this->subCameraAt.x = player->actor.world.pos.x;
                this->subCameraAt.z = player->actor.world.pos.z;
            }

            Play_CameraSetAtEye(play, 0, &this->subCameraAt, &this->subCameraEye);

            if (this->frameCount == 176) {
                Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_DOOR_SHUTTER, 164.72f, -480.0f,
                                   397.68002f, 0, -0x705C, 0, SHUTTER_GOHMA_BLOCK << 6);
            }

            if (this->frameCount == 176) {
                play->envCtx.unk_BF = 3;
                play->envCtx.unk_D6 = 0xFFFF;
            }

            if (this->frameCount == 190) {
                Player_SetCsActionWithHaltedActors(play, &this->actor, 2);
            }

            if (this->frameCount >= 228) {
                cam = Play_GetCamera(play, 0);
                cam->eye = this->subCameraEye;
                cam->eyeNext = this->subCameraEye;
                cam->at = this->subCameraAt;
                func_800C08AC(play, this->subCameraId, 0);
                this->subCameraId = 0;
                func_80064534(play, &play->csCtx);
                Player_SetCsActionWithHaltedActors(play, &this->actor, 7);
                this->actionState = 3;
            }
            break;

        case 3: // wait for the player to look at Gohma
            if (fabsf(this->actor.projectedPos.x) < 150.0f && fabsf(this->actor.projectedPos.y) < 250.0f &&
                this->actor.projectedPos.z < 800.0f && this->actor.projectedPos.z > 0.0f) {
                this->lookedAtFrames++;
                Math_ApproachZeroF(&this->actor.speedXZ, 0.5f, 2.0f);
                Math_ApproachS(&this->actor.world.rot.y,
                               Actor_WorldYawTowardActor(&this->actor, &GET_PLAYER(play)->actor) + 0x8000, 2, 0xBB8);
                this->eyeLidBottomRotX = this->eyeLidTopRotX = this->eyeIrisRotX = this->eyeIrisRotY = 0;
            } else {
                this->lookedAtFrames = 0;
                BossGoma_UpdateCeilingMovement(this, play, 0.0f, -5.0f, true);
            }

            if (this->lookedAtFrames > 15) {
                BossGoma_SetupEncounterState4(this, play);
            }
            break;

        case 4: // focus Gohma on the ceiling
            if (Animation_OnFrame(&this->skelanime, 15.0f)) {
                Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_DEMO_EYE);
            }

            if (this->framesUntilNextAction <= 40) {
                // (22, -25, 45) is towards room entrance
                Math_ApproachF(&this->subCameraEye.x, this->actor.world.pos.x + 22.0f, 0.2f, 100.0f);
                Math_ApproachF(&this->subCameraEye.y, this->actor.world.pos.y - 25.0f, 0.2f, 100.0f);
                Math_ApproachF(&this->subCameraEye.z, this->actor.world.pos.z + 45.0f, 0.2f, 100.0f);
                Math_ApproachF(&this->subCameraAt.x, this->actor.world.pos.x, 0.2f, 100.0f);
                Math_ApproachF(&this->subCameraAt.y, this->actor.world.pos.y + 5.0f, 0.2f, 100.0f);
                Math_ApproachF(&this->subCameraAt.z, this->actor.world.pos.z, 0.2f, 100.0f);

                if (this->framesUntilNextAction == 30) {
                    play->envCtx.unk_BF = 4;
                }

                if (this->framesUntilNextAction < 20) {
                    SkelAnime_Update(&this->skelanime);
                    Math_ApproachF(&this->eyeIrisScaleX, 1.0f, 0.8f, 0.4f);
                    Math_ApproachF(&this->eyeIrisScaleY, 1.0f, 0.8f, 0.4f);

                    if (Animation_OnFrame(&this->skelanime, 36.0f)) {
                        this->eyeIrisScaleX = 1.8f;
                        this->eyeIrisScaleY = 1.8f;
                    }

                    if (Animation_OnFrame(&this->skelanime, this->currentAnimFrameCount)) {
                        this->actionState = 5;
                        Animation_Change(&this->skelanime, &gGohmaWalkAnim, 2.0f, 0.0f,
                                         Animation_GetLastFrame(&gGohmaWalkAnim), ANIMMODE_LOOP, -5.0f);
                        this->framesUntilNextAction = 30;
                        this->subCameraFollowSpeed = 0.0f;
                    }
                }
            }
            break;

        case 5: // running on the ceiling
            // (98, 0, 85) is towards room entrance
            Math_ApproachF(&this->subCameraEye.x, this->actor.world.pos.x + 8.0f + 90.0f, 0.1f,
                           this->subCameraFollowSpeed * 30.0f);
            Math_ApproachF(&this->subCameraEye.y, player->actor.world.pos.y, 0.1f, this->subCameraFollowSpeed * 30.0f);
            Math_ApproachF(&this->subCameraEye.z, this->actor.world.pos.z + 45.0f + 40.0f, 0.1f,
                           this->subCameraFollowSpeed * 30.0f);
            Math_ApproachF(&this->subCameraFollowSpeed, 1.0f, 1.0f, 0.05f);
            this->subCameraAt.x = this->actor.world.pos.x;
            this->subCameraAt.y = this->actor.world.pos.y;
            this->subCameraAt.z = this->actor.world.pos.z;

            if (this->framesUntilNextAction < 0) {
                //! @bug ? unreachable, timer is >= 0
                SkelAnime_Update(&this->skelanime);
                Math_ApproachZeroF(&this->actor.speedXZ, 1.0f, 2.0f);
            } else {
                BossGoma_UpdateCeilingMovement(this, play, 0.0f, -7.5f, false);
            }

            if (this->framesUntilNextAction == 0) {
                Animation_Change(&this->skelanime, &gGohmaHangAnim, 1.0f, 0.0f, Animation_GetLastFrame(&gGohmaHangAnim),
                                 ANIMMODE_LOOP, -5.0f);
            }

            if (this->framesUntilNextAction == 0) {
                this->actionState = 9;
                this->actor.speedXZ = 0.0f;
                this->actor.velocity.y = 0.0f;
                this->actor.gravity = -2.0f;
                Animation_Change(&this->skelanime, &gGohmaInitialLandingAnim, 1.0f, 0.0f,
                                 Animation_GetLastFrame(&gGohmaInitialLandingAnim), ANIMMODE_ONCE, -5.0f);
                player->actor.world.pos.x = 0.0f;
                player->actor.world.pos.z = -30.0f;
            }
            break;

        case 9: // falling from the ceiling
            Math_ApproachF(&this->subCameraEye.x, this->actor.world.pos.x + 8.0f + 90.0f, 0.1f,
                           this->subCameraFollowSpeed * 30.0f);
            Math_ApproachF(&this->subCameraEye.y, player->actor.world.pos.y + 10.0f, 0.1f,
                           this->subCameraFollowSpeed * 30.0f);
            Math_ApproachF(&this->subCameraEye.z, this->actor.world.pos.z + 45.0f + 40.0f, 0.1f,
                           this->subCameraFollowSpeed * 30.0f);
            this->subCameraAt.x = this->actor.world.pos.x;
            this->subCameraAt.y = this->actor.world.pos.y;
            this->subCameraAt.z = this->actor.world.pos.z;
            SkelAnime_Update(&this->skelanime);
            Math_ApproachS(&this->actor.shape.rot.x, 0, 2, 0xBB8);
            Math_ApproachS(&this->actor.world.rot.y, Actor_WorldYawTowardActor(&this->actor, &GET_PLAYER(play)->actor),
                           2, 0x7D0);

            if (this->actor.bgCheckFlags & BGCHECKFLAG_GROUND) {
                this->actionState = 130;
                this->actor.velocity.y = 0.0f;
                Animation_Change(&this->skelanime, &gGohmaInitialLandingAnim, 1.0f, 0.0f,
                                 Animation_GetLastFrame(&gGohmaInitialLandingAnim), ANIMMODE_ONCE, -2.0f);
                this->currentAnimFrameCount = Animation_GetLastFrame(&gGohmaInitialLandingAnim);
                BossGoma_PlayEffectsAndSfx(this, play, 0, 5);
                this->framesUntilNextAction = 15;
                func_800A9F6C(0.0f, 0xC8, 0x14, 0x14);
            }
            break;

        case 130: // focus Gohma on the ground
            Math_ApproachF(&this->subCameraEye.x, this->actor.world.pos.x + 8.0f + 90.0f, 0.1f,
                           this->subCameraFollowSpeed * 30.0f);
            Math_ApproachF(&this->subCameraEye.y, player->actor.world.pos.y + 10.0f, 0.1f,
                           this->subCameraFollowSpeed * 30.0f);
            Math_ApproachF(&this->subCameraEye.z, this->actor.world.pos.z + 45.0f + 40.0f, 0.1f,
                           this->subCameraFollowSpeed * 30.0f);
            Math_ApproachS(&this->actor.shape.rot.x, 0, 2, 0xBB8);
            Math_ApproachS(&this->actor.world.rot.y, Actor_WorldYawTowardActor(&this->actor, &GET_PLAYER(play)->actor),
                           2, 0x7D0);
            SkelAnime_Update(&this->skelanime);
            this->subCameraAt.x = this->actor.world.pos.x;
            this->subCameraAt.z = this->actor.world.pos.z;

            if (this->framesUntilNextAction != 0) {
                f32 s = sinf(this->framesUntilNextAction * 3.1415f * 0.5f);

                this->subCameraAt.y = this->framesUntilNextAction * s * 0.7f + this->actor.world.pos.y;
            } else {
                Math_ApproachF(&this->subCameraAt.y, this->actor.focus.pos.y, 0.1f, 10.0f);
            }

            if (Animation_OnFrame(&this->skelanime, 40.0f)) {
                Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_CRY1);

                if (!Flags_GetEventChkInf(EVENTCHKINF_BEGAN_GOHMA_BATTLE)) {
                    TitleCard_InitBossName(play, &play->actorCtx.titleCtx, SEGMENTED_TO_VIRTUAL(gGohmaTitleCardENGTex),
                                           160, 180, 128, 40, true);
                }

                Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_BOSS);
                Flags_SetEventChkInf(EVENTCHKINF_BEGAN_GOHMA_BATTLE);
            }

            if (Animation_OnFrame(&this->skelanime, this->currentAnimFrameCount)) {
                this->actionState = 140;
                Animation_Change(&this->skelanime, &gGohmaStandAnim, 1.0f, 0.0f,
                                 Animation_GetLastFrame(&gGohmaStandAnim), ANIMMODE_LOOP, -10.0f);
                this->framesUntilNextAction = 20;
            }
            break;

        case 140:
            SkelAnime_Update(&this->skelanime);
            Math_ApproachF(&this->subCameraAt.y, this->actor.focus.pos.y, 0.1f, 10.0f);

            if (this->framesUntilNextAction == 0) {
                this->framesUntilNextAction = 30;
                this->actionState = 150;
                Play_ChangeCameraStatus(play, 0, 3);
            }
            break;

        case 150:
            SkelAnime_Update(&this->skelanime);
            Math_SmoothStepToF(&this->subCameraEye.x, this->actor.world.pos.x + 150.0f, 0.2f, 100.0f, 0.1f);
            Math_SmoothStepToF(&this->subCameraEye.y, this->actor.world.pos.y + 20.0f, 0.2f, 100.0f, 0.1f);
            Math_SmoothStepToF(&this->subCameraEye.z, this->actor.world.pos.z + 220.0f, 0.2f, 100.0f, 0.1f);

            if (this->framesUntilNextAction == 0) {
                cam = Play_GetCamera(play, 0);
                cam->eye = this->subCameraEye;
                cam->eyeNext = this->subCameraEye;
                cam->at = this->subCameraAt;
                func_800C08AC(play, this->subCameraId, 0);
                this->subCameraId = 0;
                BossGoma_SetupFloorMain(this);
                this->disableGameplayLogic = false;
                this->patienceTimer = ZOP_AGGRO(200, 400);
                func_80064534(play, &play->csCtx);
                Player_SetCsActionWithHaltedActors(play, &this->actor, 7);

            }
            break;
    }

    if (this->subCameraId != 0) {
        Play_CameraSetAtEye(play, this->subCameraId, &this->subCameraAt, &this->subCameraEye);
    }
}

/**
 * Handles the "Gohma defeated" cutscene and effects
 * Spawns the heart container and blue warp actors
 */
void BossGoma_Defeated(BossGoma* this, PlayState* play) {
    static Vec3f roomCenter = { -150.0f, 0.0f, -350.0f };
    f32 dx;
    f32 dz;
    s16 j;
    Vec3f vel1 = { 0.0f, 0.0f, 0.0f };
    Vec3f accel1 = { 0.0f, 1.0f, 0.0f };
    Color_RGBA8 color1 = { 255, 255, 255, 255 };
    Color_RGBA8 color2 = { 0, 100, 255, 255 };
    Vec3f vel2 = { 0.0f, 0.0f, 0.0f };
    Vec3f accel2 = { 0.0f, -0.5f, 0.0f };
    Vec3f pos;
    Camera* camera;
    Player* player = GET_PLAYER(play);
    Vec3f childPos;
    s16 i;

    SkelAnime_Update(&this->skelanime);
    Math_ApproachS(&this->actor.shape.rot.x, 0, 2, 0xBB8);

    if (Animation_OnFrame(&this->skelanime, 107.0f)) {
        BossGoma_PlayEffectsAndSfx(this, play, 0, 8);
        func_800A9F6C(0.0f, 0x96, 0x14, 0x14);
    }

    this->visualState = VISUALSTATE_DEFEATED;
    this->eyeState = EYESTATE_IRIS_NO_FOLLOW_NO_IFRAMES;

    if (this->framesUntilNextAction == 1001) {
        for (i = 0; i < 90; i++) {
            if (sDeadLimbLifetime[i] != 0) {
                this->deadLimbsState[i] = 1;
            }
        }
    }

    if (this->framesUntilNextAction < 1200 && this->framesUntilNextAction > 1100 &&
        this->framesUntilNextAction % 8 == 0) {
        EffectSsSibuki_SpawnBurst(play, &this->actor.focus.pos);
    }

    if (this->framesUntilNextAction < 1080 && this->actionState < 3) {
        if (this->framesUntilNextAction < 1070) {
            Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_LAST - SFX_FLAG);
        }

        for (i = 0; i < 4; i++) {
            //! @bug this 0-indexes into this->defeatedLimbPositions which is initialized with
            // this->defeatedLimbPositions[limb], but limb is 1-indexed in skelanime callbacks, this means effects
            // should spawn at this->defeatedLimbPositions[0] too, which is uninitialized, so map origin?
            j = (s16)(Rand_ZeroOne() * (BOSSGOMA_LIMB_MAX - 1));
            if (this->defeatedLimbPositions[j].y < 10000.0f) {
                pos.x = Rand_CenteredFloat(20.0f) + this->defeatedLimbPositions[j].x;
                pos.y = Rand_CenteredFloat(10.0f) + this->defeatedLimbPositions[j].y;
                pos.z = Rand_CenteredFloat(20.0f) + this->defeatedLimbPositions[j].z;
                func_8002836C(play, &pos, &vel1, &accel1, &color1, &color2, 500, 10, 10);
            }
        }

        for (i = 0; i < 15; i++) {
            //! @bug same as above
            j = (s16)(Rand_ZeroOne() * (BOSSGOMA_LIMB_MAX - 1));
            if (this->defeatedLimbPositions[j].y < 10000.0f) {
                pos.x = Rand_CenteredFloat(20.0f) + this->defeatedLimbPositions[j].x;
                pos.y = Rand_CenteredFloat(10.0f) + this->defeatedLimbPositions[j].y;
                pos.z = Rand_CenteredFloat(20.0f) + this->defeatedLimbPositions[j].z;
                EffectSsHahen_Spawn(play, &pos, &vel2, &accel2, 0, (s16)(Rand_ZeroOne() * 5.0f) + 10, -1, 10, NULL);
            }
        }
    }

    switch (this->actionState) {
        case 0:
            this->actionState = 1;
            func_80064520(play, &play->csCtx);
            Player_SetCsActionWithHaltedActors(play, &this->actor, 1);
            this->subCameraId = Play_CreateSubCamera(play);
            Play_ChangeCameraStatus(play, 0, 3);
            Play_ChangeCameraStatus(play, this->subCameraId, 7);
            camera = Play_GetCamera(play, 0);
            this->subCameraEye.x = camera->eye.x;
            this->subCameraEye.y = camera->eye.y;
            this->subCameraEye.z = camera->eye.z;
            this->subCameraAt.x = camera->at.x;
            this->subCameraAt.y = camera->at.y;
            this->subCameraAt.z = camera->at.z;
            dx = this->subCameraEye.x - this->actor.world.pos.x;
            dz = this->subCameraEye.z - this->actor.world.pos.z;
            this->defeatedCameraEyeDist = sqrtf(SQ(dx) + SQ(dz));
            this->defeatedCameraEyeAngle = Math_FAtan2F(dx, dz);
            this->timer = 270;
            break;

        case 1:
            dx = Math_SinS(this->actor.shape.rot.y) * 100.0f;
            dz = Math_CosS(this->actor.shape.rot.y) * 100.0f;
            Math_ApproachF(&player->actor.world.pos.x, this->actor.world.pos.x + dx, 0.5f, 5.0f);
            Math_ApproachF(&player->actor.world.pos.z, this->actor.world.pos.z + dz, 0.5f, 5.0f);

            if (this->framesUntilNextAction < 1080) {
                this->noBackfaceCulling = true;

                for (i = 0; i < 4; i++) {
                    BossGoma_ClearPixels(sClearPixelTableFirstPass, this->decayingProgress);
                    //! @bug this allows this->decayingProgress = 0x100 = 256 which is out of bounds when accessing
                    // sClearPixelTableFirstPass, though timers may prevent this from ever happening?
                    if (this->decayingProgress < 0xFF) {
                        this->decayingProgress++;
                    }
                }
            }

            if (this->framesUntilNextAction < 1070 && this->frameCount % 4 == 0 && Rand_ZeroOne() < 0.5f) {
                this->blinkTimer = 3;
            }

            this->defeatedCameraEyeAngle += 0.022f;
            Math_ApproachF(&this->defeatedCameraEyeDist, 150.0f, 0.1f, 5.0f);
            dx = sinf(this->defeatedCameraEyeAngle);
            dx = dx * this->defeatedCameraEyeDist;
            dz = cosf(this->defeatedCameraEyeAngle);
            dz = dz * this->defeatedCameraEyeDist;
            Math_SmoothStepToF(&this->subCameraEye.x, this->actor.world.pos.x + dx, 0.2f, 50.0f, 0.1f);
            Math_SmoothStepToF(&this->subCameraEye.y, this->actor.world.pos.y + 20.0f, 0.2f, 50.0f, 0.1f);
            Math_SmoothStepToF(&this->subCameraEye.z, this->actor.world.pos.z + dz, 0.2f, 50.0f, 0.1f);
            Math_SmoothStepToF(&this->subCameraAt.x, this->firstTailLimbWorldPos.x, 0.2f, 50.0f, 0.1f);
            Math_SmoothStepToF(&this->subCameraAt.y, this->actor.focus.pos.y, 0.5f, 100.0f, 0.1f);
            Math_SmoothStepToF(&this->subCameraAt.z, this->firstTailLimbWorldPos.z, 0.2f, 50.0f, 0.1f);

            if (GameInteractor_Should(VB_GOHMA_DEATH_SCENE, this->timer == 80, this)) {
                Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_BOSS_CLEAR);
            }

            if (this->timer == 0) {
                this->actionState = 2;
                Play_ChangeCameraStatus(play, 0, 3);
                this->timer = 70;
                this->decayingProgress = 0;
                this->subCameraFollowSpeed = 0.0f;
                if (GameInteractor_Should(VB_SPAWN_HEART_CONTAINER, true)) {
                    Actor_Spawn(&play->actorCtx, play, ACTOR_ITEM_B_HEART, this->actor.world.pos.x,
                                this->actor.world.pos.y, this->actor.world.pos.z, 0, 0, 0, 0);
                }
            }
            break;

        case 2:
            camera = Play_GetCamera(play, 0);
            Math_SmoothStepToF(&this->subCameraEye.x, camera->eye.x, 0.2f, this->subCameraFollowSpeed * 50.0f, 0.1f);
            Math_SmoothStepToF(&this->subCameraEye.y, camera->eye.y, 0.2f, this->subCameraFollowSpeed * 50.0f, 0.1f);
            Math_SmoothStepToF(&this->subCameraEye.z, camera->eye.z, 0.2f, this->subCameraFollowSpeed * 50.0f, 0.1f);
            Math_SmoothStepToF(&this->subCameraAt.x, camera->at.x, 0.2f, this->subCameraFollowSpeed * 50.0f, 0.1f);
            Math_SmoothStepToF(&this->subCameraAt.y, camera->at.y, 0.2f, this->subCameraFollowSpeed * 50.0f, 0.1f);
            Math_SmoothStepToF(&this->subCameraAt.z, camera->at.z, 0.2f, this->subCameraFollowSpeed * 50.0f, 0.1f);
            Math_SmoothStepToF(&this->subCameraFollowSpeed, 1.0f, 1.0f, 0.02f, 0.0f);

            if (this->timer == 0) {
                childPos = roomCenter;
                this->timer = 30;
                this->actionState = 3;

                for (i = 0; i < 10000; i++) {
                    if ((fabsf(childPos.x - player->actor.world.pos.x) < 100.0f &&
                         fabsf(childPos.z - player->actor.world.pos.z) < 100.0f) ||
                        (fabsf(childPos.x - this->actor.world.pos.x) < 150.0f &&
                         fabsf(childPos.z - this->actor.world.pos.z) < 150.0f)) {
                        childPos.x = Rand_CenteredFloat(400.0f) + -150.0f;
                        childPos.z = Rand_CenteredFloat(400.0f) + -350.0f;
                    } else {
                        break;
                    }
                }

                if (GameInteractor_Should(VB_SPAWN_BLUE_WARP, true, this)) {
                    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_DOOR_WARP1, childPos.x,
                                       this->actor.world.pos.y, childPos.z, 0, 0, 0, WARP_DUNGEON_CHILD);
                }
                Flags_SetClear(play, play->roomCtx.curRoom.num);
            }

            for (i = 0; i < 4; i++) {
                BossGoma_ClearPixels(sClearPixelTableSecondPass, this->decayingProgress);
                //! @bug same as sClearPixelTableFirstPass
                if (this->decayingProgress < 0xFF) {
                    this->decayingProgress++;
                }
            }
            break;

        case 3:
            for (i = 0; i < 4; i++) {
                BossGoma_ClearPixels(sClearPixelTableSecondPass, this->decayingProgress);
                //! @bug same as sClearPixelTableFirstPass
                if (this->decayingProgress < 0xFF) {
                    this->decayingProgress++;
                }
            }

            if (GameInteractor_Should(VB_GOHMA_DEATH_SCENE, this->timer == 0, this)) {
                if (Math_SmoothStepToF(&this->actor.scale.y, 0, 1.0f, 0.00075f, 0.0f) <= 0.001f) {
                    camera = Play_GetCamera(play, 0);
                    camera->eye = this->subCameraEye;
                    camera->eyeNext = this->subCameraEye;
                    camera->at = this->subCameraAt;
                    func_800C08AC(play, this->subCameraId, 0);
                    this->subCameraId = 0;
                    func_80064534(play, &play->csCtx);
                    Player_SetCsActionWithHaltedActors(play, &this->actor, 7);
                    Actor_Kill(&this->actor);
                }

                this->actor.scale.x = this->actor.scale.z = this->actor.scale.y;
            }
            break;
    }

    if (this->subCameraId != 0) {
        Play_CameraSetAtEye(play, this->subCameraId, &this->subCameraAt, &this->subCameraEye);
    }

    if (this->blinkTimer != 0) {
        this->blinkTimer--;
        play->envCtx.adjAmbientColor[0] += 40;
        play->envCtx.adjAmbientColor[1] += 40;
        play->envCtx.adjAmbientColor[2] += 80;
        play->envCtx.adjFogColor[0] += 10;
        play->envCtx.adjFogColor[1] += 10;
        play->envCtx.adjFogColor[2] += 20;
    } else {
        play->envCtx.adjAmbientColor[0] -= 20;
        play->envCtx.adjAmbientColor[1] -= 20;
        play->envCtx.adjAmbientColor[2] -= 40;
        play->envCtx.adjFogColor[0] -= 5;
        play->envCtx.adjFogColor[1] -= 5;
        play->envCtx.adjFogColor[2] -= 10;
    }

    if (play->envCtx.adjAmbientColor[0] > 200) {
        play->envCtx.adjAmbientColor[0] = 200;
    }
    if (play->envCtx.adjAmbientColor[1] > 200) {
        play->envCtx.adjAmbientColor[1] = 200;
    }
    if (play->envCtx.adjAmbientColor[2] > 200) {
        play->envCtx.adjAmbientColor[2] = 200;
    }
    if (play->envCtx.adjFogColor[0] > 70) {
        play->envCtx.adjFogColor[0] = 70;
    }
    if (play->envCtx.adjFogColor[1] > 70) {
        play->envCtx.adjFogColor[1] = 70;
    }
    if (play->envCtx.adjFogColor[2] > 140) {
        play->envCtx.adjFogColor[2] = 140;
    }

    if (play->envCtx.adjAmbientColor[0] < 0) {
        play->envCtx.adjAmbientColor[0] = 0;
    }
    if (play->envCtx.adjAmbientColor[1] < 0) {
        play->envCtx.adjAmbientColor[1] = 0;
    }
    if (play->envCtx.adjAmbientColor[2] < 0) {
        play->envCtx.adjAmbientColor[2] = 0;
    }
    if (play->envCtx.adjFogColor[0] < 0) {
        play->envCtx.adjFogColor[0] = 0;
    }
    if (play->envCtx.adjFogColor[1] < 0) {
        play->envCtx.adjFogColor[1] = 0;
    }
    if (play->envCtx.adjFogColor[2] < 0) {
        play->envCtx.adjFogColor[2] = 0;
    }
}

/**
 * If the player backs off, cancel the attack, or attack.
 */
void BossGoma_FloorAttackPosture(BossGoma* this, PlayState* play) {
    if (BossGoma_TryBarrage(this)) {
        return;
    }

    SkelAnime_Update(&this->skelanime);
    Math_ApproachZeroF(&this->actor.speedXZ, 0.5f, 2.0f);

    if (this->skelanime.curFrame >= (19.0f + 1.0f / 3.0f) && this->skelanime.curFrame <= 30.0f) {
        Math_ApproachS(&this->actor.world.rot.y, Actor_WorldYawTowardActor(&this->actor, &GET_PLAYER(play)->actor), 3,
                       0xBB8);
    }

    if (Animation_OnFrame(&this->skelanime, Animation_GetLastFrame(&gGohmaPrepareAttackAnim))) {
        if (this->actor.xzDistToPlayer < ZOP_AGGRO(250.0f, 350.0f)) {
            BossGoma_SetupFloorPrepareAttack(this);
        } else {
            BossGoma_SetupFloorMain(this);
        }
    }

    this->eyeState = EYESTATE_IRIS_FOLLOW_NO_IFRAMES;
    this->visualState = VISUALSTATE_RED;
}

/**
 * Only lasts 1 frame. Plays a sound.
 */
void BossGoma_FloorPrepareAttack(BossGoma* this, PlayState* play) {
    SkelAnime_Update(&this->skelanime);

    if (this->framesUntilNextAction == 0) {
        BossGoma_SetupFloorAttack(this);
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_CRY1);
    }

    this->eyeState = EYESTATE_IRIS_FOLLOW_NO_IFRAMES;
    this->visualState = VISUALSTATE_RED;
}

/**
 * Gohma attacks, then the action eventually goes back to BossGoma_FloorMain
 */
void BossGoma_FloorAttack(BossGoma* this, PlayState* play) {
    s16 i;

    this->actor.flags |= ACTOR_FLAG_SFX_FOR_PLAYER_BODY_HIT;
    SkelAnime_Update(&this->skelanime);

    switch (this->actionState) {
        case 0:
            for (i = 0; i < this->collider.count; i++) {
                if (this->collider.elements[i].info.toucherFlags & 2) {
                    this->framesUntilNextAction = 10;
                    break;
                }
            }

            if (Animation_OnFrame(&this->skelanime, 10.0f)) {
                BossGoma_PlayEffectsAndSfx(this, play, 3, 5);
                Actor_RequestQuakeAndRumble(&this->actor, play, 5, 15);
            }

            if (Animation_OnFrame(&this->skelanime, Animation_GetLastFrame(&gGohmaAttackAnim))) {
                this->actionState = 1;
                Animation_Change(&this->skelanime, &gGohmaRestAfterAttackAnim, 1.0f, 0.0f,
                                 Animation_GetLastFrame(&gGohmaRestAfterAttackAnim), ANIMMODE_LOOP, -1.0f);

                if (this->framesUntilNextAction == 0) {
                    this->timer = ZOP_AGGRO((s16)(Rand_ZeroOne() * 30.0f) + 30, (s16)(Rand_ZeroOne() * 10.0f) + 8);
                }
            }
            break;

        case 1:
            if (Animation_OnFrame(&this->skelanime, 3.0f)) {
                Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_UNARI2);
            }

            if (this->timer == 0) {
                this->actionState = 2;
                Animation_Change(&this->skelanime, &gGohmaRecoverAfterAttackAnim, 1.0f, 0.0f,
                                 Animation_GetLastFrame(&gGohmaRecoverAfterAttackAnim), ANIMMODE_ONCE, -5.0f);
            }
            break;

        case 2:
            if (Animation_OnFrame(&this->skelanime, Animation_GetLastFrame(&gGohmaRecoverAfterAttackAnim))) {
                BossGoma_SetupFloorIdle(this);
            }
            break;
    }

    this->eyeState = EYESTATE_IRIS_FOLLOW_NO_IFRAMES;
    this->visualState = VISUALSTATE_RED;
}

/**
 * Plays the animation to its end, then goes back to BossGoma_FloorStunned
 */
void BossGoma_FloorDamaged(BossGoma* this, PlayState* play) {
    SkelAnime_Update(&this->skelanime);

    if (Animation_OnFrame(&this->skelanime, Animation_GetLastFrame(&gGohmaDamageAnim))) {
        BossGoma_SetupFloorStunned(this);
        this->patienceTimer = 0;
    }

    this->eyeState = EYESTATE_IRIS_NO_FOLLOW_NO_IFRAMES;
    Math_ApproachF(&this->eyeIrisScaleX, 0.4f, 0.5f, 0.2f);
    this->visualState = VISUALSTATE_HIT;
}

/**
 * Gohma is back on the floor after the player struck it down from the ceiling.
 * Sets patience to 0
 * Gohma is then stunned (BossGoma_FloorStunned)
 */
void BossGoma_FloorLandStruckDown(BossGoma* this, PlayState* play) {
    SkelAnime_Update(&this->skelanime);

    if (Animation_OnFrame(&this->skelanime, this->currentAnimFrameCount)) {
        BossGoma_SetupFloorStunned(this);
        this->sfxFaintTimer = 92;
        this->patienceTimer = 0;
        this->framesUntilNextAction = ZOP_HARD(150, 100);
    }

    Actor_SpawnFloorDustRing(play, &this->actor, &this->actor.world.pos, 55.0f, 4, 8.0f, 500, 10, true);
}

/**
 * Gohma is back on the floor after the player has killed its children Gohmas.
 * Plays an animation then goes to usual floor behavior, with refilled patience.
 */
void BossGoma_FloorLand(BossGoma* this, PlayState* play) {
    SkelAnime_Update(&this->skelanime);

    if (Animation_OnFrame(&this->skelanime, this->currentAnimFrameCount)) {
        BossGoma_SetupFloorIdle(this);
        this->patienceTimer = ZOP_AGGRO(200, 400);
    }
}

/**
 * Gohma is stunned and vulnerable. It can only be damaged during this action.
 */
void BossGoma_FloorStunned(BossGoma* this, PlayState* play) {
    if (this->sfxFaintTimer <= 90) {
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_FAINT - 0x800);
    }
    SkelAnime_Update(&this->skelanime);

    if (this->timer == 1) {
        Actor_SpawnFloorDustRing(play, &this->actor, &this->actor.world.pos, 55.0f, 4, 8.0f, 500, 10, true);
    }

    Math_ApproachZeroF(&this->actor.speedXZ, 0.5f, 1.0f);

    if (this->framesUntilNextAction == 0) {
        BossGoma_SetupFloorMain(this);
        if (BossGoma_InPhase2(this) && Rand_ZeroOne() < 0.5f) {
            // Phase 2: half the time she keeps fighting on the floor instead of retreating to the ceiling
            this->patienceTimer = 160;
        }
        if (this->patienceTimer == 0 && this->actor.xzDistToPlayer < 130.0f) {
            this->timer = 20;
        }
    }

    Math_ApproachS(&this->actor.shape.rot.x, 0, 2, 0xBB8);
    this->eyeState = EYESTATE_IRIS_NO_FOLLOW_NO_IFRAMES;
    Math_ApproachF(&this->eyeIrisScaleX, 0.4f, 0.5f, 0.2f);
    this->visualState = VISUALSTATE_STUNNED;
}

/**
 * Gohma goes back to the floor after the player killed the three gohmas it spawned
 */
void BossGoma_FallJump(BossGoma* this, PlayState* play) {
    SkelAnime_Update(&this->skelanime);
    Math_ApproachS(&this->actor.shape.rot.x, 0, 2, 0xBB8);
    Math_ApproachS(&this->actor.world.rot.y, Actor_WorldYawTowardActor(&this->actor, &GET_PLAYER(play)->actor), 2,
                   0x7D0);

    if (this->actor.bgCheckFlags & BGCHECKFLAG_GROUND) {
        BossGoma_SetupFloorLand(this);
        this->actor.velocity.y = 0.0f;
        BossGoma_PlayEffectsAndSfx(this, play, 0, 8);
        Actor_RequestQuakeAndRumble(&this->actor, play, 5, 0xF);
        if (ZOP_DifficultyEnabled()) {
            BossGoma_SetupLandingWave();
            BossGoma_SpawnShockwave(this, this->actor.world.pos.y);
        }
    }
}

/**
 * Gohma falls to the floor after the player hit it
 */
void BossGoma_FallStruckDown(BossGoma* this, PlayState* play) {
    SkelAnime_Update(&this->skelanime);
    Math_ApproachS(&this->actor.shape.rot.x, 0, 2, 0xBB8);
    Math_ApproachS(&this->actor.world.rot.y, Actor_WorldYawTowardActor(&this->actor, &GET_PLAYER(play)->actor), 3,
                   0x7D0);

    if (this->actor.bgCheckFlags & BGCHECKFLAG_GROUND) {
        BossGoma_SetupFloorLandStruckDown(this);
        this->actor.velocity.y = 0.0f;
        BossGoma_PlayEffectsAndSfx(this, play, 0, 8);
        Actor_RequestQuakeAndRumble(&this->actor, play, 0xA, 0xF);
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_DAM1);
    }
}

/**
 * Spawn three gohmas, one after the other. Cannot be interrupted
 */
void BossGoma_CeilingSpawnGohmas(BossGoma* this, PlayState* play) {
    s16 i;

    SkelAnime_Update(&this->skelanime);

    if (this->frameCount % 16 == 0) {
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_UNARI);
    }

    Math_ApproachZeroF(&this->actor.speedXZ, 0.5f, 2.0f);
    this->spawnGohmasActionTimer++;

    switch (this->spawnGohmasActionTimer) {
        case 24:
            // BOSSGOMA_LIMB_TAIL1, the tail limb closest to the body
            this->tailLimbsScaleTimers[3] = 10;
            break;
        case 32:
            // BOSSGOMA_LIMB_TAIL2
            this->tailLimbsScaleTimers[2] = 10;
            break;
        case 40:
            // BOSSGOMA_LIMB_TAIL3
            this->tailLimbsScaleTimers[1] = 10;
            break;
        case 48:
            // BOSSGOMA_LIMB_TAIL4, the furthest from the body
            this->tailLimbsScaleTimers[0] = 10;
            break;
    }

    if (this->tailLimbsScaleTimers[0] == 2) {
        for (i = 0; i < ARRAY_COUNT(this->childrenGohmaState); i++) {
            if (this->childrenGohmaState[i] == 0) {
                BossGoma_SpawnChildGohma(this, play, i);
                break;
            }
        }

        if (this->childrenGohmaState[0] == 0 || this->childrenGohmaState[1] == 0 || this->childrenGohmaState[2] == 0) {
            this->spawnGohmasActionTimer = 23;
        }
    }

    if (this->spawnGohmasActionTimer >= 64) {
        BossGoma_SetupCeilingIdle(this);
    }

    this->eyeState = EYESTATE_IRIS_NO_FOLLOW_NO_IFRAMES;
}

/**
 * Prepare to spawn children gohmas, red eye for 70 frames
 * During this time, the player can interrupt by hitting Gohma and make it fall from the ceiling
 */
void BossGoma_CeilingPrepareSpawnGohmas(BossGoma* this, PlayState* play) {
    SkelAnime_Update(&this->skelanime);

    if (BossGoma_InPhase2(this)) {
        // Brooding pose, but instead of eggs three volleys of pearl arrows, a tracking laser or both; then drop back
        // to the floor. The eye stays shut meanwhile (see BossGoma_UpdateEye).
        if ((sZopAttack & BOSSGOMA_ATTACK_LASER) && this->framesUntilNextAction == 100) {
            ZOP_SpawnGohmaLaser(play, &this->actor);
            Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_CRY1);
        }
        if ((sZopAttack & BOSSGOMA_ATTACK_VOLLEYS) &&
            (this->framesUntilNextAction == 90 || this->framesUntilNextAction == 62 ||
             this->framesUntilNextAction == 34)) {
            ZOP_SpawnPearlVolley(play, &this->actor, 5);
            Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_CRY1);
        }
        if (this->framesUntilNextAction == 0) {
            BossGoma_SetupFallJump(this);
        }
        this->eyeState = EYESTATE_IRIS_NO_FOLLOW_NO_IFRAMES;
        this->visualState = VISUALSTATE_RED;
        return;
    }

    if (this->framesUntilNextAction == 0) {
        BossGoma_SetupCeilingSpawnGohmas(this);
    }

    this->eyeState = EYESTATE_IRIS_NO_FOLLOW_NO_IFRAMES;
    this->visualState = VISUALSTATE_RED;
}

/**
 * On the floor, not doing anything special.
 */
void BossGoma_FloorIdle(BossGoma* this, PlayState* play) {
    if (BossGoma_TryBarrage(this)) {
        return;
    }

    SkelAnime_Update(&this->skelanime);
    Math_ApproachZeroF(&this->actor.speedXZ, 0.5f, 2.0f);
    Math_ApproachS(&this->actor.shape.rot.x, 0, 2, 0xBB8);

    if (this->framesUntilNextAction == 0) {
        BossGoma_SetupFloorMain(this);
    }
}

/**
 * On the ceiling, not doing anything special.
 * Eventually spawns children gohmas, jumping down to the floor when they are killed, or staying on the ceiling as long
 * as any is still alive.
 */
void BossGoma_CeilingIdle(BossGoma* this, PlayState* play) {
    s16 i;

    SkelAnime_Update(&this->skelanime);
    Math_ApproachZeroF(&this->actor.speedXZ, 0.5f, 2.0f);

    if (this->framesUntilNextAction == 0) {
        Actor* nearbyEnTest = NULL;
        if (CVarGetInteger(CVAR_ENHANCEMENT("RandomizedEnemies"), 0)) {
            nearbyEnTest = Actor_FindNearby(play, &this->actor, -1, ACTORCAT_ENEMY, 8000.0f);
        }
        if (this->childrenGohmaState[0] == 0 && this->childrenGohmaState[1] == 0 && this->childrenGohmaState[2] == 0) {
            // if no child gohma has been spawned
            BossGoma_SetupCeilingPrepareSpawnGohmas(this);
        } else if ((this->childrenGohmaState[0] < 0 && this->childrenGohmaState[1] < 0 &&
                    this->childrenGohmaState[2] < 0) ||
                   (nearbyEnTest == NULL && CVarGetInteger(CVAR_ENHANCEMENT("RandomizedEnemies"), 0))) {
            // In authentic gameplay, check if all baby Ghomas are dead. In Enemy Randomizer, check if there's no
            // enemies alive.
            BossGoma_SetupFallJump(this);
        } else {
            for (i = 0; i < ARRAY_COUNT(this->childrenGohmaState); i++) {
                if (this->childrenGohmaState[i] == 0) {
                    // if any child gohma hasn't been spawned
                    // this seems unreachable since BossGoma_CeilingSpawnGohmas spawns all three and can't be
                    // interrupted
                    BossGoma_SetupCeilingSpawnGohmas(this);
                    return;
                }
            }
            // if all children gohmas have been spawned
            BossGoma_SetupCeilingMoveToCenter(this);
        }
    }
}

/**
 * Gohma approaches the player as long as it has patience (see patienceTimer), then moves away from the player
 * Gohma climbs any wall it collides with
 * Uses the "walk cautiously" animation
 */
void BossGoma_FloorMain(BossGoma* this, PlayState* play) {
    s16 rot;

    SkelAnime_Update(&this->skelanime);

    if (Animation_OnFrame(&this->skelanime, 1.0f)) {
        this->doNotMoveThisFrame = true;
    } else if (Animation_OnFrame(&this->skelanime, 30.0f)) {
        this->doNotMoveThisFrame = true;
    } else if (Animation_OnFrame(&this->skelanime, 15.0f)) {
        this->doNotMoveThisFrame = true;
    } else if (Animation_OnFrame(&this->skelanime, 16.0f)) {
        this->doNotMoveThisFrame = true;
    }

    // Hop attack in phase 1 too (phase 2 tries it below, after the barrage)
    if (!BossGoma_InPhase2(this) && BossGoma_TryHop(this, play)) {
        return;
    }

    // Phase 2: mixes it up between climbing to the ceiling (patience runs out) and shooting from the floor
    if (BossGoma_InPhase2(this)) {
        if (this->patienceTimer > 220) {
            this->patienceTimer = 220;
        }
        if (BossGoma_TryBarrage(this)) {
            return;
        }
        if (BossGoma_TryHop(this, play)) {
            return;
        }
        if (sZopFloorShootCooldown > 0) {
            sZopFloorShootCooldown--;
        } else if (this->patienceTimer > 20 && this->actor.xzDistToPlayer > 160.0f &&
                   (this->actor.bgCheckFlags & BGCHECKFLAG_GROUND)) {
            BossGoma_SetupPhase2FloorShoot(this);
            return;
        }
    }

    if (Animation_OnFrame(&this->skelanime, 15.0f)) {
        BossGoma_PlayEffectsAndSfx(this, play, 1, 3);
    } else if (Animation_OnFrame(&this->skelanime, 30.0f)) {
        BossGoma_PlayEffectsAndSfx(this, play, 2, 3);
    }

    if (this->frameCount % 64 == 0) {
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_CRY2);
    }

    if (!this->doNotMoveThisFrame) {
        rot = Actor_WorldYawTowardActor(&this->actor, &GET_PLAYER(play)->actor);

        if (this->patienceTimer != 0) {
            this->patienceTimer--;

            if (this->actor.xzDistToPlayer < ZOP_AGGRO(150.0f, 210.0f)) {
                BossGoma_SetupFloorAttackPosture(this);
            }

            Math_ApproachF(&this->actor.speedXZ, BOSSGOMA_P2_SPEED(ZOP_AGGRO(10.0f / 3.0f, 6.5f)), 0.5f, 2.0f);
            Math_ApproachS(&this->actor.world.rot.y, rot, 5, 0x3E8);
        } else {
            if (this->timer != 0) {
                // move away from the player, walking backwards
                Math_ApproachF(&this->actor.speedXZ, BOSSGOMA_P2_CLIMB(-10.0f), 0.5f, BOSSGOMA_P2_CLIMB(2.0f));
                this->skelanime.playSpeed = BOSSGOMA_P2_CLIMB(-3.0f);
                if (this->timer == 1) {
                    this->actor.speedXZ = 0.0f;
                }
            } else {
                // move away from the player, walking forwards
                Math_ApproachF(&this->actor.speedXZ, BOSSGOMA_P2_CLIMB(20.0f / 3.0f), 0.5f, BOSSGOMA_P2_CLIMB(2.0f));
                this->skelanime.playSpeed = BOSSGOMA_P2_CLIMB(2.0f);
                rot += 0x8000;
            }

            Math_ApproachS(&this->actor.world.rot.y, rot, 3, (s16)BOSSGOMA_P2_CLIMB(0x9C4));
        }
    }

    if (this->actor.bgCheckFlags & BGCHECKFLAG_GROUND) {
        this->actor.velocity.y = 0.0f;
    }

    if (this->actor.bgCheckFlags & BGCHECKFLAG_WALL) {
        BossGoma_SetupWallClimb(this);
    }

    if (this->framesUntilNextAction == 0 && this->patienceTimer != 0) {
        BossGoma_SetupFloorIdle(this);
    }
}

/**
 * Gohma moves up until it reaches the ceiling
 */
void BossGoma_WallClimb(BossGoma* this, PlayState* play) {
    SkelAnime_Update(&this->skelanime);

    if (this->frameCount % 8 == 0) {
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_CLIM);
    }

    Math_ApproachF(&this->actor.velocity.y, BOSSGOMA_P2_CLIMB(5.0f), 0.5f, BOSSGOMA_P2_CLIMB(2.0f));
    Math_ApproachS(&this->actor.shape.rot.x, -0x4000, 2, (s16)BOSSGOMA_P2_CLIMB(0x7D0));
    Math_ApproachS(&this->actor.world.rot.y, this->actor.wallYaw + 0x8000, 2, 0x5DC);

    // -320 is a bit below boss room ceiling
    if (this->actor.world.pos.y > -320.0f) {
        BossGoma_SetupCeilingMoveToCenter(this);
        // allow new spawns
        this->childrenGohmaState[0] = this->childrenGohmaState[1] = this->childrenGohmaState[2] = 0;
    }
}

/**
 * Goes to BossGoma_CeilingIdle after enough time and after being close enough to the center of the ceiling.
 */
void BossGoma_CeilingMoveToCenter(BossGoma* this, PlayState* play) {
    s16 angle;
    s16 absDiff;

    BossGoma_UpdateCeilingMovement(this, play, 0.0f, -5.0f, true);

    if (this->frameCount % 64 == 0) {
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_CRY2);
    }

    Math_ApproachS(&this->actor.shape.rot.x, -0x8000, 3, 0x3E8);

    // avoid walking into a wall?
    if (this->actor.bgCheckFlags & BGCHECKFLAG_WALL) {
        angle = this->actor.shape.rot.y + 0x8000;

        if (angle < this->actor.wallYaw) {
            absDiff = this->actor.wallYaw - angle;
            angle = angle + absDiff / 2;
        } else {
            absDiff = angle - this->actor.wallYaw;
            angle = this->actor.wallYaw + absDiff / 2;
        }

        this->actor.world.pos.z += Math_CosS(angle) * (5.0f + Rand_ZeroOne() * 5.0f) + Rand_CenteredFloat(2.0f);
        this->actor.world.pos.x += Math_SinS(angle) * (5.0f + Rand_ZeroOne() * 5.0f) + Rand_CenteredFloat(2.0f);
    }

    // timer setup to 30-60
    if (this->framesUntilNextAction == 0 && fabsf(-150.0f - this->actor.world.pos.x) < 100.0f &&
        fabsf(-350.0f - this->actor.world.pos.z) < 100.0f) {
        BossGoma_SetupCeilingIdle(this);
    }
}

/**
 * Update eye-related properties
 *  - open/close (eye lid rotation)
 *  - look at the player (iris rotation)
 *  - iris scale, when menacing or damaged
 */
void BossGoma_UpdateEye(BossGoma* this, PlayState* play) {
    s16 targetEyeIrisRotX;
    s16 targetEyeIrisRotY;

    if (!this->disableGameplayLogic) {
        Player* player = GET_PLAYER(play);

        if (this->eyeState == EYESTATE_IRIS_FOLLOW_BONUS_IFRAMES) {
            // player + 0xA73 seems to be related to "throwing something"
            if (player->unk_A73 != 0) {
                player->unk_A73 = 0;
                this->eyeClosedTimer = 12;
            }

            if (this->frameCount % 16 == 0 && Rand_ZeroOne() < 0.3f) {
                this->eyeClosedTimer = 7;
            }
        }

        if (this->childrenGohmaState[0] > 0 || this->childrenGohmaState[1] > 0 || this->childrenGohmaState[2] > 0) {
            this->eyeClosedTimer = 7;
        }

        // Phase 2: eye shut on the way up and while shooting from the ceiling, so she can't simply be shot down
        if (BossGoma_InPhase2(this) &&
            (this->actionFunc == BossGoma_WallClimb || this->actionFunc == BossGoma_CeilingMoveToCenter ||
             this->actionFunc == BossGoma_CeilingIdle || this->actionFunc == BossGoma_CeilingPrepareSpawnGohmas ||
             this->actionFunc == BossGoma_FallJump)) {
            this->eyeClosedTimer = 7;
        }

        // Phase 2 on the floor: the eye alternates between briefly open and shut (about 2.5-4 s). Not while
        // she lies stunned, so a successful stun can always be followed up.
        s32 inCycle =
            BossGoma_InPhase2(this) && sZopPhase2Announced &&
            (this->actionFunc == BossGoma_FloorMain || this->actionFunc == BossGoma_FloorIdle ||
             this->actionFunc == BossGoma_FloorAttackPosture || this->actionFunc == BossGoma_FloorPrepareAttack ||
             this->actionFunc == BossGoma_FloorAttack || this->actionFunc == BossGoma_FloorLand ||
             this->actionFunc == BossGoma_Phase2FloorShoot || this->actionFunc == BossGoma_Phase2Barrage ||
             this->actionFunc == BossGoma_Hop);

        if (inCycle && !sZopEyeWasInCycle) {
            if (sZopEyeCycleOpenStart > 0) {
                // Rest of the reward window opened at the barrage slam: the eye stays open a little longer
                sZopEyeCycleClosed = false;
                sZopEyeCycleTimer = sZopEyeCycleOpenStart;
                sZopEyeCycleOpenStart = 0;
            } else if (sZopEyeCycleFromRoar) {
                sZopEyeCycleClosed = false;
                sZopEyeCycleTimer = 24;
            } else {
                sZopEyeCycleClosed = true;
                sZopEyeCycleTimer = Rand_S16Offset(50, 30);
            }
            sZopEyeCycleFromRoar = false;
        }
        sZopEyeWasInCycle = inCycle;

        if (inCycle) {
            if (sZopEyeCycleTimer > 0) {
                sZopEyeCycleTimer--;
            } else {
                sZopEyeCycleClosed = !sZopEyeCycleClosed;
                // Open only briefly (about 1.2-1.8 s): a short window for the slingshot or a Deku Nut
                sZopEyeCycleTimer = sZopEyeCycleClosed ? Rand_S16Offset(50, 30) : Rand_S16Offset(24, 12);
            }
            if (sZopEyeCycleClosed) {
                this->eyeClosedTimer = 7;
            }
        }

        // Barrage warning: three quick flashes of the open eye (open on frames 4-7, 12-15, 20-23)
        if (this->actionFunc == BossGoma_Phase2Signal) {
            s16 elapsed = BOSSGOMA_SIGNAL_FRAMES - this->framesUntilNextAction;

            this->eyeClosedTimer = (elapsed >= 4 && elapsed < 24 && (elapsed % 8) >= 4) ? 0 : 2;
        }

        if (this->eyeClosedTimer != 0) {
            this->eyeClosedTimer--;
            if (sZopEyeClosedFrames < 0x7FFF) {
                sZopEyeClosedFrames++;
            }
            sZopEyeOpenFrames = 0;
            // close eye; while her laser is out it stays a narrow glowing slit the beam comes out of (still shut for
            // hits, only an open eye can be hit)
            if (sZopPhase2Announced && ZOP_GohmaLaserActive(play)) {
                Math_ApproachS(&this->eyeLidBottomRotX, BOSSGOMA_EYE_LID_BOTTOM_SLIT, 1, 0xC00);
                Math_ApproachS(&this->eyeLidTopRotX, BOSSGOMA_EYE_LID_TOP_SLIT, 1, 0x7D0);
            } else {
                Math_ApproachS(&this->eyeLidBottomRotX, -0xA98, 1, 0xC00);
                Math_ApproachS(&this->eyeLidTopRotX, 0x1600, 1, 0x7D0);
            }
        } else {
            // Phase 2: a crystal switch chime tells that the eye is open (and hittable) again
            if (sZopPhase2Announced &&
                sZopEyeClosedFrames >=
                    (this->actionFunc == BossGoma_Phase2Signal ? 3 : BOSSGOMA_EYE_CHIME_MIN_CLOSED) &&
                this->actionFunc != BossGoma_Defeated) {
                Audio_PlayActorSound2(&this->actor, NA_SE_EV_DIAMOND_SWITCH);
                sZopEyeFlashTimer = BOSSGOMA_EYE_FLASH_FRAMES;
            }
            sZopEyeClosedFrames = 0;
            if (sZopEyeOpenFrames < 0x7FFF) {
                sZopEyeOpenFrames++;
            }
            // open eye, wide open so an open eye is unmistakable
            Math_ApproachS(&this->eyeLidBottomRotX, BOSSGOMA_EYE_LID_BOTTOM_WIDE, 1, 0xC00);
            Math_ApproachS(&this->eyeLidTopRotX, BOSSGOMA_EYE_LID_TOP_WIDE, 1, 0x7D0);
        }

        if (this->eyeState != EYESTATE_IRIS_NO_FOLLOW_NO_IFRAMES) {
            targetEyeIrisRotY =
                Actor_WorldYawTowardActor(&this->actor, &GET_PLAYER(play)->actor) - this->actor.shape.rot.y;
            targetEyeIrisRotX =
                Actor_WorldPitchTowardActor(&this->actor, &GET_PLAYER(play)->actor) - this->actor.shape.rot.x;

            if (this->actor.shape.rot.x > 0x4000 || this->actor.shape.rot.x < -0x4000) {
                targetEyeIrisRotY = -(s16)(targetEyeIrisRotY + 0x8000);
                targetEyeIrisRotX = -0xBB8;
            }

            if (targetEyeIrisRotY > 0x1770) {
                targetEyeIrisRotY = 0x1770;
            }

            if (targetEyeIrisRotY < -0x1770) {
                targetEyeIrisRotY = -0x1770;
            }

            Math_ApproachS(&this->eyeIrisRotY, targetEyeIrisRotY, 3, 0x7D0);
            Math_ApproachS(&this->eyeIrisRotX, targetEyeIrisRotX, 3, 0x7D0);
        } else {
            Math_ApproachS(&this->eyeIrisRotY, 0, 3, 0x3E8);
            Math_ApproachS(&this->eyeIrisRotX, 0, 3, 0x3E8);
        }

        Math_ApproachF(&this->eyeIrisScaleX, 1.0f, 0.2f, 0.07f);
        Math_ApproachF(&this->eyeIrisScaleY, 1.0f, 0.2f, 0.07f);
    }
}

/**
 * Part of achieving visual effects when spawning children gohmas,
 * inflating each tail limb one after the other.
 */
void BossGoma_UpdateTailLimbsScale(BossGoma* this) {
    s16 i;

    if (this->frameCount % 128 == 0) {
        this->unusedTimer++;
        if (this->unusedTimer >= 3) {
            this->unusedTimer = 0;
        }
    }

    // See BossGoma_CeilingSpawnGohmas for `tailLimbsScaleTimers` usage
    for (i = 0; i < ARRAY_COUNT(this->tailLimbsScaleTimers); i++) {
        if (this->tailLimbsScaleTimers[i] != 0) {
            this->tailLimbsScaleTimers[i]--;
            Math_ApproachF(&this->tailLimbsScale[i], 1.5f, 0.2f, 0.1f);
        } else {
            Math_ApproachF(&this->tailLimbsScale[i], 1.0f, 0.2f, 0.1f);
        }
    }
}

void BossGoma_UpdateHit(BossGoma* this, PlayState* play) {
    if (this->invincibilityFrames != 0) {
        this->invincibilityFrames--;
    } else {
        ColliderInfo* acHitInfo = this->collider.elements[0].info.acHitInfo;
        s32 damage;

        if (this->eyeClosedTimer == 0 && this->actionFunc != BossGoma_CeilingSpawnGohmas &&
            this->actionFunc != BossGoma_Phase2Signal && // the barrage warning flashes can't be hit
            (this->collider.elements[0].info.bumperFlags & BUMP_HIT)) {
            this->collider.elements[0].info.bumperFlags &= ~BUMP_HIT;

            if (this->actionFunc == BossGoma_CeilingMoveToCenter || this->actionFunc == BossGoma_CeilingIdle ||
                this->actionFunc == BossGoma_CeilingPrepareSpawnGohmas) {
                ZOP_StopGohmaLasers(play);
                BossGoma_SetupFallStruckDown(this);
                BossGoma_HitFlash();
                Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_DAM2);
            } else if (this->actionFunc == BossGoma_FloorStunned &&
                       (damage = CollisionCheck_GetSwordDamage(acHitInfo->toucher.dmgFlags, play)) != 0) {
                BossGoma_HitFlash();
                this->actor.colChkInfo.health -= damage;

                if ((s8)this->actor.colChkInfo.health > 0) {
                    Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_DAM1);
                    EffectSsSibuki_SpawnBurst(play, &this->actor.focus.pos);

                    if (!sZopPhase2Announced && BossGoma_InPhase2(this)) {
                        sZopPhase2Announced = true;
                        BossGoma_SetupPhase2Slam(this);
                    } else {
                        BossGoma_SetupFloorDamaged(this);
                    }
                } else {
                    BossGoma_SetupDefeated(this, play);
                    Enemy_StartFinishingBlow(play, &this->actor);
                    GameInteractor_ExecuteOnBossDefeat(&this->actor);
                }

                this->invincibilityFrames = 10;
            } else if (this->actionFunc != BossGoma_FloorStunned && this->patienceTimer != 0 &&
                       (acHitInfo->toucher.dmgFlags & 0x00000005)) {
                Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_DAM2);
                Audio_StopSfxById(NA_SE_EN_GOMA_CRY1);
                this->invincibilityFrames = 10;
                ZOP_StopGohmaLasers(play);
                BossGoma_SetupFloorStunned(this);
                BossGoma_HitFlash();
                this->sfxFaintTimer = 100;

                if (acHitInfo->toucher.dmgFlags & 1) {
                    this->framesUntilNextAction = ZOP_STUN(40);
                } else {
                    this->framesUntilNextAction = ZOP_HARD(90, 100);
                }

                this->timer = 4;
                Actor_RequestQuakeAndRumble(&this->actor, play, 4, 0xC);
            }
        }
    }
}

void BossGoma_UpdateMainEnvColor(BossGoma* this) {
    static f32 colors1[][3] = {
        { 255.0f, 17.0f, 0.0f },  { 0.0f, 255.0f, 170.0f }, { 50.0f, 50.0f, 50.0f },
        { 0.0f, 255.0f, 170.0f }, { 0.0f, 255.0f, 170.0f }, { 0.0f, 255.0f, 170.0f },
    };
    static f32 colors2[][3] = {
        { 255.0f, 17.0f, 0.0f },  { 0.0f, 255.0f, 170.0f }, { 50.0f, 50.0f, 50.0f },
        { 0.0f, 255.0f, 170.0f }, { 0.0f, 0.0f, 255.0f },   { 255.0f, 17.0f, 0.0f },
    };

    if (this->visualState == VISUALSTATE_DEFAULT && this->frameCount & 0x10) {
        Math_ApproachF(&this->mainEnvColor[0], 50.0f, 0.5f, 20.0f);
        Math_ApproachF(&this->mainEnvColor[1], 50.0f, 0.5f, 20.0f);
        Math_ApproachF(&this->mainEnvColor[2], 50.0f, 0.5f, 20.0f);
    } else if (this->invincibilityFrames != 0) {
        if (this->invincibilityFrames & 2) {
            this->mainEnvColor[0] = colors2[this->visualState][0];
            this->mainEnvColor[1] = colors2[this->visualState][1];
            this->mainEnvColor[2] = colors2[this->visualState][2];
        } else {
            this->mainEnvColor[0] = colors1[this->visualState][0];
            this->mainEnvColor[1] = colors1[this->visualState][1];
            this->mainEnvColor[2] = colors1[this->visualState][2];
        }
    } else {
        Math_ApproachF(&this->mainEnvColor[0], colors1[this->visualState][0], 0.5f, 20.0f);
        Math_ApproachF(&this->mainEnvColor[1], colors1[this->visualState][1], 0.5f, 20.0f);
        Math_ApproachF(&this->mainEnvColor[2], colors1[this->visualState][2], 0.5f, 20.0f);
    }
}

void BossGoma_UpdateEyeEnvColor(BossGoma* this) {
    static f32 targetEyeEnvColors[][3] = {
        { 255.0f, 17.0f, 0.0f },  { 255.0f, 255.0f, 255.0f }, { 50.0f, 50.0f, 50.0f },
        { 0.0f, 255.0f, 170.0f }, { 0.0f, 255.0f, 170.0f },   { 0.0f, 255.0f, 170.0f },
    };

    Math_ApproachF(&this->eyeEnvColor[0], targetEyeEnvColors[this->visualState][0], 0.5f, 20.0f);
    Math_ApproachF(&this->eyeEnvColor[1], targetEyeEnvColors[this->visualState][1], 0.5f, 20.0f);
    Math_ApproachF(&this->eyeEnvColor[2], targetEyeEnvColors[this->visualState][2], 0.5f, 20.0f);
}

// Collider elements, in the order of sColliderJntSphElementInit
#define BOSSGOMA_COLLIDER_EYE 0
#define BOSSGOMA_COLLIDER_L_ANTENNA_CLAW 8
#define BOSSGOMA_COLLIDER_R_ANTENNA_CLAW 9

static void BossGoma_SetTouch(BossGoma* this, s32 element, s32 on) {
    if (on) {
        this->collider.elements[element].info.toucherFlags |= TOUCH_ON;
    } else {
        this->collider.elements[element].info.toucherFlags &= ~TOUCH_ON;
    }
}

// Testing aid (gZOP.TestEnv.GohmaEyeTest): Gohma stands in front of Link in her stand pose; every 5 s the eyelids
// switch to the next pose of the table (offsets added to the animation, bottom / top)
static const s16 sZopEyeTestPoses[][2] = {
    { 0, 0 }, { -0xA98, 0x1600 }, { BOSSGOMA_EYE_LID_BOTTOM_SLIT, BOSSGOMA_EYE_LID_TOP_SLIT }, { -0x500, 0x0C00 },
};
static s16 sZopEyeTestFrames;

static void BossGoma_ZopEyeTest(BossGoma* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    s32 pose = (sZopEyeTestFrames++ / 120) % ARRAY_COUNT(sZopEyeTestPoses);

    if (sZopEyeTestFrames % 120 == 1) {
        osSyncPrintf("ZOP eye test pose %d\n", pose);
    }
    if (sZopEyeTestFrames % 30 == 1) {
        BossGoma_SpawnShockwave(this, player->actor.world.pos.y); // to look at the barrage shockwave
    }
    SkelAnime_Update(&this->skelanime);
    this->actor.world.pos.x = player->actor.world.pos.x + Math_SinS(player->actor.shape.rot.y) * 280.0f;
    this->actor.world.pos.z = player->actor.world.pos.z + Math_CosS(player->actor.shape.rot.y) * 280.0f;
    this->actor.world.pos.y = player->actor.world.pos.y + 45.0f;
    this->actor.shape.rot.x = 0;
    this->actor.shape.rot.y = this->actor.world.rot.y =
        Math_Vec3f_Yaw(&this->actor.world.pos, &Play_GetCamera(play, 0)->eye);
    this->actor.speedXZ = 0.0f;
    this->actor.velocity.y = 0.0f;
    this->actor.gravity = 0.0f;
    this->eyeLidBottomRotX = sZopEyeTestPoses[pose][0];
    this->eyeLidTopRotX = sZopEyeTestPoses[pose][1];
    this->eyeIrisRotX = this->eyeIrisRotY = 0;
    // Pose 0 is marked by a big iris, so the order can be told apart on screenshots
    this->eyeIrisScaleX = this->eyeIrisScaleY = (pose == 0) ? 1.7f : 1.0f;
    this->eyeState = EYESTATE_IRIS_FOLLOW_NO_IFRAMES;
}

void BossGoma_Update(Actor* thisx, PlayState* play) {
    BossGoma* this = (BossGoma*)thisx;
    s32 pad;

    // Testing aid 2: Gohma stands in front of Link at half health and starts the phase 2 intro right away
    // Testing aid 3: the same, but she skips the intro and starts her barrage attack
    // Testing aid 4: the same, but she starts her hop attack
    if (ZOP_TestGohmaEye() >= 2 && ZOP_TestGohmaEye() <= 4) {
        static u8 sZopFdTestStarted = false;

        if (this->actionFunc == BossGoma_Encounter || !sZopFdTestStarted) {
            Player* player = GET_PLAYER(play);

            sZopFdTestStarted = true;
            this->actor.world.pos.x = player->actor.world.pos.x + Math_SinS(player->actor.shape.rot.y) * 300.0f;
            this->actor.world.pos.z = player->actor.world.pos.z + Math_CosS(player->actor.shape.rot.y) * 300.0f;
            this->actor.world.pos.y = player->actor.world.pos.y;
            this->actor.shape.rot.x = 0;
            this->actor.shape.rot.y = this->actor.world.rot.y = player->actor.shape.rot.y + 0x8000;
            this->actor.flags |= ACTOR_FLAG_ATTENTION_ENABLED;
            this->actor.gravity = -2.0f;
            this->actor.colChkInfo.health = (u8)CLAMP_MIN(this->actor.colChkInfo.health / 2, 1);
            sZopPhase2Announced = true;
            if (ZOP_TestGohmaEye() == 3) {
                this->disableGameplayLogic = false;
                BossGoma_SetupPhase2Signal(this);
            } else if (ZOP_TestGohmaEye() == 4) {
                this->disableGameplayLogic = false;
                sZopHopCooldown = 0;
                this->actor.bgCheckFlags |= BGCHECKFLAG_GROUND;
                BossGoma_TryHop(this, play);
            } else {
                BossGoma_SetupPhase2Slam(this);
            }
        }
    } else if (ZOP_TestGohmaEye()) {
        if (this->actionFunc != BossGoma_ZopEyeTest) {
            Animation_Change(&this->skelanime, &gGohmaStandAnim, 1.0f, 0.0f, Animation_GetLastFrame(&gGohmaStandAnim),
                             ANIMMODE_LOOP, 0.0f);
            this->actionFunc = BossGoma_ZopEyeTest;
            this->disableGameplayLogic = true;
            this->visualState = VISUALSTATE_DEFAULT;
            sZopEyeTestFrames = 0;
        }
        this->actionFunc(this, play);
        BossGoma_UpdateShockwave();
        return;
    }

    this->visualState = VISUALSTATE_DEFAULT;
    this->frameCount++;

    if (this->framesUntilNextAction != 0) {
        this->framesUntilNextAction--;
    }

    if (this->timer != 0) {
        this->timer--;
    }

    if (this->sfxFaintTimer != 0) {
        this->sfxFaintTimer--;
    }

    if (sZopPhase2Announced && !this->disableGameplayLogic && sZopBarrageCooldown > 0) {
        sZopBarrageCooldown--;
    }
    if (!this->disableGameplayLogic && sZopHopCooldown > 0 && this->actionFunc != BossGoma_Encounter) {
        sZopHopCooldown--;
    }

    this->eyeState = EYESTATE_IRIS_FOLLOW_BONUS_IFRAMES;
    this->actionFunc(this, play);
    this->actor.shape.rot.y = this->actor.world.rot.y;

    if (!this->doNotMoveThisFrame) {
        Actor_MoveXZGravity(&this->actor);
    } else {
        this->doNotMoveThisFrame = false;
    }

    if (this->actor.world.pos.y < -400.0f) {
        Actor_UpdateBgCheckInfo(play, &this->actor, 30.0f, 30.0f, 80.0f, 5);
    } else {
        Actor_UpdateBgCheckInfo(play, &this->actor, 0.0f, 30.0f, 80.0f, 1);
    }

    BossGoma_UpdateEye(this, play);
    if (sZopEyeFlashTimer > 0) {
        sZopEyeFlashTimer--;
    }
    BossGoma_UpdateShockwave();
    if (sZopWaveCanHit) {
        // The ring band runs from 150x to 300x its scale (like Ganondorf's shockwave). Link only gets caught while he
        // stands on the ground, so a jump at the right moment (backflip, forward hop) clears it.
        Player* player = GET_PLAYER(play);
        f32 dist = Math_Vec3f_DistXZ(&sZopWavePos, &player->actor.world.pos);

        f32 inner = sZopWaveScale * 300.0f * sZopWaveInner;

        if (sZopWaveAlpha <= 0 || inner > sZopWaveRadius) {
            sZopWaveCanHit = false;
        } else if (dist >= inner && dist <= sZopWaveScale * 300.0f && dist < sZopWaveRadius &&
                   (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND)) {
            s16 yaw = Math_Vec3f_Yaw(&sZopWavePos, &player->actor.world.pos);

            sZopWaveCanHit = false;
            if (sZopWaveDamage > 0) {
                Actor_SetPlayerKnockbackLarge(play, &this->actor, 8.0f, yaw, 5.0f, sZopWaveDamage);
            } else {
                Actor_SetPlayerKnockbackLargeNoDamage(play, &this->actor, 17.0f, yaw, 8.0f);
            }
        }
    }
    if (sZopHitFlash > 0) {
        // Full right away, then fading out; cleared on the last frame
        sZopHitFlash--;
        BossGoma_SetHaze(play, (f32)sZopHitFlash / (BOSSGOMA_HIT_FLASH_FRAMES - 1));
    }
    BossGoma_UpdateMainEnvColor(this);
    BossGoma_UpdateEyeEnvColor(this);
    BossGoma_UpdateTailLimbsScale(this);

    if (!this->disableGameplayLogic) {
        BossGoma_UpdateHit(this, play);
        CollisionCheck_SetAC(play, &play->colChkCtx, &this->collider.base);
        CollisionCheck_SetOC(play, &play->colChkCtx, &this->collider.base);

        if (this->actionFunc != BossGoma_FloorStunned && this->actionFunc != BossGoma_FloorDamaged &&
            (this->actionFunc != BossGoma_FloorMain || this->timer == 0)) {
            // ZeldaOOTProject: no contact damage from the front. The eye never hurts on touch (Link stands right at
            // it to hit it), and the antenna claws only hurt while they actually strike. Legs and tail still do.
            BossGoma_SetTouch(this, BOSSGOMA_COLLIDER_EYE, false);
            BossGoma_SetTouch(this, BOSSGOMA_COLLIDER_L_ANTENNA_CLAW, this->actionFunc == BossGoma_FloorAttack);
            BossGoma_SetTouch(this, BOSSGOMA_COLLIDER_R_ANTENNA_CLAW, this->actionFunc == BossGoma_FloorAttack);
            CollisionCheck_SetAT(play, &play->colChkCtx, &this->collider.base);
        }
    }
}

s32 BossGoma_OverrideLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* thisx) {
    BossGoma* this = (BossGoma*)thisx;
    s32 doNotDrawLimb = false;

    OPEN_DISPS(play->state.gfxCtx);

    gDPPipeSync(POLY_OPA_DISP++);
    if (sZopPhase2Announced && this->visualState != VISUALSTATE_DEFEATED) {
        // Phase 2: the glowing accents shift from green/cyan toward red
        gDPSetEnvColor(POLY_OPA_DISP++, (s16)(this->mainEnvColor[0] * 0.3f + 178.0f),
                       (s16)(this->mainEnvColor[1] * 0.35f), (s16)(this->mainEnvColor[2] * 0.25f), 255);
    } else {
        gDPSetEnvColor(POLY_OPA_DISP++, (s16)this->mainEnvColor[0], (s16)this->mainEnvColor[1],
                       (s16)this->mainEnvColor[2], 255);
    }

    if (this->deadLimbsState[limbIndex] >= 2) {
        *dList = NULL;
    }

    switch (limbIndex) {
        case BOSSGOMA_LIMB_EYE:
            if (this->eyeState == EYESTATE_IRIS_FOLLOW_BONUS_IFRAMES && this->eyeLidBottomRotX < -0xA8C) {
                *dList = NULL;
            } else if (this->invincibilityFrames != 0) {
                gDPSetEnvColor(POLY_OPA_DISP++, (s16)(Rand_ZeroOne() * 255.0f), (s16)(Rand_ZeroOne() * 255.0f),
                               (s16)(Rand_ZeroOne() * 255.0f), 63);
            } else {
                gDPSetEnvColor(POLY_OPA_DISP++, (s16)this->eyeEnvColor[0], (s16)this->eyeEnvColor[1],
                               (s16)this->eyeEnvColor[2], 63);
            }
            break;

        case BOSSGOMA_LIMB_EYE_LID_BOTTOM_ROOT2:
            // The intro and defeat scenes rely on the animations' own lid motion (like vanilla)
            rot->x = ((sZopLidBaseValid && !BossGoma_LidsFollowAnim(this)) ? sZopLidBaseBottom : rot->x) +
                     this->eyeLidBottomRotX;
            break;

        case BOSSGOMA_LIMB_EYE_LID_TOP_ROOT2:
            rot->x = ((sZopLidBaseValid && !BossGoma_LidsFollowAnim(this)) ? sZopLidBaseTop : rot->x) +
                     this->eyeLidTopRotX;
            break;

        case BOSSGOMA_LIMB_IRIS_ROOT2:
            rot->x += this->eyeIrisRotX;
            rot->y += this->eyeIrisRotY;
            break;

        case BOSSGOMA_LIMB_IRIS:
            if (this->eyeState == EYESTATE_IRIS_FOLLOW_BONUS_IFRAMES && this->eyeLidBottomRotX < -0xA8C) {
                *dList = NULL;
            } else {
                if (this->visualState == VISUALSTATE_DEFEATED) {
                    gDPSetEnvColor(POLY_OPA_DISP++, 50, 50, 50, 255);
                } else {
                    gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 255);
                }

                Matrix_TranslateRotateZYX(pos, rot);

                if (*dList != NULL) {
                    if (this->skelanime.skeletonHeader->skeletonType == SKELANIME_TYPE_FLEX) {
                        MATRIX_TOMTX(*play->flexLimbOverrideMTX);
                    }

                    Matrix_Push();
                    Matrix_Scale(this->eyeIrisScaleX, this->eyeIrisScaleY, 1.0f, MTXMODE_APPLY);

                    if (this->skelanime.skeletonHeader->skeletonType == SKELANIME_TYPE_FLEX) {
                        gSPMatrix(POLY_OPA_DISP++, *play->flexLimbOverrideMTX, G_MTX_LOAD);
                    } else {
                        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
                                  G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                    }

                    gSPDisplayList(POLY_OPA_DISP++, *dList);
                    Matrix_Pop();

                    if (this->skelanime.skeletonHeader->skeletonType == SKELANIME_TYPE_FLEX) {
                        (*play->flexLimbOverrideMTX)++;
                    }
                }

                doNotDrawLimb = true;
            }
            break;

        case BOSSGOMA_LIMB_TAIL4:
        case BOSSGOMA_LIMB_TAIL3:
        case BOSSGOMA_LIMB_TAIL2:
        case BOSSGOMA_LIMB_TAIL1:
            Matrix_TranslateRotateZYX(pos, rot);

            if (*dList != NULL) {
                if (this->skelanime.skeletonHeader->skeletonType == SKELANIME_TYPE_FLEX) {
                    MATRIX_TOMTX(*play->flexLimbOverrideMTX);
                }

                Matrix_Push();
                Matrix_Scale(this->tailLimbsScale[limbIndex - BOSSGOMA_LIMB_TAIL4],
                             this->tailLimbsScale[limbIndex - BOSSGOMA_LIMB_TAIL4],
                             this->tailLimbsScale[limbIndex - BOSSGOMA_LIMB_TAIL4], MTXMODE_APPLY);

                if (this->skelanime.skeletonHeader->skeletonType == SKELANIME_TYPE_FLEX) {
                    gSPMatrix(POLY_OPA_DISP++, *play->flexLimbOverrideMTX, G_MTX_LOAD);
                } else {
                    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
                              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                }

                gSPDisplayList(POLY_OPA_DISP++, *dList);
                Matrix_Pop();

                if (this->skelanime.skeletonHeader->skeletonType == SKELANIME_TYPE_FLEX) {
                    (*play->flexLimbOverrideMTX)++;
                }
            }

            doNotDrawLimb = true;
            break;
    }

    CLOSE_DISPS(play->state.gfxCtx);

    return doNotDrawLimb;
}

void BossGoma_PostLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* thisx) {
    static Vec3f tailZero = { 0.0f, 0.0f, 0.0f };
    static Vec3f clawBackLocalPos = { 0.0f, 0.0f, 0.0f };
    static Vec3f focusEyeLocalPos = { 0.0f, 300.0f, 2650.0f }; // in the center of the surface of the lens
    static Vec3f zero = { 0.0f, 0.0f, 0.0f };
    Vec3f childPos;
    Vec3s childRot;
    EnGoma* babyGohma;
    BossGoma* this = (BossGoma*)thisx;
    s32 pad;
    MtxF mtx;

    if (limbIndex == BOSSGOMA_LIMB_TAIL4) { // tail end/last part
        Matrix_MultVec3f(&tailZero, &this->lastTailLimbWorldPos);
    } else if (limbIndex == BOSSGOMA_LIMB_TAIL1) { // tail start/first part
        Matrix_MultVec3f(&tailZero, &this->firstTailLimbWorldPos);
    } else if (limbIndex == BOSSGOMA_LIMB_EYE) {
        Matrix_MultVec3f(&focusEyeLocalPos, &this->actor.focus.pos);
        // Where the eye looks (its local +Z), for cutscene shots of the eye whatever pose she is in
        {
            static Vec3f eyeAheadLocalPos = { 0.0f, 300.0f, 6650.0f };
            Matrix_MultVec3f(&eyeAheadLocalPos, &sZopEyeAheadPos);
        }
    } else if (limbIndex == BOSSGOMA_LIMB_R_FEET_BACK) {
        Matrix_MultVec3f(&clawBackLocalPos, &this->rightHandBackLimbWorldPos);
    } else if (limbIndex == BOSSGOMA_LIMB_L_FEET_BACK) {
        Matrix_MultVec3f(&clawBackLocalPos, &this->leftHandBackLimbWorldPos);
    }

    if (this->visualState == VISUALSTATE_DEFEATED) {
        if (*dList != NULL) {
            Matrix_MultVec3f(&clawBackLocalPos, &this->defeatedLimbPositions[limbIndex]);
        } else {
            this->defeatedLimbPositions[limbIndex].y = 10000.0f;
        }
    }

    if (this->deadLimbsState[limbIndex] == 1) {
        this->deadLimbsState[limbIndex] = 2;
        Matrix_MultVec3f(&zero, &childPos);
        Matrix_Get(&mtx);
        Matrix_MtxFToYXZRotS(&mtx, &childRot, 0);
        // These are the pieces of Gohma as it falls apart. It appears to use the same actor as the baby gohmas.
        babyGohma = (EnGoma*)Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_GOMA, childPos.x,
                                                childPos.y, childPos.z, childRot.x, childRot.y, childRot.z,
                                                sDeadLimbLifetime[limbIndex] + 100);
        if (babyGohma != NULL) {
            babyGohma->bossLimbDL = *dList;
            babyGohma->actor.objBankIndex = this->actor.objBankIndex;
        }
    }

    Collider_UpdateSpheres(limbIndex, &this->collider);
}

Gfx* BossGoma_EmptyDlist(GraphicsContext* gfxCtx) {
    Gfx* dListHead;
    Gfx* dList;

    dList = dListHead = Graph_Alloc(gfxCtx, sizeof(Gfx) * 1);

    gSPEndDisplayList(dListHead++);

    return dList;
}

Gfx* BossGoma_NoBackfaceCullingDlist(GraphicsContext* gfxCtx) {
    Gfx* dListHead;
    Gfx* dList;

    dList = dListHead = Graph_Alloc(gfxCtx, sizeof(Gfx) * 4);

    gDPPipeSync(dListHead++);
    gDPSetRenderMode(dListHead++, G_RM_PASS, G_RM_AA_ZB_TEX_EDGE2);
    gSPClearGeometryMode(dListHead++, G_CULL_BACK);
    gSPEndDisplayList(dListHead++);

    return dList;
}

void BossGoma_Draw(Actor* thisx, PlayState* play) {
    BossGoma* this = (BossGoma*)thisx;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(0.0f, -4000.0f, 0.0f, MTXMODE_APPLY);

    // Invalidate Texture Cache since Goma modifies her own texture
    if (this->visualState == VISUALSTATE_DEFEATED) {
        gSPInvalidateTexCache(POLY_OPA_DISP++, sClearPixelTex16);
        gSPInvalidateTexCache(POLY_OPA_DISP++, sClearPixelTex32);
    }

    if (this->noBackfaceCulling) {
        gSPSegment(POLY_OPA_DISP++, 0x08, BossGoma_NoBackfaceCullingDlist(play->state.gfxCtx));
    } else {
        gSPSegment(POLY_OPA_DISP++, 0x08, BossGoma_EmptyDlist(play->state.gfxCtx));
    }

    // Phase 2: red skin and shell, faded in during the phase 2 cutscene
    {
        f32 recolor = 0.0f;

        if (sZopPhase2Announced && (this->actionFunc != BossGoma_Phase2Slam || sZopSlamIntoBarrage)) {
            recolor = 1.0f;
            if (this->actionFunc == BossGoma_Phase2FakeDeath) {
                // Normal colors while she lies there, red as she rises
                recolor = CLAMP((f32)(sZopFdElapsed - BOSSGOMA_FD_RISE) / sZopFdRiseFrames, 0.0f, 1.0f);
            } else if (this->actionFunc == BossGoma_Phase2Roar) {
                recolor = (f32)(BOSSGOMA_PHASE2_ROAR_FRAMES - this->framesUntilNextAction) /
                          BOSSGOMA_PHASE2_EYE_SHOT_FRAME;
                recolor = CLAMP_MAX(recolor, 1.0f);
            }
        }
        if (ZOP_GohmaSetRecolor(recolor) || sZopRecolorDirty) {
            sZopRecolorDirty = false;
            gSPInvalidateTexCache(POLY_OPA_DISP++, (uintptr_t)gGohmaBodyTex);
            gSPInvalidateTexCache(POLY_OPA_DISP++, (uintptr_t)gGohmaShellUndersideTex);
            gSPInvalidateTexCache(POLY_OPA_DISP++, (uintptr_t)gGohmaDarkShellTex);
            gSPInvalidateTexCache(POLY_OPA_DISP++, (uintptr_t)gGohmaShellTex);
        }
    }

    SkelAnime_DrawSkeletonOpa(play, &this->skelanime, BossGoma_OverrideLimbDraw, BossGoma_PostLimbDraw, this);

    // Phase 2: a short lens flare on the eye when it opens: a round glow and a wide horizontal streak
    if (sZopEyeFlashTimer > 0) {
        f32 t = (f32)sZopEyeFlashTimer / BOSSGOMA_EYE_FLASH_FRAMES; // 1 -> 0
        f32 grow = 1.0f - t;
        f32 size = (0.35f + 0.65f * Math_SinF(grow * 3.14159265f)) * 0.09f;
        u8 alpha = (u8)(255.0f * CLAMP(t * 1.6f, 0.0f, 1.0f));

        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x80, 255, 255, 235, alpha);
        gDPSetEnvColor(POLY_XLU_DISP++, 140, 230, 255, 0);

        Matrix_Translate(this->actor.focus.pos.x, this->actor.focus.pos.y, this->actor.focus.pos.z, MTXMODE_NEW);
        Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
        Matrix_Push();
        Matrix_Scale(size, size, 1.0f, MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, gEffFlash1DL);
        Matrix_Pop();
        Matrix_Scale(size * 3.5f, size * 0.22f, 1.0f, MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, gEffFlash1DL);
    }

    if (sZopWaveAlpha > 0) {
        f32 xz = sZopWaveScale * 200.0f / 1500.0f;

        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gDPPipeSync(POLY_XLU_DISP++);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, sZopWaveAlpha);
        gDPSetEnvColor(POLY_XLU_DISP++, 210, 215, 225, 128);
        gSPSegment(POLY_XLU_DISP++, 0x08,
                   Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, sZopWaveTimer * 100, 0, 64, 32, 1, sZopWaveTimer * 100,
                                      0, 64, 32, 100, 0, 100, 0));
        Matrix_Translate(sZopWavePos.x, sZopWavePos.y, sZopWavePos.z, MTXMODE_NEW);
        Matrix_Scale(xz, 0.8f * 200.0f / 1500.0f, xz, MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, gGanondorfShockwaveDL);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

void BossGoma_SpawnChildGohma(BossGoma* this, PlayState* play, s16 i) {
    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_GOMA, this->lastTailLimbWorldPos.x,
                       this->lastTailLimbWorldPos.y - 50.0f, this->lastTailLimbWorldPos.z, 0, i * (0x10000 / 3), 0, i);

    this->childrenGohmaState[i] = 1;
}

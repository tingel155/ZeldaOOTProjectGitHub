/*
 * File: z_boss_ganondrof.c
 * Overlay: ovl_Boss_Ganondrof
 * Description: Phantom Ganon
 */

#include "z_boss_ganondrof.h"
#include "objects/object_gnd/object_gnd.h"
#include "overlays/actors/ovl_En_fHG/z_en_fhg.h"
#include "overlays/actors/ovl_En_Fhg_Fire/z_en_fhg_fire.h"
#include "overlays/effects/ovl_Effect_Ss_Fhg_Flash/z_eff_ss_fhg_flash.h"
#include "overlays/effects/ovl_Effect_Ss_Hahen/z_eff_ss_hahen.h"
#include "overlays/actors/ovl_Door_Warp1/z_door_warp1.h"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/ZeldaOOTProject/ZeldaOOTProjectHooks.h"
#include "objects/gameplay_keep/gameplay_keep.h"

#define FLAGS                                                                                 \
    (ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE | ACTOR_FLAG_UPDATE_CULLING_DISABLED | \
     ACTOR_FLAG_DRAW_CULLING_DISABLED)

typedef enum {
    /* 0 */ THROW_NORMAL,
    /* 1 */ THROW_SLOW
} BossGanondrofThrowAction;

typedef enum {
    /* 0 */ STUNNED_FALL,
    /* 1 */ STUNNED_GROUND
} BossGanondrofStunnedAction;

typedef enum {
    /* 0 */ CHARGE_WINDUP,
    /* 1 */ CHARGE_START,
    /* 2 */ CHARGE_ATTACK,
    /* 3 */ CHARGE_FINISH
} BossGanondrofChargeAction;

void BossGanondrof_Init(Actor* thisx, PlayState* play);
void BossGanondrof_Destroy(Actor* thisx, PlayState* play);
void BossGanondrof_Update(Actor* thisx, PlayState* play);
void BossGanondrof_Draw(Actor* thisx, PlayState* play);

void BossGanondrof_SetupIntro(BossGanondrof* this, PlayState* play);
void BossGanondrof_Intro(BossGanondrof* this, PlayState* play);
void BossGanondrof_SetupPaintings(BossGanondrof* this);
void BossGanondrof_Paintings(BossGanondrof* this, PlayState* play);
void BossGanondrof_SetupNeutral(BossGanondrof* this, f32 arg1);
void BossGanondrof_Neutral(BossGanondrof* this, PlayState* play);
void BossGanondrof_SetupThrow(BossGanondrof* this, PlayState* play);
void BossGanondrof_Throw(BossGanondrof* this, PlayState* play);
void BossGanondrof_SetupBlock(BossGanondrof* this, PlayState* play);
void BossGanondrof_Block(BossGanondrof* this, PlayState* play);
void BossGanondrof_SetupReturn(BossGanondrof* this, PlayState* play);
void BossGanondrof_Return(BossGanondrof* this, PlayState* play);
void BossGanondrof_SetupCharge(BossGanondrof* this, PlayState* play);
void BossGanondrof_Charge(BossGanondrof* this, PlayState* play);
void BossGanondrof_Stunned(BossGanondrof* this, PlayState* play);
void BossGanondrof_Death(BossGanondrof* this, PlayState* play);

const ActorInit Boss_Ganondrof_InitVars = {
    ACTOR_BOSS_GANONDROF,
    ACTORCAT_BOSS,
    FLAGS,
    OBJECT_GND,
    sizeof(BossGanondrof),
    (ActorFunc)BossGanondrof_Init,
    (ActorFunc)BossGanondrof_Destroy,
    (ActorFunc)BossGanondrof_Update,
    (ActorFunc)BossGanondrof_Draw,
    NULL,
};

static ColliderCylinderInit sCylinderInitBody = {
    {
        COLTYPE_HIT3,
        AT_ON | AT_TYPE_ENEMY,
        AC_ON | AC_TYPE_PLAYER,
        OC1_ON | OC1_TYPE_ALL,
        OC2_TYPE_1,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK0,
        { 0xFFCFFFFF, 0x00, 0x10 },
        { 0xFFCFFFFE, 0x00, 0x00 },
        TOUCH_ON | TOUCH_SFX_NORMAL,
        BUMP_ON | BUMP_HOOKABLE,
        OCELEM_ON,
    },
    { 30, 90, -50, { 0, 0, 0 } },
};

static ColliderCylinderInit sCylinderInitSpear = {
    {
        COLTYPE_HIT3,
        AT_ON | AT_TYPE_ENEMY,
        AC_ON | AC_TYPE_PLAYER,
        OC1_ON | OC1_TYPE_ALL,
        OC2_TYPE_1,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK0,
        { 0xFFCFFFFF, 0x00, 0x30 },
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_ON | TOUCH_SFX_NORMAL,
        BUMP_ON,
        OCELEM_ON,
    },
    { 20, 30, -20, { 0, 0, 0 } },
};

// clang-format off
static u8 sDecayMaskHigh[16 * 16] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,
    1,0,1,1,0,0,0,0,1,1,1,1,1,1,0,1,
    1,0,1,1,1,0,0,0,0,1,1,1,1,1,0,1,
    1,0,0,1,1,1,1,0,0,0,1,1,1,0,0,1,
    1,0,0,1,1,1,1,1,0,0,0,1,1,0,0,1,
    1,0,1,1,1,1,0,0,0,0,0,1,0,0,0,1,
    1,1,1,1,1,1,1,1,0,0,0,0,1,1,0,1,
    1,0,1,1,1,1,1,0,0,0,0,1,1,1,0,1,
    1,0,0,1,1,1,0,0,0,1,1,1,1,1,0,1,
    1,0,0,0,0,0,0,0,0,0,1,1,1,1,0,1,
    1,0,0,0,1,1,0,0,0,1,1,1,1,1,1,1,
    1,0,0,1,1,1,1,0,1,1,1,1,1,1,1,1,
    1,0,1,1,1,1,1,0,0,1,1,1,1,1,0,1,
    1,1,1,1,1,1,1,0,0,1,1,1,0,0,0,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
};

static u8 sDecayMaskLow[16 * 16] = {
    1,1,1,0,1,0,0,1,0,0,1,1,1,1,1,1,
    0,0,0,0,0,0,0,0,1,1,1,0,0,1,1,0,
    1,0,1,1,0,0,0,0,0,1,1,0,0,1,0,0,
    1,0,0,1,1,0,0,0,0,0,1,1,1,0,0,0,
    0,0,0,1,1,1,0,0,0,0,0,1,1,0,0,1,
    0,0,0,1,0,0,1,0,0,0,0,0,1,0,0,1,
    1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,
    1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,
    1,0,0,0,1,0,0,0,0,0,0,0,1,0,0,1,
    0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,
    0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,
    1,0,0,0,0,1,0,0,0,0,0,1,1,1,0,0,
    1,0,0,0,0,1,0,0,0,0,1,0,1,1,0,0,
    0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,
    1,0,0,0,1,0,1,0,0,0,1,1,0,0,0,1,
    1,0,0,1,1,1,0,0,0,1,1,1,0,0,0,1,
};

static u8 sDecayMaskTotal[16 * 16] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
};
// clang-format on

static u8 sDecayTex[16 * 16] = { { 0 } };

// These are Phantom Ganon's body textures, but I don't know which is which.
static void* sLimbTex_rgba16_8x8[] = {
    gPhantomGanonLimbTex_00A800, gPhantomGanonLimbTex_00AE80, gPhantomGanonLimbTex_00AF00,
    gPhantomGanonLimbTex_00C180, gPhantomGanonLimbTex_00C400,
};
static void* sLimbTex_rgba16_16x8[] = {
    gPhantomGanonLimbTex_00B980, gPhantomGanonLimbTex_00C480, gPhantomGanonLimbTex_00BC80,
    gPhantomGanonLimbTex_00BD80, gPhantomGanonLimbTex_00C080,
};
static void* sLimbTex_rgba16_16x16[] = {
    gPhantomGanonLimbTex_00C200, gPhantomGanonLimbTex_00A000, gPhantomGanonLimbTex_00A200,
    gPhantomGanonLimbTex_00A400, gPhantomGanonLimbTex_00A600, gPhantomGanonLimbTex_00A880,
    gPhantomGanonLimbTex_00B780, gPhantomGanonLimbTex_00BA80, gPhantomGanonLimbTex_00BE80,
};
static void* sLimbTex_rgba16_16x32[] = { gPhantomGanonLimbTex_00AA80, gPhantomGanonLimbTex_00AF80 };

static void* sMouthTex_ci8_16x16[] = { gPhantomGanonMouthTex, gPhantomGanonSmileTex };

static InitChainEntry sInitChain[] = {
    ICHAIN_U8(targetMode, 5, ICHAIN_CONTINUE),
    ICHAIN_S8(naviEnemyId, 0x2B, ICHAIN_CONTINUE),
    ICHAIN_F32_DIV1000(gravity, 0, ICHAIN_CONTINUE),
    ICHAIN_F32(targetArrowOffset, 0, ICHAIN_STOP),
};

static Vec3f sAudioVec = { 0.0f, 0.0f, 50.0f };

void BossGanondrof_ClearPixels(u8* mask, s16 index) {
    if (mask[index]) {
        sDecayTex[index] = 1;
    }
}

void BossGanondrof_SetColliderPos(Vec3f* pos, ColliderCylinder* collider) {
    collider->dim.pos.x = pos->x;
    collider->dim.pos.y = pos->y;
    collider->dim.pos.z = pos->z;
}

static s16 sZopBurstCooldown; // catch-and-burst attack, see below
static s16 sZopPowerTimer;     // power hit, see below (> 0: staff charged for the rest of the rally)
static s16 sZopPowerRaised;    // staff still held up (until his first charged return)
static s16 sZopPowerFlashTimer; // lens flare on the staff when it gets charged
static s16 sZopChargesLeft;     // charge attack: more charges to come in this series
// Counterattack while he lies stunned on the floor: a red aura builds up (faster and faster pulses, rising hum) and
// bursts into a small shockwave that hurts Link and throws him back
static u8 sZopAuraPlanned;  // this stun will have one
static s16 sZopAuraDelay;   // frames on the floor before it starts
static s16 sZopAuraTimer;   // -1: none, else frames into it
static u8 sZopAuraRed;      // red pulse on his body this frame
#define ZOP_AURA_CHANCE 1.0f
#define ZOP_AURA_FRAMES 40
#define ZOP_AURA_RADIUS 75.0f
#define ZOP_AURA_DAMAGE 0x18
#define ZOP_GND_CHARGE_DAMAGE 0x28
#define ZOP_CHARGE_SPEED 18.0f   // vanilla 10
#define ZOP_CHARGE_REWINDUP 12   // wind-up frames between two charges of a series
#define ZOP_SLAM_CHANCE 0.2f     // chance of the staff slam on its own whenever he picks his next move
static void BossGanondrof_ZopSetupFinale(BossGanondrof* this, PlayState* play);
static void BossGanondrof_ZopFinale(BossGanondrof* this, PlayState* play);
// Floor shots: he fires 2-4 balls one after another into the floor in front of Link, each one sends out a ring that
// Link has to jump over. He throws a big ball up over the middle of the room, and the balls come from there while he
// just floats and watches
#define ZOP_FLOORSHOT_CHANCE 0.15f // chance whenever he picks his next move
#define ZOP_FLOORSHOT_INTERVAL 40  // frames between two shots
static s16 sZopShotsLeft;
static s16 sZopShotTimer;
static Actor* sZopMotherBall;
static s16 sZopShotAngle; // where the last shot went (around the middle of the room)
static f32 sZopShotDist;
static void BossGanondrof_ZopEndMotherBall(PlayState* play);
static void BossGanondrof_ZopSetupFloorShots(BossGanondrof* this, PlayState* play);
static u8 sZopFinaleAlone; // open to attacks after the staff slam (kneeling); set for every slam
#define ZOP_POWER_FLASH_FRAMES 12

void BossGanondrof_Init(Actor* thisx, PlayState* play) {
    s32 pad;
    BossGanondrof* this = (BossGanondrof*)thisx;

    Actor_ProcessInitChain(&this->actor, sInitChain);
    ActorShape_Init(&this->actor.shape, 0.0f, NULL, 0.0f);
    Actor_SetScale(&this->actor, 0.01f);
    sZopBurstCooldown = 200;
    sZopPowerTimer = 0;
    sZopPowerRaised = false;
    sZopPowerFlashTimer = 0;
    sZopMotherBall = NULL;
    SkelAnime_Init(play, &this->skelAnime, &gPhantomGanonSkel, &gPhantomGanonRideAnim, NULL, NULL, 0);
    if (this->actor.params < GND_FAKE_BOSS) {
        this->actor.params = GND_REAL_BOSS;
        this->actor.colChkInfo.health = 30;
        this->lightNode = LightContext_InsertLight(play, &play->lightCtx, &this->lightInfo);
        Lights_PointNoGlowSetInfo(&this->lightInfo, this->actor.world.pos.x, this->actor.world.pos.y,
                                  this->actor.world.pos.z, 255, 255, 255, 255);
        BossGanondrof_SetupIntro(this, play);
    } else {
        BossGanondrof_SetupPaintings(this);
    }

    Collider_InitCylinder(play, &this->colliderBody);
    Collider_InitCylinder(play, &this->colliderSpear);
    Collider_SetCylinder(play, &this->colliderBody, &this->actor, &sCylinderInitBody);
    Collider_SetCylinder(play, &this->colliderSpear, &this->actor, &sCylinderInitSpear);
    if (ZOP_DifficultyEnabled()) {
        // ZeldaOOTProject: his charge attack hurts a lot more
        this->colliderBody.info.toucher.damage = ZOP_GND_CHARGE_DAMAGE;
        this->colliderSpear.info.toucher.damage = ZOP_GND_CHARGE_DAMAGE;
    }
    this->actor.flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
    if (Flags_GetClear(play, play->roomCtx.curRoom.num)) {
        Actor_Kill(&this->actor);
        if (GameInteractor_Should(VB_SPAWN_BLUE_WARP, true, this)) {
            Actor_Spawn(&play->actorCtx, play, ACTOR_DOOR_WARP1, GND_BOSSROOM_CENTER_X, GND_BOSSROOM_CENTER_Y,
                        GND_BOSSROOM_CENTER_Z, 0, 0, 0, WARP_DUNGEON_ADULT);
        }
        if (GameInteractor_Should(VB_SPAWN_HEART_CONTAINER, true)) {
            Actor_Spawn(&play->actorCtx, play, ACTOR_ITEM_B_HEART, 200.0f + GND_BOSSROOM_CENTER_X,
                        GND_BOSSROOM_CENTER_Y, GND_BOSSROOM_CENTER_Z, 0, 0, 0, 0);
        }
    } else {
        Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_FHG, this->actor.world.pos.x,
                           this->actor.world.pos.y, this->actor.world.pos.z, 0, 0, 0, this->actor.params);
    }

    for (int i = 0; i < ARRAY_COUNT(sDecayTex); i++) {
        sDecayTex[i] = 0;
    }

    for (int i = 0; i < 5; i++) {
        Gfx_RegisterBlendedTexture(sLimbTex_rgba16_8x8[i], sDecayTex, NULL);
        Gfx_RegisterBlendedTexture(sLimbTex_rgba16_16x8[i], sDecayTex, NULL);
    }
    for (int i = 0; i < ARRAY_COUNT(sLimbTex_rgba16_16x16); i++) {
        Gfx_RegisterBlendedTexture(sLimbTex_rgba16_16x16[i], sDecayTex, NULL);
    }
    for (int i = 0; i < ARRAY_COUNT(sLimbTex_rgba16_16x32); i++) {
        Gfx_RegisterBlendedTexture(sLimbTex_rgba16_16x32[i], sDecayTex, NULL);
    }
    Gfx_RegisterBlendedTexture(gPhantomGanonLimbTex_00B380, sDecayTex, NULL);
    Gfx_RegisterBlendedTexture(gPhantomGanonEyeTex, sDecayTex, NULL);
    for (int i = 0; i < ARRAY_COUNT(sMouthTex_ci8_16x16); i++) {
        Gfx_RegisterBlendedTexture(sMouthTex_ci8_16x16[i], sDecayTex, NULL);
    }
}

void BossGanondrof_Destroy(Actor* thisx, PlayState* play) {
    s32 pad;
    BossGanondrof* this = (BossGanondrof*)thisx;

    osSyncPrintf("DT1\n");
    SkelAnime_Free(&this->skelAnime, play);
    Collider_DestroyCylinder(play, &this->colliderBody);
    Collider_DestroyCylinder(play, &this->colliderSpear);
    if (this->actor.params == GND_REAL_BOSS) {
        LightContext_RemoveLight(play, &play->lightCtx, this->lightNode);
    }

    osSyncPrintf("DT2\n");
}

void BossGanondrof_SetupIntro(BossGanondrof* this, PlayState* play) {
    Animation_PlayLoop(&this->skelAnime, &gPhantomGanonRidePoseAnim);
    this->actionFunc = BossGanondrof_Intro;
    this->work[GND_MASK_OFF] = true;
}

void BossGanondrof_Intro(BossGanondrof* this, PlayState* play) {
    s16 i;
    s32 pad;
    EnfHG* horse = (EnfHG*)this->actor.child;

    SkelAnime_Update(&this->skelAnime);
    this->actor.world.pos = horse->actor.world.pos;
    this->actor.shape.rot.y = this->actor.world.rot.y = horse->actor.world.rot.y;

    osSyncPrintf("SW %d------------------------------------------------\n", horse->bossGndSignal);

    if ((this->timers[1] != 0) && (this->timers[1] < 25)) {
        Vec3f pos;
        Vec3f vel = { 0.0f, 0.0f, 0.0f };
        Vec3f accel = { 0.0f, 0.0f, 0.0f };

        pos.x = this->bodyPartsPos[14].x + Rand_CenteredFloat(10.0f);
        pos.y = this->bodyPartsPos[14].y + Rand_ZeroFloat(-5.0f);
        pos.z = this->bodyPartsPos[14].z + Rand_CenteredFloat(10.0f) + 5.0f;
        accel.y = 0.03f;
        EffectSsKFire_Spawn(play, &pos, &vel, &accel, (s16)Rand_ZeroFloat(10.0f) + 5, 0);
    }

    if (this->timers[1] == 20) {
        this->work[GND_MASK_OFF] = false;
    }

    if (this->timers[1] == 30) {
        Sfx_PlaySfxAtPos(&sAudioVec, NA_SE_EN_FANTOM_TRANSFORM);
    }

    if (horse->bossGndSignal == FHG_LIGHTNING) {
        Animation_Change(&this->skelAnime, &gPhantomGanonMaskOnAnim, 0.5f, 0.0f,
                         Animation_GetLastFrame(&gPhantomGanonMaskOnAnim), ANIMMODE_ONCE_INTERP, 0.0f);
        this->timers[1] = 40;
    }

    if (horse->bossGndSignal == FHG_REAR) {
        Animation_MorphToPlayOnce(&this->skelAnime, &gPhantomGanonHorseRearingAnim, -3.0f);
    }

    if (horse->bossGndSignal == FHG_RIDE) {
        Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonRidePoseAnim, -13.0f);
    }

    if (horse->bossGndSignal == FHG_SPUR) {
        EnfHG* horseTemp;

        Animation_MorphToPlayOnce(&this->skelAnime, &gPhantomGanonRideSpearRaiseAnim, -7.0f);
        horseTemp = (EnfHG*)this->actor.child;
        Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_FHG_FIRE, this->spearTip.x, this->spearTip.y,
                           this->spearTip.z, 50, FHGFIRE_LIGHT_GREEN, 0, FHGFIRE_SPEAR_LIGHT);
        this->actor.child = &horseTemp->actor;
    }

    if (horse->bossGndSignal == FHG_FINISH) {
        Animation_MorphToPlayOnce(&this->skelAnime, &gPhantomGanonRideSpearResetAnim, -5.0f);
    }

    switch (this->work[GND_EYE_STATE]) {
        case GND_EYESTATE_FADE:
            this->fwork[GND_EYE_ALPHA] += 40.0f;
            if (this->fwork[GND_EYE_ALPHA] >= 255.0f) {
                this->fwork[GND_EYE_ALPHA] = 255.0f;
            }
            break;

        case GND_EYESTATE_BRIGHTEN:
            this->fwork[GND_EYE_BRIGHTNESS] += 20.0f;
            if (this->fwork[GND_EYE_BRIGHTNESS] > 255.0f) {
                this->fwork[GND_EYE_BRIGHTNESS] = 255.0f;
            }
            break;
    }

    this->armRotY = Math_SinS(this->work[GND_VARIANCE_TIMER] * 0x6E8) * 0;
    this->armRotZ = Math_CosS(this->work[GND_VARIANCE_TIMER] * 0x8DC) * 300.0f;
    for (i = 0; i < 30; i++) {
        this->rideRotY[i] = Math_SinS(this->work[GND_VARIANCE_TIMER] * ((i * 50) + 0x7B0)) * 100.0f;
        this->rideRotZ[i] = Math_CosS(this->work[GND_VARIANCE_TIMER] * ((i * 50) + 0x8DC)) * 100.0f;
    }

    if (horse->bossGndSignal == FHG_START_FIGHT) {
        BossGanondrof_SetupPaintings(this);
        for (i = 0; i < 30; i++) {
            this->rideRotY[i] = this->rideRotZ[i] = 0.0f;
        }
    }

    horse->bossGndSignal = FHG_NO_SIGNAL;
}

void BossGanondrof_SetupPaintings(BossGanondrof* this) {
    Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonRideAnim, -5.0f);
    this->actionFunc = BossGanondrof_Paintings;
}

void BossGanondrof_Paintings(BossGanondrof* this, PlayState* play) {
    EnfHG* horse = (EnfHG*)this->actor.child;

    osSyncPrintf("RUN 1\n");
    SkelAnime_Update(&this->skelAnime);
    osSyncPrintf("RUN 2\n");

    if (horse->bossGndSignal == FHG_RAISE_SPEAR) {
        EnfHG* horseTemp;

        Animation_MorphToPlayOnce(&this->skelAnime, &gPhantomGanonRideSpearRaiseAnim, -2.0f);
        this->actor.flags |= ACTOR_FLAG_ATTENTION_ENABLED;
        horseTemp = (EnfHG*)this->actor.child;
        Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_FHG_FIRE, this->spearTip.x, this->spearTip.y,
                           this->spearTip.z, 30, FHGFIRE_LIGHT_GREEN, 0, FHGFIRE_SPEAR_LIGHT);
        this->actor.child = &horseTemp->actor;
    } else if (horse->bossGndSignal == FHG_LIGHTNING) {
        Animation_MorphToPlayOnce(&this->skelAnime, &gPhantomGanonRideSpearStrikeAnim, -2.0f);
    } else if (horse->bossGndSignal == FHG_RESET) {
        Animation_MorphToPlayOnce(&this->skelAnime, &gPhantomGanonRideSpearResetAnim, -2.0f);
    } else if (horse->bossGndSignal == FHG_RIDE) {
        Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonRideAnim, -2.0f);
        this->actor.flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
    }

    osSyncPrintf("RUN 3\n");
    this->actor.world.pos = horse->actor.world.pos;
    this->actor.world.pos.y = horse->actor.world.pos.y;
    this->actor.shape.rot.y = this->actor.world.rot.y = horse->actor.world.rot.y;
    if (this->flyMode != GND_FLY_PAINTING) {
        BossGanondrof_SetupNeutral(this, -20.0f);
        this->timers[0] = 100;
        this->colliderBody.dim.radius = 20;
        this->colliderBody.dim.height = 60;
        this->colliderBody.dim.yShift = -33;
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_LAUGH);
        this->actor.naviEnemyId = 0x1A;
    } else {
        horse->bossGndSignal = FHG_NO_SIGNAL;
        this->actor.scale.x = horse->actor.scale.x / 1.15f;
        this->actor.scale.y = horse->actor.scale.y / 1.15f;
        this->actor.scale.z = horse->actor.scale.z / 1.15f;
        osSyncPrintf("RUN 4\n");
    }
}

// ---------- ZeldaOOTProject: catch-and-burst attack
typedef enum {
    ZOP_BURST_CATCH,  // the ball flies into his spear
    ZOP_BURST_CHARGE, // he takes it in, light streams into him
    ZOP_BURST_THROW   // magic explosion, the balls fly at Link
} ZopBurstState;

#define ZOP_BURST_BALLS 5
#define ZOP_BURST_CHARGE_FRAMES 22
#define ZOP_BURST_MIN_HITS 3   // rally hits before he may catch the ball
#define ZOP_BURST_CHANCE 0.4f  // chance to catch it then
#define ZOP_BURST_COOLDOWN 450 // frames before the next one (22 s)

static s16 sZopBurstState;
static s16 sZopBurstTimer;

static void BossGanondrof_ZopBurst(BossGanondrof* this, PlayState* play);

// ---------- ZeldaOOTProject: power hit
#define ZOP_CHARGE_CHANCE 0.2f // chance of his charge attack whenever he picks his next move

#define ZOP_POWER_MIN_HITS 2   // rally hits before he may charge his staff
#define ZOP_POWER_NEAR 250.0f  // distance to Link at which the chance per return of his is lowest
#define ZOP_POWER_FAR 600.0f   // ... and highest


static void BossGanondrof_ZopPurpleSparks(BossGanondrof* this, PlayState* play, s16 count, f32 spread, f32 speed) {
    static Color_RGBA8 prim = { 255, 200, 255, 255 };
    static Color_RGBA8 env = { 170, 40, 255, 255 };
    Vec3f accel = { 0.0f, 0.0f, 0.0f };
    s16 i;

    for (i = 0; i < count; i++) {
        Vec3f pos = { this->spearTip.x + Rand_CenteredFloat(spread), this->spearTip.y + Rand_CenteredFloat(spread),
                      this->spearTip.z + Rand_CenteredFloat(spread) };
        Vec3f vel = { Rand_CenteredFloat(speed), Rand_CenteredFloat(speed) + speed * 0.3f, Rand_CenteredFloat(speed) };

        EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 2500, 12);
    }
}

void BossGanondrof_ZopTryPowerUp(BossGanondrof* this, PlayState* play, s16 rallyHits) {
    EnfHG* horse = (EnfHG*)this->actor.child;

    // The farther Link is, the easier the rally, so the more likely the power hit: 20% up close, 80% from 600 on
    f32 chance = CLAMP((this->actor.xzDistToPlayer - ZOP_POWER_NEAR) / (ZOP_POWER_FAR - ZOP_POWER_NEAR), 0.2f, 0.8f);

    if (!ZOP_DifficultyEnabled() || (sZopPowerTimer > 0) || (rallyHits < ZOP_POWER_MIN_HITS) ||
        ((this->actionFunc != BossGanondrof_Neutral) && (this->actionFunc != BossGanondrof_Return)) ||
        (Rand_ZeroOne() > chance)) {
        return;
    }
    sZopPowerTimer = 1;
    sZopPowerRaised = true;
    // The staff goes up (see BossGanondrof_Update) and flashes once
    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_FHG_FIRE, this->spearTip.x, this->spearTip.y,
                       this->spearTip.z, 8, FHGFIRE_LIGHT_GREEN, 0, FHGFIRE_SPEAR_LIGHT);
    this->actor.child = &horse->actor;
    BossGanondrof_ZopPurpleSparks(this, play, 24, 20.0f, 10.0f);
    ZOP_GndRallyFlash(0.5f);
    sZopPowerFlashTimer = ZOP_POWER_FLASH_FRAMES;
    Audio_PlayActorSound2(&this->actor, NA_SE_EV_DIAMOND_SWITCH); // the same chime as Gohma's eye flash
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_STICK);
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_MASIC1);
}

// His return: true if his staff is charged (it stays charged until the rally is over)
s32 BossGanondrof_ZopUsePower(BossGanondrof* this, PlayState* play) {
    if (sZopPowerTimer <= 0) {
        return false;
    }
    sZopPowerRaised = false;
    BossGanondrof_ZopPurpleSparks(this, play, 30, 10.0f, 16.0f);
    ZOP_GndRallyFlash(0.9f);
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_THUNDER);
    return true;
}

// The rally is over (the ball is gone): the aura goes
void BossGanondrof_ZopEndPower(void) {
    sZopPowerTimer = 0;
    sZopPowerRaised = false;
}

// Each frame: the purple aura while charged (and the staff held up until his first charged return)
static void BossGanondrof_ZopPowerUpdate(BossGanondrof* this, PlayState* play) {
    static Color_RGBA8 boltPrim = { 255, 220, 255, 255 };
    static Color_RGBA8 boltEnv = { 170, 40, 255, 255 };

    if (sZopPowerTimer <= 0) {
        return;
    }
    if ((this->actionFunc != BossGanondrof_Neutral) && (this->actionFunc != BossGanondrof_Return)) {
        BossGanondrof_ZopEndPower(); // stunned, catching the ball, ...
        return;
    }
    if (sZopPowerRaised && (this->actionFunc == BossGanondrof_Neutral) &&
        (this->skelAnime.animation != &gPhantomGanonChargeWindupAnim)) {
        Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonChargeWindupAnim, -4.0f);
    }
    BossGanondrof_ZopPurpleSparks(this, play, 2, 50.0f, 2.0f);
    if ((this->work[GND_VARIANCE_TIMER] % 3) == 0) {
        Vec3f pos = { this->spearTip.x + Rand_CenteredFloat(30.0f), this->spearTip.y + Rand_CenteredFloat(30.0f),
                      this->spearTip.z + Rand_CenteredFloat(30.0f) };

        EffectSsLightning_Spawn(play, &pos, &boltPrim, &boltEnv, (s16)(30.0f + Rand_ZeroOne() * 25.0f),
                                (s16)(Rand_ZeroOne() * 0xFFFF), 4, 1);
    }
}

s32 BossGanondrof_ZopTryCatch(BossGanondrof* this, PlayState* play, s16 rallyHits) {
    // (not while his staff is charged: then he always hits the ball back, with his power hit)
    if (!ZOP_DifficultyEnabled() || (sZopBurstCooldown > 0) || (rallyHits < ZOP_BURST_MIN_HITS) ||
        (sZopPowerTimer > 0) ||
        (this->deathState != NOT_DEAD) || (this->actionFunc == BossGanondrof_Stunned) ||
        (Rand_ZeroOne() > ZOP_BURST_CHANCE)) {
        return false;
    }
    Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonBlockAnim, -3.0f);
    this->actionFunc = BossGanondrof_ZopBurst;
    this->flyMode = GND_FLY_NEUTRAL;
    sZopBurstState = ZOP_BURST_CATCH;
    sZopBurstTimer = 20; // fallback in case the ball never arrives
    sZopBurstCooldown = ZOP_BURST_COOLDOWN;
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_LAUGH);
    return true;
}

static void BossGanondrof_ZopStartCharge(BossGanondrof* this, PlayState* play) {
    EnfHG* horse = (EnfHG*)this->actor.child;

    sZopBurstState = ZOP_BURST_CHARGE;
    sZopBurstTimer = ZOP_BURST_CHARGE_FRAMES;
    Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonChargeWindupAnim, -3.0f);
    this->shockTimer = 12;
    // the glowing ball of light on his spear while he holds it
    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_FHG_FIRE, this->spearTip.x, this->spearTip.y,
                       this->spearTip.z, ZOP_BURST_CHARGE_FRAMES, FHGFIRE_LIGHT_GREEN, 0, FHGFIRE_SPEAR_LIGHT);
    this->actor.child = &horse->actor;
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_MASIC1);
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_STICK);
}

// Called by the caught ball when it has reached his spear
void BossGanondrof_ZopAbsorbed(BossGanondrof* this, PlayState* play) {
    if ((this->actionFunc == BossGanondrof_ZopBurst) && (sZopBurstState == ZOP_BURST_CATCH)) {
        BossGanondrof_ZopStartCharge(this, play);
    }
}

static u8 sZopBurstReturns; // this burst: he hits the balls back once more after Link returned them

s32 BossGanondrof_ZopBurstReturns(BossGanondrof* this) {
    // only while he is free to swing (not stunned, not in another attack)
    return sZopBurstReturns && ((this->actionFunc == BossGanondrof_Neutral) ||
                                (this->actionFunc == BossGanondrof_Return) ||
                                (this->actionFunc == BossGanondrof_ZopBurst));
}

static void BossGanondrof_ZopExplode(BossGanondrof* this, PlayState* play) {
    static Color_RGBA8 boltPrim = { 255, 255, 255, 255 };
    static Color_RGBA8 boltEnv = { 120, 255, 120, 255 };
    Player* player = GET_PLAYER(play);
    EnfHG* horse = (EnfHG*)this->actor.child;
    s16 yaw = Math_Vec3f_Yaw(&this->spearTip, &player->actor.world.pos);
    s16 perp = yaw + 0x4000;
    Vec3f accel = { 0.0f, -0.1f, 0.0f };
    s16 i;

    sZopBurstState = ZOP_BURST_THROW;
    sZopBurstReturns = Rand_ZeroOne() < 0.3f;
    this->fwork[GND_END_FRAME] = Animation_GetLastFrame(&gPhantomGanonThrowEndAnim);
    Animation_MorphToPlayOnce(&this->skelAnime, &gPhantomGanonThrowEndAnim, 0.0f);
    this->shockTimer = 10;

    // The balls, in a ring around his spear, all aimed at Link (they close in on him)
    for (i = 0; i < ZOP_BURST_BALLS; i++) {
        s16 ang = i * (0x10000 / ZOP_BURST_BALLS) + 0x4000;
        f32 side = Math_CosS(ang) * 70.0f;
        f32 up = Math_SinS(ang) * 55.0f;
        Actor* ball = Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_FHG_FIRE,
                                         this->spearTip.x + Math_SinS(perp) * side, this->spearTip.y + up,
                                         this->spearTip.z + Math_CosS(perp) * side, 0, 0, GND_ZOP_BURST_BALL,
                                         FHGFIRE_ENERGY_BALL);

        if (ball != NULL) {
            ball->speedXZ = 14.0f;
        }
    }
    this->actor.child = &horse->actor;

    // Magic explosion: bright flash, bolts in all directions, a cloud of light, thunder
    for (i = 0; i < 40; i++) {
        Vec3f vel = { Rand_CenteredFloat(24.0f), Rand_CenteredFloat(24.0f), Rand_CenteredFloat(24.0f) };

        EffectSsFhgFlash_SpawnLightBall(play, &this->spearTip, &vel, &accel, (s16)(Rand_ZeroOne() * 120.0f) + 160,
                                        FHGFLASH_LIGHTBALL_GREEN);
    }
    for (i = 0; i < 8; i++) {
        Vec3f pos = this->spearTip;

        EffectSsLightning_Spawn(play, &pos, &boltPrim, &boltEnv, (s16)(220 + Rand_ZeroOne() * 120.0f),
                                (s16)(i * 0x2000 + Rand_CenteredFloat(0x1000)), 7, 3);
    }
    ZOP_GndRallyFlash(2.2f);
    Actor_RequestQuakeAndRumble(&this->actor, play, 4, 10);
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_THUNDER);
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_HIT_THUNDER);
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_VOICE);
}

static void BossGanondrof_ZopBurst(BossGanondrof* this, PlayState* play) {
    SkelAnime_Update(&this->skelAnime);
    this->actor.world.pos.x += this->actor.velocity.x;
    this->actor.world.pos.z += this->actor.velocity.z;
    Math_ApproachZeroF(&this->actor.velocity.x, 1.0f, 0.5f);
    Math_ApproachZeroF(&this->actor.velocity.z, 1.0f, 0.5f);
    this->actor.world.pos.y += 2.0f * Math_SinS(this->work[GND_VARIANCE_TIMER] * 1500);
    Math_ApproachS(&this->actor.shape.rot.y, this->actor.yawTowardsPlayer, 5, 0x7D0);
    if (sZopBurstTimer > 0) {
        sZopBurstTimer--;
    }

    switch (sZopBurstState) {
        case ZOP_BURST_CATCH:
            if (sZopBurstTimer == 0) {
                BossGanondrof_ZopStartCharge(this, play);
            }
            break;
        case ZOP_BURST_CHARGE: {
            // Light streams into him from all around, the room flickers brighter and brighter
            Vec3f accel = { 0.0f, 0.0f, 0.0f };
            f32 progress = 1.0f - (f32)sZopBurstTimer / ZOP_BURST_CHARGE_FRAMES;
            s16 i;

            for (i = 0; i < 3; i++) {
                Vec3f pos;
                Vec3f vel;
                s16 yaw = (s16)(Rand_ZeroOne() * 0xFFFF);
                s16 pitch = (s16)Rand_CenteredFloat(0x8000);
                f32 r = 90.0f + Rand_ZeroOne() * 40.0f;

                pos.x = this->spearTip.x + Math_SinS(yaw) * Math_CosS(pitch) * r;
                pos.y = this->spearTip.y + Math_SinS(pitch) * r;
                pos.z = this->spearTip.z + Math_CosS(yaw) * Math_CosS(pitch) * r;
                vel.x = (this->spearTip.x - pos.x) / 7.0f;
                vel.y = (this->spearTip.y - pos.y) / 7.0f;
                vel.z = (this->spearTip.z - pos.z) / 7.0f;
                EffectSsFhgFlash_SpawnLightBall(play, &pos, &vel, &accel, (s16)(Rand_ZeroOne() * 60.0f) + 120,
                                                (i == 0) ? FHGFLASH_LIGHTBALL_LIGHTBLUE : FHGFLASH_LIGHTBALL_GREEN);
            }
            if ((sZopBurstTimer & 3) == 0) {
                ZOP_GndRallyFlash(0.2f + 0.6f * progress);
            }
            if (sZopBurstTimer == 0) {
                BossGanondrof_ZopExplode(this, play);
            }
            break;
        }
        case ZOP_BURST_THROW:
            if (Animation_OnFrame(&this->skelAnime, this->fwork[GND_END_FRAME])) {
                BossGanondrof_SetupNeutral(this, -6.0f);
                this->flyMode = GND_FLY_NEUTRAL;
                this->timers[0] = 100; // let the balls fly first
            }
            break;
    }
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_FLOAT - SFX_FLAG);
}

// ---------- ZeldaOOTProject: floor shots
static void BossGanondrof_ZopFloorShots(BossGanondrof* this, PlayState* play);

static void BossGanondrof_ZopSetupFloorShots(BossGanondrof* this, PlayState* play) {
    EnfHG* horse = (EnfHG*)this->actor.child;

    this->actionFunc = BossGanondrof_ZopFloorShots;
    sZopShotsLeft = 2 + (s16)(Rand_ZeroOne() * 2.99f);
    sZopShotTimer = 0;
    sZopShotAngle = (s16)(Rand_ZeroOne() * 0xFFFF);
    sZopShotDist = -1.0f; // none yet
    // the big ball, thrown up over the middle of the room
    this->fwork[GND_END_FRAME] = Animation_GetLastFrame(&gPhantomGanonThrowEndAnim);
    Animation_MorphToPlayOnce(&this->skelAnime, &gPhantomGanonThrowEndAnim, -2.0f);
    sZopMotherBall = Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_FHG_FIRE, this->spearTip.x,
                                        this->spearTip.y, this->spearTip.z, 0, 0, GND_ZOP_MOTHER_BALL,
                                        FHGFIRE_ENERGY_BALL);
    this->actor.child = &horse->actor;
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_MASIC1);
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_VOICE);
}

// The big ball is gone: a burst of light where it was
static void BossGanondrof_ZopEndMotherBall(PlayState* play) {
    if (sZopMotherBall != NULL) {
        Vec3f accel = { 0.0f, -0.3f, 0.0f };
        s16 i;

        for (i = 0; i < 30; i++) {
            Vec3f vel = { Rand_CenteredFloat(18.0f), Rand_CenteredFloat(18.0f), Rand_CenteredFloat(18.0f) };

            EffectSsFhgFlash_SpawnLightBall(play, &sZopMotherBall->world.pos, &vel, &accel,
                                            (s16)(Rand_ZeroOne() * 80.0f) + 120, FHGFLASH_LIGHTBALL_GREEN);
        }
        Audio_PlayActorSound2(sZopMotherBall, NA_SE_EN_FANTOM_THUNDER);
        ZOP_GndRallyLight(0.0f);
        Actor_Kill(sZopMotherBall);
        sZopMotherBall = NULL;
    }
}

static void BossGanondrof_ZopFloorShots(BossGanondrof* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    Actor* mother = sZopMotherBall;
    Vec3f motherTarget = { GND_BOSSROOM_CENTER_X, GND_BOSSROOM_CENTER_Y + GND_ZOP_MOTHER_HEIGHT,
                           GND_BOSSROOM_CENTER_Z };

    SkelAnime_Update(&this->skelAnime);
    if (Animation_OnFrame(&this->skelAnime, this->fwork[GND_END_FRAME])) {
        Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonNeutralAnim, -6.0f);
    }
    // he just floats about and watches
    this->actor.world.pos.x += this->actor.velocity.x + Math_SinS(this->work[GND_VARIANCE_TIMER] * 0x300) * 1.2f;
    this->actor.world.pos.z += this->actor.velocity.z + Math_CosS(this->work[GND_VARIANCE_TIMER] * 0x380) * 1.2f;
    Math_ApproachZeroF(&this->actor.velocity.x, 1.0f, 0.5f);
    Math_ApproachZeroF(&this->actor.velocity.z, 1.0f, 0.5f);
    this->actor.world.pos.y += 2.0f * Math_SinS(this->work[GND_VARIANCE_TIMER] * 1500);
    Math_ApproachS(&this->actor.shape.rot.y, this->actor.yawTowardsPlayer, 5, 0x7D0);
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_FLOAT - SFX_FLAG);

    if ((mother == NULL) || (mother->update == NULL)) {
        sZopMotherBall = NULL;
        BossGanondrof_SetupNeutral(this, -6.0f);
        this->flyMode = GND_FLY_NEUTRAL;
        this->timers[0] = 30;
        return;
    }
    // the big ball lights the whole room up in a bright, flickering haze
    ZOP_GndRallyLight(0.45f);
    if (Rand_ZeroOne() < 0.25f) {
        ZOP_GndRallyFlash(0.05f + Rand_ZeroOne() * 0.12f);
    }
    // wait until it has arrived over the middle of the room
    if ((sZopShotsLeft > 0) && (Math_Vec3f_DistXYZ(&mother->world.pos, &motherTarget) > 25.0f)) {
        sZopShotTimer = 15;
        return;
    }
    if (sZopShotTimer > 0) {
        sZopShotTimer--;
        return;
    }
    if (sZopShotsLeft <= 0) {
        BossGanondrof_ZopEndMotherBall(play);
        BossGanondrof_SetupNeutral(this, -6.0f);
        this->flyMode = GND_FLY_NEUTRAL;
        this->timers[0] = 30;
        return;
    }

    // A shot from the big ball into the floor, each one at a clearly different place than the one before: on the
    // opposite side of the arena (far left after far right, far back after far front, ...), and near the middle
    // after near the edge and the other way round
    {
        EnfHG* horse = (EnfHG*)this->actor.child;
        Vec3f spot;
        f32 dx;
        f32 dy;
        f32 dz;
        Actor* ball;

        // (no longer as far apart as possible: a quarter to a third of a turn on, at any distance)
        if (sZopShotDist >= 0.0f) {
            sZopShotAngle += ((Rand_ZeroOne() < 0.5f) ? -1 : 1) * (s16)(0x2000 + Rand_ZeroOne() * 0x2000);
        }
        sZopShotDist = 80.0f + Rand_ZeroOne() * 190.0f;
        spot.x = GND_BOSSROOM_CENTER_X + Math_SinS(sZopShotAngle) * sZopShotDist;
        spot.y = GND_BOSSROOM_CENTER_Y;
        spot.z = GND_BOSSROOM_CENTER_Z + Math_CosS(sZopShotAngle) * sZopShotDist;
        ball = Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_FHG_FIRE, mother->world.pos.x,
                                  mother->world.pos.y, mother->world.pos.z, 0, 0,
                                  (Rand_ZeroOne() < 0.4f) ? GND_ZOP_LANE_BALL : 0, FHGFIRE_ENERGY_BALL);
        this->actor.child = &horse->actor;
        if (ball != NULL) {
            dx = spot.x - ball->world.pos.x;
            dy = spot.y - ball->world.pos.y;
            dz = spot.z - ball->world.pos.z;
            ball->world.rot.y = (s16)(Math_FAtan2F(dx, dz) * (0x8000 / M_PI));
            ball->world.rot.x = (s16)(Math_FAtan2F(dy, sqrtf(SQ(dx) + SQ(dz))) * (0x8000 / M_PI));
            ball->speedXZ = 11.0f;
        }
        Audio_PlayActorSound2(mother, NA_SE_EN_FANTOM_MASIC2);
        sZopShotsLeft--;
        sZopShotTimer = (sZopShotsLeft > 0) ? ZOP_FLOORSHOT_INTERVAL + (s16)Rand_CenteredFloat(8.0f) : 40;
    }
}

void BossGanondrof_SetupNeutral(BossGanondrof* this, f32 arg1) {
    Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonNeutralAnim, arg1);
    this->actionFunc = BossGanondrof_Neutral;
    this->actor.flags |= ACTOR_FLAG_ATTENTION_ENABLED;
    this->fwork[GND_FLOAT_SPEED] = 0.0f;
    this->timers[0] = (s16)(Rand_ZeroOne() * 64.0f) + 30;
}

void BossGanondrof_Neutral(BossGanondrof* this, PlayState* play) {
    f32 targetX;
    f32 targetY;
    f32 targetZ;
    Player* player = GET_PLAYER(play);
    Actor* playerx = &player->actor;
    Actor* thisx = &this->actor;
    f32 rand01;
    s16 i;

    SkelAnime_Update(&this->skelAnime);
    switch (this->flyMode) {
        case GND_FLY_NEUTRAL:
            if (this->timers[0] == 0) {
                this->timers[0] = (s16)(Rand_ZeroOne() * 64.0f) + 30;
                rand01 = Rand_ZeroOne();
                if (ZOP_DifficultyEnabled() && (rand01 < ZOP_CHARGE_CHANCE) && !ZOP_TestGanondrofSlam()) {
                    // ZeldaOOTProject: he flies at Link a lot more often (vanilla: only below 5 health)
                    this->flyMode = GND_FLY_CHARGE;
                    this->timers[0] = 60;
                    this->fwork[GND_FLOAT_SPEED] = 0.0f;
                    Audio_PlayActorSound2(thisx, NA_SE_EN_FANTOM_LAUGH);
                } else if (ZOP_DifficultyEnabled() &&
                           ((rand01 < ZOP_CHARGE_CHANCE + ZOP_SLAM_CHANCE) || (ZOP_TestGanondrofSlam() == 1)) &&
                           (ZOP_TestGanondrofSlam() != 2)) {
                    // ZeldaOOTProject: the staff slam with its rings, without a charge before it; afterwards he is open
                    // to attacks for a moment
                    BossGanondrof_ZopSetupFinale(this, play);
                } else if (ZOP_DifficultyEnabled() &&
                           ((rand01 < ZOP_CHARGE_CHANCE + ZOP_SLAM_CHANCE + ZOP_FLOORSHOT_CHANCE) || (ZOP_TestGanondrofSlam() == 2))) {
                    // ZeldaOOTProject: balls into the floor, rings to jump over
                    BossGanondrof_ZopSetupFloorShots(this, play);
                } else if (thisx->colChkInfo.health < 5) {
                    if (rand01 < 0.25f) {
                        BossGanondrof_SetupThrow(this, play);
                    } else if (rand01 >= 0.8f) {
                        this->flyMode = GND_FLY_CHARGE;
                        this->timers[0] = 60;
                        this->fwork[GND_FLOAT_SPEED] = 0.0f;
                        Audio_PlayActorSound2(thisx, NA_SE_EN_FANTOM_LAUGH);
                    } else {
                        this->flyMode = GND_FLY_VOLLEY;
                        this->timers[0] = 60;
                        this->fwork[GND_FLOAT_SPEED] = 0.0f;
                        Audio_PlayActorSound2(thisx, NA_SE_EN_FANTOM_LAUGH);
                    }
                } else if ((rand01 < 0.5f) || (this->work[GND_THROW_COUNT] < 5)) {
                    BossGanondrof_SetupThrow(this, play);
                } else {
                    this->flyMode = GND_FLY_VOLLEY;
                    this->timers[0] = 60;
                    this->fwork[GND_FLOAT_SPEED] = 0.0f;
                    Audio_PlayActorSound2(thisx, NA_SE_EN_FANTOM_LAUGH);
                }
            }

            if (this->timers[1] != 0) {
                targetX = GND_BOSSROOM_CENTER_X;
                targetZ = GND_BOSSROOM_CENTER_Z;
            } else {
                // ZeldaOOTProject: keeps more distance to Link (400 instead of 180 in front of him) and may hover
                // out to 400 from the middle of the room (the walls are at ~410-440)
                targetX = playerx->world.pos.x + (ZOP_HARD(180.0f, 400.0f) * Math_SinS(playerx->shape.rot.y));
                targetZ = playerx->world.pos.z + (ZOP_HARD(180.0f, 400.0f) * Math_CosS(playerx->shape.rot.y));
                if (sqrtf(SQ(targetX - GND_BOSSROOM_CENTER_X) + SQ(targetZ - GND_BOSSROOM_CENTER_Z)) >
                    ZOP_HARD(280.0f, 400.0f)) {
                    this->timers[1] = 50;
                    this->fwork[GND_FLOAT_SPEED] = 0.0f;
                }
            }

            // (ZeldaOOTProject: a bit lower, so he stays in the picture)
            targetY = playerx->world.pos.y + (ZOP_DifficultyEnabled() ? 75.0f : 100.0f);
            targetX += Math_SinS(this->work[GND_VARIANCE_TIMER] * 0x500) * 100.0f;
            targetZ += Math_CosS(this->work[GND_VARIANCE_TIMER] * 0x700) * 100.0f;
            break;
        case GND_FLY_VOLLEY:
            targetX = GND_BOSSROOM_CENTER_X - 14.0f;
            targetZ = GND_BOSSROOM_CENTER_Z + 265.0f;

            targetY = playerx->world.pos.y + (ZOP_DifficultyEnabled() ? 150.0f : 200.0f);
            targetX += Math_SinS(this->work[GND_VARIANCE_TIMER] * 0x500) * 100.0f;
            targetZ += Math_CosS(this->work[GND_VARIANCE_TIMER] * 0x700) * 100.0f;
            if (this->timers[0] == 0) {
                this->flyMode = GND_FLY_RETURN;
                this->returnSuccess = false;
                BossGanondrof_SetupThrow(this, play);
                this->timers[0] = 80;
            }
            break;
        case GND_FLY_RETURN:
            targetX = GND_BOSSROOM_CENTER_X - 14.0f;
            targetZ = GND_BOSSROOM_CENTER_Z + 265.0f;

            targetY = playerx->world.pos.y + (ZOP_DifficultyEnabled() ? 150.0f : 200.0f);
            targetX += Math_SinS(this->work[GND_VARIANCE_TIMER] * 0x500) * 50.0f;
            targetZ += Math_CosS(this->work[GND_VARIANCE_TIMER] * 0x700) * 50.0f;
            if (this->returnSuccess) {
                this->returnSuccess = false;
                BossGanondrof_SetupReturn(this, play);
                this->timers[0] = 80;
            }

            if (this->timers[0] == 0) {
                this->flyMode = GND_FLY_NEUTRAL;
            }
            break;
        case GND_FLY_CHARGE:
            targetX = GND_BOSSROOM_CENTER_X - 14.0f;
            targetZ = GND_BOSSROOM_CENTER_Z + 215.0f;

            targetY = playerx->world.pos.y + (ZOP_DifficultyEnabled() ? 115.0f : 150.0f);
            targetX += Math_SinS(this->work[GND_VARIANCE_TIMER] * 0x500) * 100.0f;
            targetZ += Math_CosS(this->work[GND_VARIANCE_TIMER] * 0x700) * 100.0f;
            if (this->timers[0] == 0) {
                BossGanondrof_SetupCharge(this, play);
            }
            break;
    }

    Math_ApproachF(&thisx->world.pos.x, targetX, 0.05f, this->fwork[GND_FLOAT_SPEED]);
    if (this->timers[2] != 0) {
        Math_ApproachF(&thisx->world.pos.y, targetY + 100.0f, 0.1f, 50.0f);
    } else {
        Math_ApproachF(&thisx->world.pos.y, targetY, 0.05f, 10.0f);
    }

    Math_ApproachF(&thisx->world.pos.z, targetZ, 0.05f, this->fwork[GND_FLOAT_SPEED]);
    Math_ApproachF(&this->fwork[GND_FLOAT_SPEED], 50.0f, 1.0f, 0.5f);
    thisx->velocity.x = thisx->world.pos.x - thisx->prevPos.x;
    thisx->velocity.z = thisx->world.pos.z - thisx->prevPos.z;
    thisx->world.pos.y += 2.0f * Math_SinS(this->work[GND_VARIANCE_TIMER] * 1500);
    Math_ApproachS(&thisx->shape.rot.y, thisx->yawTowardsPlayer, 5, 0xBB8);
    if ((this->work[GND_VARIANCE_TIMER] & 1) == 0) {
        Vec3f pos;
        Vec3f vel = { 0.0f, 0.0f, 0.0f };
        Vec3f accel = { 0.0f, 0.0f, 0.0f };

        for (i = 0; i < 3; i++) {
            pos.x = Rand_CenteredFloat(20.0f) + this->spearTip.x;
            pos.y = Rand_CenteredFloat(20.0f) + this->spearTip.y;
            pos.z = Rand_CenteredFloat(20.0f) + this->spearTip.z;
            accel.y = -0.08f;
            EffectSsFhgFlash_SpawnLightBall(play, &pos, &vel, &accel, (s16)(Rand_ZeroOne() * 80.0f) + 150,
                                            FHGFLASH_LIGHTBALL_GREEN);
        }
    }

    if (player->unk_A73 != 0) {
        BossGanondrof_SetupBlock(this, play);
    }

    Audio_PlayActorSound2(thisx, NA_SE_EN_FANTOM_FLOAT - SFX_FLAG);
}

void BossGanondrof_SetupThrow(BossGanondrof* this, PlayState* play) {
    EnfHG* horseTemp;
    s16 lightTime;

    this->fwork[GND_END_FRAME] = Animation_GetLastFrame(&gPhantomGanonThrowAnim);
    Animation_MorphToPlayOnce(&this->skelAnime, &gPhantomGanonThrowAnim, -5.0f);
    this->actionFunc = BossGanondrof_Throw;
    if ((Rand_ZeroOne() <= 0.1f) && (this->work[GND_THROW_COUNT] >= 10) && (this->flyMode == GND_FLY_NEUTRAL)) {
        this->work[GND_ACTION_STATE] = THROW_SLOW;
        this->work[GND_THROW_FRAME] = 1000;
        lightTime = 32;
    } else {
        this->work[GND_ACTION_STATE] = THROW_NORMAL;
        this->work[GND_THROW_FRAME] = 25;
        lightTime = 25;
    }

    horseTemp = (EnfHG*)this->actor.child;
    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_FHG_FIRE, this->spearTip.x, this->spearTip.y,
                       this->spearTip.z, lightTime, FHGFIRE_LIGHT_GREEN, 0, FHGFIRE_SPEAR_LIGHT);
    this->actor.child = &horseTemp->actor;
    this->work[GND_THROW_COUNT]++;
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_STICK);
}

void BossGanondrof_Throw(BossGanondrof* this, PlayState* play) {
    SkelAnime_Update(&this->skelAnime);
    osSyncPrintf("this->fwork[GND_END_FRAME] = %d\n", (s16)this->fwork[GND_END_FRAME]);
    osSyncPrintf("this->work[GND_SHOT_FRAME] = %d\n", this->work[GND_THROW_FRAME]);
    if (Animation_OnFrame(&this->skelAnime, this->fwork[GND_END_FRAME])) {
        BossGanondrof_SetupNeutral(this, -6.0f);
    }

    if ((this->work[GND_ACTION_STATE] != THROW_NORMAL) && Animation_OnFrame(&this->skelAnime, 21.0f)) {
        this->fwork[GND_END_FRAME] = Animation_GetLastFrame(&gPhantomGanonThrowEndAnim);
        Animation_MorphToPlayOnce(&this->skelAnime, &gPhantomGanonThrowEndAnim, 0.0f);
        this->work[GND_THROW_FRAME] = 10;
    }

    if (Animation_OnFrame(&this->skelAnime, this->work[GND_THROW_FRAME])) {
        if (this->flyMode <= GND_FLY_NEUTRAL) {
            Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_MASIC2);
        } else {
            Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_MASIC1);
        }

        Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_VOICE);
    }

    if (Animation_OnFrame(&this->skelAnime, this->work[GND_THROW_FRAME])) {
        EnfHG* horseTemp = (EnfHG*)this->actor.child;

        Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_FHG_FIRE, this->spearTip.x, this->spearTip.y,
                           this->spearTip.z, this->work[GND_ACTION_STATE], 0, 0, FHGFIRE_ENERGY_BALL);
        this->actor.child = &horseTemp->actor;
    }

    Math_ApproachS(&this->actor.shape.rot.y, this->actor.yawTowardsPlayer, 5, 0x7D0);
    this->actor.world.pos.x += this->actor.velocity.x;
    this->actor.world.pos.z += this->actor.velocity.z;
    Math_ApproachZeroF(&this->actor.velocity.x, 1.0f, 0.5f);
    Math_ApproachZeroF(&this->actor.velocity.z, 1.0f, 0.5f);
    this->actor.world.pos.y += 2.0f * Math_SinS(this->work[GND_VARIANCE_TIMER] * 1500);
}

void BossGanondrof_SetupReturn(BossGanondrof* this, PlayState* play) {
    static AnimationHeader* returnAnim[] = { &gPhantomGanonReturn1Anim, &gPhantomGanonReturn2Anim };
    s16 rand = Rand_ZeroOne() * 1.99f;

    this->fwork[GND_END_FRAME] = Animation_GetLastFrame(returnAnim[rand]);
    Animation_MorphToPlayOnce(&this->skelAnime, returnAnim[rand], 0.0f);
    this->actionFunc = BossGanondrof_Return;
}

void BossGanondrof_Return(BossGanondrof* this, PlayState* play) {
    SkelAnime_Update(&this->skelAnime);
    if (Animation_OnFrame(&this->skelAnime, 5.0f)) {
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_VOICE);
        osSyncPrintf("VOISE               2  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        osSyncPrintf("VOISE               2  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    }

    if (Animation_OnFrame(&this->skelAnime, this->fwork[GND_END_FRAME])) {
        BossGanondrof_SetupNeutral(this, 0.0f);
    }

    this->actor.world.pos.x += this->actor.velocity.x;
    this->actor.world.pos.z += this->actor.velocity.z;
    Math_ApproachZeroF(&this->actor.velocity.x, 1.0f, 0.5f);
    Math_ApproachZeroF(&this->actor.velocity.z, 1.0f, 0.5f);
    this->actor.world.pos.y += 2.0f * Math_SinS(this->work[GND_VARIANCE_TIMER] * 1500);
    if (this->returnSuccess) {
        this->returnSuccess = false;
        BossGanondrof_SetupReturn(this, play);
        this->timers[0] = 80;
    }
}

// ZeldaOOTProject: the red aura on the floor and its shockwave
static void BossGanondrof_ZopAura(BossGanondrof* this, PlayState* play) {
    static f32 sHumFreq;
    static f32 sHumVolume = 1.0f;
    static s8 sHumReverb = 0;
    static Color_RGBA8 prim = { 255, 120, 80, 255 };
    static Color_RGBA8 env = { 255, 0, 0, 255 };
    Player* player = GET_PLAYER(play);
    f32 intensity;
    s16 period;
    s16 i;

    if (sZopAuraTimer < 0) {
        if (sZopAuraDelay != 0) {
            if (sZopAuraDelay > 0) {
                sZopAuraDelay--;
            }
            return;
        }
        sZopAuraTimer = 0;
        if (this->timers[0] < ZOP_AURA_FRAMES + 5) {
            this->timers[0] = ZOP_AURA_FRAMES + 5; // he stays down until it bursts
        }
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_VOICE);
    }
    if (sZopAuraTimer > ZOP_AURA_FRAMES) {
        return;
    }
    intensity = (f32)sZopAuraTimer / ZOP_AURA_FRAMES;

    // red sparks rising around him, more and more of them
    for (i = 0; i < 1 + (s16)(intensity * 5.0f); i++) {
        f32 ang = Rand_ZeroOne() * 2.0f * M_PI;
        f32 r = 30.0f + Rand_ZeroOne() * (30.0f + intensity * 40.0f);
        Vec3f pos = { this->actor.world.pos.x + sinf(ang) * r, this->actor.world.pos.y + Rand_ZeroOne() * 90.0f,
                      this->actor.world.pos.z + cosf(ang) * r };
        Vec3f vel = { 0.0f, 1.5f + intensity * 3.0f, 0.0f };
        Vec3f accel = { 0.0f, 0.1f, 0.0f };

        EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 2500 + (s16)(intensity * 2500.0f),
                                        14);
    }
    // his body pulses red, faster and faster
    period = 12 - (s16)(intensity * 10.0f);
    sZopAuraRed = (sZopAuraTimer % period) < (period / 2 + 1);
    // a rising hum (the charging shield of the Twinrova fight, has to be triggered every frame)
    sHumFreq = 0.6f + intensity * 1.1f;
    Audio_PlaySfxGeneral((intensity < 0.5f) ? (NA_SE_IT_SHIELD_CHARGE_LV1 & ~SFX_FLAG)
                                            : (NA_SE_IT_SHIELD_CHARGE_LV2 & ~SFX_FLAG),
                         &this->actor.projectedPos, 4, &sHumFreq, &sHumVolume, &sHumReverb);

    if (sZopAuraTimer == ZOP_AURA_FRAMES) {
        // the shockwave
        f32 dx = player->actor.world.pos.x - this->actor.world.pos.x;
        f32 dz = player->actor.world.pos.z - this->actor.world.pos.z;

        sZopAuraRed = false;
        Actor_Spawn(&play->actorCtx, play, ACTOR_EN_FHG_FIRE, this->actor.world.pos.x, this->actor.world.pos.y + 20.0f,
                    this->actor.world.pos.z, 250, 0, 0, FHGFIRE_LIGHTNING_BURST);
        ZOP_SpawnGndRing(play, this->actor.world.pos.x, this->actor.world.pos.y - 38.0f, this->actor.world.pos.z,
                         ZOP_AURA_RADIUS + 40.0f, 14.0f);
        if ((SQ(dx) + SQ(dz) < SQ(ZOP_AURA_RADIUS)) && (player->invincibilityTimer == 0) &&
            (gSaveContext.nayrusLoveTimer == 0)) {
            Actor_SetPlayerKnockback(play, &this->actor, 10.0f, (s16)(Math_FAtan2F(dx, dz) * (0x8000 / M_PI)), 7.0f,
                                     PLAYER_KNOCKBACK_LARGE, ZOP_AURA_DAMAGE);
        }
        ZOP_GndRallyFlash(1.0f);
        Actor_RequestQuakeAndRumble(&this->actor, play, 5, 10);
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_HIT_THUNDER);
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_THUNDER_GND);
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_LAUGH);
        this->timers[0] = 12; // and up he gets
    }
    sZopAuraTimer++;
}

void BossGanondrof_SetupStunned(BossGanondrof* this, PlayState* play) {
    BossGanondrof_ZopEndMotherBall(play);
    if ((this->actionFunc == BossGanondrof_Stunned) && sZopAuraPlanned && (sZopAuraTimer < 0) &&
        (sZopAuraDelay < 0)) {
        // ZeldaOOTProject: Link's first hit while he is down sets off the red aura, sometimes at once, sometimes a
        // little later; he stays down long enough for it to burst
        sZopAuraDelay = 1 + (s16)(Rand_ZeroOne() * 40.0f);
        if (this->timers[0] < sZopAuraDelay + ZOP_AURA_FRAMES + 8) {
            this->timers[0] = sZopAuraDelay + ZOP_AURA_FRAMES + 8;
        }
    }
    if (this->actionFunc != BossGanondrof_Stunned) {
        sZopAuraPlanned = ZOP_DifficultyEnabled() && (Rand_ZeroOne() < ZOP_AURA_CHANCE);
        sZopAuraDelay = -1; // set by Link's first hit while he is down
        if (sZopAuraPlanned && (this->actionFunc == BossGanondrof_ZopFinale)) {
            // hit while kneeling after his staff slam: that already is the first hit
            sZopAuraDelay = 1 + (s16)(Rand_ZeroOne() * 40.0f);
            this->timers[0] = sZopAuraDelay + ZOP_AURA_FRAMES + 8;
        }
        sZopAuraTimer = -1;
        this->fwork[GND_END_FRAME] = Animation_GetLastFrame(&gPhantomGanonAirDamageAnim);
        Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonAirDamageAnim, 0.0f);
        this->timers[0] = 50;
        this->shockTimer = 60;
    } else {
        this->fwork[GND_END_FRAME] = Animation_GetLastFrame(&gPhantomGanonGroundDamageAnim);
        Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonGroundDamageAnim, 0.0f);
    }

    this->actionFunc = BossGanondrof_Stunned;
    this->work[GND_ACTION_STATE] = STUNNED_FALL;
    this->actor.velocity.x = 0.0f;
    this->actor.velocity.z = 0.0f;
}

void BossGanondrof_Stunned(BossGanondrof* this, PlayState* play) {
    osSyncPrintf("DAMAGE   .................................\n");
    SkelAnime_Update(&this->skelAnime);
    this->actor.gravity = -0.2f;
    if (this->actor.world.pos.y <= 5.0f) {
        if (this->work[GND_ACTION_STATE] == STUNNED_FALL) {
            this->fwork[GND_END_FRAME] = Animation_GetLastFrame(&gPhantomGanonStunnedAnim);
            Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonStunnedAnim, -10.0f);
            this->work[GND_ACTION_STATE] = STUNNED_GROUND;
        }

        this->actor.velocity.y = 0.0f;
        this->actor.gravity = 0.0f;
        if (Animation_OnFrame(&this->skelAnime, this->fwork[GND_END_FRAME])) {
            Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_DAMAGE2);
        }
        if (sZopAuraPlanned) {
            BossGanondrof_ZopAura(this, play);
        }

        this->actor.flags |= ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER;
    }

    osSyncPrintf("TIME0 %d ********************************************\n", this->timers[0]);
    if (this->timers[0] == 0) {
        BossGanondrof_SetupNeutral(this, -5.0f);
        this->timers[0] = 30;
        this->timers[2] = 30;
        this->flyMode = GND_FLY_NEUTRAL;
        this->actor.velocity.y = 0.0f;
        this->actor.gravity = 0.0f;
    }

    Actor_MoveXZGravity(&this->actor);
}

void BossGanondrof_SetupBlock(BossGanondrof* this, PlayState* play) {
    this->fwork[GND_END_FRAME] = Animation_GetLastFrame(&gPhantomGanonBlockAnim);
    Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonBlockAnim, -3.0f);
    this->actionFunc = BossGanondrof_Block;
    this->timers[0] = 10;
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_STICK);
}

void BossGanondrof_Block(BossGanondrof* this, PlayState* play) {
    this->colliderBody.base.colType = COLTYPE_METAL;
    SkelAnime_Update(&this->skelAnime);
    this->actor.world.pos.x += this->actor.velocity.x;
    this->actor.world.pos.z += this->actor.velocity.z;
    Math_ApproachZeroF(&this->actor.velocity.x, 1.0f, 0.5f);
    Math_ApproachZeroF(&this->actor.velocity.z, 1.0f, 0.5f);
    this->actor.world.pos.y += 2.0f * Math_SinS(this->work[GND_VARIANCE_TIMER] * 1500);
    if (this->timers[0] == 0) {
        BossGanondrof_SetupNeutral(this, -5.0f);
        this->timers[0] = 10;
        this->flyMode = GND_FLY_NEUTRAL;
    }
}

// ZeldaOOTProject: the start of a charge: his staff flares up, lightning lashes out, he laughs
static void BossGanondrof_ZopChargeFlash(BossGanondrof* this, PlayState* play) {
    static Color_RGBA8 boltPrim = { 255, 255, 255, 255 };
    static Color_RGBA8 boltEnv = { 120, 200, 255, 255 };
    s16 i;

    sZopPowerFlashTimer = ZOP_POWER_FLASH_FRAMES;
    for (i = 0; i < 5; i++) {
        Vec3f pos = this->spearTip;

        EffectSsLightning_Spawn(play, &pos, &boltPrim, &boltEnv, (s16)(150 + Rand_ZeroOne() * 100.0f),
                                (s16)(i * (0x10000 / 5) + Rand_CenteredFloat(0x1800)), 6, 2);
    }
    this->shockTimer = 15;
    ZOP_GndRallyFlash(0.9f);
    Audio_PlayActorSound2(&this->actor, NA_SE_EV_DIAMOND_SWITCH);
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_THUNDER);
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_LAUGH);
}

// ZeldaOOTProject: height of the arena floor under (x, z) (not Link's height: he may be in the air or below)
static f32 BossGanondrof_ZopFloorY(PlayState* play, f32 x, f32 z) {
    CollisionPoly* poly;
    Vec3f pos = { x, GND_BOSSROOM_CENTER_Y + 300.0f, z };
    f32 y = BgCheck_EntityRaycastFloor1(&play->colCtx, &poly, &pos);

    return (y > BGCHECK_Y_MIN) ? y : GND_BOSSROOM_CENTER_Y;
}

// ZeldaOOTProject: the end of a charge: he slams lightning into the floor, a damaging burst and a ring spreading over
// the floor that Link has to jump over
static void BossGanondrof_ZopChargeImpact(BossGanondrof* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    f32 groundY = BossGanondrof_ZopFloorY(play, this->actor.world.pos.x, this->actor.world.pos.z);

    Actor_Spawn(&play->actorCtx, play, ACTOR_EN_FHG_FIRE, this->actor.world.pos.x, groundY + 20.0f,
                this->actor.world.pos.z, 400, 0, 0, FHGFIRE_LIGHTNING_BURST);
    ZOP_SpawnGndRing(play, this->actor.world.pos.x, groundY, this->actor.world.pos.z, 450.0f, 10.0f);
    ZOP_GndRallyFlash(1.6f);
    Actor_RequestQuakeAndRumble(&this->actor, play, 5, 12);
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_HIT_THUNDER);
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_THUNDER_GND);
}

// ---------- ZeldaOOTProject: staff slam (after a charge series, or on its own). He flies to the middle of the room,
// raises his staff, rams it into the floor, and rings spread out over the floor, one after another at uneven
// intervals and speeds, that Link has to jump over like skipping ropes
typedef enum {
    ZOP_FINALE_FLY,
    ZOP_FINALE_RAISE,
    ZOP_FINALE_SLAM,
    ZOP_FINALE_RINGS,
    ZOP_FINALE_RECOVER
} ZopFinaleState;

#define ZOP_FINALE_RING_RADIUS 650.0f
#define ZOP_KNEEL_HEIGHT 38.0f // his origin over the floor when kneeling (stunned: y 5 over the floor at -33)

static s16 sZopFinaleState;
static s16 sZopFinaleTimer;
static s16 sZopFinaleRingsLeft;
static f32 sZopFinaleGroundY;

static void BossGanondrof_ZopFinale(BossGanondrof* this, PlayState* play);

static void BossGanondrof_ZopSetupFinale(BossGanondrof* this, PlayState* play) {
    Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonNeutralAnim, -6.0f);
    this->actionFunc = BossGanondrof_ZopFinale;
    this->actor.speedXZ = 0.0f;
    this->actor.velocity.x = this->actor.velocity.y = this->actor.velocity.z = 0.0f;
    this->actor.gravity = 0.0f;
    sZopFinaleState = ZOP_FINALE_FLY;
    sZopFinaleAlone = true; // always open to attacks after the slam (on its own or after a charge series)
    sZopFinaleTimer = 50; // fallback
    sZopFinaleGroundY = BossGanondrof_ZopFloorY(play, GND_BOSSROOM_CENTER_X, GND_BOSSROOM_CENTER_Z);
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_LAUGH);
}

static void BossGanondrof_ZopFinaleRing(BossGanondrof* this, PlayState* play, f32 speed) {
    ZOP_SpawnGndRing(play, GND_BOSSROOM_CENTER_X, sZopFinaleGroundY, GND_BOSSROOM_CENTER_Z, ZOP_FINALE_RING_RADIUS,
                     speed);
}

static void BossGanondrof_ZopFinale(BossGanondrof* this, PlayState* play) {
    Actor* thisx = &this->actor;

    SkelAnime_Update(&this->skelAnime);
    Math_ApproachS(&thisx->shape.rot.y, thisx->yawTowardsPlayer, 5, 0x7D0);
    if (sZopFinaleTimer > 0) {
        sZopFinaleTimer--;
    }

    if (sZopFinaleAlone && (sZopFinaleState >= ZOP_FINALE_RINGS)) {
        this->shockTimer = 2;
        Math_ApproachF(&thisx->world.pos.y, sZopFinaleGroundY + ZOP_KNEEL_HEIGHT, 0.5f, 8.0f);
        if ((this->work[GND_VARIANCE_TIMER] % 20) == 0) {
            Audio_PlayActorSound2(thisx, NA_SE_EN_FANTOM_DAMAGE2);
        }
    }

    switch (sZopFinaleState) {
        case ZOP_FINALE_FLY: {
            f32 dx = GND_BOSSROOM_CENTER_X - thisx->world.pos.x;
            f32 dz = GND_BOSSROOM_CENTER_Z - thisx->world.pos.z;

            Math_ApproachF(&thisx->world.pos.x, GND_BOSSROOM_CENTER_X, 0.2f, 22.0f);
            Math_ApproachF(&thisx->world.pos.z, GND_BOSSROOM_CENTER_Z, 0.2f, 22.0f);
            Math_ApproachF(&thisx->world.pos.y, sZopFinaleGroundY + 170.0f, 0.2f, 12.0f);
            if (((SQ(dx) + SQ(dz)) < SQ(15.0f)) || (sZopFinaleTimer == 0)) {
                sZopFinaleState = ZOP_FINALE_RAISE;
                sZopFinaleTimer = 16;
                Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonChargeWindupAnim, -3.0f);
                BossGanondrof_ZopChargeFlash(this, play);
            }
            break;
        }
        case ZOP_FINALE_RAISE:
            thisx->world.pos.y += 2.0f * Math_SinS(this->work[GND_VARIANCE_TIMER] * 1500);
            if (sZopFinaleTimer == 0) {
                sZopFinaleState = ZOP_FINALE_SLAM;
                sZopFinaleTimer = 6;
                Animation_MorphToPlayOnce(&this->skelAnime, &gPhantomGanonChargeStartAnim, 0.0f);
                Audio_PlayActorSound2(thisx, NA_SE_EN_FANTOM_ATTACK);
            }
            break;
        case ZOP_FINALE_SLAM:
            // down he goes, staff first
            Math_ApproachF(&thisx->world.pos.y, sZopFinaleGroundY + ZOP_KNEEL_HEIGHT + 15.0f, 0.6f, 35.0f);
            if (sZopFinaleTimer == 0) {
                Actor_Spawn(&play->actorCtx, play, ACTOR_EN_FHG_FIRE, GND_BOSSROOM_CENTER_X, sZopFinaleGroundY + 20.0f,
                            GND_BOSSROOM_CENTER_Z, 500, 0, 0, FHGFIRE_LIGHTNING_BURST);
                BossGanondrof_ZopFinaleRing(this, play, 9.0f);
                this->shockTimer = 20;
                ZOP_GndRallyFlash(2.0f);
                Actor_RequestQuakeAndRumble(thisx, play, 8, 15);
                Audio_PlayActorSound2(thisx, NA_SE_EN_FANTOM_HIT_THUNDER);
                Audio_PlayActorSound2(thisx, NA_SE_EN_FANTOM_THUNDER_GND);
                Audio_PlayActorSound2(thisx, NA_SE_EN_FANTOM_THUNDER);
                if (sZopFinaleAlone) {
                    // worn out by the slam: he kneels on the floor and crackles like when a ball hit him, open to
                    // attacks
                    Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonStunnedAnim, -10.0f);
                    Audio_PlayActorSound2(thisx, NA_SE_EN_FANTOM_DAMAGE);
                }
                sZopFinaleState = ZOP_FINALE_RINGS;
                sZopFinaleRingsLeft = 2 + (s16)(Rand_ZeroOne() * 2.99f); // 2-4 more
                sZopFinaleTimer = 14 + (s16)(Rand_ZeroOne() * 18.0f);
            }
            break;
        case ZOP_FINALE_RINGS:
            // more rings, at uneven intervals and speeds, so Link can't just jump in a fixed rhythm
            if (sZopFinaleTimer == 0) {
                BossGanondrof_ZopFinaleRing(this, play, 7.5f + Rand_ZeroOne() * 4.5f);
                this->shockTimer = 8;
                ZOP_GndRallyFlash(0.8f);
                Actor_RequestQuakeAndRumble(thisx, play, 4, 8);
                Audio_PlayActorSound2(thisx, NA_SE_EN_FANTOM_THUNDER_GND);
                if (--sZopFinaleRingsLeft > 0) {
                    sZopFinaleTimer = 12 + (s16)(Rand_ZeroOne() * 22.0f);
                } else {
                    sZopFinaleState = ZOP_FINALE_RECOVER;
                    sZopFinaleTimer = sZopFinaleAlone ? 50 : 30;
                }
            }
            break;
        case ZOP_FINALE_RECOVER:
            if (sZopFinaleTimer == 0) {
                BossGanondrof_SetupNeutral(this, -6.0f);
                this->flyMode = GND_FLY_NEUTRAL;
                this->timers[0] = 40;
            }
            break;
    }
    Audio_PlayActorSound2(thisx, NA_SE_EN_FANTOM_FLOAT - SFX_FLAG);
}

void BossGanondrof_SetupCharge(BossGanondrof* this, PlayState* play) {
    this->fwork[GND_END_FRAME] = Animation_GetLastFrame(&gPhantomGanonChargeWindupAnim);
    Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonChargeWindupAnim, -3.0f);
    this->actionFunc = BossGanondrof_Charge;
    this->timers[0] = 20;
    this->work[GND_ACTION_STATE] = CHARGE_WINDUP;
    if (ZOP_DifficultyEnabled()) {
        // ZeldaOOTProject: a series of 2-3 charges (3 more often the weaker he gets)
        sZopChargesLeft = (Rand_ZeroOne() < 0.3f + 0.5f * (1.0f - (s8)this->actor.colChkInfo.health /
                                                                       (f32)GND_ZOP_PHASE2_HEALTH))
                              ? 2
                              : 1;
        BossGanondrof_ZopChargeFlash(this, play);
    }
}

void BossGanondrof_Charge(BossGanondrof* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    Actor* playerx = &player->actor;
    Actor* thisx = &this->actor;
    f32 dxCenter = thisx->world.pos.x - GND_BOSSROOM_CENTER_X;
    f32 dzCenter = thisx->world.pos.z - GND_BOSSROOM_CENTER_Z;

    this->colliderBody.base.colType = COLTYPE_METAL;
    SkelAnime_Update(&this->skelAnime);
    switch (this->work[GND_ACTION_STATE]) {
        case CHARGE_WINDUP:
            if (this->timers[0] == 218) {
                Audio_PlayActorSound2(thisx, NA_SE_EN_FANTOM_STICK);
            }

            if (this->timers[0] == 19) {
                Audio_PlayActorSound2(thisx, NA_SE_EN_FANTOM_ATTACK);
            }

            thisx->world.pos.x += thisx->velocity.x;
            thisx->world.pos.z += thisx->velocity.z;
            Math_ApproachZeroF(&thisx->velocity.x, 1.0f, 0.5f);
            Math_ApproachZeroF(&thisx->velocity.z, 1.0f, 0.5f);
            if (this->timers[0] == 0) {
                this->work[GND_ACTION_STATE] = CHARGE_START;
                this->timers[0] = 10;
                thisx->speedXZ = 0.0f;
                this->fwork[GND_END_FRAME] = Animation_GetLastFrame(&gPhantomGanonChargeStartAnim);
                Animation_MorphToPlayOnce(&this->skelAnime, &gPhantomGanonChargeStartAnim, 0.0f);
            }

            Math_ApproachS(&thisx->shape.rot.y, thisx->yawTowardsPlayer, 5, 0x7D0);
            break;
        case CHARGE_START:
            if (Animation_OnFrame(&this->skelAnime, this->fwork[GND_END_FRAME])) {
                this->fwork[GND_END_FRAME] = Animation_GetLastFrame(&gPhantomGanonChargeAnim);
                Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonChargeAnim, 0.0f);
                this->work[GND_ACTION_STATE] = CHARGE_ATTACK;
            }
        case CHARGE_ATTACK:
            if (this->timers[0] != 0) {
                Vec3f vecToLink;

                Math_ApproachS(&thisx->shape.rot.y, thisx->yawTowardsPlayer, 5, 0x7D0);
                vecToLink.x = playerx->world.pos.x - thisx->world.pos.x;
                vecToLink.y = playerx->world.pos.y + 40.0f - thisx->world.pos.y;
                vecToLink.z = playerx->world.pos.z - thisx->world.pos.z;
                thisx->world.rot.y = thisx->shape.rot.y;
                thisx->world.rot.x =
                    Math_FAtan2F(vecToLink.y, sqrtf(SQ(vecToLink.x) + SQ(vecToLink.z))) * (0x8000 / M_PI);
            }

            Actor_UpdateVelocityXYZ(thisx);
            Actor_UpdatePos(thisx);
            if (ZOP_DifficultyEnabled()) {
                // faster, and crackling with lightning all over
                Math_ApproachF(&thisx->speedXZ, ZOP_CHARGE_SPEED, 1.0f, 1.5f);
                EffectSsFhgFlash_SpawnShock(play, thisx, &thisx->world.pos, 45, FHGFLASH_SHOCK_PG);
                EffectSsFhgFlash_SpawnShock(play, thisx, &thisx->world.pos, 45, FHGFLASH_SHOCK_PG);
            } else {
                Math_ApproachF(&thisx->speedXZ, 10.0f, 1.0f, 0.5f);
            }
            if ((sqrtf(SQ(dxCenter) + SQ(dzCenter)) > 280.0f) || (thisx->xyzDistToPlayerSq < SQ(100.0f))) {
                this->work[GND_ACTION_STATE] = CHARGE_FINISH;
                this->timers[0] = 20;
                if (ZOP_DifficultyEnabled()) {
                    BossGanondrof_ZopChargeImpact(this, play);
                }
            }
            break;
        case CHARGE_FINISH:
            thisx->gravity = 0.2f;
            Actor_MoveXZGravity(thisx);
            osSyncPrintf("YP %f @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n", thisx->world.pos.y);
            if (thisx->world.pos.y < 5.0f) {
                thisx->world.pos.y = 5.0f;
                thisx->velocity.y = 0.0f;
            }

            if (sqrtf(SQ(dxCenter) + SQ(dzCenter)) > 280.0f) {
                Math_ApproachZeroF(&thisx->speedXZ, 1.0f, 2.0f);
                this->timers[0] = 0;
            }

            if (this->timers[0] == 0) {
                Math_ApproachZeroF(&thisx->speedXZ, 1.0f, 2.0f);
                Math_ApproachZeroF(&thisx->velocity.y, 1.0f, 2.0f);
                Math_ApproachS(&thisx->shape.rot.y, thisx->yawTowardsPlayer, 5, 0x7D0);
                if ((thisx->speedXZ <= 0.5f) && (fabsf(thisx->velocity.y) <= 0.1f)) {
                    if (ZOP_DifficultyEnabled() && (sZopChargesLeft > 0)) {
                        // ZeldaOOTProject: turns around for the next charge of the series right away
                        sZopChargesLeft--;
                        this->work[GND_ACTION_STATE] = CHARGE_WINDUP;
                        this->timers[0] = ZOP_CHARGE_REWINDUP;
                        thisx->speedXZ = 0.0f;
                        thisx->velocity.y = 0.0f;
                        thisx->gravity = 0.0f;
                        Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonChargeWindupAnim, -3.0f);
                        BossGanondrof_ZopChargeFlash(this, play);
                        Audio_PlayActorSound2(thisx, NA_SE_EN_FANTOM_ATTACK);
                    } else if (ZOP_DifficultyEnabled()) {
                        BossGanondrof_ZopSetupFinale(this, play);
                    } else {
                        BossGanondrof_SetupNeutral(this, -10.0f);
                        this->timers[0] = 30;
                        this->flyMode = GND_FLY_NEUTRAL;
                    }
                }
            }
            break;
    }

    if (thisx->world.pos.y > (GND_BOSSROOM_CENTER_Y + 83.0f)) {
        thisx->world.pos.y += 2.0f * Math_SinS(this->work[GND_VARIANCE_TIMER] * 1500);
    }
    {
        s16 i;
        Vec3f pos;
        Vec3f vel = { 0.0f, 0.0f, 0.0f };
        Vec3f accel = { 0.0f, 0.0f, 0.0f };
        Vec3f baseOffset = { 0.0f, 50.0f, 0.0f };
        Vec3f offset;

        baseOffset.y = 10.0f;
        for (i = 0; i < 10; i++) {
            Matrix_Push();
            Matrix_RotateY((thisx->shape.rot.y / (f32)0x8000) * M_PI, MTXMODE_NEW);
            Matrix_RotateX((thisx->shape.rot.x / (f32)0x8000) * M_PI, MTXMODE_APPLY);
            Matrix_RotateZ((this->work[GND_PARTICLE_ANGLE] / (f32)0x8000) * M_PI, MTXMODE_APPLY);
            Matrix_MultVec3f(&baseOffset, &offset);
            Matrix_Pop();
            pos.x = this->spearTip.x + offset.x;
            pos.y = this->spearTip.y + offset.y;
            pos.z = this->spearTip.z + offset.z;
            vel.x = (offset.x * 500.0f) / 1000.0f;
            vel.y = (offset.y * 500.0f) / 1000.0f;
            vel.z = (offset.z * 500.0f) / 1000.0f;
            accel.x = (offset.x * -50.0f) / 1000.0f;
            accel.y = (offset.y * -50.0f) / 1000.0f;
            accel.z = (offset.z * -50.0f) / 1000.0f;
            EffectSsFhgFlash_SpawnLightBall(play, &pos, &vel, &accel, 150, i % 7);
            this->work[GND_PARTICLE_ANGLE] += 0x1A5C;
        }
    }

    if (!(this->work[GND_VARIANCE_TIMER] & 7)) {
        EnfHG* horse = (EnfHG*)thisx->child;

        Actor_SpawnAsChild(&play->actorCtx, thisx, play, ACTOR_EN_FHG_FIRE, this->spearTip.x, this->spearTip.y,
                           this->spearTip.z, 8, FHGFIRE_LIGHT_BLUE, 0, FHGFIRE_SPEAR_LIGHT);
        thisx->child = &horse->actor;
    }
}

void BossGanondrof_SetupDeath(BossGanondrof* this, PlayState* play) {
    Animation_PlayOnce(&this->skelAnime, &gPhantomGanonDeathBlowAnim);
    this->fwork[GND_END_FRAME] = Animation_GetLastFrame(&gPhantomGanonDeathBlowAnim);
    this->actionFunc = BossGanondrof_Death;
    Audio_QueueSeqCmd(0x1 << 28 | SEQ_PLAYER_BGM_MAIN << 24 | 0x100FF);
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_DEAD);
    this->deathState = DEATH_START;
    this->actor.flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
    this->work[GND_VARIANCE_TIMER] = 0;
    this->shockTimer = 50;
}

void BossGanondrof_Death(BossGanondrof* this, PlayState* play) {
    u8 holdCamera = false;
    u8 bodyDecayLevel = 0;
    f32 camX;
    f32 camZ;
    f32 pad;
    Player* player = GET_PLAYER(play);
    Camera* camera = Play_GetCamera(play, 0);

    osSyncPrintf("PYP %f\n", player->actor.floorHeight);
    SkelAnime_Update(&this->skelAnime);
    this->work[GND_DEATH_SFX_TIMER]++;
    if (((60 < this->work[GND_DEATH_SFX_TIMER]) && (this->work[GND_DEATH_SFX_TIMER] < 500)) ||
        ((501 < this->work[GND_DEATH_SFX_TIMER]) && (this->work[GND_DEATH_SFX_TIMER] < 620))) {
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_LAST - SFX_FLAG);
    }

    switch (this->deathState) {
        case DEATH_START:
            func_80064520(play, &play->csCtx);
            Player_SetCsActionWithHaltedActors(play, &this->actor, 1);
            this->deathCamera = Play_CreateSubCamera(play);
            Play_ChangeCameraStatus(play, CAM_ID_MAIN, CAM_STAT_WAIT);
            osSyncPrintf("7\n");
            Play_ChangeCameraStatus(play, this->deathCamera, CAM_STAT_ACTIVE);
            osSyncPrintf("8\n");
            this->deathState = DEATH_THROES;
            player->actor.speedXZ = 0.0f;
            this->timers[0] = 50;
            this->cameraEye = camera->eye;
            this->cameraAt = camera->at;
            this->cameraNextEye.x = this->targetPos.x;
            this->cameraNextEye.y = GND_BOSSROOM_CENTER_Y + 83.0f;
            this->cameraNextEye.z = (this->targetPos.z + 100.0f) + 50;
            this->cameraNextAt.x = this->targetPos.x;
            this->cameraNextAt.y = this->targetPos.y - 10.0f;
            this->cameraNextAt.z = this->targetPos.z;
            this->cameraEyeVel.x = fabsf(camera->eye.x - this->cameraNextEye.x);
            this->cameraEyeVel.y = fabsf(camera->eye.y - this->cameraNextEye.y);
            this->cameraEyeVel.z = fabsf(camera->eye.z - this->cameraNextEye.z);
            this->cameraAtVel.x = fabsf(camera->at.x - this->cameraNextAt.x);
            this->cameraAtVel.y = fabsf(camera->at.y - this->cameraNextAt.y);
            this->cameraAtVel.z = fabsf(camera->at.z - this->cameraNextAt.z);
            this->cameraAccel = 0.02f;
            this->cameraEyeMaxVel.x = this->cameraEyeMaxVel.y = this->cameraEyeMaxVel.z = 0.05f;
            this->work[GND_ACTION_STATE] = DEATH_SPASM;
            this->timers[0] = 150;
            this->cameraAtMaxVel.x = 0.2f;
            this->cameraAtMaxVel.y = 0.2f;
            this->cameraAtMaxVel.z = 0.2f;
        case DEATH_THROES:
            switch (this->work[GND_ACTION_STATE]) {
                case DEATH_SPASM:
                    if (GameInteractor_Should(VB_PHANTOM_GANON_DEATH_SCENE, true, this)) {
                        if (Animation_OnFrame(&this->skelAnime, this->fwork[GND_END_FRAME])) {
                            this->fwork[GND_END_FRAME] = Animation_GetLastFrame(&gPhantomGanonAirDamageAnim);
                            Animation_Change(&this->skelAnime, &gPhantomGanonAirDamageAnim, 0.5f, 0.0f,
                                             this->fwork[GND_END_FRAME], ANIMMODE_ONCE_INTERP, 0.0f);
                            this->work[GND_ACTION_STATE] = DEATH_LIMP;
                        }
                    }
                    break;
                case DEATH_LIMP:
                    if (Animation_OnFrame(&this->skelAnime, this->fwork[GND_END_FRAME])) {
                        this->fwork[GND_END_FRAME] = Animation_GetLastFrame(&gPhantomGanonLimpAnim);
                        Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonLimpAnim, -20.0f);
                        this->work[GND_ACTION_STATE] = DEATH_HUNCHED;
                    }
                case DEATH_HUNCHED:
                    bodyDecayLevel = 1;
                    break;
            }
            if (GameInteractor_Should(VB_PHANTOM_GANON_DEATH_SCENE, true, this)) {
                Math_ApproachS(&this->actor.shape.rot.y, this->work[GND_VARIANCE_TIMER] * -100, 5, 0xBB8);
                Math_ApproachF(&this->cameraNextEye.z, this->targetPos.z + 60.0f, 0.02f, 0.5f);
                Math_ApproachF(&this->actor.world.pos.y, GND_BOSSROOM_CENTER_Y + 133.0f, 0.05f, 100.0f);
                this->actor.world.pos.y += Math_SinS(this->work[GND_VARIANCE_TIMER] * 1500);
                this->cameraNextAt.x = this->targetPos.x;
                this->cameraNextAt.y = this->targetPos.y - 10.0f;
                this->cameraNextAt.z = this->targetPos.z;
                if (this->timers[0] == 0) {
                    this->deathState = DEATH_WARP;
                    this->timers[0] = 350;
                    this->timers[1] = 50;
                    this->fwork[GND_CAMERA_ZOOM] = 300.0f;
                    this->cameraNextEye.y = GND_BOSSROOM_CENTER_Y + 233.0f;
                    player->actor.world.pos.x = GND_BOSSROOM_CENTER_X - 200.0f;
                    player->actor.world.pos.z = GND_BOSSROOM_CENTER_Z;
                    holdCamera = true;
                    bodyDecayLevel = 1;
                }
            }
            break;
        case DEATH_WARP:
            if (this->timers[1] == 1) {
                EnfHG* horseTemp = (EnfHG*)this->actor.child;

                Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_FHG_FIRE, GND_BOSSROOM_CENTER_X,
                                   GND_BOSSROOM_CENTER_Y + 3.0f, GND_BOSSROOM_CENTER_Z, 0x4000, 0, 0,
                                   FHGFIRE_WARP_DEATH);
                this->actor.child = &horseTemp->actor;
                Message_StartTextbox(play, 0x108E, NULL);
            }

            this->actor.shape.rot.y -= 0xC8;
            this->actor.world.pos.y += Math_SinS(this->work[GND_VARIANCE_TIMER] * 1500);
            this->fwork[GND_CAMERA_ANGLE] += 0x78;
            camX = Math_SinS(this->fwork[GND_CAMERA_ANGLE]) * this->fwork[GND_CAMERA_ZOOM];
            camZ = Math_CosS(this->fwork[GND_CAMERA_ANGLE]) * this->fwork[GND_CAMERA_ZOOM];
            this->cameraEye.x = GND_BOSSROOM_CENTER_X + camX;
            this->cameraEye.y = this->cameraNextEye.y;
            this->cameraEye.z = GND_BOSSROOM_CENTER_Z + camZ;
            this->cameraAt.x = GND_BOSSROOM_CENTER_X;
            this->cameraAt.y = GND_BOSSROOM_CENTER_Y + 23.0f;
            this->cameraAt.z = GND_BOSSROOM_CENTER_Z;
            Math_ApproachF(&this->cameraNextEye.y, GND_BOSSROOM_CENTER_Y + 33.0f, 0.05f, 0.5f);
            Math_ApproachF(&this->fwork[GND_CAMERA_ZOOM], 170.0f, 0.05f, 1.0f);
            Math_ApproachF(&this->actor.world.pos.x, GND_BOSSROOM_CENTER_X, 0.05f, 1.5f);
            Math_ApproachF(&this->actor.world.pos.y, GND_BOSSROOM_CENTER_Y + 83.0f, 0.05f, 1.0f);
            Math_ApproachF(&this->actor.world.pos.z, GND_BOSSROOM_CENTER_Z, 0.05f, 1.5f);
            if (this->timers[0] == 0) {
                this->deathState = DEATH_SCREAM;
                this->timers[0] = 50;
                Animation_MorphToLoop(&this->skelAnime, &gPhantomGanonScreamAnim, -10.0f);
                this->actor.world.pos.x = GND_BOSSROOM_CENTER_X;
                this->actor.world.pos.y = GND_BOSSROOM_CENTER_Y + 83.0f;
                this->actor.world.pos.z = GND_BOSSROOM_CENTER_Z;
                this->actor.shape.rot.y = 0;
                this->work[GND_BODY_DECAY_INDEX] = 0;
                Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_LAST);
            }

            holdCamera = true;
            bodyDecayLevel = 1;
            break;
        case DEATH_SCREAM:
            holdCamera = true;
            bodyDecayLevel = 2;
            this->actor.world.pos.y = GND_BOSSROOM_CENTER_Y + 83.0f;
            this->cameraEye.x = GND_BOSSROOM_CENTER_X;
            this->cameraEye.y = GND_BOSSROOM_CENTER_Y + 83.0f;
            this->cameraEye.z = GND_BOSSROOM_CENTER_Z + 50.0f;
            this->cameraAt.x = GND_BOSSROOM_CENTER_X;
            this->cameraAt.y = GND_BOSSROOM_CENTER_Y + 103.0f;
            this->cameraAt.z = GND_BOSSROOM_CENTER_Z;
            if (this->timers[0] == 0) {
                this->deathState = DEATH_DISINTEGRATE;
                Animation_MorphToPlayOnce(&this->skelAnime, &gPhantomGanonLastPoseAnim, -10.0f);
                this->work[GND_BODY_DECAY_INDEX] = 0;
                this->timers[0] = 40;
            }
            break;
        case DEATH_DISINTEGRATE:
            holdCamera = true;
            bodyDecayLevel = 3;
            Math_ApproachZeroF(&this->cameraEye.y, 0.05f, 1.0f); // approaches GND_BOSSROOM_CENTER_Y + 33.0f
            Math_ApproachF(&this->cameraEye.z, GND_BOSSROOM_CENTER_Z + 170.0f, 0.05f, 2.0f);
            Math_ApproachF(&this->cameraAt.y, GND_BOSSROOM_CENTER_Y + 53.0f, 0.05f, 1.0f);
            if (this->timers[0] == 0) {
                this->timers[0] = 250;
                this->deathState = DEATH_FINISH;
            }
            break;
        case DEATH_FINISH:
            holdCamera = true;
            bodyDecayLevel = 10;
            if (this->timers[0] == 150) {
                Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_BOSS_CLEAR);
                if (GameInteractor_Should(VB_SPAWN_BLUE_WARP, true, this)) {
                    Actor_Spawn(&play->actorCtx, play, ACTOR_DOOR_WARP1, GND_BOSSROOM_CENTER_X, GND_BOSSROOM_CENTER_Y,
                                GND_BOSSROOM_CENTER_Z, 0, 0, 0, WARP_DUNGEON_ADULT);
                }
            }

            Math_ApproachZeroF(&this->cameraEye.y, 0.05f, 1.0f); // GND_BOSSROOM_CENTER_Y + 33.0f
            Math_ApproachF(&this->cameraEye.z, GND_BOSSROOM_CENTER_Z + 170.0f, 0.05f, 2.0f);
            Math_ApproachF(&this->cameraAt.y, GND_BOSSROOM_CENTER_Y + 53.0f, 0.05f, 1.0f);
            if (this->timers[0] == 0) {
                EnfHG* horse = (EnfHG*)this->actor.child;

                camera->eye = this->cameraEye;
                camera->eyeNext = this->cameraEye;
                camera->at = this->cameraAt;
                func_800C08AC(play, this->deathCamera, 0);
                this->deathCamera = 0;
                func_80064534(play, &play->csCtx);
                Player_SetCsActionWithHaltedActors(play, &this->actor, 7);
                if (GameInteractor_Should(VB_SPAWN_HEART_CONTAINER, true)) {
                    Actor_Spawn(&play->actorCtx, play, ACTOR_ITEM_B_HEART, GND_BOSSROOM_CENTER_X, GND_BOSSROOM_CENTER_Y,
                                GND_BOSSROOM_CENTER_Z + 200.0f, 0, 0, 0, 0);
                }
                this->actor.child = &horse->actor;
                this->killActor = true;
                horse->killActor = true;
                Flags_SetClear(play, play->roomCtx.curRoom.num);
                Flags_SetSwitch(play, 0x22);
            }
            break;
    }

    if (bodyDecayLevel) {
        Vec3f pos;
        Vec3f vel = { 0.0f, 0.0f, 0.0f };
        Vec3f accelKFire = { 0.0f, 0.0f, 0.0f };
        Vec3f accelHahen = { 0.0f, -0.5f, 0.0f };
        s16 limbDecayIndex;
        s16 i;

        vel.x = this->actor.world.pos.x - this->actor.prevPos.x;
        vel.z = this->actor.world.pos.z - this->actor.prevPos.z;
        if (bodyDecayLevel < 10) {
            if (this->work[GND_DEATH_ENV_TIMER] == 0) {
                if (play->envCtx.unk_BF == 0) {
                    play->envCtx.unk_BF = 3;
                    this->work[GND_DEATH_ENV_TIMER] = (s16)Rand_ZeroFloat(5.0f) + 4.0f;
                    play->envCtx.unk_D6 = 0x28;
                } else {
                    play->envCtx.unk_BF = 0;
                    this->work[GND_DEATH_ENV_TIMER] = (s16)Rand_ZeroFloat(2.0f) + 2.0f;
                    play->envCtx.unk_D6 = 0x14;
                }
            } else {
                this->work[GND_DEATH_ENV_TIMER]--;
            }

            for (i = 0; i <= 0; i++) {
                limbDecayIndex = this->work[GND_LIMB_DECAY_INDEX];
                this->work[GND_LIMB_DECAY_INDEX]++;
                this->work[GND_LIMB_DECAY_INDEX] %= 25;
                pos.x = this->bodyPartsPos[limbDecayIndex].x + Rand_CenteredFloat(5.0f);
                pos.y = this->bodyPartsPos[limbDecayIndex].y + Rand_CenteredFloat(5.0f);
                pos.z = this->bodyPartsPos[limbDecayIndex].z + Rand_CenteredFloat(5.0f);
                accelKFire.y = 0.0f;

                if (bodyDecayLevel == 3) {
                    accelKFire.y = -0.2f;
                    accelKFire.x = (GND_BOSSROOM_CENTER_X - pos.x) * 0.002f;
                    accelKFire.z = (GND_BOSSROOM_CENTER_Z - pos.z) * 0.002f;
                    accelHahen.x = (GND_BOSSROOM_CENTER_X - pos.x) * 0.001f;
                    accelHahen.y = -1.0f;
                    accelHahen.z = (GND_BOSSROOM_CENTER_Z - pos.z) * 0.001f;
                }

                EffectSsKFire_Spawn(play, &pos, &vel, &accelKFire, (s16)Rand_ZeroFloat(20.0f) + 15, bodyDecayLevel);
                if ((Rand_ZeroOne() < 0.5f) || (bodyDecayLevel == 3)) {
                    EffectSsHahen_Spawn(play, &pos, &vel, &accelHahen, 0, (s16)Rand_ZeroFloat(4.0f) + 7,
                                        HAHEN_OBJECT_DEFAULT, 10, NULL);
                }
            }
        } else {
            play->envCtx.unk_BF = 0;
            play->envCtx.unk_D6 = 0x14;
        }

        this->work[GND_BODY_DECAY_FLAG] = true;
        for (i = 0; i < 5; i++) {
            if (bodyDecayLevel == 1) {
                BossGanondrof_ClearPixels(sDecayMaskLow, this->work[GND_BODY_DECAY_INDEX]);
            } else if (bodyDecayLevel == 2) {
                BossGanondrof_ClearPixels(sDecayMaskHigh, this->work[GND_BODY_DECAY_INDEX]);
            } else {
                BossGanondrof_ClearPixels(sDecayMaskTotal, this->work[GND_BODY_DECAY_INDEX]);
            }

            if (this->work[GND_BODY_DECAY_INDEX] < 0xFF) {
                this->work[GND_BODY_DECAY_INDEX]++;
            }
        }
    }

    if (this->deathCamera != 0) {
        if (!holdCamera) {
            Math_ApproachF(&this->cameraEye.x, this->cameraNextEye.x, this->cameraEyeMaxVel.x,
                           this->cameraEyeVel.x * this->cameraSpeedMod);
            Math_ApproachF(&this->cameraEye.y, this->cameraNextEye.y, this->cameraEyeMaxVel.y,
                           this->cameraEyeVel.y * this->cameraSpeedMod);
            Math_ApproachF(&this->cameraEye.z, this->cameraNextEye.z, this->cameraEyeMaxVel.z,
                           this->cameraEyeVel.z * this->cameraSpeedMod);
            Math_ApproachF(&this->cameraAt.x, this->cameraNextAt.x, this->cameraAtMaxVel.x,
                           this->cameraAtVel.x * this->cameraSpeedMod);
            Math_ApproachF(&this->cameraAt.y, this->cameraNextAt.y, this->cameraAtMaxVel.y,
                           this->cameraAtVel.y * this->cameraSpeedMod);
            Math_ApproachF(&this->cameraAt.z, this->cameraNextAt.z, this->cameraAtMaxVel.z,
                           this->cameraAtVel.z * this->cameraSpeedMod);
            Math_ApproachF(&this->cameraSpeedMod, 1.0f, 1.0f, this->cameraAccel);
        }

        Play_CameraSetAtEye(play, this->deathCamera, &this->cameraAt, &this->cameraEye);
    }
}

void BossGanondrof_CollisionCheck(BossGanondrof* this, PlayState* play) {
    s32 acHit;
    EnfHG* horse = (EnfHG*)this->actor.child;
    ColliderInfo* hurtbox;

    if (this->work[GND_INVINC_TIMER] != 0) {
        this->work[GND_INVINC_TIMER]--;
        this->returnCount = 0;
        this->colliderBody.base.acFlags &= ~AC_HIT;
    } else {
        acHit = this->colliderBody.base.acFlags & AC_HIT;
        if ((acHit && ((s8)this->actor.colChkInfo.health > 0)) || (this->returnCount != 0)) {
            if (acHit) {
                this->colliderBody.base.acFlags &= ~AC_HIT;
                hurtbox = this->colliderBody.info.acHitInfo;
            }
            if (this->flyMode != GND_FLY_PAINTING) {
                if (acHit && (this->actionFunc != BossGanondrof_Stunned) && (hurtbox->toucher.dmgFlags & 0x0001F8A4)) {
                    Audio_PlayActorSound2(&this->actor, NA_SE_PL_WALK_GROUND - SFX_FLAG);
                    osSyncPrintf("hit != 0 \n");
                } else if (this->actionFunc != BossGanondrof_Charge) {
                    if (this->returnCount == 0) {
                        u8 dmg;
                        u8 canKill = false;
                        s32 dmgFlags = hurtbox->toucher.dmgFlags;

                        if (dmgFlags & 0x80) {
                            return;
                        }
                        dmg = CollisionCheck_GetSwordDamage(dmgFlags, play);
                        (dmg == 0) ? (dmg = 2) : (canKill = true);
                        if (((s8)this->actor.colChkInfo.health > 2) || canKill) {
                            this->actor.colChkInfo.health -= dmg;
                        }

                        if ((s8)this->actor.colChkInfo.health <= 0) {
                            BossGanondrof_SetupDeath(this, play);
                            Enemy_StartFinishingBlow(play, &this->actor);
                            GameInteractor_ExecuteOnBossDefeat(&this->actor);
                            return;
                        }
                    }
                    BossGanondrof_SetupStunned(this, play);
                    if (this->returnCount >= 2) {
                        this->timers[0] = 120;
                    }
                    this->work[GND_INVINC_TIMER] = 10;
                    horse->hitTimer = 20;
                    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_DAMAGE);
                } else {
                    Audio_PlayActorSound2(&this->actor, NA_SE_PL_WALK_GROUND - SFX_FLAG);
                }
            } else if (acHit && (hurtbox->toucher.dmgFlags & 0x0001F8A4)) {
                this->work[GND_INVINC_TIMER] = 10;
                this->actor.colChkInfo.health -= 2;
                horse->hitTimer = 20;
                Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_DAMAGE);
            }
            this->returnCount = 0;
        }
    }
}

void BossGanondrof_Update(Actor* thisx, PlayState* play) {
    f32 cs;
    f32 sn;

    // ZeldaOOTProject testing aid (gZOP.TestEnv.GanondrofPhase2): no intro, straight to floating (see EnfHG_Update)
    if (ZOP_TestGanondrofPhase2() && (thisx->params == GND_REAL_BOSS) &&
        (((BossGanondrof*)thisx)->actionFunc == BossGanondrof_Intro)) {
        BossGanondrof_SetupPaintings((BossGanondrof*)thisx);
    }
    f32 legRotTargetY;
    f32 legRotTargetZ;
    f32 legSplitTarget;
    s32 pad2;
    s16 i;
    s32 pad;
    BossGanondrof* this = (BossGanondrof*)thisx;
    EnfHG* horse;

    if (sZopBurstCooldown > 0) {
        sZopBurstCooldown--;
    }
    // ZeldaOOTProject testing aid (gZOP.TestEnv.GanondrofSlam): Link is put into the arena (from the elevator below)
    if ((ZOP_TestGanondrofSlam() || ZOP_TestGanondrofPhase2()) && (this->actor.params == GND_REAL_BOSS) &&
        (GET_PLAYER(play)->actor.world.pos.y < GND_BOSSROOM_CENTER_Y - 50.0f)) {
        Player* zopPlayer = GET_PLAYER(play);

        zopPlayer->actor.world.pos.x = zopPlayer->actor.home.pos.x = GND_BOSSROOM_CENTER_X;
        zopPlayer->actor.world.pos.y = zopPlayer->actor.home.pos.y = GND_BOSSROOM_CENTER_Y;
        zopPlayer->actor.world.pos.z = zopPlayer->actor.home.pos.z = GND_BOSSROOM_CENTER_Z + 250.0f;
        zopPlayer->actor.prevPos = zopPlayer->actor.world.pos;
    }
    if (this->actor.params == GND_REAL_BOSS) {
        BossGanondrof_ZopPowerUpdate(this, play);
        if (sZopPowerFlashTimer > 0) {
            sZopPowerFlashTimer--;
        }
    }
    osSyncPrintf("MOVE START %d\n", this->actor.params);
    this->actor.flags &= ~ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER;
    this->colliderBody.base.colType = COLTYPE_HIT3;
    if (this->killActor) {
        Actor_Kill(&this->actor);
        return;
    }
    this->work[GND_VARIANCE_TIMER]++;
    horse = (EnfHG*)this->actor.child;
    osSyncPrintf("MOVE START EEEEEEEEEEEEEEEEEEEEEE%d\n", this->actor.params);

    this->actionFunc(this, play);

    for (i = 0; i < ARRAY_COUNT(this->timers); i++) {
        if (this->timers[i]) {
            this->timers[i]--;
        }
    }
    if (this->work[GND_UNKTIMER_1]) {
        this->work[GND_UNKTIMER_1]--;
    }
    if (this->work[GND_UNKTIMER_2]) {
        this->work[GND_UNKTIMER_2]--;
    }

    if (this->actionFunc != BossGanondrof_Death) {
        BossGanondrof_CollisionCheck(this, play);
    }

    osSyncPrintf("MOVE END\n");
    BossGanondrof_SetColliderPos(&this->targetPos, &this->colliderBody);
    BossGanondrof_SetColliderPos(&this->spearTip, &this->colliderSpear);
    if ((this->flyMode == GND_FLY_PAINTING) && !horse->bossGndInPainting) {
        CollisionCheck_SetAC(play, &play->colChkCtx, &this->colliderBody.base);
    }
    if ((this->actionFunc == BossGanondrof_Stunned) && (this->timers[0] > 1)) {
        CollisionCheck_SetAC(play, &play->colChkCtx, &this->colliderBody.base);
        CollisionCheck_SetOC(play, &play->colChkCtx, &this->colliderBody.base);
    } else if (this->actionFunc == BossGanondrof_Block) {
        CollisionCheck_SetAC(play, &play->colChkCtx, &this->colliderBody.base);
    } else if ((this->actionFunc == BossGanondrof_ZopFinale) && sZopFinaleAlone &&
               (sZopFinaleState >= ZOP_FINALE_RINGS)) {
        // ZeldaOOTProject: open to attacks after his staff slam on its own
        CollisionCheck_SetAC(play, &play->colChkCtx, &this->colliderBody.base);
        CollisionCheck_SetOC(play, &play->colChkCtx, &this->colliderBody.base);
    } else if (this->actionFunc == BossGanondrof_Charge) {
        CollisionCheck_SetAC(play, &play->colChkCtx, &this->colliderBody.base);
        CollisionCheck_SetAT(play, &play->colChkCtx, &this->colliderBody.base);
        CollisionCheck_SetAT(play, &play->colChkCtx, &this->colliderSpear.base);
    }

    this->actor.focus.pos = this->targetPos;

    sn = Math_SinS(-this->actor.shape.rot.y);
    cs = Math_CosS(-this->actor.shape.rot.y);
    legRotTargetY = ((sn * this->actor.velocity.z) + (cs * this->actor.velocity.x)) * 300.0f;
    legRotTargetZ = ((-sn * this->actor.velocity.x) + (cs * this->actor.velocity.z)) * 300.0f;
    Math_ApproachF(&this->legRotY, legRotTargetY, 1.0f, 600.0f);
    Math_ApproachF(&this->legRotZ, legRotTargetZ, 1.0f, 600.0f);
    if ((this->flyMode != GND_FLY_PAINTING) && (this->actionFunc != BossGanondrof_Stunned) &&
        (this->deathState == NOT_DEAD)) {
        legSplitTarget = (Math_SinS(this->work[GND_VARIANCE_TIMER] * 0x8DC) * -500.0f) - 500.0f;
    } else {
        legSplitTarget = 0.0f;
    }

    Math_ApproachF(&this->legSplitY, legSplitTarget, 1.0f, 100.0f);
    if (this->shockTimer != 0) {
        s16 j;

        this->shockTimer--;
        osSyncPrintf("F 1\n");
        for (j = 0; j < 7; j++) {
            osSyncPrintf("F 15\n");
            EffectSsFhgFlash_SpawnShock(play, &this->actor, &this->actor.world.pos, 45, FHGFLASH_SHOCK_PG);
        }
        osSyncPrintf("F 2\n");
    }

    if (this->actor.params == GND_REAL_BOSS) {
        if (sZopPowerTimer > 0) {
            // purple light from the charged staff
            Lights_PointNoGlowSetInfo(&this->lightInfo, this->spearTip.x, this->spearTip.y, this->spearTip.z, 200, 90,
                                      255, 300);
        } else {
            Lights_PointNoGlowSetInfo(&this->lightInfo, this->spearTip.x, this->spearTip.y, this->spearTip.z, 255,
                                      255, 255, 200);
        }
    }
}

s32 BossGanondrof_OverrideLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* thisx) {
    BossGanondrof* this = (BossGanondrof*)thisx;

    switch (limbIndex) {
        case 15:
            if ((this->actionFunc == BossGanondrof_Intro) && this->work[GND_MASK_OFF]) {
                *dList = gPhantomGanonFaceDL;
            }
            rot->y += this->rideRotY[limbIndex];
            rot->z += this->rideRotZ[limbIndex];
            break;

        case 19:
            rot->y += this->legRotY + this->legSplitY;
            rot->z += this->legRotZ;
            break;

        case 20:
            rot->y += this->legRotY + this->legSplitY;
            rot->z += this->legRotZ;
            break;

        case 21:
            rot->y += this->legRotY + this->legSplitY;
            rot->z += this->legRotZ;
            break;

        case 23:
            rot->y += this->legRotY - this->legSplitY;
            rot->z += this->legRotZ;
            break;

        case 24:
            rot->y += this->legRotY - this->legSplitY;
            rot->z += this->legRotZ;
            break;

        case 25:
            rot->y += this->legRotY - this->legSplitY;
            rot->z += this->legRotZ;
            break;

        case 5:
        case 6:
        case 7:
            rot->y += this->armRotY;
            rot->z += this->armRotZ;
            break;

        case 8:
        case 9:
        case 10:
            rot->y += this->armRotY;
            rot->z += this->armRotZ;
            break;

        case 13:
            if (this->deathState != NOT_DEAD) {
                *dList = NULL;
            }
        default:
            rot->y += this->rideRotY[limbIndex];
            rot->z += this->rideRotZ[limbIndex];
            break;
    }

    return 0;
}

void BossGanondrof_PostLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* thisx) {
    static Vec3f zeroVec = { 0.0f, 0.0f, 0.0f };
    static Vec3f spearVec = { 0.0f, 0.0f, 6000.0f };

    BossGanondrof* this = (BossGanondrof*)thisx;

    if (limbIndex == 14) {
        Matrix_MultVec3f(&zeroVec, &this->targetPos);
    } else if (limbIndex == 13) {
        Matrix_MultVec3f(&spearVec, &this->spearTip);
    }

    if (((this->flyMode != GND_FLY_PAINTING) || (this->actionFunc == BossGanondrof_Intro)) && (limbIndex <= 25)) {
        Matrix_MultVec3f(&zeroVec, &this->bodyPartsPos[limbIndex - 1]);
    }
}

Gfx* BossGanondrof_GetClearPixelDList(GraphicsContext* gfxCtx) {
    Gfx* dList = (Gfx*)Graph_Alloc(gfxCtx, sizeof(Gfx) * 4);
    Gfx* dListHead = dList;

    gDPPipeSync(dListHead++);
    gDPSetRenderMode(dListHead++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_TEX_EDGE2);
    gSPClearGeometryMode(dListHead++, G_CULL_BACK);
    gSPEndDisplayList(dListHead++);

    return dList;
}

Gfx* BossGanondrof_GetNullDList(GraphicsContext* gfxCtx) {
    Gfx* dList = (Gfx*)Graph_Alloc(gfxCtx, sizeof(Gfx) * 1);
    Gfx* dListHead = dList;

    gSPEndDisplayList(dListHead++);
    return dList;
}

void BossGanondrof_Draw(Actor* thisx, PlayState* play) {
    s32 pad;
    BossGanondrof* this = (BossGanondrof*)thisx;
    EnfHG* horse;

    OPEN_DISPS(play->state.gfxCtx);

    if (this->work[GND_BODY_DECAY_FLAG]) {
        gSPInvalidateTexCache(POLY_OPA_DISP++, sDecayTex);
    }

    osSyncPrintf("MOVE P = %x\n", this->actor.update);
    osSyncPrintf("STOP TIMER = %d ==============\n", this->actor.freezeTimer);
    horse = (EnfHG*)this->actor.child;
    if (this->flyMode == GND_FLY_PAINTING) {
        Matrix_RotateY((horse->turnRot * 3.1416f) / (f32)0x8000, MTXMODE_APPLY);
    }

    osSyncPrintf("YP %f\n", this->actor.world.pos.y);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    if ((this->work[GND_INVINC_TIMER] & 4) ||
        (sZopAuraRed && (this->actionFunc == BossGanondrof_Stunned) && (sZopAuraTimer >= 0))) {
        POLY_OPA_DISP = Gfx_SetFog(POLY_OPA_DISP, 255, 50, 0, 0, 900, 1099);
    } else {
        POLY_OPA_DISP = Gfx_SetFog(POLY_OPA_DISP, (u32)horse->warpColorFilterR, (u32)horse->warpColorFilterG,
                                   (u32)horse->warpColorFilterB, 0, (s32)horse->warpColorFilterUnk1 + 995,
                                   (s32)horse->warpColorFilterUnk2 + 1000);
    }

    osSyncPrintf("DRAW 11\n");
    osSyncPrintf("EYE_COL %d\n", (s16)this->fwork[GND_EYE_BRIGHTNESS]);
    gDPSetEnvColor(POLY_OPA_DISP++, (s16)this->fwork[GND_EYE_BRIGHTNESS], (s16)this->fwork[GND_EYE_BRIGHTNESS],
                   (s16)this->fwork[GND_EYE_BRIGHTNESS], (s16)this->fwork[GND_EYE_ALPHA]);
    if (this->work[GND_BODY_DECAY_FLAG]) {
        gSPSegment(POLY_OPA_DISP++, 0x08, BossGanondrof_GetClearPixelDList(play->state.gfxCtx));
    } else {
        gSPSegment(POLY_OPA_DISP++, 0x08, BossGanondrof_GetNullDList(play->state.gfxCtx));
    }

    SkelAnime_DrawSkeletonOpa(play, &this->skelAnime, BossGanondrof_OverrideLimbDraw, BossGanondrof_PostLimbDraw, this);
    osSyncPrintf("DRAW 22\n");
    POLY_OPA_DISP = Play_SetFog(play, POLY_OPA_DISP);

    // ZeldaOOTProject: power hit charged, a short light blue lens flare on the staff (like Gohma's eye flash): a round
    // glow and a wide horizontal streak
    if ((sZopPowerFlashTimer > 0) && (this->actor.params == GND_REAL_BOSS)) {
        f32 t = (f32)sZopPowerFlashTimer / ZOP_POWER_FLASH_FRAMES; // 1 -> 0
        f32 size = (0.35f + 0.65f * Math_SinF((1.0f - t) * 3.14159265f)) * 0.1f;
        u8 alpha = (u8)(255.0f * CLAMP(t * 1.6f, 0.0f, 1.0f));

        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x80, 255, 255, 255, alpha);
        gDPSetEnvColor(POLY_XLU_DISP++, 140, 230, 255, 0);
        Matrix_Translate(this->spearTip.x, this->spearTip.y, this->spearTip.z, MTXMODE_NEW);
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
    CLOSE_DISPS(play->state.gfxCtx);
    osSyncPrintf("DRAW END %d\n", this->actor.params);
}

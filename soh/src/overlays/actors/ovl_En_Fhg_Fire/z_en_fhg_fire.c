/*
 * File: z_en_fhg_fire.c
 * Overlay: ovl_En_Fhg_Fire
 * Description: Phantom Ganon's Lighting Attack
 */

#include "z_en_fhg_fire.h"
#include "objects/object_fhg/object_fhg.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include "overlays/actors/ovl_Boss_Ganondrof/z_boss_ganondrof.h"
#include "overlays/actors/ovl_En_fHG/z_en_fhg.h"
#include "overlays/effects/ovl_Effect_Ss_Fhg_Flash/z_eff_ss_fhg_flash.h"
#include "soh/Enhancements/ZeldaOOTProject/ZeldaOOTProjectHooks.h"

// ZeldaOOTProject: Phantom Ganon's tennis gets longer in the course of the fight. At first he misses about as early as
// in the original, with every bit of health he loses he keeps it up longer (see EnFhgFire_ZopMisses). The ball speeds
// up as in the original, except for his power hits (staff raised, purple aura), which are a lot faster.
#define ZOP_GND_LINK_SPEEDUP 1.0f
#define ZOP_GND_PHANTOM_SPEEDUP 2.0f
#define ZOP_GND_MAX_SPEED 20.0f
#define ZOP_GND_POWER_SPEEDUP 5.0f       // his first return with the charged staff
#define ZOP_GND_POWER_SPEEDUP_MORE 2.0f  // each one after that in the same rally
#define ZOP_GND_POWER_MAX_SPEED 28.0f
#define ZOP_POWER_HITS actor.home.rot.z // power hits this ball has taken (raise its top speed)
// Damage of a ball that hits Link (vanilla 0x10)
#define ZOP_GND_BALL_DAMAGE 0x20
// He hits the ball back when it is about this far from him (vanilla 150)
#define ZOP_GND_RETURN_DIST 70.0f
// A ball he throws on the fly (not in a volley) and Link hits back starts a rally this often; otherwise it hits him
#define ZOP_GND_FREE_BALL_RALLY 0.9f

// Rally state of the energy ball (unused slots of work / fwork for this kind of fire)
#define ZOP_RALLY_HITS work[FHGFIRE_US_2]
#define ZOP_ARC_HEIGHT fwork[FHGFIRE_UF_1]
#define ZOP_ARC_SIDE fwork[FHGFIRE_UF_2]
#define ZOP_ARC_TOTAL fwork[FHGFIRE_UF_4]
#define ZOP_ARC_TRAVELED fwork[12]
#define ZOP_ARC_PREV_Y fwork[13]
#define ZOP_ARC_PREV_SIDE fwork[14]
#define ZOP_ARC_PERP_YAW fwork[FHGFIRE_WARP_TEX_1_X]
#define ZOP_ARC_TARGET_X fwork[FHGFIRE_WARP_TEX_1_Y]
#define ZOP_ARC_TARGET_Y fwork[FHGFIRE_WARP_TEX_2_X]
#define ZOP_ARC_TARGET_Z fwork[FHGFIRE_WARP_TEX_2_Y]
#define ZOP_ARC_FLAT_END 0.8f // the last 20% of the way are straight, so the ball comes in where it is aimed
#define ZOP_GND_ARCS 0         // curved flight paths (off for now)
// The charge sound only plays while it is triggered every frame (like in the Twinrova fight): frames left, pitch
#define ZOP_SOUND_FRAMES fwork[FHGFIRE_WARP_TEX_SPEED]
#define ZOP_SOUND_FREQ fwork[FHGFIRE_WARP_ALPHA]
#define ZOP_SOUND_LENGTH 16
// Catch-and-burst attack (see BossGanondrof_ZopTryCatch): the caught ball flies into his spear, the burst balls
#define ZOP_FHGFIRE_ABSORB 3
#define ZOP_ABSORB_START_DIST fwork[FHGFIRE_BURST_SCALE]
#define ZOP_IS_BURST_BALL (this->actor.world.rot.z == GND_ZOP_BURST_BALL)
#define ZOP_GND_RALLY_SOUND 0 // rising charge sound on every hit (off for now)

/**
 * ZeldaOOTProject: one more hit in a rally. The Mirror Shield charge sound (Twinrova fight), rising in level and pitch
 * with every hit, the room lights up a bit more, and the flight to (tx, ty, tz) gets a curve: straight at first, then
 * ever higher arcs and sideways swings.
 */
static void EnFhgFire_ZopRallyHit(EnFhgFire* this, PlayState* play, f32 tx, f32 ty, f32 tz) {
    s16 hits = ++this->ZOP_RALLY_HITS;
    f32 dx = tx - this->actor.world.pos.x;
    f32 dy = ty - this->actor.world.pos.y;
    f32 dz = tz - this->actor.world.pos.z;

    this->ZOP_SOUND_FREQ = CLAMP_MAX(0.45f + hits * 0.09f, 1.9f);
    this->ZOP_SOUND_FRAMES = ZOP_GND_RALLY_SOUND ? ZOP_SOUND_LENGTH : 0.0f;

    // Late in a rally every hit gets more violent: from the 5th hit sparks of the ball fly off into the dark, from the
    // 7th lightning bolts lash out in all directions, with thunder, and rain starts; both grow with the rally
    if (hits >= 5) {
        s16 i;
        s16 count = CLAMP_MAX(hits - 3, 14) * 3;
        Vec3f accel = { 0.0f, -0.15f, 0.0f };

        for (i = 0; i < count; i++) {
            Vec3f vel;
            f32 speed = 4.0f + Rand_ZeroOne() * (3.0f + hits * 0.3f);
            s16 yaw = (s16)(Rand_ZeroOne() * 0xFFFF);
            f32 up = Rand_CenteredFloat(1.2f);

            vel.x = Math_SinS(yaw) * speed;
            vel.y = up * speed;
            vel.z = Math_CosS(yaw) * speed;
            // Big, bright sparks (the trail balls are about 150-230)
            EffectSsFhgFlash_SpawnLightBall(play, &this->actor.world.pos, &vel, &accel,
                                            (s16)(Rand_ZeroOne() * 120.0f) + 160,
                                            (i & 1) ? FHGFLASH_LIGHTBALL_LIGHTBLUE : FHGFLASH_LIGHTBALL_GREEN);
        }
        // and a burst of glitter right at the impact
        for (i = 0; i < count / 2; i++) {
            Vec3f vel = { Rand_CenteredFloat(14.0f), Rand_CenteredFloat(14.0f), Rand_CenteredFloat(14.0f) };
            Vec3f glitterAccel = { 0.0f, -0.3f, 0.0f };
            Color_RGBA8 prim = { 255, 255, 255, 255 };
            Color_RGBA8 env = { 100, 255, 200, 255 };

            EffectSsKiraKira_SpawnDispersed(play, &this->actor.world.pos, &vel, &glitterAccel, &prim, &env, 3000, 16);
        }
    }
    if (hits >= 7) {
        static Color_RGBA8 boltPrim = { 255, 255, 255, 255 };
        static Color_RGBA8 boltEnv = { 120, 230, 255, 255 };
        s16 i;
        s16 bolts = CLAMP_MAX(hits - 7, 6);

        for (i = 0; i < bolts; i++) {
            Vec3f pos = this->actor.world.pos;
            s16 yaw = (s16)(i * (0x10000 / bolts) + Rand_CenteredFloat(0x2000));

            EffectSsLightning_Spawn(play, &pos, &boltPrim, &boltEnv, (s16)(200 + Rand_ZeroOne() * 120.0f), yaw,
                                    (s16)(6 + Rand_ZeroOne() * 3.0f), (s16)CLAMP_MAX(1 + hits / 5, 4));
        }
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_THUNDER);
        ZOP_GndRallyFlash(1.0f);
        ZOP_GndRallyRain(true);
        if (hits >= 11) {
            Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_SPARK);
        }
    }
    ZOP_GndRallyGlow(hits);

    this->ZOP_ARC_TOTAL = sqrtf(SQ(dx) + SQ(dy) + SQ(dz));
    this->ZOP_ARC_TRAVELED = 0.0f;
    this->ZOP_ARC_PREV_Y = 0.0f;
    this->ZOP_ARC_PREV_SIDE = 0.0f;
    this->ZOP_ARC_TARGET_X = tx;
    this->ZOP_ARC_TARGET_Y = ty;
    this->ZOP_ARC_TARGET_Z = tz;
    this->ZOP_ARC_HEIGHT = (ZOP_GND_ARCS && (hits >= 3)) ? CLAMP_MAX(hits - 2, 10) * 16.0f : 0.0f;
    this->ZOP_ARC_SIDE = (ZOP_GND_ARCS && (hits >= 6))
                             ? ((Rand_ZeroOne() < 0.5f) ? -1.0f : 1.0f) * CLAMP_MAX(hits - 5, 10) * 11.0f
                             : 0.0f;
    this->ZOP_ARC_PERP_YAW = (f32)(s16)(Math_FAtan2F(dx, dz) * (0x8000 / M_PI) + 0x4000);
}

// Keeps the rally hit sound going for a moment after each hit
static void EnFhgFire_ZopRallySound(EnFhgFire* this) {
    static f32 sFreq;
    static f32 sVolume = 1.0f;
    static s8 sReverb = 0;
    s16 hits = this->ZOP_RALLY_HITS;

    if (this->ZOP_SOUND_FRAMES <= 0.0f) {
        return;
    }
    this->ZOP_SOUND_FRAMES -= 1.0f;
    sFreq = this->ZOP_SOUND_FREQ;
    Audio_PlaySfxGeneral((hits <= 5)    ? (NA_SE_IT_SHIELD_CHARGE_LV1 & ~SFX_FLAG)
                         : (hits <= 10) ? (NA_SE_IT_SHIELD_CHARGE_LV2 & ~SFX_FLAG)
                                        : (NA_SE_IT_SHIELD_CHARGE_LV3 & ~SFX_FLAG),
                         &gSfxDefaultPos, 4, &sFreq, &sVolume, &sReverb);
}

// Moves the ball along its current curve (the offsets come back to zero on arrival)
static void EnFhgFire_ZopRallyArc(EnFhgFire* this) {
    f32 p;
    f32 y;
    f32 side;
    s16 perp;

    if (this->ZOP_ARC_TOTAL <= 0.0f) {
        return;
    }
    perp = (s16)this->ZOP_ARC_PERP_YAW;
    // Progress from the real distance left to the target, measured on the straight path (without the curve offset)
    {
        f32 bx = this->actor.world.pos.x - Math_SinS(perp) * this->ZOP_ARC_PREV_SIDE;
        f32 by = this->actor.world.pos.y - this->ZOP_ARC_PREV_Y;
        f32 bz = this->actor.world.pos.z - Math_CosS(perp) * this->ZOP_ARC_PREV_SIDE;
        f32 left = sqrtf(SQ(this->ZOP_ARC_TARGET_X - bx) + SQ(this->ZOP_ARC_TARGET_Y - by) +
                         SQ(this->ZOP_ARC_TARGET_Z - bz));

        p = CLAMP(1.0f - left / this->ZOP_ARC_TOTAL, 0.0f, 1.0f);
        // never back (after passing the target the distance grows again)
        p = MAX(p, this->ZOP_ARC_TRAVELED);
        this->ZOP_ARC_TRAVELED = p;
    }
    {
        f32 curve = CLAMP_MAX(p / ZOP_ARC_FLAT_END, 1.0f);

        y = this->ZOP_ARC_HEIGHT * sinf(curve * M_PI);
        side = this->ZOP_ARC_SIDE * sinf(curve * M_PI);
    }
    this->actor.world.pos.y += y - this->ZOP_ARC_PREV_Y;
    this->actor.world.pos.x += Math_SinS(perp) * (side - this->ZOP_ARC_PREV_SIDE);
    this->actor.world.pos.z += Math_CosS(perp) * (side - this->ZOP_ARC_PREV_SIDE);
    this->ZOP_ARC_PREV_Y = y;
    this->ZOP_ARC_PREV_SIDE = side;
    if (p >= 1.0f) {
        this->ZOP_ARC_TOTAL = 0.0f;
    }
}

#define FLAGS (ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED)

typedef enum {
    /*  0 */ STRIKE_INIT,
    /* 10 */ STRIKE_BURST = 10,
    /* 11 */ STRIKE_TRAILS
} StrikeMode;

typedef enum {
    /* 0 */ TRAIL_INIT,
    /* 1 */ TRAIL_APPEAR,
    /* 2 */ TRAIL_DISSIPATE
} TrailMode;

typedef enum {
    /* 0 */ BALL_FIZZLE,
    /* 1 */ BALL_BURST,
    /* 2 */ BALL_IMPACT
} BallKillMode;

void EnFhgFire_Init(Actor* thisx, PlayState* play);
void EnFhgFire_Destroy(Actor* thisx, PlayState* play);
void EnFhgFire_Update(Actor* thisx, PlayState* play);
void EnFhgFire_Draw(Actor* thisx, PlayState* play);

void EnFhgFire_LightningStrike(EnFhgFire* this, PlayState* play);
void EnFhgFire_LightningTrail(EnFhgFire* this, PlayState* play);
void EnFhgFire_LightningShock(EnFhgFire* this, PlayState* play);
void EnFhgFire_LightningBurst(EnFhgFire* this, PlayState* play);
void EnFhgFire_SpearLight(EnFhgFire* this, PlayState* play);
void EnFhgFire_EnergyBall(EnFhgFire* this, PlayState* play);
void EnFhgFire_PhantomWarp(EnFhgFire* this, PlayState* play);

const ActorInit En_Fhg_Fire_InitVars = {
    ACTOR_EN_FHG_FIRE,
    ACTORCAT_BOSS,
    FLAGS,
    OBJECT_FHG,
    sizeof(EnFhgFire),
    (ActorFunc)EnFhgFire_Init,
    (ActorFunc)EnFhgFire_Destroy,
    (ActorFunc)EnFhgFire_Update,
    (ActorFunc)EnFhgFire_Draw,
    NULL,
};

static ColliderCylinderInit sCylinderInit = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_ENEMY,
        AC_ON | AC_TYPE_PLAYER,
        OC1_ON | OC1_TYPE_ALL,
        OC2_TYPE_1,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK6,
        { 0x00100700, 0x03, 0x20 },
        { 0x0D900700, 0x00, 0x00 },
        TOUCH_ON,
        BUMP_ON,
        OCELEM_ON,
    },
    { 20, 30, 10, { 0, 0, 0 } },
};

void EnFhgFire_SetUpdate(EnFhgFire* this, EnFhgFireUpdateFunc updateFunc) {
    this->updateFunc = updateFunc;
}

void EnFhgFire_Init(Actor* thisx, PlayState* play) {
    s32 pad;
    EnFhgFire* this = (EnFhgFire*)thisx;
    Player* player = GET_PLAYER(play);

    ActorShape_Init(&this->actor.shape, 0.0f, NULL, 0.0f);
    if ((this->actor.params == FHGFIRE_LIGHTNING_SHOCK) || (this->actor.params == FHGFIRE_LIGHTNING_BURST) ||
        (this->actor.params == FHGFIRE_ENERGY_BALL)) {
        Collider_InitCylinder(play, &this->collider);
        Collider_SetCylinder(play, &this->collider, &this->actor, &sCylinderInit);
    }
    this->fwork[FHGFIRE_ALPHA] = 200.0f;
    Actor_SetScale(&this->actor, 0.0f);

    if (this->actor.params == FHGFIRE_LIGHTNING_STRIKE) {
        EnFhgFire_SetUpdate(this, EnFhgFire_LightningStrike);
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_THUNDER);
    } else if (this->actor.params >= FHGFIRE_LIGHTNING_TRAIL) {
        EnFhgFire_SetUpdate(this, EnFhgFire_LightningTrail);
        this->actor.shape.rot = this->actor.world.rot;
    }
    if (this->actor.params == FHGFIRE_LIGHTNING_SHOCK) {
        this->actor.draw = NULL;
        EnFhgFire_SetUpdate(this, EnFhgFire_LightningShock);
        this->actor.speedXZ = 30.0f;
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_SPARK);
    } else if (this->actor.params == FHGFIRE_LIGHTNING_BURST) {
        EnFhgFire_SetUpdate(this, EnFhgFire_LightningBurst);
        this->fwork[FHGFIRE_ALPHA] = 255.0f;
        this->work[FHGFIRE_TIMER] = 32;
        this->work[FHGFIRE_FX_TIMER] = 50;
        this->lensFlareTimer = 10;

        this->fwork[FHGFIRE_BURST_SCALE] = this->actor.world.rot.x / 100.0f;
        this->collider.dim.radius = this->actor.world.rot.x * 0.13f;
        this->collider.dim.height = this->actor.world.rot.x * 0.13f;
        this->collider.dim.yShift = 0;
    } else if (this->actor.params == FHGFIRE_SPEAR_LIGHT) {
        osSyncPrintf("yari hikari ct 1\n"); // "light spear"
        EnFhgFire_SetUpdate(this, EnFhgFire_SpearLight);
        osSyncPrintf("yari hikari ct 2\n");
        this->work[FHGFIRE_TIMER] = this->actor.world.rot.x;
        this->work[FHGFIRE_FIRE_MODE] = this->actor.world.rot.y;
    } else if ((this->actor.params == FHGFIRE_WARP_EMERGE) || (this->actor.params == FHGFIRE_WARP_RETREAT) ||
               (this->actor.params == FHGFIRE_WARP_DEATH)) {
        Actor_SetScale(&this->actor, 7.0f);
        EnFhgFire_SetUpdate(this, EnFhgFire_PhantomWarp);
        if (this->actor.params == FHGFIRE_WARP_DEATH) {
            this->work[FHGFIRE_TIMER] = 440;
            this->actor.scale.z = 1.0f;
        } else {
            this->work[FHGFIRE_TIMER] = 76;
            Audio_PlayActorSound2(&this->actor, NA_SE_EV_FANTOM_WARP_S);
            Audio_PlayActorSound2(&this->actor, NA_SE_EV_FANTOM_WARP_S2);
        }
    } else if (this->actor.params == FHGFIRE_ENERGY_BALL) {
        f32 dxL;
        f32 dyL;
        f32 dzL;
        f32 dxzL;

        this->actor.speedXZ = (this->actor.world.rot.x == 0) ? 8.0f : 3.0f;
        EnFhgFire_SetUpdate(this, EnFhgFire_EnergyBall);

        this->work[FHGFIRE_TIMER] = 70;
        this->work[FHGFIRE_FX_TIMER] = 2;

        dxL = player->actor.world.pos.x - this->actor.world.pos.x;
        dyL = player->actor.world.pos.y + 30.0f - this->actor.world.pos.y;
        dzL = player->actor.world.pos.z - this->actor.world.pos.z;
        this->actor.world.rot.y = Math_FAtan2F(dxL, dzL) * (0x8000 / M_PI);
        dxzL = sqrtf(SQ(dxL) + SQ(dzL));
        this->actor.world.rot.x = Math_FAtan2F(dyL, dxzL) * (0x8000 / M_PI);
        this->collider.dim.radius = 40;
        this->collider.dim.height = 50;
        this->collider.dim.yShift = -25;
        this->lightNode = LightContext_InsertLight(play, &play->lightCtx, &this->lightInfo);
        Lights_PointNoGlowSetInfo(&this->lightInfo, this->actor.world.pos.x, this->actor.world.pos.y,
                                  this->actor.world.pos.z, 255, 255, 255, 255);
    }
}

void EnFhgFire_Destroy(Actor* thisx, PlayState* play) {
    s32 pad;
    EnFhgFire* this = (EnFhgFire*)thisx;

    if ((this->actor.params == FHGFIRE_LIGHTNING_SHOCK) || (this->actor.params == FHGFIRE_LIGHTNING_BURST) ||
        (this->actor.params == FHGFIRE_ENERGY_BALL)) {
        Collider_DestroyCylinder(play, &this->collider);
    }

    if (this->actor.params == FHGFIRE_ENERGY_BALL) {
        LightContext_RemoveLight(play, &play->lightCtx, this->lightNode);
    }
}

void EnFhgFire_LightningStrike(EnFhgFire* this, PlayState* play) {
    Camera* camera = Play_GetCamera(play, 0);
    s16 i;

    switch (this->work[FHGFIRE_FIRE_MODE]) {
        case STRIKE_INIT:
            this->work[FHGFIRE_FIRE_MODE] = STRIKE_BURST;
            this->work[FHGFIRE_TIMER] = 7;
            break;
        case STRIKE_BURST:
            this->actor.shape.rot.y =
                Camera_GetInputDirYaw(camera) + 0x8000 * (this->work[FHGFIRE_VARIANCE_TIMER] & 0xFF);
            Math_ApproachF(&this->fwork[FHGFIRE_SCALE], 1.0f, 1.0f, 0.2f);

            if (this->work[FHGFIRE_TIMER] == 0) {
                this->work[FHGFIRE_FIRE_MODE] = STRIKE_TRAILS;
                this->actor.shape.rot.z += 0x8000;
                this->work[FHGFIRE_TIMER] = 37;
                this->actor.world.pos.y -= 200.0f;

                Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_FHG_FIRE, this->actor.world.pos.x,
                                   this->actor.world.pos.y, this->actor.world.pos.z, 500, 0, 0,
                                   FHGFIRE_LIGHTNING_BURST);
                {
                    Vec3f sp7C;
                    Vec3f sp70 = { 0.0f, -1.0f, 0.0f };

                    for (i = 0; i < 35; i++) {
                        sp7C.x = Rand_CenteredFloat(30.f);
                        sp7C.y = Rand_ZeroFloat(5.0f) + 3.0f;
                        sp7C.z = Rand_CenteredFloat(30.f);
                        sp70.y = -0.2f;
                        EffectSsFhgFlash_SpawnLightBall(play, &this->actor.world.pos, &sp7C, &sp70,
                                                        (s16)(Rand_ZeroOne() * 100.0f) + 240, FHGFLASH_LIGHTBALL_GREEN);
                    }
                }
                Actor_RequestQuakeAndRumble(&this->actor, play, 4, 10);
            }

            break;
        case STRIKE_TRAILS:
            this->actor.shape.rot.y =
                Camera_GetInputDirYaw(camera) + (this->work[FHGFIRE_VARIANCE_TIMER] & 0xFF) * 0x8000;

            Math_ApproachF(&this->fwork[FHGFIRE_SCALE], 0.0f, 1.0f, 0.2f);
            if (this->work[FHGFIRE_TIMER] == 30) {
                s16 randY = (Rand_ZeroOne() < 0.5f) ? 0x1000 : 0;

                for (i = 0; i < 8; i++) {
                    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_FHG_FIRE, this->actor.world.pos.x,
                                       this->actor.world.pos.y, this->actor.world.pos.z, 0, (i * 0x2000) + randY,
                                       0x4000, FHGFIRE_LIGHTNING_TRAIL + i);
                }

                for (i = 0; i < 8; i++) {
                    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_FHG_FIRE, this->actor.world.pos.x,
                                       this->actor.world.pos.y, this->actor.world.pos.z, 0, (i * 0x2000) + randY, 0,
                                       FHGFIRE_LIGHTNING_SHOCK);
                }
            }

            if (this->work[FHGFIRE_TIMER] == 0) {
                Actor_Kill(&this->actor);
            }
    }

    Actor_SetScale(&this->actor, this->fwork[FHGFIRE_SCALE]);
}

void EnFhgFire_LightningTrail(EnFhgFire* this, PlayState* play) {
    osSyncPrintf("FF MOVE 1\n");
    this->actor.shape.rot.x += (s16)(Rand_ZeroOne() * 4000.0f) + 0x4000;

    switch (this->work[FHGFIRE_FIRE_MODE]) {
        case TRAIL_INIT:
            this->work[FHGFIRE_FIRE_MODE] = TRAIL_APPEAR;
            this->work[FHGFIRE_TIMER] = (s16)(Rand_ZeroOne() * 7.0f) + 7;
        case TRAIL_APPEAR:
            Math_ApproachF(&this->fwork[FHGFIRE_SCALE], 1.7f, 1.0f, 0.34f);

            if (this->work[FHGFIRE_TIMER] == 0) {
                this->work[FHGFIRE_FIRE_MODE] = TRAIL_DISSIPATE;
                this->work[FHGFIRE_TIMER] = 10;
                this->actor.world.pos.z += Math_SinS(this->actor.shape.rot.y) * -200.0f * this->fwork[FHGFIRE_SCALE];
                this->actor.world.pos.x += Math_CosS(this->actor.shape.rot.y) * 200.0f * this->fwork[FHGFIRE_SCALE];
                this->actor.shape.rot.y += 0x8000;
            }
            break;
        case TRAIL_DISSIPATE:
            Math_ApproachZeroF(&this->fwork[FHGFIRE_SCALE], 1.0f, 0.34f);
            if (this->work[FHGFIRE_TIMER] == 0) {
                Actor_Kill(&this->actor);
            }
            break;
    }

    Actor_SetScale(&this->actor, this->fwork[FHGFIRE_SCALE]);
    if (this->actor.scale.x > 1.0f) {
        this->actor.scale.x = 1.0f;
    }

    osSyncPrintf("FF MOVE 2\n");
}

void EnFhgFire_LightningShock(EnFhgFire* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    Vec3f pos;

    if (this->collider.base.atFlags & AT_HIT) {
        this->collider.base.atFlags &= ~AT_HIT;
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_HIT_THUNDER);
    }

    if (Rand_ZeroOne() < 0.5f) {
        pos = this->actor.world.pos;
        pos.y -= 20.0f;
        EffectSsFhgFlash_SpawnShock(play, &this->actor, &pos, 200, FHGFLASH_SHOCK_NO_ACTOR);
    }

    Actor_MoveXZGravity(&this->actor);
    Collider_UpdateCylinder(&this->actor, &this->collider);
    if (player->invincibilityTimer == 0) {
        CollisionCheck_SetAT(play, &play->colChkCtx, &this->collider.base);
    }

    Actor_UpdateBgCheckInfo(play, &this->actor, 50.0f, 50.0f, 100.0f, 1);
    if (this->actor.bgCheckFlags & BGCHECKFLAG_WALL) {
        Actor_Kill(&this->actor);
    }
}

void EnFhgFire_LightningBurst(EnFhgFire* this, PlayState* play) {
    Player* player = GET_PLAYER(play);

    play->envCtx.fillScreen = true;
    this->actor.shape.rot.y += 0x1000;

    if (this->work[FHGFIRE_FX_TIMER] == 49) {
        play->envCtx.unk_BF = 1;
        play->envCtx.unk_D6 = 0xFF;
    }
    if (this->work[FHGFIRE_FX_TIMER] == 31) {
        play->envCtx.unk_BF = 0x00;
        play->envCtx.unk_D6 = 0x14;
    }
    if (this->work[FHGFIRE_FX_TIMER] >= 48) {
        play->envCtx.screenFillColor[0] = play->envCtx.screenFillColor[1] = play->envCtx.screenFillColor[2] = 255;

        if ((this->work[FHGFIRE_TIMER] % 2) != 0) {
            play->envCtx.screenFillColor[3] = 70;
        } else {
            play->envCtx.screenFillColor[3] = 0;
        }
    } else {
        play->envCtx.screenFillColor[3] = 0;
    }

    if (this->work[FHGFIRE_TIMER] <= 20) {
        Math_ApproachZeroF(&this->fwork[FHGFIRE_ALPHA], 1.0f, 45.0f);
        Math_ApproachZeroF(&this->fwork[FHGFIRE_SCALE], 1.0f, 0.5f);
    } else {
        Math_ApproachF(&this->fwork[FHGFIRE_SCALE], this->fwork[FHGFIRE_BURST_SCALE], 0.5f, 3.0f);
    }

    Actor_SetScale(&this->actor, this->fwork[FHGFIRE_SCALE]);
    if (this->fwork[FHGFIRE_BURST_SCALE] > 3.0f) {
        Collider_UpdateCylinder(&this->actor, &this->collider);
        if (player->invincibilityTimer == 0) {
            CollisionCheck_SetAT(play, &play->colChkCtx, &this->collider.base);
        }
    }

    if (this->work[FHGFIRE_TIMER] == 0) {
        Actor_Kill(&this->actor);
        play->envCtx.fillScreen = false;
    }

    if (this->lensFlareTimer != 0) {
        this->lensFlareTimer--;
        this->lensFlareOn = true;
        Math_ApproachF(&this->lensFlareScale, 40.0f, 0.3f, 10.0f);
    } else {
        Math_ApproachZeroF(&this->lensFlareScale, 1.0f, 5.0f);
        if (this->lensFlareScale == 0.0f) {
            this->lensFlareOn = false;
        }
    }

    gCustomLensFlareOn = this->lensFlareOn;
    gCustomLensFlarePos = this->actor.world.pos;
    gLensFlareScale = this->lensFlareScale;
    gLensFlareColorIntensity = 10.0f;
    gLensFlareScreenFillAlpha = 0;
}

void EnFhgFire_SpearLight(EnFhgFire* this, PlayState* play) {
    BossGanondrof* bossGnd;
    s16 i;

    osSyncPrintf("yari hikari 1\n");
    bossGnd = (BossGanondrof*)this->actor.parent;
    if ((this->work[FHGFIRE_VARIANCE_TIMER] % 2) != 0) {
        Actor_SetScale(&this->actor, 6.0f);
    } else {
        Actor_SetScale(&this->actor, 5.25f);
    }

    this->actor.world.pos = bossGnd->spearTip;
    this->actor.shape.rot.z += (s16)(Rand_ZeroOne() * 0x4E20) + 0x4000;

    osSyncPrintf("yari hikari 2\n");
    if (this->work[FHGFIRE_FIRE_MODE] == FHGFIRE_LIGHT_GREEN) {
        Vec3f ballPos;
        Vec3f ballVel = { 0.0f, 0.0f, 0.0f };
        Vec3f ballAccel = { 0.0f, 0.0f, 0.0f };

        osSyncPrintf("FLASH !!\n");

        for (i = 0; i < 2; i++) {
            ballPos.x = Rand_CenteredFloat(20.0f) + this->actor.world.pos.x;
            ballPos.y = Rand_CenteredFloat(20.0f) + this->actor.world.pos.y;
            ballPos.z = Rand_CenteredFloat(20.0f) + this->actor.world.pos.z;
            ballAccel.y = -0.08f;

            EffectSsFhgFlash_SpawnLightBall(play, &ballPos, &ballVel, &ballAccel, (s16)(Rand_ZeroOne() * 80.0f) + 150,
                                            FHGFLASH_LIGHTBALL_GREEN);
        }
    }

    if (this->work[FHGFIRE_TIMER] == 0) {
        Actor_Kill(&this->actor);
    }
}

// Does Phantom Ganon miss this return (returnCount = Link's returns so far)? Early in phase 2: possible from the 3rd on
// (50%), for sure from the 4th; at the end: from the 9th (30%), for sure from the 12th.
static s32 EnFhgFire_ZopMisses(BossGanondrof* bossGnd, s16 returnCount) {
    f32 p = CLAMP(1.0f - (s8)bossGnd->actor.colChkInfo.health / (f32)GND_ZOP_PHASE2_HEALTH, 0.0f, 1.0f);
    s16 minReturns = 2 + (s16)(p * 6.0f + 0.5f);
    s16 sureMiss = 4 + (s16)(p * 8.0f + 0.5f);
    f32 chance = 0.5f - 0.2f * p;

    return (returnCount >= sureMiss) || ((returnCount > minReturns) && (Rand_ZeroOne() < chance));
}

/**
 * ZeldaOOTProject: a ball of Phantom Ganon's burst. It closes in on Link and only a spin attack or Nayru's Love sends it
 * back, and then it hits Phantom Ganon for sure. Shield and normal slashes don't help. Returns the kill mode.
 */
static u8 EnFhgFire_ZopBurstBall(EnFhgFire* this, PlayState* play, BossGanondrof* bossGnd, Player* player, f32 dxL,
                                 f32 dyL, f32 dzL) {
    f32 dist = sqrtf(SQ(dxL) + SQ(dyL) + SQ(dzL));
    s32 reflect = false;
    s32 byNayru = false;

    if (this->collider.base.acFlags & AC_HIT) {
        ColliderInfo* hurtbox = this->collider.info.acHitInfo;

        this->collider.base.acFlags &= ~AC_HIT;
        if ((hurtbox != NULL) && !(hurtbox->toucher.dmgFlags & 0x00100000) &&
            ((hurtbox->toucher.dmgFlags & (DMG_SPIN_KOKIRI | DMG_SPIN_GIANT | DMG_SPIN_MASTER)) ||
             (player->meleeWeaponAnimation >= PLAYER_MWA_SPIN_ATTACK_1H))) {
            reflect = true;
        }
    }
    if (!reflect && (gSaveContext.nayrusLoveTimer != 0) && (dist <= 70.0f)) {
        reflect = byNayru = true;
    }

    if (reflect) {
        f32 dxPG = bossGnd->targetPos.x - this->actor.world.pos.x;
        f32 dyPG = bossGnd->targetPos.y - this->actor.world.pos.y;
        f32 dzPG = bossGnd->targetPos.z - this->actor.world.pos.z;
        Vec3f accel = { 0.0f, -0.5f, 0.0f };
        s16 i;

        for (i = 0; i < 30; i++) {
            Vec3f vel = { Rand_CenteredFloat(20.0f), Rand_CenteredFloat(20.0f), Rand_CenteredFloat(20.0f) };

            EffectSsFhgFlash_SpawnLightBall(play, &this->actor.world.pos, &vel, &accel,
                                            (s16)(Rand_ZeroOne() * 25.0f) + 50, FHGFLASH_LIGHTBALL_LIGHTBLUE);
        }
        // Back to him, and it can't miss
        this->work[FHGFIRE_FIRE_MODE] = FHGFIRE_LIGHT_BLUE;
        this->work[FHGFIRE_RETURN_COUNT] = 100;
        this->work[FHGFIRE_FX_TIMER] = 2;
        this->actor.speedXZ = 14.0f;
        this->actor.world.rot.y = (s16)(Math_FAtan2F(dxPG, dzPG) * (0x8000 / M_PI));
        this->actor.world.rot.x = (s16)(Math_FAtan2F(dyPG, sqrtf(SQ(dxPG) + SQ(dzPG))) * (0x8000 / M_PI));
        ZOP_GndRallyFlash(0.6f);
        Audio_PlaySfxGeneral(byNayru ? NA_SE_IT_SHIELD_REFLECT_MG : NA_SE_IT_SWORD_REFLECT_MG,
                             &player->actor.projectedPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                             &gSfxDefaultReverb);
        Rumble_Request(this->actor.xyzDistToPlayerSq, 0xB4, 0x14, 0x64);
        return BALL_FIZZLE;
    }

    // Closes in on Link
    if (dist > 100.0f) {
        Math_ApproachS(&this->actor.world.rot.y, (s16)(Math_FAtan2F(dxL, dzL) * (0x8000 / M_PI)), 3, 0x480);
        Math_ApproachS(&this->actor.world.rot.x,
                       (s16)(Math_FAtan2F(dyL, sqrtf(SQ(dxL) + SQ(dzL))) * (0x8000 / M_PI)), 3, 0x3C0);
    }
    if (dist <= 25.0f) {
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_HIT_THUNDER);
        Actor_SetPlayerKnockback(play, &this->actor, 3.0f, this->actor.world.rot.y, 0.0f, 3,
                                             ZOP_DifficultyEnabled() ? ZOP_GND_BALL_DAMAGE : 0x10);
        return BALL_BURST;
    }
    return BALL_FIZZLE;
}

void EnFhgFire_EnergyBall(EnFhgFire* this, PlayState* play) {
    f32 dxL;
    f32 dyL;
    f32 dzL;
    f32 dxzL;
    f32 dxPG;
    f32 dyPG;
    f32 dzPG;
    u8 killMode = BALL_FIZZLE;
    u8 canBottleReflect1;
    Player* player = GET_PLAYER(play);

    if (this->work[FHGFIRE_KILL_TIMER] != 0) {
        this->work[FHGFIRE_KILL_TIMER]--;
        if (this->work[FHGFIRE_KILL_TIMER] == 0) {
            Actor_Kill(&this->actor);
            return;
        }
    } else {
        s32 canBottleReflect2;
        BossGanondrof* bossGnd = (BossGanondrof*)this->actor.parent;

        dxPG = bossGnd->targetPos.x - this->actor.world.pos.x;
        dyPG = bossGnd->targetPos.y - this->actor.world.pos.y;
        dzPG = bossGnd->targetPos.z - this->actor.world.pos.z;
        dxL = player->actor.world.pos.x - this->actor.world.pos.x;
        dyL = player->actor.world.pos.y + 40.0f - this->actor.world.pos.y;
        dzL = player->actor.world.pos.z - this->actor.world.pos.z;
        Actor_UpdateVelocityXYZ(&this->actor);
        Actor_UpdatePos(&this->actor);
        if (ZOP_DifficultyEnabled()) {
            EnFhgFire_ZopRallyArc(this);
            EnFhgFire_ZopRallySound(this);
        }
        if (this->work[FHGFIRE_VARIANCE_TIMER] & 1) {
            Actor_SetScale(&this->actor, 6.0f);
        } else {
            Actor_SetScale(&this->actor, 5.25f);
        }
        this->actor.shape.rot.z += (s16)(Rand_ZeroOne() * 0x4E20) + 0x4000;
        {
            u8 lightBallColor1 = FHGFLASH_LIGHTBALL_GREEN;
            s16 i1;
            Vec3f spD4;
            Vec3f spC8 = { 0.0f, 0.0f, 0.0f };
            Vec3f spBC = { 0.0f, 0.0f, 0.0f };

            if (this->work[FHGFIRE_FIRE_MODE] >= FHGFIRE_LIGHT_BLUE) {
                lightBallColor1 = FHGFLASH_LIGHTBALL_LIGHTBLUE;
            }
            for (i1 = 0; i1 < 3; i1++) {
                spD4.x = Rand_CenteredFloat(20.0f) + this->actor.world.pos.x;
                spD4.y = Rand_CenteredFloat(20.0f) + this->actor.world.pos.y;
                spD4.z = Rand_CenteredFloat(20.0f) + this->actor.world.pos.z;
                spBC.y = -0.08f;
                EffectSsFhgFlash_SpawnLightBall(play, &spD4, &spC8, &spBC, (s16)(Rand_ZeroOne() * 80.0f) + 150,
                                                lightBallColor1);
            }
        }
        switch (this->work[FHGFIRE_FIRE_MODE]) {
            case FHGFIRE_LIGHT_GREEN:
                if (ZOP_IS_BURST_BALL) {
                    killMode = EnFhgFire_ZopBurstBall(this, play, bossGnd, player, dxL, dyL, dzL);
                    break;
                }
                canBottleReflect1 =
                    ((player->stateFlags1 & PLAYER_STATE1_SWINGING_BOTTLE) &&
                     (ABS((s16)(player->actor.shape.rot.y - (s16)(bossGnd->actor.yawTowardsPlayer + 0x8000))) <
                      0x2000) &&
                     (sqrtf(SQ(dxL) + SQ(dyL) + SQ(dzL)) <= 25.0f))
                        ? true
                        : false;
                if ((this->collider.base.acFlags & AC_HIT) || canBottleReflect1) {
                    ColliderInfo* hurtbox = this->collider.info.acHitInfo;
                    s16 i2;
                    Vec3f spA8;
                    Vec3f sp9C = { 0.0f, -0.5f, 0.0f };
                    s16 angleModX;
                    s16 angleModY;

                    for (i2 = 0; i2 < 30; i2++) {
                        spA8.x = Rand_CenteredFloat(20.0f);
                        spA8.y = Rand_CenteredFloat(20.0f);
                        spA8.z = Rand_CenteredFloat(20.0f);
                        EffectSsFhgFlash_SpawnLightBall(play, &this->actor.world.pos, &spA8, &sp9C,
                                                        (s16)(Rand_ZeroOne() * 25.0f) + 50, FHGFLASH_LIGHTBALL_GREEN);
                    }
                    canBottleReflect2 = canBottleReflect1;
                    if (!canBottleReflect2 && (hurtbox->toucher.dmgFlags & 0x00100000)) {
                        killMode = BALL_IMPACT;
                        Audio_PlaySfxGeneral(NA_SE_IT_SHIELD_REFLECT_MG, &player->actor.projectedPos, 4,
                                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                             &gSfxDefaultReverb);
                        Rumble_Request(this->actor.xyzDistToPlayerSq, 0xFF, 0x14, 0x96);
                    } else {
                        if (bossGnd->flyMode == GND_FLY_NEUTRAL) {
                            if (ZOP_DifficultyEnabled()) {
                                // No more misses: mostly he takes it up and a rally starts, rarely it hits him
                                angleModX = 0;
                                angleModY = 0;
                                if (Rand_ZeroOne() < ZOP_GND_FREE_BALL_RALLY) {
                                    bossGnd->flyMode = GND_FLY_RETURN;
                                    bossGnd->returnSuccess = false;
                                    bossGnd->timers[0] = 100;
                                    this->work[FHGFIRE_RETURN_COUNT] = 1;
                                    this->actor.speedXZ = 9.0f;
                                } else {
                                    this->actor.speedXZ = 15.0f;
                                }
                            } else {
                                angleModX = Rand_CenteredFloat(0x2000);
                                angleModY = Rand_CenteredFloat(0x2000);
                                this->actor.speedXZ = 15.0f;
                            }
                        } else {
                            angleModX = 0;
                            angleModY = 0;
                            this->work[FHGFIRE_RETURN_COUNT]++;
                            if (ZOP_DifficultyEnabled()) {
                                if (EnFhgFire_ZopMisses(bossGnd, this->work[FHGFIRE_RETURN_COUNT])) {
                                    this->work[FHGFIRE_RETURN_COUNT] = 100;
                                }
                                // A spin attack hits it back harder, but no longer shortens the rally
                                this->actor.speedXZ += (!canBottleReflect2 && (player->meleeWeaponAnimation >= 24))
                                                           ? ZOP_GND_LINK_SPEEDUP + 0.7f
                                                           : ZOP_GND_LINK_SPEEDUP;
                            } else {
                                if ((this->work[FHGFIRE_RETURN_COUNT] > 3) && (Rand_ZeroOne() < 0.5f)) {
                                    this->work[FHGFIRE_RETURN_COUNT] = 100;
                                }

                                if (!canBottleReflect2 && (player->meleeWeaponAnimation >= 24)) {
                                    this->actor.speedXZ = 20.0f;
                                    this->work[FHGFIRE_RETURN_COUNT] = 4;
                                } else {
                                    this->actor.speedXZ += 1.0f;
                                }
                            }
                        }
                        this->actor.world.rot.y = (s16)(Math_FAtan2F(dxPG, dzPG) * (0x8000 / M_PI)) + angleModY;
                        this->actor.world.rot.x =
                            (s16)(Math_FAtan2F(dyPG, sqrtf((dxPG * dxPG) + (dzPG * dzPG))) * (0x8000 / M_PI)) +
                            angleModX;
                        this->work[FHGFIRE_FIRE_MODE] = FHGFIRE_LIGHT_BLUE;
                        this->work[FHGFIRE_FX_TIMER] = 2;
                        if (ZOP_DifficultyEnabled()) {
                            EnFhgFire_ZopRallyHit(this, play, bossGnd->targetPos.x, bossGnd->targetPos.y,
                                                  bossGnd->targetPos.z);
                            if (this->work[FHGFIRE_RETURN_COUNT] < 100) {
                                BossGanondrof_ZopTryPowerUp(bossGnd, play, this->ZOP_RALLY_HITS);
                            }
                        }
                        Audio_PlaySfxGeneral(NA_SE_IT_SWORD_REFLECT_MG, &player->actor.projectedPos, 4,
                                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                             &gSfxDefaultReverb);
                        Rumble_Request(this->actor.xyzDistToPlayerSq, 0xB4, 0x14, 0x64);
                    }
                } else if (sqrtf(SQ(dxL) + SQ(dyL) + SQ(dzL)) <= 25.0f) {
                    killMode = BALL_BURST;
                    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_HIT_THUNDER);
                    if ((bossGnd->flyMode >= GND_FLY_VOLLEY) && (this->work[FHGFIRE_RETURN_COUNT] >= 2)) {
                        Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_LAUGH);
                    }
                    Actor_SetPlayerKnockback(play, &this->actor, 3.0f, this->actor.world.rot.y, 0.0f, 3,
                                             ZOP_DifficultyEnabled() ? ZOP_GND_BALL_DAMAGE : 0x10);
                }
                break;
            case FHGFIRE_LIGHT_BLUE:
                if ((bossGnd->flyMode == GND_FLY_RETURN) && (this->work[FHGFIRE_RETURN_COUNT] < 100)) {
                    this->actor.world.rot.y = Math_FAtan2F(dxPG, dzPG) * (0x8000 / M_PI);
                    // His swing starts this far away and takes 8 frames, so the ball turns about the base distance
                    // in front of him (ZeldaOOTProject: later, closer to him)
                    if ((sqrtf(SQ(dxPG) + SQ(dzPG)) <
                         ((ZOP_DifficultyEnabled() ? ZOP_GND_RETURN_DIST : 150.0f) + (this->actor.speedXZ * 8.0f)))) {
                        if (BossGanondrof_ZopTryCatch(bossGnd, play, this->ZOP_RALLY_HITS)) {
                            // ZeldaOOTProject: he catches it instead of hitting it back
                            this->work[FHGFIRE_FIRE_MODE] = ZOP_FHGFIRE_ABSORB;
                            this->ZOP_ABSORB_START_DIST =
                                Math_Vec3f_DistXYZ(&this->actor.world.pos, &bossGnd->spearTip);
                        } else {
                            this->work[FHGFIRE_FIRE_MODE] = FHGFIRE_LIGHT_REFLECT;
                            bossGnd->returnSuccess = true;
                            this->work[FHGFIRE_TIMER] = 8;
                        }
                    }
                } else {
                    if (this->work[FHGFIRE_RETURN_COUNT] >= 100) {
                        if ((sqrtf(SQ(dxPG) + SQ(dyPG) + SQ(dzPG)) < 100.0f)) {
                            bossGnd->returnSuccess = true;
                        }
                        this->actor.world.rot.y = Math_FAtan2F(dxPG, dzPG) * (0x8000 / M_PI);
                        this->actor.world.rot.x = Math_FAtan2F(dyPG, sqrtf(SQ(dxPG) + SQ(dzPG))) * (0x8000 / M_PI);
                    }
                    if ((fabsf(dxPG) < 30.0f) && (fabsf(dzPG) < 30.0f) && (fabsf(dyPG) < 45.0f)) {
                        killMode = BALL_IMPACT;
                        bossGnd->returnCount = this->work[FHGFIRE_RETURN_COUNT] + 1;
                        if (ZOP_DifficultyEnabled()) {
                            ZOP_GndRallyFlash(1.8f); // Phantom Ganon is hit: one big bright flash
                        }
                        Audio_PlaySfxGeneral(NA_SE_EN_FANTOM_HIT_THUNDER, &bossGnd->actor.projectedPos, 4,
                                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                             &gSfxDefaultReverb);
                        Audio_PlaySfxGeneral(NA_SE_EN_FANTOM_DAMAGE, &bossGnd->actor.projectedPos, 4,
                                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                             &gSfxDefaultReverb);
                    }
                }
                break;
            case ZOP_FHGFIRE_ABSORB: {
                // Flies into his spear, getting smaller, and is gone
                f32 dx = bossGnd->spearTip.x - this->actor.world.pos.x;
                f32 dy = bossGnd->spearTip.y - this->actor.world.pos.y;
                f32 dz = bossGnd->spearTip.z - this->actor.world.pos.z;
                f32 d = sqrtf(SQ(dx) + SQ(dy) + SQ(dz));

                this->work[FHGFIRE_FX_TIMER] = 2; // no collision on the way
                this->actor.world.rot.y = (s16)(Math_FAtan2F(dx, dz) * (0x8000 / M_PI));
                this->actor.world.rot.x = (s16)(Math_FAtan2F(dy, sqrtf(SQ(dx) + SQ(dz))) * (0x8000 / M_PI));
                this->actor.speedXZ = CLAMP(d * 0.8f, 12.0f, 32.0f);
                if (this->ZOP_ABSORB_START_DIST > 1.0f) {
                    Actor_SetScale(&this->actor,
                                   this->actor.scale.x * CLAMP(d / this->ZOP_ABSORB_START_DIST, 0.2f, 1.0f));
                }
                if (d < 15.0f) {
                    Vec3f accel = { 0.0f, 0.0f, 0.0f };
                    s16 i;

                    for (i = 0; i < 20; i++) {
                        Vec3f vel = { Rand_CenteredFloat(8.0f), Rand_CenteredFloat(8.0f), Rand_CenteredFloat(8.0f) };

                        EffectSsFhgFlash_SpawnLightBall(play, &bossGnd->spearTip, &vel, &accel,
                                                        (s16)(Rand_ZeroOne() * 60.0f) + 100,
                                                        FHGFLASH_LIGHTBALL_LIGHTBLUE);
                    }
                    ZOP_GndRallyGlow(0);
                    ZOP_GndRallyRain(false);
                    BossGanondrof_ZopEndPower();
                    this->actor.draw = NULL;
                    this->work[FHGFIRE_KILL_TIMER] = 30;
                    BossGanondrof_ZopAbsorbed(bossGnd, play);
                    return;
                }
                break;
            }
            case FHGFIRE_LIGHT_REFLECT:
                if (this->work[FHGFIRE_TIMER] == 0) {
                    s16 i3;
                    Vec3f sp88;
                    Vec3f sp7C = { 0.0f, -0.5f, 0.0f };

                    for (i3 = 0; i3 < 30; i3++) {
                        sp88.x = Rand_CenteredFloat(20.0f);
                        sp88.y = Rand_CenteredFloat(20.0f);
                        sp88.z = Rand_CenteredFloat(20.0f);
                        EffectSsFhgFlash_SpawnLightBall(play, &this->actor.world.pos, &sp88, &sp7C,
                                                        (s16)(Rand_ZeroOne() * 40.0f) + 80, FHGFLASH_LIGHTBALL_GREEN);
                    }
                    this->actor.world.rot.y = Math_FAtan2F(dxL, dzL) * (0x8000 / M_PI);
                    dxzL = sqrtf(SQ(dxL) + SQ(dzL));
                    this->actor.world.rot.x = Math_FAtan2F(dyL, dxzL) * (0x8000 / M_PI);
                    this->work[FHGFIRE_FIRE_MODE] = FHGFIRE_LIGHT_GREEN;
                    Audio_PlayActorSound2(&this->actor, NA_SE_IT_SWORD_REFLECT_MG);
                    if (ZOP_DifficultyEnabled()) {
                        EnFhgFire_ZopRallyHit(this, play, player->actor.world.pos.x,
                                              player->actor.world.pos.y + 40.0f,
                                              player->actor.world.pos.z);
                    }
                    this->actor.speedXZ += ZOP_DifficultyEnabled() ? ZOP_GND_PHANTOM_SPEEDUP : 2.0f;
                    if (ZOP_DifficultyEnabled() && BossGanondrof_ZopUsePower(bossGnd, play)) {
                        this->actor.speedXZ +=
                            (this->ZOP_POWER_HITS == 0) ? ZOP_GND_POWER_SPEEDUP : ZOP_GND_POWER_SPEEDUP_MORE;
                        this->ZOP_POWER_HITS++;
                    }
                }
                break;
        }

        osSyncPrintf("F_FIRE_MODE %d\n", this->work[FHGFIRE_FIRE_MODE]);
        osSyncPrintf("fly_mode    %d\n", bossGnd->flyMode);
        if (this->work[FHGFIRE_FX_TIMER] == 0) {
            Actor_UpdateBgCheckInfo(play, &this->actor, 50.0f, 50.0f, 100.0f, 7);
            if ((this->actor.bgCheckFlags & 0x19) || killMode) {
                u8 lightBallColor2 = FHGFLASH_LIGHTBALL_GREEN;
                s16 i4;
                Vec3f sp6C;
                Vec3f sp60 = { 0.0f, -1.0f, 0.0f };

                if (this->work[FHGFIRE_FIRE_MODE] > FHGFIRE_LIGHT_GREEN) {
                    lightBallColor2 = FHGFLASH_LIGHTBALL_LIGHTBLUE;
                }
                for (i4 = 0; i4 < 30; i4++) {
                    sp6C.x = Rand_CenteredFloat(20.0f);
                    sp6C.y = Rand_CenteredFloat(20.0f);
                    sp6C.z = Rand_CenteredFloat(20.0f);
                    sp60.y = -0.1f;
                    EffectSsFhgFlash_SpawnLightBall(play, &this->actor.world.pos, &sp6C, &sp60,
                                                    (s16)(Rand_ZeroOne() * 50.0f) + 100, lightBallColor2);
                }
                if (killMode == BALL_BURST) {
                    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_FHG_FIRE, this->actor.world.pos.x,
                                       player->actor.world.pos.y + 20.0f, this->actor.world.pos.z, 0xC8, 0, 0,
                                       FHGFIRE_LIGHTNING_BURST);
                } else if ((killMode == BALL_FIZZLE) && ZOP_IS_BURST_BALL &&
                           (this->work[FHGFIRE_FIRE_MODE] == FHGFIRE_LIGHT_GREEN)) {
                    // ZeldaOOTProject: a burst ball Link dodged hits the floor: a damaging lightning burst (like when it
                    // hits Link) and a ring spreading over the floor that Link has to jump over
                    f32 groundY = (this->actor.floorHeight > BGCHECK_Y_MIN) ? this->actor.floorHeight
                                                                             : this->actor.world.pos.y;

                    Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_FHG_FIRE, this->actor.world.pos.x,
                                       groundY + 20.0f, this->actor.world.pos.z, 300, 0, 0, FHGFIRE_LIGHTNING_BURST);
                    ZOP_SpawnGndRing(play, this->actor.world.pos.x, groundY, this->actor.world.pos.z, 320.0f, 9.0f);
                    ZOP_GndRallyFlash(0.5f);
                }
                bossGnd->flyMode = GND_FLY_NEUTRAL;
                this->work[FHGFIRE_KILL_TIMER] = 30;
                ZOP_GndRallyGlow(0);
                if (!ZOP_IS_BURST_BALL) {
                    BossGanondrof_ZopEndPower(); // rally over: his staff loses its aura
                }
                // Only the end of a real rally stops the rain (not a free ball popping on Link's shield); it then
                // dies down slowly
                if (this->ZOP_RALLY_HITS > 0) {
                    ZOP_GndRallyRain(false);
                }
                this->actor.draw = NULL;
                if (killMode == BALL_FIZZLE) {
                    Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_THUNDER_GND);
                }
                return;
            } else {
                Collider_UpdateCylinder(&this->actor, &this->collider);
                osSyncPrintf("BEFORE setAC   %d\n", this->collider.base.shape);
                CollisionCheck_SetAC(play, &play->colChkCtx, &this->collider.base);
                osSyncPrintf("AFTER  setAC\n");
            }
        }
        Lights_PointNoGlowSetInfo(&this->lightInfo, (s16)this->actor.world.pos.x, (s16)this->actor.world.pos.y,
                                  (s16)this->actor.world.pos.z, 255, 255, 255, 200);
        {
            f32 maxSpeed = ZOP_DifficultyEnabled()
                               ? CLAMP_MAX(ZOP_GND_MAX_SPEED + this->ZOP_POWER_HITS * ZOP_GND_POWER_SPEEDUP,
                                           ZOP_GND_POWER_MAX_SPEED)
                               : 20.0f;

            if (this->actor.speedXZ > maxSpeed) {
                this->actor.speedXZ = maxSpeed;
            }
        }
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_FANTOM_FIRE - SFX_FLAG);
        // "Why ah ah ah ah"
        osSyncPrintf("なぜだああああああああ      %d\n", this->work[FHGFIRE_VARIANCE_TIMER]);
    }
}

void EnFhgFire_PhantomWarp(EnFhgFire* this, PlayState* play) {
    EnfHG* horse = (EnfHG*)this->actor.parent;
    f32 scrollDirection;

    this->fwork[FHGFIRE_WARP_TEX_1_X] += 25.0f * this->fwork[FHGFIRE_WARP_TEX_SPEED];
    this->fwork[FHGFIRE_WARP_TEX_1_Y] -= 40.0f * this->fwork[FHGFIRE_WARP_TEX_SPEED];
    this->fwork[FHGFIRE_WARP_TEX_2_X] += 5.0f * this->fwork[FHGFIRE_WARP_TEX_SPEED];
    this->fwork[FHGFIRE_WARP_TEX_2_Y] -= 30.0f * this->fwork[FHGFIRE_WARP_TEX_SPEED];

    if (this->actor.params == FHGFIRE_WARP_DEATH) {
        if (this->work[FHGFIRE_TIMER] > 70) {
            Audio_PlayActorSound2(&this->actor, NA_SE_EV_FANTOM_WARP_L - SFX_FLAG);
            Audio_PlayActorSound2(&this->actor, NA_SE_EV_FANTOM_WARP_L2 - SFX_FLAG);
        }

        if (this->work[FHGFIRE_TIMER] == 70) {
            Audio_PlayActorSound2(&this->actor, NA_SE_EV_FANTOM_WARP_S);
            Audio_PlayActorSound2(&this->actor, NA_SE_EV_FANTOM_WARP_S2);
        }
    }

    if (this->work[FHGFIRE_TIMER] > 50) {
        scrollDirection = 1.0f;
        if (this->actor.params > FHGFIRE_WARP_EMERGE) {
            scrollDirection = -1.0f;
        }
        Math_ApproachF(&this->fwork[FHGFIRE_WARP_TEX_SPEED], scrollDirection, 1.0f, 0.04f);
        Math_ApproachF(&this->fwork[FHGFIRE_WARP_ALPHA], 255.0f, 1.0f, 10.2f);
    } else if (this->work[FHGFIRE_TIMER] <= 25) {
        Math_ApproachZeroF(&this->fwork[FHGFIRE_WARP_TEX_SPEED], 1.0f, 0.04f);
        Math_ApproachZeroF(&this->fwork[FHGFIRE_WARP_ALPHA], 1.0f, 10.2f);
    }

    osSyncPrintf("EFC 1\n");
    if ((this->work[FHGFIRE_TIMER] == 0) || ((this->actor.params == FHGFIRE_WARP_EMERGE) && horse->fhgFireKillWarp)) {
        Actor_Kill(&this->actor);
    }
    osSyncPrintf("EFC 2\n");
}

void EnFhgFire_Update(Actor* thisx, PlayState* play) {
    s32 pad;
    EnFhgFire* this = (EnFhgFire*)thisx;

    this->work[FHGFIRE_VARIANCE_TIMER]++;

    if (this->work[FHGFIRE_TIMER] != 0) {
        this->work[FHGFIRE_TIMER]--;
    }
    if (this->work[FHGFIRE_FX_TIMER] != 0) {
        this->work[FHGFIRE_FX_TIMER]--;
    }

    this->updateFunc(this, play);
}

static void* sDustTextures[] = {
    gDust1Tex, gDust2Tex, gDust3Tex, gDust4Tex, gDust5Tex, gDust6Tex, gDust7Tex, gDust8Tex,
};

void EnFhgFire_Draw(Actor* thisx, PlayState* play) {
    s32 pad;
    EnFhgFire* this = (EnFhgFire*)thisx;

    OPEN_DISPS(play->state.gfxCtx);

    if (this->actor.params == FHGFIRE_LIGHTNING_BURST) {
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, (s8)this->fwork[FHGFIRE_ALPHA]);
        gDPSetEnvColor(POLY_XLU_DISP++, 165, 255, 75, 0);
        gDPPipeSync(POLY_XLU_DISP++);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, SEGMENTED_TO_VIRTUAL(gPhantomLightningBlastDL));
    } else if ((this->actor.params == FHGFIRE_SPEAR_LIGHT) || (this->actor.params == FHGFIRE_ENERGY_BALL)) {
        osSyncPrintf("yari hikari draw 1\n");
        Matrix_ReplaceRotation(&play->billboardMtxF);
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, (s8)this->fwork[FHGFIRE_ALPHA]);

        if (this->work[FHGFIRE_FIRE_MODE] > FHGFIRE_LIGHT_GREEN) {
            gDPSetEnvColor(POLY_XLU_DISP++, 0, 255, 255, 0);
        } else {
            gDPSetEnvColor(POLY_XLU_DISP++, 165, 255, 75, 0);
        }
        gDPPipeSync(POLY_XLU_DISP++);
        Matrix_RotateZ((this->actor.shape.rot.z / (f32)0x8000) * 3.1416f, MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, gPhantomEnergyBallDL);
    } else if ((this->actor.params == FHGFIRE_WARP_EMERGE) || (this->actor.params == FHGFIRE_WARP_RETREAT) ||
               (this->actor.params == FHGFIRE_WARP_DEATH)) {
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 0, 0, 0, (u8)this->fwork[FHGFIRE_WARP_ALPHA]);
        gDPSetEnvColor(POLY_XLU_DISP++, 90, 50, 95, (s8)(this->fwork[FHGFIRE_WARP_ALPHA] * 0.5f));
        gDPPipeSync(POLY_XLU_DISP++);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPSegment(POLY_XLU_DISP++, 0x08,
                   Gfx_TwoTexScrollEx(
                       play->state.gfxCtx, 0, (s16)this->fwork[FHGFIRE_WARP_TEX_1_X],
                       (s16)this->fwork[FHGFIRE_WARP_TEX_1_Y], 0x40, 0x40, 1, (s16)this->fwork[FHGFIRE_WARP_TEX_2_X],
                       (s16)this->fwork[FHGFIRE_WARP_TEX_2_Y], 0x40, 0x40, 25 * this->fwork[FHGFIRE_WARP_TEX_SPEED],
                       -40 * this->fwork[FHGFIRE_WARP_TEX_SPEED], 5 * this->fwork[FHGFIRE_WARP_TEX_SPEED],
                       -30 * this->fwork[FHGFIRE_WARP_TEX_SPEED]));
        gSPDisplayList(POLY_XLU_DISP++, gPhantomWarpDL);
    } else {
        osSyncPrintf("FF DRAW 1\n");
        Matrix_Translate(0.0f, -100.0f, 0.0f, MTXMODE_APPLY);
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, (s8)this->fwork[FHGFIRE_ALPHA]);
        gDPSetEnvColor(POLY_XLU_DISP++, 0, 255, 30, 0);
        gDPPipeSync(POLY_XLU_DISP++);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, gPhantomLightningDL);
        osSyncPrintf("FF DRAW 2\n");
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

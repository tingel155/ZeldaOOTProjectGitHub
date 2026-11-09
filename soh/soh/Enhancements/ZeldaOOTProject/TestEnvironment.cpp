#include "TestEnvironment.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
#include "variables.h"
#include "functions.h"
#include "macros.h"
void Play_Init(GameState* thisx);
void Sram_InitDebugSave(void);
s32 Object_Spawn(ObjectContext* objectCtx, s16 objectId);
extern PlayState* gPlayState;
}

// Enemies placed around Link once the scene is running (Enemies = 1): two Wolfos that keep attacking and four
// Deku Babas in a ring, close enough to lunge at him
struct TestEnemy {
    s16 actorId;
    s16 objectId;
    s16 params;
    float distance;
    u16 angle;
};
static const TestEnemy sTestEnemies[] = {
    { ACTOR_EN_WF, OBJECT_WF, 0, 220.0f, 0x0000 },        { ACTOR_EN_WF, OBJECT_WF, 0, 220.0f, 0x8000 },
    { ACTOR_EN_DEKUBABA, OBJECT_DEKUBABA, 0, 95.0f, 0x2000 }, { ACTOR_EN_DEKUBABA, OBJECT_DEKUBABA, 0, 95.0f, 0x6000 },
    { ACTOR_EN_DEKUBABA, OBJECT_DEKUBABA, 0, 95.0f, 0xA000 }, { ACTOR_EN_DEKUBABA, OBJECT_DEKUBABA, 0, 95.0f, 0xE000 },
};
static int sEnemySpawnDelay = -1;

static void TestEnv_SpawnEnemies() {
    if (sEnemySpawnDelay < 0 || --sEnemySpawnDelay > 0) {
        return;
    }

    // Load the objects first; spawn once all of them are resident
    bool allLoaded = true;
    for (const TestEnemy& enemy : sTestEnemies) {
        s32 index = Object_GetIndex(&gPlayState->objectCtx, enemy.objectId);
        if (index < 0) {
            Object_Spawn(&gPlayState->objectCtx, enemy.objectId);
            allLoaded = false;
        } else if (!Object_IsLoaded(&gPlayState->objectCtx, index)) {
            allLoaded = false;
        }
    }
    if (!allLoaded) {
        sEnemySpawnDelay = 1;
        return;
    }

    Player* player = GET_PLAYER(gPlayState);
    for (const TestEnemy& enemy : sTestEnemies) {
        s16 angle = (s16)(player->actor.shape.rot.y + enemy.angle);
        f32 x = player->actor.world.pos.x + Math_SinS(angle) * enemy.distance;
        f32 z = player->actor.world.pos.z + Math_CosS(angle) * enemy.distance;
        Actor_Spawn(&gPlayState->actorCtx, gPlayState, enemy.actorId, x, player->actor.world.pos.y, z, 0,
                    angle + 0x8000, 0, enemy.params);
    }
    sEnemySpawnDelay = -1;
}

static void TestEnv_Boot(GameState* state) {
    // Debug save (fileNum 0xFF): full inventory, nothing is ever written to disk
    gSaveContext.fileNum = 0xFF;
    Sram_InitDebugSave();
    gSaveContext.gameMode = GAMEMODE_NORMAL;

    // Adult: C-Left Nayru's Love, C-Down Din's Fire, C-Right Farore's Wind
    // Child: C-Left Slingshot, C-Down Deku Nuts, C-Right Bombs (what the first dungeons call for)
    static const u8 sAdultCItems[3] = { ITEM_NAYRUS_LOVE, ITEM_DINS_FIRE, ITEM_FARORES_WIND };
    static const u8 sChildCItems[3] = { ITEM_SLINGSHOT, ITEM_NUT, ITEM_BOMB };
    const u8* cItems = sChildCItems;

    if (CVarGetInteger(CVAR_ZOP_TESTENV("Adult"), 1)) {
        gSaveContext.linkAge = LINK_AGE_ADULT;
        Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_MASTER);
        Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_MIRROR);
        gSaveContext.equips.buttonItems[0] = ITEM_SWORD_MASTER;
        cItems = sAdultCItems;
    } else {
        gSaveContext.linkAge = LINK_AGE_CHILD;
        Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_KOKIRI);
        Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_DEKU);
        gSaveContext.equips.buttonItems[0] = ITEM_SWORD_KOKIRI;
    }

    for (int i = 0; i < 3; i++) {
        gSaveContext.equips.buttonItems[i + 1] = cItems[i];
        gSaveContext.equips.cButtonSlots[i] = SLOT(cItems[i]);
    }

    gSaveContext.healthCapacity = gSaveContext.health = CVarGetInteger(CVAR_ZOP_TESTENV("Hearts"), 20) * 0x10;
    gSaveContext.isMagicAcquired = true;
    gSaveContext.isDoubleMagicAcquired = true;
    gSaveContext.isDoubleDefenseAcquired = CVarGetInteger(CVAR_ZOP_TESTENV("DoubleDefense"), 0);
    // Same magic bootstrap as the map select: the meter fills up on scene start
    gSaveContext.magicFillTarget = MAGIC_DOUBLE_METER;
    gSaveContext.magic = 0;
    gSaveContext.magicCapacity = 0;
    gSaveContext.magicLevel = 0;

    gSaveContext.dayTime = CVarGetInteger(CVAR_ZOP_TESTENV("DayTime"), 0x8000);
    gSaveContext.nightFlag = (gSaveContext.dayTime < 0x4555 || gSaveContext.dayTime >= 0xC001);

    // Spawns Link with Nayru's Love already active (see Player_Init)
    if (CVarGetInteger(CVAR_ZOP_TESTENV("NayrusLoveActive"), 0)) {
        gSaveContext.nayrusLoveTimer = 1;
    }

    for (int i = 0; i < ARRAY_COUNT(gSaveContext.buttonStatus); i++) {
        gSaveContext.buttonStatus[i] = BTN_ENABLED;
    }
    gSaveContext.forceRisingButtonAlphas = 0;
    gSaveContext.nextHudVisibilityMode = 0;
    gSaveContext.hudVisibilityMode = 0;
    gSaveContext.hudVisibilityModeTimer = 0;

    gSaveContext.entranceIndex = CVarGetInteger(CVAR_ZOP_TESTENV("Entrance"), ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN);
    gSaveContext.respawnFlag = 0;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].entranceIndex = ENTR_LOAD_OPENING;
    gSaveContext.seqId = (u8)NA_BGM_DISABLED;
    gSaveContext.natureAmbienceId = 0xFF;
    gSaveContext.showTitleCard = true;
    gWeatherMode = 0;

    GameInteractor_ExecuteOnLoadGame(gSaveContext.fileNum);

    state->running = false;
    SET_NEXT_GAMESTATE(state, Play_Init, PlayState);
}

static void RegisterTestEnvironment() {
    // First frame of the boot logo: jump straight into gameplay
    COND_HOOK(OnZTitleUpdate, CVarGetInteger(CVAR_ZOP_TESTENV("Enabled"), 0),
              [](void* gameState) { TestEnv_Boot((GameState*)gameState); });

    bool enemies = CVarGetInteger(CVAR_ZOP_TESTENV("Enabled"), 0) && CVarGetInteger(CVAR_ZOP_TESTENV("Enemies"), 0);
    COND_HOOK(OnSceneSpawnActors, enemies, []() { sEnemySpawnDelay = 20; });
    COND_HOOK(OnPlayerUpdate, enemies, []() { TestEnv_SpawnEnemies(); });
}

static RegisterShipInitFunc initFunc(RegisterTestEnvironment,
                                     { CVAR_ZOP_TESTENV("Enabled"), CVAR_ZOP_TESTENV("Enemies") });

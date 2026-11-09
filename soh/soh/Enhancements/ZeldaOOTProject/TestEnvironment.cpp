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
}

static void TestEnv_Boot(GameState* state) {
    // Debug save (fileNum 0xFF): full inventory, nothing is ever written to disk
    gSaveContext.fileNum = 0xFF;
    Sram_InitDebugSave();
    gSaveContext.gameMode = GAMEMODE_NORMAL;

    if (CVarGetInteger(CVAR_ZOP_TESTENV("Adult"), 1)) {
        gSaveContext.linkAge = LINK_AGE_ADULT;
        Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_MASTER);
        Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_MIRROR);
        gSaveContext.equips.buttonItems[0] = ITEM_SWORD_MASTER;
    }

    // C-Left: Nayru's Love, C-Down: Din's Fire, C-Right: Farore's Wind
    static const u8 sCItems[3] = { ITEM_NAYRUS_LOVE, ITEM_DINS_FIRE, ITEM_FARORES_WIND };
    for (int i = 0; i < 3; i++) {
        gSaveContext.equips.buttonItems[i + 1] = sCItems[i];
        gSaveContext.equips.cButtonSlots[i] = SLOT(sCItems[i]);
    }

    gSaveContext.healthCapacity = gSaveContext.health = 20 * 0x10;
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
}

static RegisterShipInitFunc initFunc(RegisterTestEnvironment, { CVAR_ZOP_TESTENV("Enabled") });

// ZeldaOOTProject map editor, stage 1: look at the maps.
//
// While the window is open the game logic is frozen (Play_Update skips its frame) and the view is driven by a free
// editor camera with Unity-like controls:
//   right mouse button held + mouse  look around      right mouse button held + W/A/S/D  fly, Q/E down/up
//   Shift                            faster           mouse wheel (with right button)    change fly speed
//   mouse wheel                      move forward     middle mouse button drag           pan
//   Alt + left mouse button drag     orbit            F                                  jump to Link
// All rooms of the scene are drawn at once, so a whole dungeon can be looked at without walking through its doors.

#include "MapEditor.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <libultraship/bridge/consolevariablebridge.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/cvar_prefixes.h"
#include "soh/resource/type/Scene.h"
#include "soh/resource/type/scenecommand/SetMesh.h"
#include "soh/util.h"

extern "C" {
#include <z64.h>
#include "variables.h"
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
}

#define CVAR_ZOP_MAPEDITOR(var) "gZOP.MapEditor." var

namespace {

constexpr float kPi = 3.14159265f;
constexpr const char* kLowResCVar = "gLowResMode";

struct EditorCam {
    Vec3f pos = { 0.0f, 0.0f, 0.0f };
    float yaw = 0.0f;   // radians, 0 looks along +z
    float pitch = 0.0f; // radians, positive looks up
    bool init = false;
};

EditorCam sCam;
bool sActive = false;
bool sWasActive = false;
bool sWarping = false;
s16 sWarpScene = -1;
int sWarpSettle = 0;
int sSavedLowRes = -1;
bool sLooking = false;
bool sPanning = false;
bool sOrbiting = false;
int sSelScene = -1;

// The rooms of the current scene, so they can all be drawn
s16 sCacheScene = -1;
int sCacheRooms = -1;
std::vector<std::shared_ptr<Ship::IResource>> sRoomRes;
std::vector<MeshHeader*> sRoomMesh;

bool DrawAllRooms() {
    return CVarGetInteger(CVAR_ZOP_MAPEDITOR("AllRooms"), 1);
}
bool HideActors() {
    return CVarGetInteger(CVAR_ZOP_MAPEDITOR("HideActors"), 0);
}
bool FarView() {
    return CVarGetInteger(CVAR_ZOP_MAPEDITOR("FarView"), 1);
}
bool HighRes() {
    return CVarGetInteger(CVAR_ZOP_MAPEDITOR("HighRes"), 1);
}
float FlySpeed() {
    return CVarGetFloat(CVAR_ZOP_MAPEDITOR("Speed"), 600.0f);
}
float Fov() {
    return CVarGetFloat(CVAR_ZOP_MAPEDITOR("Fov"), 60.0f);
}

bool EditorOpen() {
    return (gPlayState != nullptr) && CVarGetInteger(CVAR_WINDOW("ZOPMapEditor"), 0);
}

Vec3f Forward() {
    return { cosf(sCam.pitch) * sinf(sCam.yaw), sinf(sCam.pitch), cosf(sCam.pitch) * cosf(sCam.yaw) };
}

// Screen right (horizontal)
Vec3f Right() {
    return { -cosf(sCam.yaw), 0.0f, sinf(sCam.yaw) };
}

void AddScaled(Vec3f& v, const Vec3f& d, float s) {
    v.x += d.x * s;
    v.y += d.y * s;
    v.z += d.z * s;
}

void LookFrom(const Vec3f& eye, const Vec3f& at) {
    float dx = at.x - eye.x;
    float dy = at.y - eye.y;
    float dz = at.z - eye.z;

    sCam.pos = eye;
    sCam.yaw = atan2f(dx, dz);
    sCam.pitch = atan2f(dy, sqrtf(dx * dx + dz * dz));
    sCam.init = true;
}

void InitCamFromGame() {
    Camera* cam = gPlayState->cameraPtrs[gPlayState->activeCamera];

    if (cam != nullptr) {
        LookFrom(cam->eye, cam->at);
    } else {
        Player* player = GET_PLAYER(gPlayState);
        Vec3f eye = player->actor.world.pos;
        Vec3f at = eye;
        eye.y += 120.0f;
        eye.z -= 250.0f;
        at.y += 40.0f;
        LookFrom(eye, at);
    }
}

void FocusLink() {
    Player* player = GET_PLAYER(gPlayState);
    Vec3f at = player->actor.world.pos;
    Vec3f f;
    Vec3f eye;

    at.y += 40.0f;
    f = Forward();
    eye = at;
    AddScaled(eye, f, -250.0f);
    LookFrom(eye, at);
}

void Activate() {
    sCam.init = false;
    // let the game finish loading and fading in before it gets frozen
    sWarping = true;
    sWarpScene = gPlayState->sceneNum;
    sWarpSettle = 0;
    if (HighRes()) {
        sSavedLowRes = CVarGetInteger(kLowResCVar, 0);
        CVarSetInteger(kLowResCVar, 0);
    }
}

void Deactivate() {
    if (sSavedLowRes >= 0) {
        CVarSetInteger(kLowResCVar, sSavedLowRes);
        sSavedLowRes = -1;
    }
    sLooking = sPanning = sOrbiting = false;
}

void ApplyCam() {
    Vec3f eye = sCam.pos;
    Vec3f at = eye;
    Vec3f up = { 0.0f, 1.0f, 0.0f };

    AddScaled(at, Forward(), 100.0f);
    gPlayState->view.fovy = Fov();
    func_800AA358(&gPlayState->view, &eye, &at, &up);
    // same check as Play_Draw (scenes without a sky have no skybox matrix)
    if (gPlayState->skyboxId && (gPlayState->skyboxId != SKYBOX_UNSET_1D) && !gPlayState->envCtx.skyboxDisabled) {
        SkyboxDraw_UpdateMatrix(&gPlayState->skyboxCtx, eye.x, eye.y, eye.z);
    }
    if (FarView()) {
        // furthest possible fog and draw distance, so the whole map can be seen
        gPlayState->view.zFar = 12800;
        gPlayState->lightCtx.fogNear = 996;
        gPlayState->lightCtx.fogFar = 12800;
    }
}

// Runs at the start of every Play_Draw
void OnDrawBegin() {
    bool want = EditorOpen();

    if (want && !sWasActive) {
        Activate();
    } else if (!want && sWasActive) {
        Deactivate();
    }
    sWasActive = sActive = want;
    if (!sActive) {
        return;
    }

    // While warping the game runs normally; once the new map has settled the editor takes over again
    if (sWarping) {
        if ((gPlayState->sceneNum == sWarpScene) && (gPlayState->transitionTrigger == TRANS_TRIGGER_OFF) &&
            (gPlayState->transitionMode == TRANS_MODE_OFF)) {
            if (++sWarpSettle > 45) {
                sWarping = false;
                sCam.init = false;
            }
        } else {
            sWarpSettle = 0;
        }
        return;
    }

    if (!sCam.init) {
        InitCamFromGame();
    }
    // no frozen area name over the view
    gPlayState->actorCtx.titleCtx.alpha = 0;
    gPlayState->actorCtx.titleCtx.durationTimer = 0;
    gPlayState->actorCtx.titleCtx.delayTimer = 0;
    // and no frozen cutscene letterbox (the game moves it back once it runs again)
    ShrinkWindow_SetCurrentVal(0);
    ApplyCam();
}

int FindEntrance(int scene) {
    for (int i = 0; i < ENTR_MAX; i++) {
        if (gEntranceTable[i].scene == scene) {
            return i;
        }
    }
    return -1;
}

void WarpTo(int scene) {
    int entrance = FindEntrance(scene);

    if ((entrance < 0) || (gPlayState == nullptr)) {
        return;
    }
    gPlayState->nextEntranceIndex = entrance;
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType = TRANS_TYPE_INSTANT;
    sWarping = true;
    sWarpScene = scene;
    sWarpSettle = 0;
}

void RebuildRoomCache(PlayState* play) {
    sRoomRes.clear();
    sRoomMesh.clear();
    sCacheScene = play->sceneNum;
    sCacheRooms = play->numRooms;

    for (int i = 0; i < play->numRooms; i++) {
        std::shared_ptr<Ship::IResource> res;
        MeshHeader* mesh = nullptr;

        if ((play->roomList != nullptr) && (play->roomList[i].fileName != nullptr)) {
            res = ResourceMgr_GetResourceByNameHandlingMQ(play->roomList[i].fileName);
        }
        if (res != nullptr) {
            auto room = std::static_pointer_cast<SOH::Scene>(res);
            for (auto& cmd : room->commands) {
                if ((cmd != nullptr) && (cmd->cmdId == SOH::SceneCommandID::SetMesh)) {
                    mesh = (MeshHeader*)((SOH::SetMesh*)cmd.get())->GetRawPointer();
                    break;
                }
            }
        }
        sRoomRes.push_back(res);
        sRoomMesh.push_back(mesh);
    }
}

// Handles the camera input; called every GUI frame while the window is open
void UpdateCamInput() {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiContext& g = *GImGui;
    ImGuiWindow* hovered = g.HoveredWindow;
    bool overGame = (hovered == nullptr) || (strncmp(hovered->Name, "Main Game", 9) == 0) ||
                    ((hovered->RootWindow != nullptr) && (strncmp(hovered->RootWindow->Name, "Main Game", 9) == 0));
    float dt = std::clamp(io.DeltaTime, 0.0f, 0.1f);
    float speed = FlySpeed();
    Vec3f f;
    Vec3f r;

    if (!sCam.init || sWarping) {
        return;
    }

    if (overGame && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        sLooking = true;
    }
    if (overGame && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        sPanning = true;
    }
    if (overGame && io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        sOrbiting = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        sLooking = false;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        sPanning = false;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        sOrbiting = false;
    }

    if (io.KeyShift) {
        speed *= 3.0f;
    }

    if (sLooking) {
        float move = speed * dt;

        sCam.yaw -= io.MouseDelta.x * 0.0045f;
        sCam.pitch -= io.MouseDelta.y * 0.0045f;
        sCam.pitch = std::clamp(sCam.pitch, -1.55f, 1.55f);

        f = Forward();
        r = Right();
        if (!io.WantTextInput) {
            if (ImGui::IsKeyDown(ImGuiKey_W)) {
                AddScaled(sCam.pos, f, move);
            }
            if (ImGui::IsKeyDown(ImGuiKey_S)) {
                AddScaled(sCam.pos, f, -move);
            }
            if (ImGui::IsKeyDown(ImGuiKey_D)) {
                AddScaled(sCam.pos, r, move);
            }
            if (ImGui::IsKeyDown(ImGuiKey_A)) {
                AddScaled(sCam.pos, r, -move);
            }
            if (ImGui::IsKeyDown(ImGuiKey_E)) {
                sCam.pos.y += move;
            }
            if (ImGui::IsKeyDown(ImGuiKey_Q)) {
                sCam.pos.y -= move;
            }
        }
        // the wheel changes the fly speed while looking around (like in Unity)
        if (io.MouseWheel != 0.0f) {
            CVarSetFloat(CVAR_ZOP_MAPEDITOR("Speed"),
                         std::clamp(FlySpeed() * powf(1.2f, io.MouseWheel), 50.0f, 20000.0f));
        }
    } else if (sOrbiting) {
        float dist = 300.0f;
        Vec3f pivot = sCam.pos;

        AddScaled(pivot, Forward(), dist);
        sCam.yaw -= io.MouseDelta.x * 0.006f;
        sCam.pitch -= io.MouseDelta.y * 0.006f;
        sCam.pitch = std::clamp(sCam.pitch, -1.55f, 1.55f);
        sCam.pos = pivot;
        AddScaled(sCam.pos, Forward(), -dist);
    } else if (sPanning) {
        float k = speed / 600.0f;

        AddScaled(sCam.pos, Right(), -io.MouseDelta.x * k);
        sCam.pos.y += io.MouseDelta.y * k;
    } else if (overGame && (io.MouseWheel != 0.0f)) {
        AddScaled(sCam.pos, Forward(), io.MouseWheel * 80.0f * (speed / 600.0f));
    }

    if (overGame && !io.WantTextInput && !sLooking && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        FocusLink();
    }
}

} // namespace

extern "C" int ZOP_MapEditorFrozen(void) {
    return sActive && !sWarping;
}

extern "C" int ZOP_MapEditorHideActors(void) {
    return sActive && !sWarping && HideActors();
}

extern "C" void ZOP_MapEditorDrawExtraRooms(PlayState* play, unsigned int flags) {
    if (!sActive || sWarping || !DrawAllRooms() || (play->numRooms <= 1)) {
        return;
    }
    if ((sCacheScene != play->sceneNum) || (sCacheRooms != play->numRooms)) {
        RebuildRoomCache(play);
    }

    for (int i = 0; i < (int)sRoomMesh.size(); i++) {
        MeshHeader* mesh = sRoomMesh[i];
        Room room = {};

        if ((i == play->roomCtx.curRoom.num) || (i == play->roomCtx.prevRoom.num) || (mesh == nullptr)) {
            continue;
        }
        // pre-rendered backgrounds only make sense from their fixed camera
        if (mesh->base.type == 1) {
            continue;
        }
        room.num = i;
        room.meshHeader = mesh;
        room.segment = sRoomRes[i].get();
        Room_Draw(play, &room, flags);
    }

    // the loaded room's segment again for whatever is drawn next
    if (play->roomCtx.curRoom.segment != nullptr) {
        gSegments[3] = VIRTUAL_TO_PHYSICAL(play->roomCtx.curRoom.segment);
        OPEN_DISPS(play->state.gfxCtx);
        gSPSegment(POLY_OPA_DISP++, 0x03, (uintptr_t)play->roomCtx.curRoom.segment);
        gSPSegment(POLY_XLU_DISP++, 0x03, (uintptr_t)play->roomCtx.curRoom.segment);
        CLOSE_DISPS(play->state.gfxCtx);
    }
}

void ZopMapEditorWindow::InitElement() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayDrawBegin>(OnDrawBegin);
}

void ZopMapEditorWindow::DrawElement() {
    if (gPlayState == nullptr) {
        ImGui::TextWrapped("Lade zuerst einen Spielstand, dann kann der Editor die Maps zeigen.");
        return;
    }

    UpdateCamInput();

    int scene = gPlayState->sceneNum;
    ImGui::Text("Map: %s (Szene 0x%02X)", SohUtils::GetSceneName(scene).c_str(), scene);
    ImGui::Text("Räume: %d   geladen: %d", gPlayState->numRooms, gPlayState->roomCtx.curRoom.num);
    if (sWarping) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Lade Map ...");
    }

    ImGui::SeparatorText("Map wählen");
    if (sSelScene < 0) {
        sSelScene = scene;
    }
    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::BeginCombo("##scene", SohUtils::GetSceneName(sSelScene).c_str())) {
        for (int i = 0; i < SCENE_ID_MAX; i++) {
            if (FindEntrance(i) < 0) {
                continue;
            }
            std::string label = SohUtils::GetSceneName(i) + "##" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), i == sSelScene)) {
                sSelScene = i;
            }
            if (i == sSelScene) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(sWarping || (sSelScene == scene));
    if (ImGui::Button("Laden", ImVec2(80.0f, 0.0f))) {
        WarpTo(sSelScene);
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Ansicht");
    bool allRooms = DrawAllRooms();
    if (ImGui::Checkbox("Alle Räume zeigen", &allRooms)) {
        CVarSetInteger(CVAR_ZOP_MAPEDITOR("AllRooms"), allRooms);
    }
    bool hideActors = HideActors();
    if (ImGui::Checkbox("Actors ausblenden (auch Link)", &hideActors)) {
        CVarSetInteger(CVAR_ZOP_MAPEDITOR("HideActors"), hideActors);
    }
    bool farView = FarView();
    if (ImGui::Checkbox("Weite Sicht (kein Nebel)", &farView)) {
        CVarSetInteger(CVAR_ZOP_MAPEDITOR("FarView"), farView);
    }
    bool highRes = HighRes();
    if (ImGui::Checkbox("Hohe Auflösung im Editor", &highRes)) {
        CVarSetInteger(CVAR_ZOP_MAPEDITOR("HighRes"), highRes);
        if (highRes && (sSavedLowRes < 0)) {
            sSavedLowRes = CVarGetInteger(kLowResCVar, 0);
            CVarSetInteger(kLowResCVar, 0);
        } else if (!highRes && (sSavedLowRes >= 0)) {
            CVarSetInteger(kLowResCVar, sSavedLowRes);
            sSavedLowRes = -1;
        }
    }
    float speed = FlySpeed();
    if (ImGui::SliderFloat("Tempo", &speed, 50.0f, 20000.0f, "%.0f", ImGuiSliderFlags_Logarithmic)) {
        CVarSetFloat(CVAR_ZOP_MAPEDITOR("Speed"), speed);
    }
    float fov = Fov();
    if (ImGui::SliderFloat("Sichtfeld", &fov, 30.0f, 110.0f, "%.0f")) {
        CVarSetFloat(CVAR_ZOP_MAPEDITOR("Fov"), fov);
    }

    ImGui::Text("Kamera: %.0f  %.0f  %.0f", sCam.pos.x, sCam.pos.y, sCam.pos.z);
    if (ImGui::Button("Zu Link (F)")) {
        FocusLink();
    }
    ImGui::SameLine();
    if (ImGui::Button("Spielkamera übernehmen")) {
        InitCamFromGame();
    }

    ImGui::SeparatorText("Steuerung");
    ImGui::TextWrapped("Rechte Maustaste halten: umsehen, dabei W/A/S/D fliegen, Q/E runter/hoch, "
                       "Mausrad = Tempo. Shift = schneller. Mausrad allein: vor/zurück. "
                       "Mittlere Maustaste ziehen: verschieben. Alt + linke Maustaste ziehen: drehen um die Mitte. "
                       "F: zu Link. Das Spiel ist pausiert, solange dieses Fenster offen ist.");
}

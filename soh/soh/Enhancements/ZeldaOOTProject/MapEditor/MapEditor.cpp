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
#include "MapMeshes.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>

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

// extern "C": OPEN_DISPS declares C-linkage helpers at block scope
extern "C" void ZopMapEditor_EmitXlu(Gfx* dl);

namespace {

constexpr float kPi = 3.14159265f;
constexpr const char* kLowResCVar = "gSettings.LowResMode";

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
bool sLooking = false;
bool sPanning = false;
bool sOrbiting = false;
int sSelScene = -1;

// The rooms of the current scene, so they can all be drawn
s16 sCacheScene = -1;
int sCacheRooms = -1;
std::vector<std::shared_ptr<Ship::IResource>> sRoomRes;
std::vector<MeshHeader*> sRoomMesh;

// Selection (indices into ZopMap::SceneMeshes)
std::vector<int> sSelected;
s16 sSelectionScene = -1;
int sSelectionVersion = 0;
int sHighlightVersion = -1;
std::vector<Vtx> sHlVtx;
std::vector<Gfx> sHlDl;
bool sClickArmed = false;
ImVec2 sClickStart;
std::string sStatus;

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

bool IsSelected(int idx) {
    return std::find(sSelected.begin(), sSelected.end(), idx) != sSelected.end();
}

void SelectMesh(int idx, bool toggle) {
    if (!toggle) {
        sSelected.clear();
        if (idx >= 0) {
            sSelected.push_back(idx);
        }
    } else if (idx >= 0) {
        auto it = std::find(sSelected.begin(), sSelected.end(), idx);
        if (it != sSelected.end()) {
            sSelected.erase(it);
        } else {
            sSelected.push_back(idx);
        }
    }
    sSelectionVersion++;
}

void CheckSelectionScene() {
    if (sSelectionScene != gPlayState->sceneNum) {
        sSelectionScene = gPlayState->sceneNum;
        sSelected.clear();
        sSelectionVersion++;
        sStatus.clear();
    }
}

std::string ExportDir() {
    std::string def = Ship::Context::GetPathRelativeToAppDirectory("zop_export");
    return CVarGetString(CVAR_ZOP_MAPEDITOR("ExportDir"), def.c_str());
}

Vec3f CamUp() {
    Vec3f f = Forward();
    Vec3f r = Right();
    return { r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x };
}

// Ray from the camera through the mouse position (false if the mouse isn't over the game view)
bool MouseRay(Vec3f& dir) {
    ImGuiWindow* gw = ImGui::FindWindowByName("Main Game");
    if ((gw == nullptr) || (gw->Size.x < 1.0f) || (gw->Size.y < 1.0f)) {
        return false;
    }
    // the game image: the whole window, or 4:3 in the middle in N64 mode
    ImVec2 pos = gw->Pos;
    ImVec2 size = gw->Size;
    if (CVarGetInteger(kLowResCVar, 0) == 1) {
        float sw = size.y * 4.0f / 3.0f;
        pos.x += floorf(size.x / 2.0f - sw / 2.0f);
        size.x = sw;
    }
    ImVec2 m = ImGui::GetIO().MousePos;
    float x = (m.x - pos.x) / size.x * 2.0f - 1.0f;
    float y = 1.0f - (m.y - pos.y) / size.y * 2.0f;
    if ((fabsf(x) > 1.0f) || (fabsf(y) > 1.0f)) {
        return false;
    }
    float th = tanf(Fov() * kPi / 360.0f);
    float aspect = size.x / size.y;
    Vec3f f = Forward();
    Vec3f r = Right();
    Vec3f u = CamUp();
    dir = f;
    AddScaled(dir, r, x * th * aspect);
    AddScaled(dir, u, y * th);
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    dir.x /= len;
    dir.y /= len;
    dir.z /= len;
    return true;
}

bool RayBox(const Vec3f& o, const Vec3f& d, const ZopMap::V3& mn, const ZopMap::V3& mx, float maxT) {
    float t0 = 0.0f, t1 = maxT;
    const float* oo = &o.x;
    const float* dd = &d.x;
    const float* lo = &mn.x;
    const float* hi = &mx.x;
    for (int i = 0; i < 3; i++) {
        if (fabsf(dd[i]) < 1e-8f) {
            if ((oo[i] < lo[i] - 1.0f) || (oo[i] > hi[i] + 1.0f)) {
                return false;
            }
            continue;
        }
        float a = (lo[i] - 1.0f - oo[i]) / dd[i];
        float b = (hi[i] + 1.0f - oo[i]) / dd[i];
        if (a > b) {
            std::swap(a, b);
        }
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) {
            return false;
        }
    }
    return true;
}

// Moeller-Trumbore, both sides
bool RayTri(const Vec3f& o, const Vec3f& d, const ZopMap::V3& a, const ZopMap::V3& b, const ZopMap::V3& c, float& t) {
    float e1x = b.x - a.x, e1y = b.y - a.y, e1z = b.z - a.z;
    float e2x = c.x - a.x, e2y = c.y - a.y, e2z = c.z - a.z;
    float px = d.y * e2z - d.z * e2y, py = d.z * e2x - d.x * e2z, pz = d.x * e2y - d.y * e2x;
    float det = e1x * px + e1y * py + e1z * pz;
    if (fabsf(det) < 1e-6f) {
        return false;
    }
    float inv = 1.0f / det;
    float tx = o.x - a.x, ty = o.y - a.y, tz = o.z - a.z;
    float u = (tx * px + ty * py + tz * pz) * inv;
    if ((u < 0.0f) || (u > 1.0f)) {
        return false;
    }
    float qx = ty * e1z - tz * e1y, qy = tz * e1x - tx * e1z, qz = tx * e1y - ty * e1x;
    float v = (d.x * qx + d.y * qy + d.z * qz) * inv;
    if ((v < 0.0f) || (u + v > 1.0f)) {
        return false;
    }
    t = (e2x * qx + e2y * qy + e2z * qz) * inv;
    return t > 0.0f;
}

int PickMesh() {
    Vec3f dir;
    if (!MouseRay(dir)) {
        return -1;
    }
    const auto& meshes = ZopMap::SceneMeshes(gPlayState);
    bool pickXlu = CVarGetInteger(CVAR_ZOP_MAPEDITOR("PickXlu"), 1);
    float best = 1e9f;
    int bestIdx = -1;
    for (size_t i = 0; i < meshes.size(); i++) {
        const ZopMap::Mesh& m = meshes[i];
        if ((m.xlu && !pickXlu) || !RayBox(sCam.pos, dir, m.bbMin, m.bbMax, best)) {
            continue;
        }
        for (size_t v = 0; v + 2 < m.pos.size(); v += 3) {
            float t;
            if (RayTri(sCam.pos, dir, m.pos[v], m.pos[v + 1], m.pos[v + 2], t) && (t < best)) {
                best = t;
                bestIdx = (int)i;
            }
        }
    }
    return bestIdx;
}

void BuildHighlight() {
    const auto& meshes = ZopMap::SceneMeshes(gPlayState);
    size_t total = 0;
    for (int idx : sSelected) {
        if (idx < (int)meshes.size()) {
            total += meshes[idx].pos.size();
        }
    }
    sHlVtx.clear();
    sHlDl.clear();
    sHlVtx.reserve(total);
    for (int idx : sSelected) {
        if (idx >= (int)meshes.size()) {
            continue;
        }
        for (const ZopMap::V3& p : meshes[idx].pos) {
            Vtx v = {};
            v.v.ob[0] = (s16)p.x;
            v.v.ob[1] = (s16)p.y;
            v.v.ob[2] = (s16)p.z;
            v.v.cn[0] = v.v.cn[1] = v.v.cn[2] = v.v.cn[3] = 255;
            sHlVtx.push_back(v);
        }
    }

    uint32_t rm = Z_CMP | IM_RD | CVG_DST_FULL | FORCE_BL | ZMODE_DEC;
    uint32_t blc1 = GBL_c1(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_1MA);
    uint32_t blc2 = GBL_c2(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_1MA);
    sHlDl.push_back(gsDPPipeSync());
    sHlDl.push_back(gsSPTexture(0, 0, 0, G_TX_RENDERTILE, G_OFF));
    sHlDl.push_back(gsDPSetCycleType(G_CYC_1CYCLE));
    sHlDl.push_back(gsDPSetRenderMode(rm | blc1, rm | blc2));
    sHlDl.push_back(gsDPSetCombineMode(G_CC_PRIMITIVE, G_CC_PRIMITIVE));
    sHlDl.push_back(gsSPLoadGeometryMode(G_ZBUFFER));
    sHlDl.push_back(gsDPSetPrimColor(0, 0, 255, 170, 30, 120));
    for (size_t i = 0; i < sHlVtx.size(); i += 30) {
        size_t n = std::min<size_t>(30, sHlVtx.size() - i);
        sHlDl.push_back(gsSPVertex((uintptr_t)&sHlVtx[i], (u32)n, 0));
        for (size_t t = 0; t + 2 < n; t += 3) {
            sHlDl.push_back(gsSP1Triangle((u8)t, (u8)(t + 1), (u8)(t + 2), 0));
        }
    }
    sHlDl.push_back(gsSPEndDisplayList());
    sHighlightVersion = sSelectionVersion;
}

// Runs at the end of every Play_Draw: the selected meshes glow orange
void OnDrawEnd() {
    if (!sActive || sWarping || sSelected.empty() || (gPlayState == nullptr)) {
        return;
    }
    if (sHighlightVersion != sSelectionVersion) {
        BuildHighlight();
    }
    if (sHlVtx.empty()) {
        return;
    }
    ZopMapEditor_EmitXlu(sHlDl.data());
}

void FocusSelection() {
    const auto& meshes = ZopMap::SceneMeshes(gPlayState);
    bool any = false;
    ZopMap::V3 mn = {}, mx = {};
    for (int idx : sSelected) {
        if (idx >= (int)meshes.size()) {
            continue;
        }
        const ZopMap::Mesh& m = meshes[idx];
        if (!any) {
            mn = m.bbMin;
            mx = m.bbMax;
            any = true;
        } else {
            mn = { std::min(mn.x, m.bbMin.x), std::min(mn.y, m.bbMin.y), std::min(mn.z, m.bbMin.z) };
            mx = { std::max(mx.x, m.bbMax.x), std::max(mx.y, m.bbMax.y), std::max(mx.z, m.bbMax.z) };
        }
    }
    if (!any) {
        return;
    }
    Vec3f at = { (mn.x + mx.x) / 2, (mn.y + mx.y) / 2, (mn.z + mx.z) / 2 };
    float radius =
        sqrtf((mx.x - mn.x) * (mx.x - mn.x) + (mx.y - mn.y) * (mx.y - mn.y) + (mx.z - mn.z) * (mx.z - mn.z)) / 2;
    float dist = radius / tanf(Fov() * kPi / 360.0f) + 30.0f;
    Vec3f eye = at;
    AddScaled(eye, Forward(), -dist);
    LookFrom(eye, at);
}

void ExportSelection() {
    const auto& meshes = ZopMap::SceneMeshes(gPlayState);
    std::string scene = SohUtils::GetSceneName(gPlayState->sceneNum);
    std::string dir = ExportDir();
    int ok = 0;
    std::string err;
    for (int idx : sSelected) {
        if (idx >= (int)meshes.size()) {
            continue;
        }
        std::string out;
        if (ZopMap::ExportMesh(meshes[idx], scene, dir, out, err)) {
            ok++;
        }
    }
    sStatus = std::to_string(ok) + " FBX-Datei(en) exportiert nach " + dir;
    if (!err.empty()) {
        sStatus += "\nFehler: " + err;
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

// The editor shows the map in full resolution; the player's setting (e.g. N64 mode) is kept in a saved backup CVar,
// so it comes back even if the game is closed while the editor is open
void EnterHighRes() {
    if (CVarGetInteger(CVAR_ZOP_MAPEDITOR("LowResBackup"), -1) < 0) {
        CVarSetInteger(CVAR_ZOP_MAPEDITOR("LowResBackup"), CVarGetInteger(kLowResCVar, 0));
    }
    CVarSetInteger(kLowResCVar, 0);
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

void LeaveHighRes() {
    int backup = CVarGetInteger(CVAR_ZOP_MAPEDITOR("LowResBackup"), -1);
    if (backup >= 0) {
        CVarSetInteger(kLowResCVar, backup);
        CVarSetInteger(CVAR_ZOP_MAPEDITOR("LowResBackup"), -1);
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
}

void Activate() {
    sCam.init = false;
    // let the game finish loading and fading in before it gets frozen
    sWarping = true;
    sWarpScene = gPlayState->sceneNum;
    sWarpSettle = 0;
    if (HighRes()) {
        EnterHighRes();
    }
}

void Deactivate() {
    LeaveHighRes();
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
        if (sSelected.empty()) {
            FocusLink();
        } else {
            FocusSelection();
        }
    }

    // left click (without Alt, without dragging) picks the mesh under the mouse; Ctrl adds/removes
    if (overGame && !io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        sClickArmed = true;
        sClickStart = io.MousePos;
    }
    if (sClickArmed && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        sClickArmed = false;
        float dx = io.MousePos.x - sClickStart.x;
        float dy = io.MousePos.y - sClickStart.y;
        if ((dx * dx + dy * dy < 25.0f) && overGame) {
            int idx = PickMesh();
            if ((idx >= 0) || !io.KeyCtrl) {
                SelectMesh(idx, io.KeyCtrl);
            }
        }
    }
}

} // namespace

extern "C" void ZopMapEditor_EmitXlu(Gfx* dl) {
    OPEN_DISPS(gPlayState->state.gfxCtx);
    gSPMatrix(POLY_XLU_DISP++, &gMtxClear, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, dl);
    CLOSE_DISPS(gPlayState->state.gfxCtx);
}

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
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayDrawEnd>(OnDrawEnd);
}

void ZopMapEditorWindow::DrawElement() {
    if (gPlayState == nullptr) {
        ImGui::TextWrapped("Lade zuerst einen Spielstand, dann kann der Editor die Maps zeigen.");
        return;
    }

    CheckSelectionScene();
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
        if (highRes) {
            EnterHighRes();
        } else {
            LeaveHighRes();
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

    ImGui::SeparatorText("Meshes");
    if (sWarping) {
        ImGui::TextDisabled("(nach dem Laden)");
    } else {
        const auto& meshes = ZopMap::SceneMeshes(gPlayState);
        size_t tris = 0;
        for (int idx : sSelected) {
            if (idx < (int)meshes.size()) {
                tris += meshes[idx].TriCount();
            }
        }
        ImGui::Text("Ausgewählt: %d Mesh(es), %d Dreiecke", (int)sSelected.size(), (int)tris);
        ImGui::BeginDisabled(sSelected.empty());
        if (ImGui::Button("Auswahl als FBX exportieren")) {
            ExportSelection();
        }
        ImGui::SameLine();
        if (ImGui::Button("Auswahl leeren")) {
            SelectMesh(-1, false);
        }
        ImGui::EndDisabled();
        bool pickXlu = CVarGetInteger(CVAR_ZOP_MAPEDITOR("PickXlu"), 1);
        if (ImGui::Checkbox("Transparente Meshes anklickbar", &pickXlu)) {
            CVarSetInteger(CVAR_ZOP_MAPEDITOR("PickXlu"), pickXlu);
        }

        static char dirBuf[512];
        static bool dirInit = false;
        if (!dirInit) {
            snprintf(dirBuf, sizeof(dirBuf), "%s", ExportDir().c_str());
            dirInit = true;
        }
        ImGui::TextUnformatted("Export-Ordner:");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##exportdir", dirBuf, sizeof(dirBuf))) {
            CVarSetString(CVAR_ZOP_MAPEDITOR("ExportDir"), dirBuf);
        }
        if (!sStatus.empty()) {
            ImGui::TextWrapped("%s", sStatus.c_str());
        }

        if (ImGui::TreeNode("Alle Meshes der Map")) {
            int lastRoom = -1;
            bool roomOpen = false;
            for (size_t i = 0; i < meshes.size(); i++) {
                const ZopMap::Mesh& m = meshes[i];
                if (m.room != lastRoom) {
                    if (roomOpen) {
                        ImGui::TreePop();
                    }
                    lastRoom = m.room;
                    roomOpen = ImGui::TreeNode((void*)(intptr_t)(m.room + 1), "Raum %d", m.room);
                }
                if (!roomOpen) {
                    continue;
                }
                std::string label =
                    m.label + "  (" + std::to_string(m.TriCount()) + " Dreiecke)##" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), IsSelected((int)i))) {
                    SelectMesh((int)i, ImGui::GetIO().KeyCtrl);
                }
            }
            if (roomOpen) {
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
    }

    ImGui::SeparatorText("Steuerung");
    ImGui::TextWrapped("Rechte Maustaste halten: umsehen, dabei W/A/S/D fliegen, Q/E runter/hoch, "
                       "Mausrad = Tempo. Shift = schneller. Mausrad allein: vor/zurück. "
                       "Mittlere Maustaste ziehen: verschieben. Alt + linke Maustaste ziehen: drehen um die Mitte. "
                       "Linksklick: Mesh auswählen, Strg + Linksklick: hinzufügen/entfernen. "
                       "F: zur Auswahl (oder zu Link). Das Spiel ist pausiert, solange dieses Fenster offen ist.");
}

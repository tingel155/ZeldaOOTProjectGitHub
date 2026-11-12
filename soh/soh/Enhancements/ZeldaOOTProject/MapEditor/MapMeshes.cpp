// ZeldaOOTProject map editor: CPU decoder for the room display lists (see MapMeshes.h).
//
// Follows the same commands the Fast3D interpreter executes for room geometry (F3DEX2 plus the OTR resource opcodes):
// vertex loads, triangles, sub display lists, texture image/tile/load commands, combiner, other modes and colors.
// Everything else is skipped.

#include "MapMeshes.h"

#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>

#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <fast/resource/type/Texture.h>

#include "soh/ResourceManagerHelpers.h"
#include "soh/resource/type/Scene.h"
#include "soh/resource/type/scenecommand/SetMesh.h"

extern "C" {
#include <z64.h>
#include "variables.h"
#include "functions.h"
#include "macros.h"
}

namespace ZopMap {

namespace {

constexpr uint32_t kGeomLighting = 0x00020000; // F3DEX2 G_LIGHTING

struct TileDesc {
    int fmt = 0, siz = 0, line = 0, tmem = 0, palette = 0;
    int cms = 0, cmt = 0, masks = 0, maskt = 0, shifts = 0, shiftt = 0;
    int uls = 0, ult = 0, lrs = 0, lrt = 0;
};

struct TexSrc {
    std::shared_ptr<Fast::Texture> tex;
    std::string name;
    bool animated = false;
};

struct TlutLoad {
    int tmem;
    int count;
    TexSrc src;
};

struct LoadedVtx {
    V3 pos;
    int16_t s, t;
    uint8_t c[4];
};

struct DecodeState {
    Ship::ResourceManager* rm = nullptr;
    LoadedVtx vtx[64] = {};
    TileDesc tiles[8];
    TexSrc timg;
    std::map<int, TexSrc> tmemSrc;
    std::vector<TlutLoad> tluts;
    int texTile = 0;
    bool texOn = false;
    float scaleS = 1.0f, scaleT = 1.0f;
    uint32_t geom = 0;
    uint32_t otherH = 0, otherL = 0;
    uint32_t comb0 = 0, comb1 = 0;
    uint32_t prim = 0xFFFFFFFF, env = 0xFFFFFFFF;
    bool matDirty = true;
    int curMat = -1;
    Mesh* mesh = nullptr;
    int cmdBudget = 400000;
};

uint32_t Bits(uint64_t w, int pos, int width) {
    return (uint32_t)((w >> pos) & ((1ULL << width) - 1));
}

bool IsOtrPath(uintptr_t p) {
    if ((p < 0x10000) || (p & 1)) {
        return false;
    }
#if UINTPTR_MAX > 0xFFFFFFFFu
    if (p > 0x0000FFFFFFFFFFFFull) {
        return false;
    }
#endif
    return memcmp((const char*)p, "__OTR__", 7) == 0;
}

// Segmented N64 addresses can't be resolved here (scene animation segments etc.)
bool IsSegmented(uintptr_t p) {
    return p < 0x10000000;
}

uint64_t HashOf(const Gfx* cmd) {
    return ((uint64_t)(uint32_t)cmd->words.w0 << 32) + (uint32_t)cmd->words.w1;
}

TexSrc TextureByName(DecodeState& st, const char* name) {
    TexSrc src;
    if ((name == nullptr) || (name[0] == '\0')) {
        return src;
    }
    auto res = st.rm->LoadResource(name);
    src.tex = std::dynamic_pointer_cast<Fast::Texture>(res);
    if (src.tex != nullptr) {
        src.name = name;
    }
    return src;
}

TexSrc TextureByHash(DecodeState& st, uint64_t hash) {
    const char* name = st.rm->GetArchiveManager()->HashToCString(hash);
    return TextureByName(st, name);
}

Gfx* DlByPath(DecodeState& st, const char* path) {
    if ((path == nullptr) || (path[0] == '\0')) {
        return nullptr;
    }
    return (Gfx*)st.rm->GetResourceRawPointer(path);
}

float Shifted(float v, int shift) {
    if ((shift > 0) && (shift <= 10)) {
        return v / (float)(1 << shift);
    }
    if (shift > 10) {
        return v * (float)(1 << (16 - shift));
    }
    return v;
}

void BuildMaterial(DecodeState& st) {
    Material m;
    const TileDesc& tile = st.tiles[st.texTile & 7];

    if (st.texOn) {
        auto it = st.tmemSrc.find(tile.tmem);
        if (it != st.tmemSrc.end()) {
            m.tex = it->second.tex;
            m.texName = it->second.name;
            m.animated = it->second.animated;
        }
        m.fmt = tile.fmt;
        m.siz = tile.siz;
        m.cms = tile.cms;
        m.cmt = tile.cmt;
        // colour-indexed: find the palette load that covers this tile's palette
        if (tile.fmt == 2 /* G_IM_FMT_CI */) {
            int start = 256 + ((tile.siz == 0) ? tile.palette * 16 : 0);
            for (auto it2 = st.tluts.rbegin(); it2 != st.tluts.rend(); ++it2) {
                if ((start >= it2->tmem) && (start < it2->tmem + it2->count)) {
                    m.tlut = it2->src.tex;
                    m.tlutName = it2->src.name;
                    m.tlutOffset = start - it2->tmem;
                    break;
                }
            }
        }
    }
    m.combine0 = st.comb0;
    m.combine1 = st.comb1;
    m.otherModeH = st.otherH;
    m.otherModeL = st.otherL;
    m.geometryMode = st.geom;
    m.primColor = st.prim;
    m.envColor = st.env;

    std::string key = m.Key();
    for (size_t i = 0; i < st.mesh->materials.size(); i++) {
        if (st.mesh->materials[i].Key() == key) {
            st.curMat = (int)i;
            st.matDirty = false;
            return;
        }
    }
    st.mesh->materials.push_back(m);
    st.curMat = (int)st.mesh->materials.size() - 1;
    st.matDirty = false;
}

void EmitVertex(DecodeState& st, const LoadedVtx& v) {
    Mesh& mesh = *st.mesh;
    const Material& mat = mesh.materials[st.curMat];
    const TileDesc& tile = st.tiles[st.texTile & 7];
    float u = 0.0f;
    float w = 0.0f;

    if (mat.tex != nullptr) {
        float texW = (mat.tex->Width > 0) ? mat.tex->Width : (float)((tile.lrs - tile.uls) / 4 + 1);
        float texH = (mat.tex->Height > 0) ? mat.tex->Height : (float)((tile.lrt - tile.ult) / 4 + 1);
        float s = Shifted(v.s * st.scaleS / 32.0f, tile.shifts) - tile.uls / 4.0f;
        float t = Shifted(v.t * st.scaleT / 32.0f, tile.shiftt) - tile.ult / 4.0f;
        u = s / std::max(texW, 1.0f);
        w = t / std::max(texH, 1.0f);
    }

    mesh.pos.push_back(v.pos);
    mesh.uv.push_back(u);
    mesh.uv.push_back(w);
    if (st.geom & kGeomLighting) {
        // the colour bytes hold normals: plain white
        mesh.col.insert(mesh.col.end(), { 255, 255, 255, v.c[3] });
    } else {
        mesh.col.insert(mesh.col.end(), { v.c[0], v.c[1], v.c[2], v.c[3] });
    }
}

void EmitTri(DecodeState& st, uint32_t a, uint32_t b, uint32_t c) {
    if ((a >= 64) || (b >= 64) || (c >= 64)) {
        return;
    }
    if (st.matDirty || (st.curMat < 0)) {
        BuildMaterial(st);
    }
    EmitVertex(st, st.vtx[a]);
    EmitVertex(st, st.vtx[b]);
    EmitVertex(st, st.vtx[c]);
    st.mesh->triMaterial.push_back(st.curMat);
}

void LoadVertices(DecodeState& st, const Vtx* src, int count, int dest) {
    if ((src == nullptr) || (dest < 0) || (dest + count > 64)) {
        return;
    }
    for (int i = 0; i < count; i++) {
        const Vtx_t& in = src[i].v;
        LoadedVtx& out = st.vtx[dest + i];
        out.pos = { (float)in.ob[0], (float)in.ob[1], (float)in.ob[2] };
        out.s = in.tc[0];
        out.t = in.tc[1];
        memcpy(out.c, in.cn, 4);
    }
}

void DecodeDl(DecodeState& st, Gfx* cmd, int depth) {
    if ((cmd == nullptr) || (depth > 16)) {
        return;
    }

    while (st.cmdBudget-- > 0) {
        uint64_t w0 = (uint64_t)cmd->words.w0;
        uintptr_t w1 = (uintptr_t)cmd->words.w1;
        uint8_t op = (uint8_t)(w0 >> 24);

        switch (op) {
            case 0x01: { // G_VTX
                int n = Bits(w0, 12, 8);
                int dest = Bits(w0, 1, 7) - n;
                if (!IsSegmented(w1)) {
                    LoadVertices(st, (const Vtx*)w1, n, dest);
                }
                break;
            }
            case 0x32: { // G_VTX_OTR_HASH: offset (or cached pointer), hash in the next word
                int n = Bits(w0, 12, 8);
                int dest = Bits(w0, 1, 7) - n;
                uintptr_t offset = w1;
                cmd++;
                const Vtx* v = nullptr;
                if (offset > 0xFFFFF) {
                    v = (const Vtx*)offset;
                } else {
                    char* base = (char*)st.rm->GetResourceRawPointer(HashOf(cmd));
                    if (base != nullptr) {
                        v = (const Vtx*)(base + offset);
                    }
                }
                LoadVertices(st, v, n, dest);
                break;
            }
            case 0x24: { // G_VTX_OTR_FILEPATH
                const char* path = (const char*)w1;
                cmd++;
                int n = (int)cmd->words.w0;
                int dest = (int)(((uintptr_t)cmd->words.w1) >> 16);
                int dataOff = (int)(((uintptr_t)cmd->words.w1) & 0xFFFF);
                const Vtx* v = (const Vtx*)DlByPath(st, path);
                if (v != nullptr) {
                    LoadVertices(st, v + dataOff, n, dest);
                }
                break;
            }
            case 0x05: // G_TRI1
                EmitTri(st, Bits(w0, 16, 8) / 2, Bits(w0, 8, 8) / 2, Bits(w0, 0, 8) / 2);
                break;
            case 0x06: // G_TRI2
            case 0x07: // G_QUAD
                EmitTri(st, Bits(w0, 17, 7), Bits(w0, 9, 7), Bits(w0, 1, 7));
                EmitTri(st, Bits(w1, 17, 7), Bits(w1, 9, 7), Bits(w1, 1, 7));
                break;

            case 0xDE: { // G_DL
                bool branch = Bits(w0, 16, 1) != 0;
                Gfx* sub = nullptr;
                if (IsOtrPath(w1)) {
                    sub = DlByPath(st, (const char*)w1 + 7);
                } else if (!IsSegmented(w1)) {
                    sub = (Gfx*)w1;
                }
                DecodeDl(st, sub, depth + 1);
                if (branch) {
                    return;
                }
                break;
            }
            case 0x27: { // G_DL_OTR_FILEPATH
                bool branch = Bits(w0, 16, 1) != 0;
                DecodeDl(st, DlByPath(st, (const char*)w1), depth + 1);
                if (branch) {
                    return;
                }
                break;
            }
            case 0x31: { // G_DL_OTR_HASH
                cmd++;
                DecodeDl(st, (Gfx*)st.rm->GetResourceRawPointer(HashOf(cmd)), depth + 1);
                break;
            }
            case 0xDF: // G_ENDDL
                return;

            case 0xD7: { // G_TEXTURE
                uint32_t s = Bits(w1, 16, 16);
                uint32_t t = Bits(w1, 0, 16);
                st.scaleS = (s == 0xFFFF) ? 1.0f : s / 65536.0f;
                st.scaleT = (t == 0xFFFF) ? 1.0f : t / 65536.0f;
                st.texTile = Bits(w0, 8, 3);
                st.texOn = Bits(w0, 1, 7) != 0;
                st.matDirty = true;
                break;
            }
            case 0xD9: // G_GEOMETRYMODE
                st.geom = (st.geom & Bits(w0, 0, 24)) | (uint32_t)w1;
                st.matDirty = true;
                break;
            case 0xE2:   // G_SETOTHERMODE_L
            case 0xE3: { // G_SETOTHERMODE_H
                int len = Bits(w0, 0, 8) + 1;
                int shift = 32 - Bits(w0, 8, 8) - len;
                uint32_t mask = (uint32_t)(((1ULL << len) - 1) << shift);
                uint32_t& mode = (op == 0xE2) ? st.otherL : st.otherH;
                mode = (mode & ~mask) | ((uint32_t)w1 & mask);
                st.matDirty = true;
                break;
            }
            case 0xEF: // G_RDPSETOTHERMODE
                st.otherH = Bits(w0, 0, 24);
                st.otherL = (uint32_t)w1;
                st.matDirty = true;
                break;
            case 0xFC: // G_SETCOMBINE
                st.comb0 = Bits(w0, 0, 24);
                st.comb1 = (uint32_t)w1;
                st.matDirty = true;
                break;
            case 0xFA: // G_SETPRIMCOLOR
                st.prim = (uint32_t)w1;
                st.matDirty = true;
                break;
            case 0xFB: // G_SETENVCOLOR
                st.env = (uint32_t)w1;
                st.matDirty = true;
                break;

            case 0xFD: // G_SETTIMG
                st.timg = TexSrc();
                if (IsOtrPath(w1)) {
                    st.timg = TextureByName(st, (const char*)w1 + 7);
                } else if (IsSegmented(w1)) {
                    st.timg.animated = true;
                }
                break;
            case 0x20: // G_SETTIMG_OTR_HASH
                cmd++;
                st.timg = TextureByHash(st, HashOf(cmd));
                break;
            case 0x25: // G_SETTIMG_OTR_FILEPATH
                st.timg = TextureByName(st, (const char*)w1);
                break;
            case 0xF5: { // G_SETTILE
                TileDesc& t = st.tiles[Bits(w1, 24, 3)];
                t.fmt = Bits(w0, 21, 3);
                t.siz = Bits(w0, 19, 2);
                t.line = Bits(w0, 9, 9);
                t.tmem = Bits(w0, 0, 9);
                t.palette = Bits(w1, 20, 4);
                t.cmt = Bits(w1, 18, 2);
                t.maskt = Bits(w1, 14, 4);
                t.shiftt = Bits(w1, 10, 4);
                t.cms = Bits(w1, 8, 2);
                t.masks = Bits(w1, 4, 4);
                t.shifts = Bits(w1, 0, 4);
                st.matDirty = true;
                break;
            }
            case 0xF2: { // G_SETTILESIZE
                TileDesc& t = st.tiles[Bits(w1, 24, 3)];
                t.uls = Bits(w0, 12, 12);
                t.ult = Bits(w0, 0, 12);
                t.lrs = Bits(w1, 12, 12);
                t.lrt = Bits(w1, 0, 12);
                st.matDirty = true;
                break;
            }
            case 0xF3:   // G_LOADBLOCK
            case 0xF4: { // G_LOADTILE
                st.tmemSrc[st.tiles[Bits(w1, 24, 3)].tmem] = st.timg;
                st.matDirty = true;
                break;
            }
            case 0xF0: { // G_LOADTLUT
                TlutLoad load;
                load.tmem = st.tiles[Bits(w1, 24, 3)].tmem;
                load.count = Bits(w1, 14, 10) + 1;
                load.src = st.timg;
                st.tluts.push_back(load);
                st.matDirty = true;
                break;
            }

            case 0x33: // G_MARKER (two words)
            case 0x36: // G_MTX_OTR (two words)
                cmd++;
                break;
            default:
                break;
        }
        cmd++;
    }
}

void FinishMesh(Mesh& mesh) {
    if (mesh.pos.empty()) {
        return;
    }
    mesh.bbMin = mesh.bbMax = mesh.pos[0];
    for (const V3& p : mesh.pos) {
        mesh.bbMin = { std::min(mesh.bbMin.x, p.x), std::min(mesh.bbMin.y, p.y), std::min(mesh.bbMin.z, p.z) };
        mesh.bbMax = { std::max(mesh.bbMax.x, p.x), std::max(mesh.bbMax.y, p.y), std::max(mesh.bbMax.z, p.z) };
    }
}

void DecodeEntry(std::vector<Mesh>& out, int room, int entry, bool xlu, Gfx* dl) {
    if ((dl == nullptr) || !IsOtrPath((uintptr_t)dl)) {
        return;
    }
    Mesh mesh;
    DecodeState st;
    st.rm = Ship::Context::GetRawInstance()->GetResourceManager().get();
    st.mesh = &mesh;
    mesh.room = room;
    mesh.entry = entry;
    mesh.xlu = xlu;
    mesh.dlPath = (const char*)dl + 7;
    mesh.label = "Raum " + std::to_string(room) + " / Teil " + std::to_string(entry) + (xlu ? " (transparent)" : "");

    DecodeDl(st, DlByPath(st, mesh.dlPath.c_str()), 0);
    if (!mesh.pos.empty()) {
        FinishMesh(mesh);
        out.push_back(std::move(mesh));
    }
}

s16 sMeshScene = -1;
bool sMeshMq = false;
std::vector<Mesh> sMeshes;

} // namespace

std::string Material::Key() const {
    char buf[160];
    snprintf(buf, sizeof(buf), "|%d|%d|%d|%d|%d|%08X%08X|%08X%08X|%08X|%08X%08X|%d", tlutOffset, fmt, siz, cms, cmt,
             combine0, combine1, otherModeH, otherModeL, geometryMode, primColor, envColor, animated ? 1 : 0);
    return texName + "|" + tlutName + buf;
}

void ForgetSceneMeshes() {
    sMeshes.clear();
    sMeshScene = -1;
}

const std::vector<Mesh>& SceneMeshes(PlayState* play) {
    bool mq = ResourceMgr_IsSceneMasterQuest(play->sceneNum);

    if ((sMeshScene == play->sceneNum) && (sMeshMq == mq)) {
        return sMeshes;
    }
    sMeshes.clear();
    sMeshScene = play->sceneNum;
    sMeshMq = mq;

    for (int room = 0; room < play->numRooms; room++) {
        if ((play->roomList == nullptr) || (play->roomList[room].fileName == nullptr)) {
            continue;
        }
        auto res = std::static_pointer_cast<SOH::Scene>(
            ResourceMgr_GetResourceByNameHandlingMQ(play->roomList[room].fileName));
        if (res == nullptr) {
            continue;
        }
        for (auto& cmd : res->commands) {
            if ((cmd == nullptr) || (cmd->cmdId != SOH::SceneCommandID::SetMesh)) {
                continue;
            }
            auto* setMesh = (SOH::SetMesh*)cmd.get();
            if (setMesh->meshHeaderType == 2) {
                for (size_t i = 0; i < setMesh->dlists2.size(); i++) {
                    DecodeEntry(sMeshes, room, (int)i, false, setMesh->dlists2[i].opa);
                    DecodeEntry(sMeshes, room, (int)i, true, setMesh->dlists2[i].xlu);
                }
            } else if (setMesh->meshHeaderType == 0) {
                for (size_t i = 0; i < setMesh->dlists.size(); i++) {
                    DecodeEntry(sMeshes, room, (int)i, false, setMesh->dlists[i].opa);
                    DecodeEntry(sMeshes, room, (int)i, true, setMesh->dlists[i].xlu);
                }
            }
            break;
        }
    }
    return sMeshes;
}

// ---- textures

namespace {

uint8_t Expand5(uint32_t v) {
    return (uint8_t)((v << 3) | (v >> 2));
}

void Rgba16(uint16_t c, uint8_t* out) {
    out[0] = Expand5((c >> 11) & 0x1F);
    out[1] = Expand5((c >> 6) & 0x1F);
    out[2] = Expand5((c >> 1) & 0x1F);
    out[3] = (c & 1) ? 255 : 0;
}

} // namespace

bool DecodeTexture(const Material& mat, int& width, int& height, std::vector<uint8_t>& rgba) {
    const Fast::Texture* tex = mat.tex.get();

    if ((tex == nullptr) || (tex->ImageData == nullptr) || (tex->Width == 0) || (tex->Height == 0) ||
        (tex->Flags & (TEX_FLAG_LOAD_AS_IMG | TEX_FLAG_LOAD_AS_RAW))) {
        return false;
    }
    width = tex->Width;
    height = tex->Height;
    size_t n = (size_t)width * height;
    const uint8_t* d = tex->ImageData;
    size_t size = tex->ImageDataSize;
    rgba.assign(n * 4, 255);

    auto px = [&](size_t i) { return &rgba[i * 4]; };

    switch (tex->Type) {
        case Fast::TextureType::RGBA32bpp:
            for (size_t i = 0; i < n && i * 4 + 3 < size; i++) {
                memcpy(px(i), d + i * 4, 4);
            }
            break;
        case Fast::TextureType::RGBA16bpp:
            for (size_t i = 0; i < n && i * 2 + 1 < size; i++) {
                Rgba16((uint16_t)((d[i * 2] << 8) | d[i * 2 + 1]), px(i));
            }
            break;
        case Fast::TextureType::Grayscale4bpp:
            for (size_t i = 0; i < n && i / 2 < size; i++) {
                uint8_t v = (i & 1) ? (d[i / 2] & 0xF) : (d[i / 2] >> 4);
                v = (uint8_t)(v * 17);
                uint8_t* p = px(i);
                p[0] = p[1] = p[2] = p[3] = v;
            }
            break;
        case Fast::TextureType::Grayscale8bpp:
            for (size_t i = 0; i < n && i < size; i++) {
                uint8_t* p = px(i);
                p[0] = p[1] = p[2] = p[3] = d[i];
            }
            break;
        case Fast::TextureType::GrayscaleAlpha4bpp:
            for (size_t i = 0; i < n && i / 2 < size; i++) {
                uint8_t v = (i & 1) ? (d[i / 2] & 0xF) : (d[i / 2] >> 4);
                uint8_t* p = px(i);
                p[0] = p[1] = p[2] = (uint8_t)(((v >> 1) * 255) / 7);
                p[3] = (v & 1) ? 255 : 0;
            }
            break;
        case Fast::TextureType::GrayscaleAlpha8bpp:
            for (size_t i = 0; i < n && i < size; i++) {
                uint8_t* p = px(i);
                p[0] = p[1] = p[2] = (uint8_t)((d[i] >> 4) * 17);
                p[3] = (uint8_t)((d[i] & 0xF) * 17);
            }
            break;
        case Fast::TextureType::GrayscaleAlpha16bpp:
            for (size_t i = 0; i < n && i * 2 + 1 < size; i++) {
                uint8_t* p = px(i);
                p[0] = p[1] = p[2] = d[i * 2];
                p[3] = d[i * 2 + 1];
            }
            break;
        case Fast::TextureType::Palette4bpp:
        case Fast::TextureType::Palette8bpp: {
            const Fast::Texture* pal = mat.tlut.get();
            bool ci4 = tex->Type == Fast::TextureType::Palette4bpp;
            if ((pal == nullptr) || (pal->ImageData == nullptr)) {
                return false;
            }
            size_t palEntries = pal->ImageDataSize / 2;
            for (size_t i = 0; i < n; i++) {
                size_t idx;
                if (ci4) {
                    if (i / 2 >= size) {
                        break;
                    }
                    idx = (i & 1) ? (d[i / 2] & 0xF) : (d[i / 2] >> 4);
                } else {
                    if (i >= size) {
                        break;
                    }
                    idx = d[i];
                }
                idx += mat.tlutOffset;
                if (idx < palEntries) {
                    Rgba16((uint16_t)((pal->ImageData[idx * 2] << 8) | pal->ImageData[idx * 2 + 1]), px(i));
                }
            }
            break;
        }
        default:
            return false;
    }
    return true;
}

} // namespace ZopMap

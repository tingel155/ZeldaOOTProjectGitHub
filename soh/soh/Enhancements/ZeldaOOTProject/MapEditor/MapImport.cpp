// ZeldaOOTProject map editor, stage 3: edited meshes back into the game.
//
// The Blender add-on writes `<name>.zopmesh.json` files into `zop_maps/` (next to soh.exe): triangles in game
// coordinates, UVs, vertex colours, a material per triangle and PNG textures. For each file the game builds a display
// list with RGBA32 textures and puts it into the resource cache under the path of the room display list it replaces
// (ResourceManager::CacheExternalResource), so the room draws the new mesh from then on - no restart, no archive.
// Removing the file and unloading the path brings the original back.

#include "MapImport.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>

#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <fast/resource/ResourceType.h>
#include <fast/resource/type/DisplayList.h>
#include <fast/resource/type/Texture.h>
#include <stb_image.h>

// OTRGlobals.cpp: drops the renderer's cached upload of the texture at this address
extern "C" void Gfx_TextureCacheDelete(const uint8_t* addr);

extern "C" {
#include <z64.h>
#include "macros.h"
}

namespace ZopMap {

namespace {

namespace fs = std::filesystem;

constexpr uint32_t kGeomLighting = 0x00020000;
constexpr int kMaxLoadTexels = 4096; // what one LOADBLOCK can describe

struct Built {
    std::shared_ptr<Fast::DisplayList> dl;
    std::vector<Vtx> vtx;
    std::vector<std::unique_ptr<std::string>> texPaths; // "__OTR__..." strings the display list points at
    std::vector<std::string> cachedTextures;            // resource paths to unload with it
    std::vector<std::shared_ptr<Fast::Texture>> textures;
};

std::map<std::string, Built> sBuilt; // by replaced display list path
// Replaced builds are kept until the game ends: the frame being drawn (and the frame interpolation) can still point at
// their display list, vertices and texture data, and freed texture memory could come back at the same address, which
// the renderer's texture cache would mistake for the old texture.
std::vector<Built> sRetired;
std::vector<Override> sOverrides;
int sBuildCounter = 0;

std::filesystem::path PathU8(const std::string& s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::string U8(const fs::path& p) {
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

uint32_t ParseHex(const nlohmann::json& j, const char* key, uint32_t def) {
    if (!j.contains(key) || !j[key].is_string()) {
        return def;
    }
    std::string s = j[key].get<std::string>();
    if (s.empty()) {
        return def;
    }
    return (uint32_t)std::stoul(s.substr(std::max<int>(0, (int)s.size() - 8)), nullptr, 16);
}

int Log2(int v) {
    int n = 0;
    while ((1 << (n + 1)) <= v) {
        n++;
    }
    return n;
}

int Pow2Near(int v) {
    int p = 1 << Log2(std::max(v, 1));
    return (v - p > p * 2 - v) ? p * 2 : p;
}

struct TexInfo {
    std::string path; // resource path (no prefix)
    int vw = 0, vh = 0; // size the display list describes
    int pw = 0, ph = 0; // real size of the texture
};

// Loads a PNG as an RGBA32 texture resource (power-of-two size, HD scale factors when bigger than one load)
bool MakeTexture(const std::string& png, const std::string& resPath, TexInfo& out, std::string& error,
                 std::shared_ptr<Fast::Texture>& keep) {
    std::ifstream f(PathU8(png), std::ios::binary);
    if (!f) {
        error = "Textur nicht gefunden: " + png;
        return false;
    }
    std::vector<unsigned char> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    int w, h, comp;
    unsigned char* px = stbi_load_from_memory(file.data(), (int)file.size(), &w, &h, &comp, 4);
    if (px == nullptr) {
        error = "Textur nicht lesbar: " + png;
        return false;
    }

    int pw = std::clamp(Pow2Near(w), 1, 1024);
    int ph = std::clamp(Pow2Near(h), 1, 1024);
    auto buffer = std::make_shared<std::vector<char>>((size_t)pw * ph * 4);
    for (int y = 0; y < ph; y++) {
        for (int x = 0; x < pw; x++) {
            int sx = x * w / pw;
            int sy = y * h / ph;
            memcpy(&(*buffer)[((size_t)y * pw + x) * 4], &px[((size_t)sy * w + sx) * 4], 4);
        }
    }
    stbi_image_free(px);

    // both axes by the same factor (the renderer's HD path expects a uniform scale)
    int vw = pw, vh = ph;
    while ((vw * vh > kMaxLoadTexels) && (vw > 1) && (vh > 1)) {
        vw /= 2;
        vh /= 2;
    }

    auto init = std::make_shared<Ship::ResourceInitData>();
    init->Path = resPath;
    init->Type = (uint32_t)Fast::ResourceType::Texture;
    init->ResourceVersion = 0;
    init->IsCustom = true;
    auto tex = std::make_shared<Fast::Texture>(init);
    tex->Type = Fast::TextureType::RGBA32bpp;
    tex->Width = (uint16_t)pw;
    tex->Height = (uint16_t)ph;
    tex->HByteScale = (float)pw / vw;
    tex->VPixelScale = (float)ph / vh;
    tex->mImageBuffer = buffer;
    tex->ImageData = (uint8_t*)buffer->data();
    tex->ImageDataSize = (uint32_t)buffer->size();

    // the renderer caches uploaded textures by data address: drop whatever it had at this one
    Gfx_TextureCacheDelete((const uint8_t*)buffer->data());
    Ship::Context::GetRawInstance()->GetResourceManager()->CacheExternalResource(resPath, tex);
    keep = tex;
    out.path = resPath;
    out.vw = vw;
    out.vh = vh;
    out.pw = pw;
    out.ph = ph;
    return true;
}

bool Build(const fs::path& file, Override& ov) {
    std::ifstream f(file);
    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        ov.error = std::string("JSON-Fehler: ") + e.what();
        return false;
    }
    ov.dlPath = j.value("displayList", "");
    ov.room = j.value("room", -1);
    ov.entry = j.value("entry", -1);
    ov.xlu = j.value("xlu", false);
    if (ov.dlPath.empty()) {
        ov.error = "Kein Ziel (displayList) in der Datei";
        return false;
    }

    const auto& mats = j["materials"];
    const auto& pos = j["positions"];  // 9 per triangle
    const auto& uv = j["uvs"];         // 6 per triangle
    const auto& col = j["colors"];     // 12 per triangle (0..1)
    const auto& triMat = j["triMaterials"];
    size_t triCount = triMat.size();
    if ((pos.size() != triCount * 9) || (uv.size() != triCount * 6) || (col.size() != triCount * 12)) {
        ov.error = "Dreiecksdaten unvollständig";
        return false;
    }
    ov.tris = (int)triCount;

    Built built;
    int version = ++sBuildCounter;
    fs::path baseDir = file.parent_path();

    // textures
    std::vector<TexInfo> texOf(mats.size());
    for (size_t m = 0; m < mats.size(); m++) {
        std::string png = mats[m].value("png", "");
        if (png.empty()) {
            continue;
        }
        fs::path p = PathU8(png);
        if (p.is_relative()) {
            p = baseDir / p;
        }
        std::string resPath = "zop_maps/" + std::to_string(version) + "/" + U8(file.stem()) + "_" + std::to_string(m);
        std::string err;
        std::shared_ptr<Fast::Texture> keep;
        if (MakeTexture(U8(p), resPath, texOf[m], err, keep)) {
            built.cachedTextures.push_back(resPath);
            built.textures.push_back(keep);
        } else if (ov.error.empty()) {
            ov.error = err; // still builds, that material just stays untextured
        }
    }

    built.vtx.reserve(triCount * 3);
    std::vector<Gfx> g;
    g.push_back(gsDPPipeSync());

    // triangles grouped by material
    std::vector<size_t> order(triCount);
    for (size_t i = 0; i < triCount; i++) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return triMat[a].get<int>() < triMat[b].get<int>(); });

    // vertices first (the display list keeps pointers into this vector)
    std::vector<int> firstVtx(triCount);
    for (size_t k = 0; k < triCount; k++) {
        size_t t = order[k];
        int m = triMat[t].get<int>();
        const TexInfo& ti = (m >= 0 && m < (int)texOf.size()) ? texOf[m] : TexInfo();
        firstVtx[k] = (int)built.vtx.size();
        for (int c = 0; c < 3; c++) {
            Vtx v = {};
            size_t pi = t * 9 + c * 3;
            v.v.ob[0] = (s16)std::clamp(lroundf(pos[pi].get<float>()), -32768L, 32767L);
            v.v.ob[1] = (s16)std::clamp(lroundf(pos[pi + 1].get<float>()), -32768L, 32767L);
            v.v.ob[2] = (s16)std::clamp(lroundf(pos[pi + 2].get<float>()), -32768L, 32767L);
            float u = uv[t * 6 + c * 2].get<float>();
            float w = 1.0f - uv[t * 6 + c * 2 + 1].get<float>();
            v.v.tc[0] = (s16)std::clamp(lroundf(u * std::max(ti.vw, 1) * 32.0f), -32768L, 32767L);
            v.v.tc[1] = (s16)std::clamp(lroundf(w * std::max(ti.vh, 1) * 32.0f), -32768L, 32767L);
            for (int ch = 0; ch < 4; ch++) {
                v.v.cn[ch] = (u8)std::clamp(lroundf(col[t * 12 + c * 4 + ch].get<float>() * 255.0f), 0L, 255L);
            }
            built.vtx.push_back(v);
        }
    }

    int curMat = -2;
    size_t k = 0;
    while (k < triCount) {
        int m = triMat[order[k]].get<int>();
        if (m != curMat) {
            curMat = m;
            nlohmann::json info = (m >= 0 && m < (int)mats.size()) ? mats[m] : nlohmann::json::object();
            const TexInfo& ti = (m >= 0 && m < (int)texOf.size()) ? texOf[m] : TexInfo();
            bool textured = !ti.path.empty();

            uint32_t defH = (G_AD_NOTPATTERN | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                            G_TD_CLAMP | G_TP_PERSP | G_CYC_2CYCLE | G_PM_NPRIMITIVE) & 0xFFFFFF; // setup list 25
            uint32_t defL = ov.xlu ? (G_RM_FOG_SHADE_A | G_RM_AA_ZB_XLU_SURF2) : (G_RM_FOG_SHADE_A | G_RM_AA_ZB_OPA_SURF2);
            uint32_t otherH = ParseHex(info, "otherModeH", defH);
            if (otherH == 0) {
                otherH = defH; // exported before the decoder ran the room setup list: the game's defaults
            }
            uint32_t otherL = ParseHex(info, "otherModeL", defL);
            // plain RGBA32 now: no palette, no mip levels
            otherH &= ~((3u << G_MDSFT_TEXTLUT) | (1u << G_MDSFT_TEXTLOD) | (3u << G_MDSFT_TEXTDETAIL));
            bool twoCycle = ((otherH >> G_MDSFT_CYCLETYPE) & 3) == (G_CYC_2CYCLE >> G_MDSFT_CYCLETYPE);
            uint32_t geom = ParseHex(info, "geometryMode",
                                     G_ZBUFFER | G_SHADE | G_CULL_BACK | G_FOG | G_SHADING_SMOOTH);
            if (!(geom & G_ZBUFFER)) {
                // exported before the decoder ran the room setup list, which turns these on
                geom |= G_ZBUFFER | G_SHADING_SMOOTH;
            }
            geom = (geom & ~kGeomLighting) | G_SHADE;

            g.push_back(gsDPPipeSync());
            g.push_back(gsDPSetOtherMode(otherH, otherL));
            g.push_back(gsSPLoadGeometryMode(geom));
            if (textured) {
                g.push_back(gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON));
                if (twoCycle) {
                    g.push_back(gsDPSetCombineMode(G_CC_MODULATERGBA, G_CC_PASS2));
                } else {
                    g.push_back(gsDPSetCombineMode(G_CC_MODULATERGBA, G_CC_MODULATERGBA));
                }
                int cms = info.value("clampMirrorS", 0) & 3;
                int cmt = info.value("clampMirrorT", 0) & 3;
                if ((ti.pw != ti.vw) || (ti.ph != ti.vh)) {
                    // the renderer cuts clamped HD textures down to the N64 tile size; wrap instead (clamped
                    // textures normally cover their faces exactly, so this looks the same)
                    cms &= ~G_TX_CLAMP;
                    cmt &= ~G_TX_CLAMP;
                }
                built.texPaths.push_back(std::make_unique<std::string>("__OTR__" + ti.path));
                const char* timg = built.texPaths.back()->c_str();
                Gfx load[] = { gsDPLoadTextureBlock(timg, G_IM_FMT_RGBA, G_IM_SIZ_32b, ti.vw, ti.vh, 0, cms, cmt,
                                                    Log2(ti.pw), Log2(ti.ph), G_TX_NOLOD, G_TX_NOLOD) };
                // masks are the real (HD) size: with the N64 size the renderer would cut HD textures down to it
                // a real image width (the macro puts 1) makes the renderer take the row stride of HD-sized textures
                // from the resource instead of the N64 tile
                load[0] = gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_32b, ti.vw, timg);
                g.insert(g.end(), std::begin(load), std::end(load));
            } else {
                g.push_back(gsSPTexture(0, 0, 0, G_TX_RENDERTILE, G_OFF));
                if (twoCycle) {
                    g.push_back(gsDPSetCombineMode(G_CC_SHADE, G_CC_PASS2));
                } else {
                    g.push_back(gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE));
                }
            }
        }

        // up to 10 triangles of this material per vertex load
        size_t end = k;
        while ((end < triCount) && (end - k < 10) && (triMat[order[end]].get<int>() == curMat)) {
            end++;
        }
        int n = (int)(end - k);
        g.push_back(gsSPVertex((uintptr_t)&built.vtx[firstVtx[k]], n * 3, 0));
        for (int t = 0; t < n; t += 2) {
            if (t + 1 < n) {
                g.push_back(gsSP2Triangles(t * 3, t * 3 + 1, t * 3 + 2, 0, t * 3 + 3, t * 3 + 4, t * 3 + 5, 0));
            } else {
                g.push_back(gsSP1Triangle(t * 3, t * 3 + 1, t * 3 + 2, 0));
            }
        }
        k = end;
    }
    g.push_back(gsDPPipeSync());
    g.push_back(gsSPEndDisplayList());

    auto init = std::make_shared<Ship::ResourceInitData>();
    init->Path = ov.dlPath;
    init->Type = (uint32_t)Fast::ResourceType::DisplayList;
    init->IsCustom = true;
    built.dl = std::make_shared<Fast::DisplayList>(init);
    built.dl->UCode = ucode_f3dex2;
    built.dl->Instructions = std::move(g);

    auto rm = Ship::Context::GetRawInstance()->GetResourceManager();
    auto old = sBuilt.find(ov.dlPath);
    if (old != sBuilt.end()) {
        for (const std::string& t : old->second.cachedTextures) {
            rm->UnloadResource(t);
        }
    }
    rm->CacheExternalResource(ov.dlPath, built.dl);
    if (old != sBuilt.end()) {
        sRetired.push_back(std::move(old->second));
    }
    sBuilt[ov.dlPath] = std::move(built);
    return true;
}

} // namespace

std::string OverrideDir() {
    return Ship::Context::GetPathRelativeToAppDirectory("zop_maps");
}

const std::vector<Override>& Overrides() {
    return sOverrides;
}

bool ApplyOverrides(bool onlyChanged) {
    std::error_code ec;
    fs::path dir = PathU8(OverrideDir());
    std::vector<Override> found;
    bool changed = false;

    if (fs::is_directory(dir, ec)) {
        for (auto& entry : fs::directory_iterator(dir, ec)) {
            std::string name = U8(entry.path().filename());
            const std::string ext = ".zopmesh.json";
            if (!entry.is_regular_file() || (name.size() <= ext.size()) ||
                (name.compare(name.size() - ext.size(), ext.size(), ext) != 0)) {
                continue;
            }
            Override ov;
            ov.file = U8(entry.path());
            ov.mtime = (int64_t)entry.last_write_time(ec).time_since_epoch().count();

            auto prev = std::find_if(sOverrides.begin(), sOverrides.end(),
                                     [&](const Override& o) { return o.file == ov.file; });
            if (onlyChanged && (prev != sOverrides.end()) && (prev->mtime == ov.mtime)) {
                found.push_back(*prev);
                continue;
            }
            Build(entry.path(), ov);
            found.push_back(ov);
            changed = true;
        }
    }

    // files that are gone: back to the original
    auto rm = Ship::Context::GetRawInstance()->GetResourceManager();
    for (const Override& o : sOverrides) {
        bool still = std::any_of(found.begin(), found.end(), [&](const Override& n) { return n.dlPath == o.dlPath; });
        if (!still && !o.dlPath.empty()) {
            auto it = sBuilt.find(o.dlPath);
            if (it != sBuilt.end()) {
                for (const std::string& t : it->second.cachedTextures) {
                    rm->UnloadResource(t);
                }
                sRetired.push_back(std::move(it->second));
                sBuilt.erase(it);
            }
            rm->UnloadResource(o.dlPath);
            changed = true;
        }
    }
    sOverrides = found;
    if (changed) {
        ForgetSceneMeshes();
    }
    return changed;
}

bool RemoveOverride(const std::string& file) {
    std::error_code ec;
    fs::remove(PathU8(file), ec);
    ApplyOverrides(true);
    return !ec;
}

} // namespace ZopMap

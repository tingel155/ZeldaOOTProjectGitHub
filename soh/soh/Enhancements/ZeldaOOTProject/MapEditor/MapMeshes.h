#pragma once

// ZeldaOOTProject map editor: the room meshes of a scene, decoded from their display lists on the CPU.
//
// A "mesh" is one display list entry of a room's mesh header (its opaque or its translucent list). Its triangles are
// stored unindexed (3 vertices per triangle) in world space, with texture coordinates normalized to the texture size
// (0..1 = one repetition), vertex colors and one material per triangle.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Fast {
class Texture;
}
struct PlayState;

namespace ZopMap {

struct V3 {
    float x, y, z;
};

struct Material {
    std::shared_ptr<Fast::Texture> tex; // texture of the render tile (null: untextured)
    std::string texName;
    std::shared_ptr<Fast::Texture> tlut; // palette for CI textures
    std::string tlutName;
    int tlutOffset = 0; // first palette entry used (CI4 palettes)
    int fmt = 0;        // G_IM_FMT_* of the tile
    int siz = 0;        // G_IM_SIZ_*
    int cms = 0;        // G_TX_WRAP / MIRROR / CLAMP bits, s and t
    int cmt = 0;
    bool animated = false; // the texture comes from a scene animation segment (not resolvable here)
    uint32_t combine0 = 0;
    uint32_t combine1 = 0;
    uint32_t otherModeH = 0;
    uint32_t otherModeL = 0;
    uint32_t geometryMode = 0;
    uint32_t primColor = 0xFFFFFFFF;
    uint32_t envColor = 0xFFFFFFFF;

    std::string Key() const;
};

struct Mesh {
    int room = 0;
    int entry = 0;
    bool xlu = false;
    std::string dlPath; // resource path of the display list
    std::string label;
    std::vector<V3> pos;           // 3 per triangle
    std::vector<float> uv;         // 2 per vertex
    std::vector<uint8_t> col;      // 4 per vertex (RGBA)
    std::vector<int> triMaterial;  // per triangle
    std::vector<Material> materials;
    V3 bbMin = { 0, 0, 0 };
    V3 bbMax = { 0, 0, 0 };

    size_t TriCount() const {
        return pos.size() / 3;
    }
};

// The meshes of all rooms of the current scene (decoded once per scene)
const std::vector<Mesh>& SceneMeshes(PlayState* play);
void ForgetSceneMeshes();

// Decodes a material's texture to RGBA8 (false if it has none)
bool DecodeTexture(const Material& mat, int& width, int& height, std::vector<uint8_t>& rgba);

// Writes `mesh` as a binary FBX (with PNG textures and a .zop.json with the N64 material data) into `dir`
bool ExportMesh(const Mesh& mesh, const std::string& sceneName, const std::string& dir, std::string& outPath,
                std::string& error);

} // namespace ZopMap

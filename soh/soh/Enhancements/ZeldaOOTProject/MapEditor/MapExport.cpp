// ZeldaOOTProject map editor: exports a room mesh as a binary FBX 7.4 file (Blender only reads binary FBX), its
// textures as PNG files next to it and a .zop.json with what the re-import needs to rebuild the N64 materials.
//
// The FBX keeps the game's coordinates (Y up, 1 unit = 1 cm in the file's unit scale), so a mesh exported and
// imported again lands in the same spot.

#include "MapMeshes.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>

namespace ZopMap {

namespace {

// ---- PNG (uncompressed deflate blocks; textures are small)

uint32_t Crc32(const uint8_t* data, size_t len, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

void PutBE32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v >> 24));
    out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)v);
}

void PngChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    PutBE32(out, (uint32_t)data.size());
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    PutBE32(out, Crc32(&out[start], out.size() - start));
}

bool WritePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(w * 4 + 1) * h);
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba.begin() + (size_t)y * w * 4, rgba.begin() + (size_t)(y + 1) * w * 4);
    }

    std::vector<uint8_t> z = { 0x78, 0x01 };
    size_t pos = 0;
    do {
        size_t len = std::min<size_t>(65535, raw.size() - pos);
        bool last = pos + len >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back((uint8_t)len);
        z.push_back((uint8_t)(len >> 8));
        z.push_back((uint8_t)~len);
        z.push_back((uint8_t)(~len >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + len);
        pos += len;
    } while (pos < raw.size());
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    PutBE32(z, (b << 16) | a);

    std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    std::vector<uint8_t> ihdr;
    PutBE32(ihdr, (uint32_t)w);
    PutBE32(ihdr, (uint32_t)h);
    ihdr.insert(ihdr.end(), { 8, 6, 0, 0, 0 }); // 8 bit RGBA
    PngChunk(png, "IHDR", ihdr);
    PngChunk(png, "IDAT", z);
    PngChunk(png, "IEND", {});

    std::ofstream f(path, std::ios::binary);
    f.write((const char*)png.data(), png.size());
    return f.good();
}

// ---- binary FBX

struct FbxNode {
    std::string name;
    std::vector<uint8_t> props;
    uint32_t numProps = 0;
    std::vector<FbxNode> children;

    template <typename T> void Raw(T v) {
        const uint8_t* p = (const uint8_t*)&v;
        props.insert(props.end(), p, p + sizeof(T));
    }
    FbxNode& I(int32_t v) {
        props.push_back('I');
        Raw(v);
        numProps++;
        return *this;
    }
    FbxNode& L(int64_t v) {
        props.push_back('L');
        Raw(v);
        numProps++;
        return *this;
    }
    FbxNode& D(double v) {
        props.push_back('D');
        Raw(v);
        numProps++;
        return *this;
    }
    FbxNode& C(bool v) {
        props.push_back('C');
        props.push_back(v ? 1 : 0);
        numProps++;
        return *this;
    }
    FbxNode& S(const std::string& s) {
        props.push_back('S');
        Raw((uint32_t)s.size());
        props.insert(props.end(), s.begin(), s.end());
        numProps++;
        return *this;
    }
    template <typename T> FbxNode& Arr(char type, const std::vector<T>& v) {
        props.push_back(type);
        Raw((uint32_t)v.size());
        Raw((uint32_t)0); // not compressed
        Raw((uint32_t)(v.size() * sizeof(T)));
        const uint8_t* p = (const uint8_t*)v.data();
        props.insert(props.end(), p, p + v.size() * sizeof(T));
        numProps++;
        return *this;
    }
    FbxNode& Add(const std::string& n) {
        children.push_back(FbxNode{ n });
        return children.back();
    }
};

void WriteNode(std::vector<uint8_t>& out, const FbxNode& node) {
    size_t start = out.size();
    out.resize(start + 12); // end offset, property count, property list length (+ name length below)
    out.push_back((uint8_t)node.name.size());
    out.insert(out.end(), node.name.begin(), node.name.end());
    out.insert(out.end(), node.props.begin(), node.props.end());
    if (!node.children.empty() || node.props.empty()) {
        for (const FbxNode& c : node.children) {
            WriteNode(out, c);
        }
        out.insert(out.end(), 13, 0);
    }
    uint32_t end = (uint32_t)out.size();
    uint32_t num = node.numProps;
    uint32_t len = (uint32_t)node.props.size();
    memcpy(&out[start], &end, 4);
    memcpy(&out[start + 4], &num, 4);
    memcpy(&out[start + 8], &len, 4);
}

std::string FbxName(const std::string& name, const char* cls) {
    std::string s = name;
    s.push_back('\0');
    s.push_back('\x01');
    s += cls;
    return s;
}

void P70(FbxNode& props, const char* name, const char* type, const char* label, const char* flags) {
    props.Add("P").S(name).S(type).S(label).S(flags);
}

std::string Sanitize(const std::string& s) {
    std::string out;
    for (char c : s) {
        out.push_back((isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_');
    }
    return out;
}

std::filesystem::path PathU8(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

std::string U8(const std::filesystem::path& p) {
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string Hex(uint32_t v) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%08X", v);
    return buf;
}

} // namespace

bool ExportMesh(const Mesh& mesh, const std::string& sceneName, const std::string& dir, std::string& outPath,
                std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(PathU8(dir), ec);
    if (ec) {
        error = "Ordner konnte nicht angelegt werden: " + dir;
        return false;
    }

    std::string base = Sanitize(sceneName) + "_r" + std::to_string(mesh.room) + "_t" + std::to_string(mesh.entry) +
                       (mesh.xlu ? "_xlu" : "_opa");
    fs::path fbxPath = PathU8(dir) / (base + ".fbx");
    fs::path texDir = PathU8(dir) / "textures";
    fs::create_directories(texDir, ec);

    // textures (one PNG per texture + palette)
    std::vector<std::string> matTexFile(mesh.materials.size());
    for (size_t i = 0; i < mesh.materials.size(); i++) {
        const Material& m = mesh.materials[i];
        int w, h;
        std::vector<uint8_t> rgba;
        if (!DecodeTexture(m, w, h, rgba)) {
            continue;
        }
        std::string file = Sanitize(m.texName);
        if (!m.tlutName.empty()) {
            file += "__" + Sanitize(m.tlutName) + "_" + std::to_string(m.tlutOffset);
        }
        file += ".png";
        fs::path p = texDir / file;
        if (!fs::exists(p) && !WritePng(U8(p), w, h, rgba)) {
            continue;
        }
        matTexFile[i] = "textures/" + file;
    }

    // ---- geometry: triangles share no vertices, so polygon vertex i is control point i
    size_t nv = mesh.pos.size();
    std::vector<double> verts;
    std::vector<int32_t> polyIdx;
    std::vector<double> uvs;
    std::vector<int32_t> uvIdx;
    std::vector<double> colors;
    std::vector<int32_t> colIdx;
    verts.reserve(nv * 3);
    for (size_t i = 0; i < nv; i++) {
        verts.push_back(mesh.pos[i].x);
        verts.push_back(mesh.pos[i].y);
        verts.push_back(mesh.pos[i].z);
        polyIdx.push_back((i % 3 == 2) ? -(int32_t)i - 1 : (int32_t)i);
        uvs.push_back(mesh.uv[i * 2]);
        uvs.push_back(1.0 - mesh.uv[i * 2 + 1]); // FBX/Blender v points up
        uvIdx.push_back((int32_t)i);
        for (int c = 0; c < 4; c++) {
            colors.push_back(mesh.col[i * 4 + c] / 255.0);
        }
        colIdx.push_back((int32_t)i);
    }
    std::vector<int32_t> triMats(mesh.triMaterial.begin(), mesh.triMaterial.end());

    const int64_t modelId = 1000, geomId = 1001;
    std::vector<FbxNode> top;
    top.reserve(8); // references into it are kept while adding

    FbxNode& hdr = top.emplace_back(FbxNode{ "FBXHeaderExtension" });
    hdr.Add("FBXHeaderVersion").I(1003);
    hdr.Add("FBXVersion").I(7400);
    hdr.Add("Creator").S("ZeldaOOTProject Map-Editor");

    FbxNode& gs = top.emplace_back(FbxNode{ "GlobalSettings" });
    gs.Add("Version").I(1000);
    FbxNode& gp = gs.Add("Properties70");
    gp.Add("P").S("UpAxis").S("int").S("Integer").S("").I(1);
    gp.Add("P").S("UpAxisSign").S("int").S("Integer").S("").I(1);
    gp.Add("P").S("FrontAxis").S("int").S("Integer").S("").I(2);
    gp.Add("P").S("FrontAxisSign").S("int").S("Integer").S("").I(1);
    gp.Add("P").S("CoordAxis").S("int").S("Integer").S("").I(0);
    gp.Add("P").S("CoordAxisSign").S("int").S("Integer").S("").I(1);
    gp.Add("P").S("UnitScaleFactor").S("double").S("Number").S("").D(1.0);

    FbxNode& objs = top.emplace_back(FbxNode{ "Objects" });
    FbxNode& geo = objs.Add("Geometry");
    geo.L(geomId).S(FbxName(base, "Geometry")).S("Mesh");
    geo.Add("Vertices").Arr('d', verts);
    geo.Add("PolygonVertexIndex").Arr('i', polyIdx);
    geo.Add("GeometryVersion").I(124);
    {
        FbxNode& e = geo.Add("LayerElementUV");
        e.I(0);
        e.Add("Version").I(101);
        e.Add("Name").S("UVMap");
        e.Add("MappingInformationType").S("ByPolygonVertex");
        e.Add("ReferenceInformationType").S("IndexToDirect");
        e.Add("UV").Arr('d', uvs);
        e.Add("UVIndex").Arr('i', uvIdx);
    }
    {
        FbxNode& e = geo.Add("LayerElementColor");
        e.I(0);
        e.Add("Version").I(101);
        e.Add("Name").S("Col");
        e.Add("MappingInformationType").S("ByPolygonVertex");
        e.Add("ReferenceInformationType").S("IndexToDirect");
        e.Add("Colors").Arr('d', colors);
        e.Add("ColorIndex").Arr('i', colIdx);
    }
    {
        FbxNode& e = geo.Add("LayerElementMaterial");
        e.I(0);
        e.Add("Version").I(101);
        e.Add("Name").S("");
        e.Add("MappingInformationType").S("ByPolygon");
        e.Add("ReferenceInformationType").S("IndexToDirect");
        e.Add("Materials").Arr('i', triMats);
    }
    {
        FbxNode& layer = geo.Add("Layer");
        layer.I(0);
        layer.Add("Version").I(100);
        for (const char* type : { "LayerElementUV", "LayerElementColor", "LayerElementMaterial" }) {
            FbxNode& le = layer.Add("LayerElement");
            le.Add("Type").S(type);
            le.Add("TypedIndex").I(0);
        }
    }

    FbxNode& model = objs.Add("Model");
    model.L(modelId).S(FbxName(base, "Model")).S("Mesh");
    model.Add("Version").I(232);
    model.Add("Properties70");
    model.Add("Shading").C(true);
    model.Add("Culling").S("CullingOff");

    FbxNode& conns = top.emplace_back(FbxNode{ "Connections" });
    conns.Add("C").S("OO").L(modelId).L(0);
    conns.Add("C").S("OO").L(geomId).L(modelId);

    nlohmann::json side;
    side["scene"] = sceneName;
    side["room"] = mesh.room;
    side["entry"] = mesh.entry;
    side["xlu"] = mesh.xlu;
    side["displayList"] = mesh.dlPath;
    side["materials"] = nlohmann::json::array();

    for (size_t i = 0; i < mesh.materials.size(); i++) {
        const Material& m = mesh.materials[i];
        int64_t matId = 2000 + (int64_t)i * 3;
        std::string matName = "mat" + std::to_string(i);
        uint8_t pr = m.primColor >> 24, pg = m.primColor >> 16, pb = m.primColor >> 8;

        FbxNode& mat = objs.Add("Material");
        mat.L(matId).S(FbxName(matName, "Material")).S("");
        mat.Add("Version").I(102);
        mat.Add("ShadingModel").S("lambert");
        mat.Add("MultiLayer").I(0);
        FbxNode& mp = mat.Add("Properties70");
        mp.Add("P").S("DiffuseColor").S("Color").S("").S("A").D(pr / 255.0).D(pg / 255.0).D(pb / 255.0);
        conns.Add("C").S("OO").L(matId).L(modelId);

        if (!matTexFile[i].empty()) {
            std::string abs = U8(PathU8(dir) / PathU8(matTexFile[i]));
            int64_t texId = matId + 1, vidId = matId + 2;
            FbxNode& tex = objs.Add("Texture");
            tex.L(texId).S(FbxName(matName, "Texture")).S("");
            tex.Add("Type").S("TextureVideoClip");
            tex.Add("Version").I(202);
            tex.Add("TextureName").S(FbxName(matName, "Texture"));
            tex.Add("Media").S(FbxName(matName, "Video"));
            tex.Add("FileName").S(abs);
            tex.Add("RelativeFilename").S(matTexFile[i]);
            FbxNode& vid = objs.Add("Video");
            vid.L(vidId).S(FbxName(matName, "Video")).S("Clip");
            vid.Add("Type").S("Clip");
            vid.Add("FileName").S(abs);
            vid.Add("RelativeFilename").S(matTexFile[i]);
            conns.Add("C").S("OO").L(vidId).L(texId);
            conns.Add("C").S("OP").L(texId).L(matId).S("DiffuseColor");
        }

        nlohmann::json jm;
        jm["name"] = matName;
        jm["texture"] = m.texName;
        jm["png"] = matTexFile[i];
        jm["tlut"] = m.tlutName;
        jm["tlutOffset"] = m.tlutOffset;
        jm["fmt"] = m.fmt;
        jm["siz"] = m.siz;
        jm["clampMirrorS"] = m.cms;
        jm["clampMirrorT"] = m.cmt;
        jm["animated"] = m.animated;
        jm["combine"] = Hex(m.combine0) + Hex(m.combine1);
        jm["otherModeH"] = Hex(m.otherModeH);
        jm["otherModeL"] = Hex(m.otherModeL);
        jm["geometryMode"] = Hex(m.geometryMode);
        jm["primColor"] = Hex(m.primColor);
        jm["envColor"] = Hex(m.envColor);
        side["materials"].push_back(jm);
    }

    std::vector<uint8_t> out;
    const char magic[] = "Kaydara FBX Binary  ";
    out.insert(out.end(), magic, magic + 20);
    out.insert(out.end(), { 0x00, 0x1A, 0x00 });
    uint32_t version = 7400;
    out.insert(out.end(), (uint8_t*)&version, (uint8_t*)&version + 4);
    for (const FbxNode& n : top) {
        WriteNode(out, n);
    }
    out.insert(out.end(), 13, 0);
    out.insert(out.end(), 16, 0); // footer padding (not read by Blender)

    std::ofstream f(fbxPath, std::ios::binary);
    f.write((const char*)out.data(), out.size());
    if (!f.good()) {
        error = "FBX konnte nicht geschrieben werden: " + U8(fbxPath);
        return false;
    }
    f.close();

    std::ofstream j(PathU8(dir) / (base + ".zop.json"));
    j << side.dump(2);

    outPath = U8(fbxPath);
    return true;
}

} // namespace ZopMap

#pragma once

// ZeldaOOTProject map editor, stage 3: meshes edited in Blender back into the game (see MapImport.cpp).

#include <cstdint>
#include <string>
#include <vector>

#include "MapMeshes.h"

namespace ZopMap {

struct Override {
    std::string file;   // the .zopmesh.json
    std::string dlPath; // room display list it replaces
    int room = -1;
    int entry = -1;
    bool xlu = false;
    int tris = 0;
    int64_t mtime = 0;
    std::string error; // empty when it built fine
};

// Folder the Blender add-on writes to (zop_maps next to soh.exe)
std::string OverrideDir();

// Builds the overrides found in OverrideDir() (only new/changed files when `onlyChanged`), restores the originals of
// removed files. Returns true if anything changed.
bool ApplyOverrides(bool onlyChanged);

const std::vector<Override>& Overrides();

// Deletes the file and brings the original mesh back
bool RemoveOverride(const std::string& file);

} // namespace ZopMap

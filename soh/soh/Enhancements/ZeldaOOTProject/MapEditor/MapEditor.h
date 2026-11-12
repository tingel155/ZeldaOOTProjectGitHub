#pragma once

// ZeldaOOTProject map editor: an in-game window to look at, export and (later) re-import the meshes of the maps.
// Stage 1: pick a map, freeze the game, fly through it with a Unity-like free camera, all rooms drawn at once.

#ifdef __cplusplus
#include <ship/window/gui/GuiWindow.h>

class ZopMapEditorWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override{};
};

extern "C" {
#endif

struct PlayState;

// The game logic is frozen while the editor is open (Play_Update skips the frame)
int ZOP_MapEditorFrozen(void);
// Actors are hidden while editing (if chosen in the editor)
int ZOP_MapEditorHideActors(void);
// Draws the rooms of the scene that aren't loaded right now (called after the normal room draws)
void ZOP_MapEditorDrawExtraRooms(struct PlayState* play, unsigned int flags);

#ifdef __cplusplus
}
#endif

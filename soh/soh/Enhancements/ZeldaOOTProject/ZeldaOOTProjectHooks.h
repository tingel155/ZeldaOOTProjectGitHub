#pragma once

// Call sites inside the vanilla C code for ZeldaOOTProject mods that cannot be built on GameInteractor hooks alone.
// Each one is a single call placed in the original source; the logic lives in the mod's C++ file.

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;
struct Player;
struct ColliderInfo;

// z_player_lib.c, Player_PostLimbDrawGameplay: the matrix stack holds Link's left hand (NayrusSword.cpp)
void ZOP_OnPlayerLeftHandDraw(struct PlayState* play, struct Player* player);

// z_collision_check.c, CollisionCheck_ApplyDamage: may change the damage an AC collider receives (NayrusSword.cpp).
// collider is a Collider* (an anonymous struct, so it cannot be forward declared)
float ZOP_ModifyAcDamage(struct PlayState* play, void* collider, struct ColliderInfo* info, float damage);

#ifdef __cplusplus
}
#endif

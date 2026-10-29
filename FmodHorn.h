#pragma once

#include "fh2mod.h"

#include <filesystem>

// Plays horns through the game's own FMOD engine (3D positional sound).
// Everything that touches FMOD runs on the game thread, from a hook on
// Studio::System::update; other threads only set the request below.
namespace fmodhorn {

// Hooks the engine. Returns false when the hook could not be installed.
bool install(const FH2ModApi* api, FH2Mod* mod, void* game_base,
             const std::filesystem::path& bank_path, int count);

// True once the engine has been captured and a first update ran.
bool ready();

// Linear gain applied to the horn (1 = as recorded).
void set_volume(float volume);


// index < 0 stops the horn. The horn loops while requested.
void request(int index);

} // namespace fmodhorn

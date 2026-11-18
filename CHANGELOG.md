# HonkHonk changelog

## v1.0.0

- Uses one 47-sound named row on platform 2.9.1+ (API 11), including the platform's A-button list picker.
- Retains the two named rows on platform 2.8.0-2.9.0 and falls back to them if the long row is refused. Keeps the legacy row cap explicitly at 32 when compiling with newer headers.
- Preserves the 0.5.1 bank, loudness, sound order, INI indexes and older numbered-slider behavior.
- Builds against the supplied 2.9.1 header; expands regression coverage for both row limits, all sounds, persistence and rejected-row fallback.

## v0.5.1

- Adds Firetruck after Martinshorn; appends Nyooom, La Cucaracha, Cyberpunk, Rizz and Stinky.
- Normalizes only the five new attachments to -23 LUFS in mono; keeps Nyooom at its checked source level.
- Preserves all 41 existing bank clips and the two named selectors; expands both selector types to 47 sounds (0-46).
- Includes Sound_List.txt with new-sound/version markers and INI indexes.
- Existing 0.5.0 INI selections 11-40 shift by +1; 0-10 stay unchanged.

## v0.5.0

- Normalized only Police2, Police3 and Martinshorn to -23 LUFS in the bank's mono mix, matching the new WAV horns. All other 38 bank sounds retain their previous audio.

- Fixed missing in-game selection: split 41 sounds across two named rows within the platform's 32-choice limit. Selecting either row synchronizes the inactive indicator and saves the global sound index. Added platform-limit, selection and persistence regression checks.

- Replaced Police using source-wav/Police.wav.
- Added Police2, Police3 and Martinshorn immediately after Police.
- Added Cat, Meow, Siren, SleighBells, Wheelspin, Win10 and Xbox at the end of the selector.
- Expanded the FSB5 bank and both named/numbered selectors to 41 sounds (0-40).
- Existing INI selections 8-30 shift by +3; 0-7 stay unchanged.
- Retained 0.4.0 controller, gameplay detection, hooks and positional audio behavior.

## v0.4.0

- Integrated [PR #1](https://github.com/suderek22/FH2_HonkHonk/pull/1): use the game's active controller snapshot instead of polling all four XInput slots every 16 ms, targeting the reported Wine/Proton lobby lag. Removed the XInput include and linker dependency. Only the active controller supplies horn input.
- Integrated [PR #2](https://github.com/suderek22/FH2_HonkHonk/pull/2): removed the `HUDStatusSpeedometer::UpdateGauges` function hook at `game + 0xAD6070`, implicated in Windows startup crashes. Gameplay detection reuses the existing HUD/car-position instruction hook at `game + 0x783E67`.
- Clear the HUD tick on a missing or unreadable car. Log the physics-only fallback when the instruction hook cannot be installed.
- Stop input setup when FMOD installation fails; do not disable unrelated successful audio/HUD hooks if the physics hook fails to enable.
- Retained the 0.3.5 named selector, sound bank and settings. Existing INIs and audio banks do not need replacement.
- Added 72 production-code regression checks for input memory, pad selection/disconnection, button release, the 250 ms gameplay boundary, HUD car loss, and hook setup/failure paths.
- MSVC build and regression/audio validation passed. Live Windows startup and Wine/Proton lobby validation remain outstanding; the underlying cause of freed trampoline memory is not proven.

## v0.3.5

- Wait up to 120 seconds for game code during slow launches.

## v0.3.4

- Named left/right horn selector with FH2 Mod Platform 2.8.0; older platforms retain the 0-30 slider.

## v0.3.3

- Sustained-loop improvements and more consistent reversing-beep timing.
- Store all 31 sounds in `HonkHonk\HZ2_SFX_HonkHonk.bank`, replacing loose WAV playback.
- Remove WinMM fallback playback and add automatic GitHub update checks.

## v0.3.1

- Remove the separate Horn options/preview page and add the `HornVolume` INI setting.
- Remove camera-relative Doppler pitch changes.

## v0.3.0

- Play horns through the game's FMOD engine as positional audio following the car.

## v0.2.4

- Last version with horn playback outside the game's audio engine.

# HonkHonk changelog

## v0.4.0

- Integrated [PR #1](https://github.com/suderek22/FH2_HonkHonk/pull/1): use the game's active controller snapshot instead of polling all four XInput slots every 16 ms, targeting the reported Wine/Proton lobby lag. Removed the XInput include and linker dependency. Only the active controller supplies horn input.
- Integrated [PR #2](https://github.com/suderek22/FH2_HonkHonk/pull/2): removed the `HUDStatusSpeedometer::UpdateGauges` function hook at `game + 0xAD6070`, implicated in Windows startup crashes. Gameplay detection reuses the existing HUD/car-position instruction hook at `game + 0x783E67`.
- Clear the HUD tick on a missing or unreadable car. Log the physics-only fallback when the instruction hook cannot be installed.
- Stop input setup when FMOD installation fails; do not disable unrelated successful audio/HUD hooks if the physics hook fails to enable.

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

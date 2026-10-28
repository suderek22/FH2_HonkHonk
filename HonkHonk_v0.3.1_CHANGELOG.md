# HonkHonk changelog

## v0.3.1
- Removed the "Horn options" menu page (horn cycling and "Play selected horn" preview). It was unreliable and will return when the Mod Platform gets better menu support. The **Horn** and **Horn sound** settings remain.
- Packaged release.

## v0.3.0 (development builds, probe 1-3)
- **The horn now uses the game's FMOD audio engine** instead of Windows `PlaySound`. It is a real 3D sound: panned by the camera, attenuated by distance (full volume up to ~5 m, silent at ~150 m) and routed through the game's audio.
- The horn sound is positioned at the player's car (world position read from the car state).
- Doppler pitch shift removed: the horn takes the listener's velocity, so there is no pitch change in chase, cockpit, hood, bumper or free camera.
- New `HornVolume` option in `HonkHonk.ini` (0-4, default 1.0).
- If the audio engine cannot be captured, the old Windows playback is used as a fallback.

## v0.2.4
- Last version using Windows audio (`PlaySound`), 31 sounds, L3 horn, Horn on/off and Horn sound slider, Horn options page.

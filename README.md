# HonkHonk v0.2.4

A proof of concept mod for Forza Horizon 2 (Azimuth 1.0.0.64) that lets you play a horn sound while driving. Click the **left stick** of your controller and the selected sound plays, looping for as long as you hold the stick down and stopping when you release it. Choose from 31 included sounds.

## Features

- Left-stick click plays the selected sound while driving; it loops while held and stops on release.
- 31 sounds included (see the list below).
- Works only while driving. It stays silent in pause, map, loading, garage and other menus.
- In-game options in the Mods menu: turn the horn on or off, choose the sound, and preview it.
- Your choices are saved in `HonkHonk.ini`.

## Requirements

- Forza Horizon 2 running through Azimuth, version 1.0.0.64.
- [FH2 Mod Platform](https://github.com/Trinculo54/FH2-Mod-Platform) v2.7.1 or newer.
- A controller (XInput compatible).

## Installation

1. Close the game.
2. Copy the contents of the `fh2-mods` folder from this package into the `fh2-mods` folder of your FH2 Mod Platform, merging when asked. You should end up with:
   ```
   fh2-mods\HonkHonk.dll
   fh2-mods\HonkHonk.ini
   fh2-mods\HonkHonk\   (the 31 .wav files)
   ```
3. Start the game. `fh2-mods\platform.log` should show HonkHonk registered with no errors.

## Using it

- **Play the horn:** while driving, click and hold the left stick (L3).
- **Settings:** open Pause > Settings > Mods.
  - **Horn:** On/Off. Applies immediately.
  - **Horn sound:** a left/right slider that picks the sound. Its description lists every number and its sound.
  - **Horn options** page: press A on **Horn sound** to cycle through the sounds, and A on **Play selected horn** to preview the sound, including outside of driving.

The first eight slider positions are: 0 Car, 1 Truck, 2 Classic, 3 Train, 4 Warthog, 5 Bicycle, 6 Reversing, 7 Police.

## Included sounds

Car, Truck, Classic, Train, Warthog, Bicycle, Reversing, Police, Boat, Doom, Doorbell, Honk, Horse, Quack, Rooster, Starter, Teams, WindowsXP, Wololo, Augh, Bad to the bone, Bruh, DejaVu, Discord, DJ, Goofy, Lego, MGS, Social credit, Spiderman, Wow.

## How it works

The mod loads the sounds from `fh2-mods\HonkHonk\` into memory when the game starts and plays them through Windows audio. It checks your controller for a left-stick click and uses two game hooks, the car physics update and the speedometer update, to know that you are driving. The horn is only triggered when both are active.

The game's own audio engine is not used, so the sound is not tied to the car's position and does not change with the camera, distance or surroundings. It plays at a steady volume, like any other app on your PC.

**Version 2.0 is planned to relay on game's audio engine.**

## Online play

Today HonkHonk plays sounds only on your own PC. It has no effect in online sessions: other players cannot hear your horn, even if they have the mod installed.
Future builds will target using game engine and usage with online mods.

## Known issues

- Horn sound preview selector is not updating, will be fixed using functionality upcoming with new FH2 Mod Platform version.

## Settings file

`fh2-mods\HonkHonk.ini`:
```
[HonkHonk]
HornEnabled=1
SelectedHorn=0
```
`HornEnabled`: 1 = on, 0 = off. `SelectedHorn`: sound number from 0 to 30.

## Uninstall

Delete `HonkHonk.dll`, `HonkHonk.ini` and the `HonkHonk` folder from `fh2-mods`.

# Install ZYG-ZXG on Linux (0.1.0 beta, unstable)

1. Download `ZYG-ZXG-linux-x86_64.tar.gz` from a GitHub release and extract it.
2. Copy the entire `ZYG-ZXG.vst3` folder into `~/.vst3/`.
3. Copy `ZYG-ZXG.clap` into `~/.clap/`.
4. Restart your DAW or rescan plugins. In REAPER, choose **VST3i: ZYG-ZXG** or **CLAP: ZYG-ZXG** on an instrument track.
5. Arm the track, enable input monitoring and play a MIDI keyboard or MIDI item. You can also click the on-screen keyboard.

## First steps

- The first time the editor opens it creates `~/Documents/ZYG-ZXG/`:
  - `Presets/`: put `.SerumPreset` and `.zygpreset` files here. Both are listed as **user presets**.
  - `Wavetables/`, `Noises/`, `Samples/`, `Impulses/`: your own audio.
  - Each folder contains a "PUT YOUR … HERE" note. You can move the library with MENU → USER LIBRARY FOLDER.
- The `presets/ZYG Showcase - Acid Morph.zygpreset` file from the archive is a good first patch. Copy it into your user Presets folder.
- **Browse presets** with the list icon or the preset-name dropdown. The browser stays open while you click or use ↑/↓ to audition. Press Enter or double-click to keep a preset and close. Afterwards, the ◀ ▶ arrows continue through the same list.
- **Modulate anything:**
  - Drag an ENV/LFO tab, a macro handle, VELO/NOTE, or the MW/PB/AT handles onto a control.
  - Or right-click any knob, field or fader → MOD SOURCE.
- **Serum 2 presets:** point ZYG-ZXG at the Serum content you legally own (MENU → SET CONTENT FOLDER, the folder that contains `Tables/` and `Samples/`). No Xfer content is included in this download.

## Important

This is a beta with incomplete Serum compatibility. **Imported Serum presets do not yet sound like Serum 2.** Some parameters are unmapped, and the parameter curves have not been calibrated. The **NOTES** badge in the top bar lists what an imported preset contains that ZYG-ZXG does not yet interpret.

If the editor misbehaves with your graphics driver:
- Switch off MENU → GPU RENDERING, or start the host with `ZYGZXG_NO_GPU=1`.
- MENU → RETRO SCREENS turns off the LCD/CRT display emulation.

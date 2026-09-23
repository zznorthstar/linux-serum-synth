# Install ZYG-ZXG on Linux (alpha)

1. Download the Linux x86_64 archive from a GitHub release and extract it.
2. Copy the entire `ZYG-ZXG.vst3` folder into `~/.vst3/`.
3. Copy `ZYG-ZXG.clap` into `~/.clap/`.
4. Restart your DAW or rescan plugins. In REAPER, choose **VST3i: ZYG-ZXG** or **CLAP: ZYG-ZXG** on an instrument track.
5. Arm the track, enable input monitoring, and play a MIDI keyboard or MIDI item. The MIDI indicator flashes on received note-ons; the voice count and output meter show whether the synth is generating audio. The **Audition C3** switch can produce a test note if the track is being processed.

Start from the built-in INIT patch, browse a mono WAV wavetable if desired, edit OSC A/B/C, SUB/NOISE, the first filter, envelope, macros and native LFO route, then click **Save ZYG** to write a `.zygpreset`. **Open ZYG** reloads one. To import a legally owned Serum 2 preset, click **Content** and select the directory containing `Tables/` and `Samples/`, then click **Load Serum**. No Xfer assets are included in this download.

This is an alpha instrument with incomplete Serum compatibility. An imported preset can sound different because many oscillator modes, LFO shapes, modulation destinations, filters, effects, arp and clips are still unsupported. **Diagnostics** copies the detailed gap report. The compatibility matrix in the repository states tested versions and limitations.

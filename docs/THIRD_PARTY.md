# Third-party research and licensing

- [Xfer Serum 2 manual](https://www.xferrecords.com/manual/serum-2/docs) and [What's New](https://static.xferrecords.com/Serum%202%20What's%20New.pdf): primary documentation, cited for feature research. No manual text or Xfer code/assets is bundled.
- [serum2gen](https://github.com/dougwithseismic/serum2gen): MIT licensed. Studied for container and identifier research. No source file has been copied into ZYG-ZXG.
- [serum2vital](https://github.com/btesser/serum2vital): GPL-3.0-or-later. Studied its documented, fixture-backed Serum 2 source-ID and FX findings as factual interoperability research. No GPL source file has been copied into ZYG-ZXG.
- [serum-preset-packager](https://github.com/KennethWussmann/serum-preset-packager): public container research. No license file was found in the examined checkout, so no code was copied.
- Python runtime dependencies: `cbor2` and `zstandard`, used only by the research CLI. Record their current licenses before distribution of the tooling.
- [JUCE 8.0.9](https://github.com/juce-framework/JUCE): pinned upstream source fetched at CMake configure time under JUCE's GPLv3/commercial dual licensing. ZYG's public build uses the GPLv3 route. No JUCE source is copied into Git.
- [nlohmann/json 3.11.3](https://github.com/nlohmann/json): MIT-licensed JSON/CBOR parsing dependency, pinned via CMake; no source copied into Git.
- [Zstandard/libzstd](https://github.com/facebook/zstd): BSD-licensed system library for Serum container decompression; linked at build/runtime, not bundled.
- The supplied Xfer installer, updater, presets, screenshot and extracted factory content are private reference material. They are excluded from Git and must not be redistributed with ZYG-ZXG.
- [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions), pinned to `55525c9858d4b25687be7759a5e0f70eccef218e`: MIT licensed, fetched with its CLAP submodules by CMake to build the CLAP wrapper. No extension source is copied into Git; its licenses must accompany distributed binaries as applicable.

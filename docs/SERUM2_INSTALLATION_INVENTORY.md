# Local Serum 2.1.5 installation inventory

This is a reproducible inventory of the user-owned Serum 2.1.5 material available on the development machine. It is reference evidence for interoperability work. It is **not** a compatibility claim, and none of these proprietary files may be committed or included in a ZYG-ZXG release.

## Supplied Windows installers

| File | Size | SHA-256 | Handling |
| --- | ---: | --- | --- |
| `Install_Xfer_Serum2_2.1.5.exe` | 1,313,084,056 bytes | `507b726d97bf78920157f3817aff003b9ee38ee961f4efd318cf43216370f695` | Listed/extracted with 7-Zip; never a ZYG runtime dependency |
| `Xfer Records Serum v2.1.5 update.exe` | 10,488,530 bytes | `fbc654b5bf03c229c0c6b8a829e2aa0457c15ec6e5fa29424b86a1eda8a655e5` | Retained as ignored reference material; not executed |

The full installer archive contains a Windows VST3 bundle at `Serum2.vst3/Contents/x86_64-win/Serum2.vst3`, an AAX plugin, VST3 resources, and factory/user content roots. These binaries may be used only as a legal local black-box oracle. ZYG-ZXG does not link, load, package, or copy their implementation.

## Extracted content tree

The ignored `.local-serum-content/` tree currently contains **7,358 files**. One is the locally generated `zygzxg-index.json`; the other 7,357 are extracted content. Top-level counts are:

| Root | Files | Root | Files |
| --- | ---: | --- | ---: |
| Arp Banks | 4 | Arp Patterns | 13 |
| Clip Banks | 16 | Clips | 141 |
| Curves | 19 | Effect Chains | 240 |
| Impulses | 110 | LFO Paths | 22 |
| LFO Shapes | 152 | Multisamples | 4,118 |
| Presets | 627 | PZ Filter | 33 |
| Samples | 856 | Skins | 691 |
| Styles | 19 | System | 7 |
| Tables | 289 | local generated index | 1 |

Observed content extensions include 4,747 FLAC files, 686 PNG files, 626 `.SerumPreset` files, 455 WAV files, 169 `.XferShape` files, 163 `.SerumFX` files, 140 SFZ files, 137 `.XferClip` files, 57 `.SerumFXRack` files, 26 single PZ filter files, 21 `.XferPath` files, 20 AIFF files, 18 `.SerumStyle` files, 14 clip banks, 12 arp patterns, five multi PZ filter files, four arp banks, three MIDI files, three fonts, and smaller metadata/system groups.

The installer list and extracted tree agree on the major content families. Inventory establishes what assets and file formats must be resolved. Each synth feature still needs semantic mapping, native DSP, editable UI, and controlled real-preset verification before its support status can advance.

## Public repository boundary

`.gitignore` excludes the installers, `.local-serum-content/`, Serum presets, audio assets, Windows executables, and plugin binaries. CI builds from source without Xfer material. Release archives contain only ZYG-ZXG binaries, project documentation, and dependency licenses.

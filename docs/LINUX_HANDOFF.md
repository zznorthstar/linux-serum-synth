# Continue the Windows checkpoint on Linux

Checkpoint branch: `checkpoint/windows-to-linux-2026-10-20`.
Read `AGENTS.md`, `HANDOFF.md`, `docs/TESTING.md` and
`docs/SERUM2_COMPATIBILITY_MATRIX.md` before continuing functional changes.

## What this checkpoint contains

The source includes the JUCE8 audio-thread/parameter handshake, Windows host
support, native DSP quality work, measured import mappings, embedded sample
recall, compressor revisions and EQ shelf/bell/cut correction. Reports describe
their limits. Windows VST3/CLAP/tools build and eight C++ suites passed, including
149 DSP checks; Python interoperability passed7/7. Twelve fresh-plugin Windows
REAPER EQ cases verified native recall and older-state behavior.

These changes have not yet been rebuilt or host-tested on Linux. The next task
is a fresh Linux build, regression run and native REAPER smoke test. Preserve
the independent native models and old-state migrations while fixing any real
platform failures. Full Serum compatibility is unfinished; pre-FX source levels,
other effects, modulation transitions and editor EQ mode choices remain open.
Replacement factory assets are deferred until the user explicitly requests them.

## Get the source and build

In an existing clean checkout:

```sh
git pull --ff-only origin main
git fetch origin
git show --stat origin/checkpoint/windows-to-linux-2026-10-20
```

Use a new Linux build directory. Do not reuse Windows CMake caches or binaries.
The Ubuntu/Debian package list below matches the repository's Linux workflow;
use equivalent development packages on other distributions.

```sh
sudo apt-get install build-essential cmake ninja-build git python3-venv \
  libzstd-dev libasound2-dev libx11-dev libxext-dev libxinerama-dev \
  libxrandr-dev libxcursor-dev libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev

cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DZYG_BUILD_CLAP=ON -DZYG_BUILD_TOOLS=ON
cmake --build build/linux --parallel 2
ctest --test-dir build/linux --output-on-failure

python3 -m venv .venv
. .venv/bin/activate
python -m pip install -e '.[oracle]'
python -m unittest discover -s tests -p test_interop.py
```

JUCE8.0.9 and other pinned source dependencies are fetched during configure.
The Linux plugin outputs are:

- `build/linux/ZYGZXG_artefacts/Release/VST3/ZYG-ZXG.vst3`
- `build/linux/ZYGZXG_artefacts/Release/CLAP/ZYG-ZXG.clap`

After building, install those native outputs in `~/.vst3/` and `~/.clap/`.
Use a disposable Linux REAPER project to check editor rendering, note-on/off,
state save/reload and a known original test preset before user projects.
The offline renderer is under `build/linux/zygzxg_render_artefacts/Release/`.
See `docs/INSTALL_LINUX.md` for the existing user-library and graphics options.

## Private material is a separate transfer

The Git checkpoint contains no Serum binaries, commercial presets, factory
audio, raw state chunks, REA decompilation or reference recordings. A source
pull cannot restore these. The local migration snapshot is private and must
not be uploaded to the public repository or attached to a release.

For continued measurements, retain or privately transfer the workspace's
`oracle-private/`, owned `Serum 2 Presets/`, additional preset libraries and
root-level musician presets. Keep them beside the repository, as on Windows.
The snapshot manifest records what was archived. Recreate Python environments
and analysis providers on the destination; do not reuse Windows virtualenvs.

Set ZYG's content folder to the Linux location of the owned content. Absolute
Windows asset paths and private host-launch scripts need remapping. Preserve
relative content structure. Missing assets are distinct from unsupported
features and must remain diagnosed. Public numerical reports remain useful
without private recordings, but fresh Serum comparisons need the reference VM.

## Keep the Windows reference VM

Linux is the native build/test target. Keep licensed Serum2, Windows REAPER and
the working REA/provider setup in the VM for reference measurements. A future
private SSH/shared-folder job runner can exchange original test specifications,
WAVs and accepted states. That bridge is not implemented by this checkpoint.
This source checkpoint and local file archive are not a hypervisor VM snapshot.

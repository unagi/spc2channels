# gme2channels

[![CI](https://github.com/unagi/spc2channels/actions/workflows/ci.yml/badge.svg)](https://github.com/unagi/spc2channels/actions/workflows/ci.yml)
[![Release](https://github.com/unagi/spc2channels/actions/workflows/release.yml/badge.svg)](https://github.com/unagi/spc2channels/actions/workflows/release.yml)

A CLI tool to extract individual channels (voices) from game music files as separate WAV files.

Uses [game-music-emu (libgme)](https://github.com/libgme/game-music-emu) for sound chip emulation, rendering each voice in isolation via per-voice muting.

## Supported Formats

All formats supported by libgme:

- **SPC** - Super Famicom / SNES (8 voices)
- **NSF** - Nintendo Entertainment System / Famicom
- **GBS** - Game Boy / Game Boy Color
- **VGM** - Sega Master System, Game Gear, Mega Drive
- **HES** - PC Engine / TurboGrafx-16
- **AY** - ZX Spectrum, Amstrad CPC
- **SAP** - Atari 8-bit
- **KSS** - MSX

## Building

### Prerequisites

- C compiler (gcc / clang)
- [game-music-emu](https://github.com/libgme/game-music-emu) (libgme)

### Installing libgme

```bash
git clone https://github.com/libgme/game-music-emu.git
cd game-music-emu
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
make -j$(nproc)
sudo make install
```

### Building gme2channels

```bash
# Dynamic linking (requires libgme installed on the system)
make

# Specify custom libgme location
make GME_PREFIX=/path/to/gme

# Static linking (portable binary)
make static GME_PREFIX=/path/to/gme
```

## Usage

### Basic (all channels as stereo WAV)

```bash
gme2channels input.spc ./output/
```

### Show file and voice info

```bash
gme2channels --info input.spc
```

### Mono output

```bash
gme2channels -f mono input.spc ./output/
```

### Per-channel format override

```bash
# Default mono, but split ch5 into separate L/R files
# (useful when L and R carry different instruments)
gme2channels -f mono --ch-format "5:split-lr" input.spc ./output/
```

### Extract specific channels only

```bash
# Channels 1, 3, and 5
gme2channels -c 1,3,5 input.spc ./output/

# Channels 1 through 4
gme2channels -c 1-4 input.spc ./output/
```

### Include full stereo mix

```bash
gme2channels --mix input.spc ./output/
```

### Disable SPC echo/reverb

```bash
gme2channels --no-echo input.spc ./output/
```

### Full example

```bash
gme2channels \
  -f mono \
  --ch-format "5:split-lr,6:mono" \
  --mix \
  --no-echo \
  -d 120 \
  --fade 8 \
  input.spc ./output/
```

## Options

| Option | Description | Default |
|---|---|---|
| `-f, --format <fmt>` | Output format: `stereo` / `mono` / `split-lr` | `stereo` |
| `-c, --channels <spec>` | Channels to extract: `1,3,5` or `1-4` | all |
| `--ch-format <spec>` | Per-channel format override: `"5:split-lr,6:mono"` | — |
| `-d, --duration <sec>` | Playback duration in seconds | auto-detect / 120s |
| `--fade <sec>` | Fade-out duration in seconds | 5 |
| `-r, --rate <hz>` | Sample rate | 44100 |
| `--no-echo` | Disable echo/reverb DSP effect (SPC only) | off |
| `--mix` | Also output full stereo mix | off |
| `--info` | Show file/voice info only (no rendering) | — |

## Output Filenames

| Format | Filename |
|---|---|
| stereo | `ch1_DSP_1.wav` |
| mono | `ch1_DSP_1_mono.wav` |
| split-lr | `ch1_DSP_1_L.wav` / `ch1_DSP_1_R.wav` |
| full mix | `full_mix.wav` |

## DAW Integration

All channel files are rendered with the same sample count and duration, so they can be imported directly into a DAW (Studio One, Logic Pro, etc.) as aligned multi-track stems for mixing and remastering.

## License

MIT (gme2channels source code)

This software links against [game-music-emu (libgme)](https://github.com/libgme/game-music-emu), which is licensed under **LGPL-2.1**. When using dynamic linking (the default), the MIT license applies to gme2channels with no additional restrictions. When distributing statically linked binaries, LGPL-2.1 Section 6 requirements apply (you must provide the means for users to relink with a modified libgme). See [LICENSE](LICENSE) for details.

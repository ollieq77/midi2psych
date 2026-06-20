# MIDI2Psych

A Windows tool for converting MIDI files into Psych Engine chart JSON. Takes two MIDI files (one per player) and outputs a chart ready to drop into a Psych Engine mod.

Available as a GUI application or a CLI tool — same binary, same options.

---

## Download

Grab the latest release from the [Releases](../../releases) page. The zip contains the executable and any required runtime DLLs — no install needed, just run it.

---

## Usage

### GUI

Double-click `midi2psych.exe`. The interface lets you select your P1 and P2 MIDI files, configure all options, and run the conversion with a live progress log.

### CLI

Run `midi2psych.exe` with arguments from a terminal and a console window will open automatically.

```
midi2psych.exe <p1.mid> <p2.mid> [output.json] [options]
```

**Arguments**

| Argument | Description |
|---|---|
| `p1.mid` | MIDI file for Player 1 (required) |
| `p2.mid` | MIDI file for Player 2 (required) |
| `output.json` | Output file path (default: `chart.json`) |

**Options**

| Flag | Description |
|---|---|
| `-s` / `--song <name>` | Song name written into the chart |
| `-b` / `--bpm <mult>` | BPM multiplier applied to the whole chart (default: 1.0) |
| `-o` / `--offset <ms>` | Shift all notes by N milliseconds |
| `-v` / `--velocity <n>` | Ignore notes below this MIDI velocity (default: 0) |
| `-p` / `--precision <n>` | Decimal places for timestamps |
| `--speed <n>` | Chart scroll speed |
| `--mania <n>` | Key count — 0=1-key, 1=2-key, 2=3-key, 3=4-key (default), 4=5-key, etc. |
| `--p1 <name>` | Player 1 character name |
| `--p2 <name>` | Player 2 character name |
| `--gf <name>` | Girlfriend character name |
| `--stage <name>` | Stage name |
| `--sustain` | Enable hold notes (uses note-on/off duration) |
| `--no-precision` | Disable high-precision timestamp output |
| `--split <n>` | Split output into multiple files, N notes per file |
| `--minify` | Strip unnecessary whitespace from the JSON |
| `--round <n>` | Round timestamps — `-1` = off, `0` = integer, `n` = N decimal places |

**Examples**

```
# Basic conversion
midi2psych.exe song_p1.mid song_p2.mid chart.json -s "My Song"

# 4-key chart with hold notes and a BPM bump
midi2psych.exe p1.mid p2.mid chart.json -s "My Song" --sustain -b 1.5

# 6-key chart, minified, with a specific stage
midi2psych.exe p1.mid p2.mid chart.json --mania 5 --minify --stage myStage
```

---

## How it works

1. **Parsing** — Both MIDI files are parsed in parallel. Tempo map, PPQ, and all note events are extracted from every track. Running status and variable-length values are handled correctly.

2. **Pitch-to-lane mapping** — By default, MIDI pitch values are distributed across lanes using a histogram-balancing algorithm: pitches are sorted by frequency and assigned greedily to the least-loaded lane, keeping the note spread as even as possible. You can switch to simple modulo mapping by disabling smart pitch mapping.

3. **Timing** — Tick values are converted to milliseconds using the full tempo map, so BPM changes mid-song are handled accurately. The BPM multiplier scales everything uniformly.

4. **Simultaneous note redistribution** — If multiple notes on the same lane land at exactly the same time, they are spread out evenly between that moment and the next note on that lane (within the same section), so the chart remains playable.

5. **Section building** — Notes are grouped into 4-beat sections. Each section gets a `mustHitSection` flag based on whether P1 or P2 has more notes in it. BPM changes within a section are flagged with `changeBPM`.

6. **Output** — A single JSON file is written in Psych Engine chart format. With `--split`, the chart is broken into multiple files at section boundaries.

---

## Building from source

Requires MinGW-w64 with g++ on your PATH. No other dependencies.

```
scripts\build_windows.bat
```

The built release lands in `dist\release\` with the executable and any bundled DLLs. Edit the config section at the top of the script to switch between `release`, `debug`, and `asan` builds or change compiler flags.

---

## Project structure

```
midi2psych/
  src/                  Source files
    main.cpp            Entry point — GUI/CLI dispatch
    midi_parser.cpp     Binary MIDI parser
    psych_converter.cpp Conversion logic and JSON output
    gui.cpp             Win32 GUI
    gui_logger.cpp      Thread-safe coloured log output
    progress_bar.cpp    Progress reporting
  include/              Header files
  scripts/
    build_windows.bat   Build script
  dist/
    build/              Compiler output
    release/            Distributable folder (exe + DLLs)
    zips/               Zipped releases (if MAKE_ZIP=1)
```

---

## License

See [LICENSE](LICENSE).
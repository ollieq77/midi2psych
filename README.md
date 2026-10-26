# MIDI2Psych

Turn two MIDI files into a Friday Night Funkin' chart for **Psych Engine**. One MIDI per player, one `chart.json` out the other end. That's it.

It's a single Windows `.exe` that works two ways: double-click it for a GUI, or run it from a terminal with arguments for CLI mode. Same options, same converter underneath.

Built mostly with spamcharts and big dense MIDIs in mind, so it's fast: both files get parsed in parallel and the sorting goes multi-threaded once the note count gets silly.

---

## Download

Grab the latest build from the [Releases](https://github.com/ollieq77/midi2psych/releases/latest) page. The zip has the exe plus any runtime DLLs it needs. No installer, just unzip and run.

---

## Quick start

### GUI

Double-click `midi2psych.exe`, then:

1. Pick your **P1** and **P2** MIDI files, and where the output JSON should go.
2. Tweak the options if you want (song name, BPM multiplier, key count, characters, and so on).
3. Hit **CONVERT** and watch the progress bar and console log do their thing.

### CLI

Run the exe with arguments from a terminal and it opens its own console window.

```
midi2psych.exe <p1.mid> <p2.mid> [output.json] [options]
```

The console waits for a keypress when it's done, so you can actually read the output.

**Arguments**

| Argument | What it is |
|---|---|
| `p1.mid` | MIDI for Player 1 (required) |
| `p2.mid` | MIDI for Player 2 (required) |
| `output.json` | Where to save the chart (default: `chart.json`) |

**Options**

| Flag | What it does |
|---|---|
| `-s` / `--song <name>` | Song name written into the chart |
| `-b` / `--bpm <mult>` | BPM multiplier for the whole chart (default `1.0`) |
| `-o` / `--offset <ms>` | Shift every note by N milliseconds |
| `-v` / `--velocity <n>` | Skip notes quieter than this MIDI velocity (default `0`) |
| `-p` / `--precision <n>` | Decimal places on timestamps (default `6`) |
| `--speed <n>` | Chart scroll speed (default `2.5`) |
| `--mania <n>` | Key count minus one: `0` = 1-key, `1` = 2-key, `3` = 4-key (default), `4` = 5-key, and so on |
| `--p1 <name>` | Player 1 character (default `bf`) |
| `--p2 <name>` | Player 2 character (default `dad`) |
| `--gf <name>` | Girlfriend character (default `gf`) |
| `--stage <name>` | Stage name (default `stage`) |
| `--sustain` | Turn note lengths into hold notes |
| `--no-precision` | Turn off high-precision timestamp output |
| `--split <n>` | Split the output into several files, about N notes each |
| `--minify` | Minify the JSON output |
| `--round <n>` | Round timestamps: `-1` = off, `0` = whole numbers, `n` = n decimal places |

**Examples**

```
# Basic conversion
midi2psych.exe song_p1.mid song_p2.mid chart.json -s "My Song"

# 4-key chart with hold notes and a 1.5x BPM bump
midi2psych.exe p1.mid p2.mid chart.json -s "My Song" --sustain -b 1.5

# 6-key chart, minified, on a custom stage
midi2psych.exe p1.mid p2.mid chart.json --mania 5 --minify --stage myStage
```

### Example

https://github.com/user-attachments/assets/007d624b-53a3-4ac5-b7de-9e9380150e94

---

## How it works

Here's what actually happens between "two MIDIs" and "chart.json":

1. **Parsing.** Each MIDI gets its own thread. The parser reads every track, pulls out the note events, the PPQ and the tempo map, and copes with running status and variable-length values. Notes quieter than your velocity cutoff get dropped here. With `--sustain` on, it also pairs each note-on with its note-off to get a length.

2. **Pitch to lane.** By default it uses *smart pitch mapping*. It counts how often each pitch shows up (across both files), sorts them most-used first, and hands each pitch to whichever lane currently has the fewest notes. The point is to keep the lanes evenly loaded instead of everything piling onto one arrow. If you turn smart mapping off (it's a checkbox in the GUI), it falls back to plain `pitch % keyCount`.

3. **Timing.** Ticks become milliseconds using the full tempo map, so mid-song BPM changes land where they should. The BPM multiplier scales everything evenly, and your offset gets added on top.

4. **Same-tick pile-ups.** When you squash a wide range of pitches onto 4 lanes, a lot of notes end up on the same lane at the exact same tick. Stacked notes are a nightmare to play. So if multiple notes share both a lane and a MIDI tick, the first one stays exactly on the tick and the rest are spaced evenly *inside that one tick*. At 120 BPM with 480 PPQ a tick is about 1 ms, so nothing shifts audibly, it just stops being a stack. Notes on *different* lanes at the same tick are left alone, so chords stay chords.

5. **Sections.** Notes are grouped into 4-beat sections. Each section's `mustHitSection` goes to whoever has more notes in it, so P1 wins ties. Empty sections just keep the previous section's setting, and the lanes flip to match so the right player is always on the right side. A BPM change inside a section gets flagged with `changeBPM`.

6. **Output.** One Psych Engine JSON file, or several if you used `--split`. Splits always happen on section boundaries, so a section never gets cut in half. Hold notes are only written out if they're at least one step (a quarter beat) long.

**Heads up:** the tempo map and PPQ are read from the **P1** MIDI (P2's tempo map is only used if P1 doesn't have one). Export both files from the same project with the same tempo and PPQ so they stay in sync.

---

## Building from source

You need MinGW-w64 with `g++` on your PATH. No other dependencies.

```
scripts\build_windows.bat
```

The build ends up in `dist\release\` with the exe and any bundled DLLs. The config block at the top of the script lets you switch between `release`, `debug` and `asan` builds, change the C++ standard, bundle extra DLLs, or zip up the release.

---

## Project layout

```
midi2psych/
  src/
    main.cpp              Entry point, decides GUI or CLI
    midi_parser.cpp       Binary MIDI parser
    psych_converter.cpp   The conversion logic and JSON writer
    gui.cpp               Win32 GUI
    gui_logger.cpp        Thread-safe coloured log output
    progress_bar.cpp      Progress reporting
  include/                Headers for all of the above
  scripts/
    build_windows.bat     Build script
  dist/                   Build output (gitignored)
    build/                Compiler output
    release/              The folder you actually ship (exe + DLLs)
    zips/                 Zipped releases (if MAKE_ZIP=1)
```

---

## License

See [LICENSE](LICENSE).
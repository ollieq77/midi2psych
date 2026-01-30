# midi2psych

A compact, practical tool that converts two MIDI tracks (player and opponent) into Psych Engine JSON charts.

What’s in this folder
- `midi2psych.cpp` — main source file
- `scripts/` — build scripts (Windows and Linux/macOS)
- `dist/` — built binaries and runtime DLLs for Windows
- example MIDI files: `bf.mid`, `dad.mid`

Quick start (Windows)
1. Install MinGW-w64 and add `C:\mingw64\bin` to your PATH.
2. Run the Windows builder from `scripts`:

```powershell
.\scripts\build_windows.bat
```

3. Run the built executable from `dist`:

```powershell
.\dist\midi2psych.exe player1.mid player2.mid
```

Common options
- `-s "name"` — song name
- `-b N` — BPM multiplier
- `--sustain` — enable sustain note handling
- `-v N` — ignore notes with velocity below N
- `-o N` — add N ms offset (use negative for earlier)

License: MIT

If you run into build or runtime errors, copy the compiler/linker output and I can help diagnose it.
# Quickstart

Minimal steps to build and run.

Windows
1. Install MinGW-w64 and add `C:\mingw64\bin` to your PATH.
2. From this repo run:

```powershell
.\scripts\build_windows.bat
```

3. Run the program from `dist`:

```powershell
.\dist\midi2psych.exe player1.mid player2.mid
```

Linux / macOS

```bash
chmod +x scripts/build_linux.sh
./scripts/build_linux.sh
./dist/midi2psych player1.mid player2.mid
```

Common flags
- `-s "name"` — song name
- `-b 1.25` — BPM multiplier
- `--sustain` — treat held notes as sustained
- `-v 40` — ignore notes with velocity < 40
- `-o 50` — add 50ms offset

Need help? Run:

```bash
./dist/midi2psych -h
```

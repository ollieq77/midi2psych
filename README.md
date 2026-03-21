# MIDI2Psych

A powerful tool for converting MIDI files to Psych Engine chart format, designed for Friday Night Funkin' modding. This converter supports both command-line interface (CLI) and graphical user interface (GUI) modes, making it easy to create custom charts from MIDI compositions.

## Features

- **Dual Interface**: Choose between CLI for automation or GUI for interactive use
- **Two-Player Support**: Convert separate MIDI files for Player 1 and Player 2 charts
- **Comprehensive Options**: Customize BPM, timing offset, velocity thresholds, and more
- **Sustain Notes**: Enable/disable sustain note conversion
- **Output Splitting**: Split large charts into multiple files
- **JSON Minification**: Optimize output file size
- **High Precision**: Configurable decimal precision for timestamps
- **Progress Tracking**: Real-time progress bars and logging

## Requirements

- **Operating System**: Windows 10/11
- **Compiler**: MinGW-w64 (GCC for Windows)
- **C++ Standard**: C++17

### Installing MinGW-w64

1. Download MinGW-w64 from [winlibs.com](https://winlibs.com/)
2. Extract to `C:\mingw64`
3. Add `C:\mingw64\bin` to your system PATH
4. Restart your terminal

Alternatively, install via winget:
```bash
winget install -e --id MSYS2.MSYS2
```

## Building

1. Clone or download the repository
2. Open Command Prompt in the project root
3. Run the build script:
   ```bash
   scripts\build_windows.bat
   ```
4. The compiled executable will be in the `dist\` folder

### Build Options

The build script supports different configurations:
- **Release** (default): Optimized build with `-O3` and LTO
- **Debug**: Unoptimized build with debug symbols
- **ASan**: Address Sanitizer enabled for debugging

Edit `scripts\build_windows.bat` to change build settings.

## Usage

### Command Line Interface

```bash
converter.exe <p1.mid> <p2.mid> [output.json] [options]
```

#### Basic Example
```bash
converter.exe player1.mid player2.mid mychart.json
```

#### Advanced Example
```bash
converter.exe song_p1.mid song_p2.mid chart.json --song "My Song" --bpm 1.2 --offset 50 --velocity 60 --sustain
```

### Options

| Option | Short | Description | Default |
|--------|-------|-------------|---------|
| `--song <name>` | `-s` | Song name for the chart | "Test Song" |
| `--bpm <mult>` | `-b` | BPM multiplier | 1.0 |
| `--offset <ms>` | `-o` | Note offset in milliseconds | 0.0 |
| `--velocity <n>` | `-v` | Minimum MIDI velocity threshold | 0 |
| `--precision <n>` | `-p` | Decimal places for timestamps | 3 |
| `--speed <n>` | | Chart scroll speed | 1.0 |
| `--p1 <name>` | | Player 1 character name | "bf" |
| `--p2 <name>` | | Player 2 character name | "dad" |
| `--gf <name>` | | Girlfriend character name | "gf" |
| `--stage <name>` | | Stage name | "stage" |
| `--sustain` | | Enable sustain notes | Disabled |
| `--no-precision` | | Disable high precision mode | Enabled |
| `--split <n>` | | Split output into files with N notes each | Disabled |
| `--minify` | | Minify JSON output | Disabled |
| `--round <n>` | | Round timestamps (-1=off, 0=int, 1=0.1, etc.) | -1 |

### Graphical User Interface

Simply run the executable without arguments to launch the GUI:

```bash
converter.exe
```

The GUI provides:
- File selection dialogs for MIDI inputs
- Output file selection
- Interactive option configuration
- Real-time progress tracking
- Log output window

## Output Format

The tool generates JSON files compatible with Psych Engine chart format, containing:
- Song metadata (name, BPM, speed, characters, stage)
- Note data with timestamps, lanes, and durations
- Tempo changes
- Section markers

## Troubleshooting

### Build Issues
- Ensure MinGW-w64 is properly installed and in PATH
- Check that all source files exist in `src/` directory
- Verify C++17 support in your GCC version

### Conversion Issues
- MIDI files should contain note data on appropriate channels
- Check velocity values if notes aren't being converted
- Use `--velocity` option to filter low-velocity notes
- Enable logging output for detailed conversion information

### GUI Issues
- Ensure Windows Common Controls are available
- Try running as administrator if file dialogs fail

## Contributing

Contributions are welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## License

This project is open source. Please check the license file for details.

## Credits

Built with C++17, Windows API, and standard MIDI parsing.
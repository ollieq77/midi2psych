#include "psych_converter.h"
#include "utils.h"

#ifdef _WIN32

#include <windows.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include <iostream>

#include "gui.h"
#include "gui_logger.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                   LPSTR /*lpCmdLine*/, int nCmdShow) {
    int     argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    // ── CLI mode: any extra arguments ────────────────────────────────────
    if (argc > 1) {
        AllocConsole();
        FILE* fDummy;
        freopen_s(&fDummy, "CONOUT$", "w", stdout);
        freopen_s(&fDummy, "CONOUT$", "w", stderr);
        freopen_s(&fDummy, "CONIN$",  "r", stdin);
        ENABLE_COLORS();

        // Wide → UTF-8
        std::vector<std::string> args;
        for (int i = 0; i < argc; ++i) {
            int len = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, NULL, 0, NULL, NULL);
            std::string s(len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, s.data(), len, NULL, NULL);
            args.push_back(std::move(s));
        }
        LocalFree(argv);

        if (argc < 3) {
            std::cout << RED "Error: Need at least 2 MIDI files!\n" RESET;
            std::cout << "Usage: converter.exe <p1.mid> <p2.mid> [output.json] [options]\n\n";
            std::cout << "Options:\n"
                      << "  -s / --song    <name>   Song name\n"
                      << "  -b / --bpm     <mult>   BPM multiplier\n"
                      << "  -o / --offset  <ms>     Note offset in ms\n"
                      << "  -v / --velocity <n>     Min MIDI velocity\n"
                      << "  -p / --precision <n>    Decimal places\n"
                      << "  --speed        <n>      Chart scroll speed\n"
                      << "  --mania        <n>      Key count (0=1-key, 2=3-key, 3=4-key(default), 4=5-key...)\n"
                      << "  --p1 / --p2 / --gf / --stage  <name>\n"
                      << "  --sustain               Enable sustain notes\n"
                      << "  --no-precision          Disable high precision\n"
                      << "  --split        <n>      Split output (N notes/file)\n"
                      << "  --minify                Minify JSON output\n"
                      << "  --round        <n>      Round timestamps (-1=off, 0=int, …)\n";
            system("pause");
            return 1;
        }

        std::string p1File  = args[1];
        std::string p2File  = args[2];
        std::string outFile = (argc > 3 && args[3][0] != '-') ? args[3] : "chart.json";

        PsychConverter converter;
        auto& cfg = converter.getConfig();

        for (int i = 3; i < argc; ++i) {
            const std::string& a = args[i];
            auto next = [&]() -> const std::string& { return args[++i]; };

            if      ((a == "-s"  || a == "--song")      && i+1 < argc) cfg.songName      = next();
            else if ((a == "-b"  || a == "--bpm")       && i+1 < argc) cfg.bpmMultiplier = std::stod(next());
            else if ((a == "-o"  || a == "--offset")    && i+1 < argc) cfg.noteOffset    = std::stod(next());
            else if ((a == "-v"  || a == "--velocity")  && i+1 < argc) cfg.minVelocity   = std::stoi(next());
            else if ((a == "-p"  || a == "--precision") && i+1 < argc) cfg.decimalPlaces = std::stoi(next());
            else if ( a == "--speed"   && i+1 < argc)                  cfg.speed         = std::stod(next());
            else if ( a == "--mania"   && i+1 < argc)                  cfg.mania         = std::stoi(next());
            else if ( a == "--p1"      && i+1 < argc)                  cfg.p1Char        = next();
            else if ( a == "--p2"      && i+1 < argc)                  cfg.p2Char        = next();
            else if ( a == "--gf"      && i+1 < argc)                  cfg.gfChar        = next();
            else if ( a == "--stage"   && i+1 < argc)                  cfg.stage         = next();
            else if ( a == "--split"   && i+1 < argc) { cfg.splitOutput = true;  cfg.notesPerSplit = std::stoi(next()); }
            else if ( a == "--round"   && i+1 < argc)                  cfg.roundTimesTo  = std::stoi(next());
            else if ( a == "--sustain")                                 cfg.sustainNotes  = true;
            else if ( a == "--minify")                                  cfg.minifyJSON    = true;
            else if ( a == "--no-precision")                            cfg.highPrecision = false;
        }

        converter.setConfig(cfg);
        bool ok = converter.convert(p1File, p2File, outFile);
        system("pause");
        return ok ? 0 : 1;
    }

    LocalFree(argv);

    // ── GUI mode ──────────────────────────────────────────────────────────
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(icex);
    icex.dwICC  = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEX wc  = {};
    wc.cbSize      = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance   = hInstance;
    wc.hCursor     = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(45, 45, 48));
    wc.lpszClassName = "MIDI2PsychConverter";
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassEx(&wc);

    g_hMainWnd = CreateWindowEx(0,
        "MIDI2PsychConverter", "MIDI to Psych Engine Converter v2.4",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 680,
        NULL, NULL, hInstance, NULL);

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

#else

// ── Non-Windows CLI stub ──────────────────────────────────────────────────────
#include <iostream>

int main(int /*argc*/, char* /*argv*/[]) {
    std::cout << "GUI mode is Windows-only. CLI support can be added here.\n";
    return 0;
}

#endif

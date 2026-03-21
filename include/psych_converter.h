#pragma once

#include <string>
#include <vector>

#ifdef _WIN32
  #include <windows.h>
#endif

#include "midi_parser.h"   // MIDINote, TempoChange

// ─── Chart data structures ────────────────────────────────────────────────────

struct ChartNote {
    double time;
    int    lane;
    double duration;

    ChartNote(double t, int l, double d = 0.0) : time(t), lane(l), duration(d) {}
};

struct Section {
    std::vector<ChartNote> notes;
    bool   mustHitSection = true;
    bool   changeBPM      = false;
    double bpm            = 120.0;
};

// ─── Converter ────────────────────────────────────────────────────────────────

class PsychConverter {
public:
    // All user-configurable knobs in one place.
    struct Config {
        std::string songName  = "Converted";
        std::string p1Char    = "bf";
        std::string p2Char    = "dad";
        std::string gfChar    = "gf";
        std::string stage     = "stage";
        double  speed         = 2.5;
        double  bpmMultiplier = 1.0;
        double  noteOffset    = 0.0;
        int     minVelocity   = 0;
        int     decimalPlaces = 6;
        int     mania         = 0;      // 0 = default(4-key), or 3,4,5,6,7,etc.
        bool    highPrecision = true;
        bool    sustainNotes  = false;
        bool    splitOutput   = false;
        int     notesPerSplit = 1000;
        bool    minifyJSON    = false;
        // -1 = off, 0 = integer, 1 = 1 d.p., 2 = 2 d.p., …
        int     roundTimesTo  = -1;
    };

    void   setConfig(const Config& cfg)  { m_config = cfg; }
    Config& getConfig()                  { return m_config; }
    void   setProgressHandle(HWND hwnd)  { m_progressHandle = hwnd; }

    // Main entry-point.  Returns true on success.
    bool convert(const std::string& p1File,
                 const std::string& p2File,
                 const std::string& outFile);

private:
    Config m_config;
    HWND   m_progressHandle = nullptr;

    // Tick → milliseconds, accounting for all tempo changes.
    double ticksToMs(uint32_t ticks, double finalBPM, uint16_t ppq,
                     const std::vector<TempoChange>& tempoChanges,
                     double multiplier) const;

    // Return the BPM active at a given millisecond timestamp.
    double getBPMAtTime(double time,
                        const std::vector<std::pair<double, double>>& timeToBPM,
                        double baseBPM) const;

    // Serialise sections to a Psych-Engine JSON string.
    std::string buildJSON(const std::vector<Section>& sections, double finalBPM) const;

    // Divide sections into chunks capped at notesPerChunk total notes.
    std::vector<std::vector<Section>> splitSections(const std::vector<Section>& sections,
                                                     int notesPerChunk) const;
};

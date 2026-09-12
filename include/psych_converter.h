#pragma once

#include <algorithm>
#include <functional>
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
        int     mania         = 3;      // 0=1-key, 2=3-key, 3=4-key(default), 4=5-key, etc.
        bool    highPrecision = true;
        bool    sustainNotes  = false;
        bool    splitOutput   = false;
        int     notesPerSplit = 1000;
        bool    minifyJSON    = false;
        // -1 = off, 0 = integer, 1 = 1 d.p., 2 = 2 d.p., …
        int     roundTimesTo  = -1;
        bool    smartPitchMapping = true;  // Intelligent pitch-to-lane distribution

        // ── Pattern Mode ──
        // When enabled, p1File/p2File (as passed to convert()) are treated as
        // MASTER midis: each note in them is a *trigger*, not a chart note.
        // A trigger at pitch C3 (60) selects patternFilesP1/P2[0] ("pattern 1"),
        // C#3 (61) selects index 1 ("pattern 2"), and so on. The selected
        // pattern's own notes are spliced in starting at the trigger's timestamp,
        // and run until the next trigger note (or the pattern's own last note,
        // whichever comes first).
        bool patternMode = false;
        std::vector<std::string> patternFilesP1;  // index 0 = pattern 1 = C3
        std::vector<std::string> patternFilesP2;
    };

    void   setConfig(const Config& cfg)  { m_config = cfg; clampConfig(); }
    Config& getConfig()                  { return m_config; }
    void   setProgressHandle(HWND hwnd)  { m_progressHandle = hwnd; }

    // Main entry-point.  Returns true on success.
    bool convert(const std::string& p1File,
                 const std::string& p2File,
                 const std::string& outFile);

private:
    Config m_config;
    HWND   m_progressHandle = nullptr;

    // Clamp config values to safe ranges
    void clampConfig() {
        m_config.mania = std::max(0, std::min(m_config.mania, 69420));
    }

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

    // Redistribute simultaneous same-lane notes evenly across the gap until
    // the next note with the same lane.
    void redistributeSimultaneousNotes(std::vector<ChartNote>& notes, double sectionLengthMs) const;

    // Intelligent pitch-to-lane mapping to avoid pitch collisions
    int smartPitchToLane(uint8_t midiNote, int keyCount, uint8_t minPitch, uint8_t maxPitch) const;

    // ── Pattern Mode ──
    // Parses masterFile as a trigger track and splices in the matching pattern
    // file's notes at each trigger's timestamp (see Config::patternMode above).
    // Returns a flat, tick-sorted note list in the MASTER file's own tick-space
    // (pattern ticks are rescaled by ppq ratio, so patterns can use any PPQ).
    // Writes the master's ppq/bpm/tempoChanges out through the reference params
    // so the caller can drop the result straight into a MIDIParser-shaped slot.
    // progressCallback (optional) is called with a value in [0,1] as master
    // parsing, pattern parsing, and splicing each make progress.
    std::vector<MIDINote> buildPatternNotes(const std::string& masterFile,
                                             const std::vector<std::string>& patternFiles,
                                             bool sustainNotes, int minVelocity,
                                             uint16_t& outPpq, double& outBpm,
                                             std::vector<TempoChange>& outTempo,
                                             const std::function<void(double)>& progressCallback = nullptr) const;
};

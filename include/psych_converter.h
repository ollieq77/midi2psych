#pragma once

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
  #include <windows.h>
#endif

#include "midi_parser.h"   // MIDINote, TempoChange

// ─── Chart data structures ────────────────────────────────────────────────────

// Field order is deliberate: 8+8+4+4 = 24 bytes with no padding (was 32), which
// matters when you're shuffling millions of these around in a sort.
struct ChartNote {
    double   time;
    double   duration;
    int      lane;
    uint32_t tick;      // source MIDI tick (used to spot notes sharing a tick)

    ChartNote(double t, int l, double d = 0.0, uint32_t tk = 0)
        : time(t), duration(d), lane(l), tick(tk) {}
};

// A section doesn't own its notes: it just remembers which slice of the chart's
// (time-sorted) note array it covers. Copying every note into its section used to
// cost as much memory again as the note array itself.
struct Section {
    size_t firstNote      = 0;      // notes [firstNote, lastNote) belong to this section
    size_t lastNote       = 0;
    bool   mustHitSection = true;
    bool   changeBPM      = false;
    double bpm            = 120.0;

    size_t noteCount() const { return lastNote - firstNote; }
};

// ─── Tempo map ────────────────────────────────────────────────────────────────

// Tick -> milliseconds in O(log T). Segment start times are precomputed once,
// so a lookup is a binary search plus one multiply instead of walking every
// tempo change from the start of the song. The maths is arranged exactly like
// the old per-call loop, so the results are bit-for-bit the same.
// `changes` must be sorted by tick.
class TempoMap {
public:
    TempoMap(const std::vector<TempoChange>& changes, double finalBPM,
             uint16_t ppq, double multiplier)
        : m_ppq(static_cast<double>(ppq)),
          m_emptyMsPerBeat(60000.0 / finalBPM) {
        const size_t n = changes.size();
        m_tick.reserve(n);
        m_msPerBeat.reserve(n);
        m_cumMs.reserve(n);

        double ms = 0.0;
        for (size_t i = 0; i < n; ++i) {
            m_tick.push_back(changes[i].tick);
            m_msPerBeat.push_back(60000.0 / (changes[i].bpm * multiplier));

            // Time at tempo change i = time at change i-1 + the segment between
            // them, played at the tempo that was active before change i.
            const uint32_t prevTick   = (i == 0) ? 0 : changes[i - 1].tick;
            const double   prevPerBeat = (i == 0) ? m_msPerBeat[0] : m_msPerBeat[i - 1];
            ms += ((changes[i].tick - prevTick) * prevPerBeat) / m_ppq;
            m_cumMs.push_back(ms);
        }
    }

    double toMs(uint32_t ticks) const {
        if (m_tick.empty())
            return (ticks * m_emptyMsPerBeat) / m_ppq;

        // k = first change at or after `ticks`
        const size_t k = static_cast<size_t>(
            std::lower_bound(m_tick.begin(), m_tick.end(), ticks) - m_tick.begin());

        if (k == 0)   // before (or exactly on) the first change
            return (ticks * m_msPerBeat[0]) / m_ppq;

        const size_t j = k - 1;   // last change strictly before `ticks`
        return m_cumMs[j] + ((ticks - m_tick[j]) * m_msPerBeat[j]) / m_ppq;
    }

private:
    double m_ppq;
    double m_emptyMsPerBeat;
    std::vector<uint32_t> m_tick;
    std::vector<double>   m_msPerBeat;   // 60000 / (bpm * multiplier)
    std::vector<double>   m_cumMs;       // song time at each change's tick
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

    // Stream the sections [first, last) as Psych-Engine JSON into `out`, using a small
    // reusable buffer (the whole file is never held in memory as one string).
    // `notes` is the chart's note array the sections point into. Returns bytes written.
    size_t writeJSON(std::ostream& out, const ChartNote* notes,
                     const Section* first, const Section* last, double finalBPM) const;

    // Divide sections into chunks capped at notesPerChunk total notes.
    // Returns [begin, end) section index ranges, so nothing gets copied.
    std::vector<std::pair<size_t, size_t>> splitSections(const std::vector<Section>& sections,
                                                         int notesPerChunk) const;

    // Notes that share a lane AND a MIDI tick get spread evenly across the
    // length of that one tick, so they aren't stacked at the exact same time.
    // Expects `notes` sorted by (tick, lane); leaves them in time order.
    void redistributeSimultaneousNotes(std::vector<ChartNote>& notes,
                                       const TempoMap& tempoMap) const;

    // Intelligent pitch-to-lane mapping to avoid pitch collisions
    int smartPitchToLane(uint8_t midiNote, int keyCount, uint8_t minPitch, uint8_t maxPitch) const;
};

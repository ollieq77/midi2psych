#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// ─── Data structures ──────────────────────────────────────────────────────────

struct MIDINote {
    uint32_t tick;
    uint8_t  note;
    uint8_t  velocity;
    uint32_t duration;

    MIDINote(uint32_t t, uint8_t n, uint8_t v, uint32_t d = 0)
        : tick(t), note(n), velocity(v), duration(d) {}
};

struct TempoChange {
    uint32_t tick;
    double   bpm;

    TempoChange(uint32_t t, double b) : tick(t), bpm(b) {}
};

// ─── Parser ───────────────────────────────────────────────────────────────────

class MIDIParser {
public:
    // Public results – read after a successful parse().
    std::vector<std::vector<MIDINote>> tracks;
    std::vector<TempoChange>           tempoChanges;
    uint16_t ppq  = 480;
    double   bpm  = 120.0;

    // Optional: called with progress in [0,1] as tracks are processed.
    std::function<void(double)> progressCallback;

    MIDIParser() = default;

    // Returns false if the file can't be opened or the header is invalid.
    bool parse(const std::string& filename, bool sustainNotes, int minVelocity);

private:
    std::vector<uint8_t> m_data;
    size_t               m_pos = 0;

    uint32_t readVarLen();
    uint16_t read16();
    uint32_t read32();
};

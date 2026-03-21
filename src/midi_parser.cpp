#include "midi_parser.h"

#include <fstream>
#include <map>

// ─── Private helpers ──────────────────────────────────────────────────────────

uint32_t MIDIParser::readVarLen() {
    uint32_t val = 0;
    uint8_t  byte;
    do {
        byte = m_data[m_pos++];
        val  = (val << 7) | (byte & 0x7f);
    } while (byte & 0x80);
    return val;
}

uint16_t MIDIParser::read16() {
    uint16_t val = static_cast<uint16_t>((m_data[m_pos] << 8) | m_data[m_pos + 1]);
    m_pos += 2;
    return val;
}

uint32_t MIDIParser::read32() {
    uint32_t val = (m_data[m_pos]     << 24) | (m_data[m_pos + 1] << 16) |
                   (m_data[m_pos + 2] <<  8) |  m_data[m_pos + 3];
    m_pos += 4;
    return val;
}

// ─── parse ────────────────────────────────────────────────────────────────────

bool MIDIParser::parse(const std::string& filename, bool sustainNotes, int minVelocity) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;

    file.seekg(0, std::ios::end);
    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    m_data.resize(fileSize);
    file.read(reinterpret_cast<char*>(m_data.data()), fileSize);
    file.close();

    m_pos = 0;

    // Header chunk
    if (read32() != 0x4D546864) return false;   // "MThd"
    if (read32() != 6)          return false;   // header length

    /* uint16_t format = */ read16();
    uint16_t numTracks = read16();
    ppq = read16();

    tracks.clear();
    tempoChanges.clear();

    for (uint16_t t = 0; t < numTracks && m_pos < m_data.size(); ++t) {
        if (progressCallback && numTracks > 0)
            progressCallback(static_cast<double>(t) / numTracks);

        if (read32() != 0x4D54726B) continue;   // "MTrk"

        uint32_t trackLen = read32();
        size_t   trackEnd = m_pos + trackLen;

        std::vector<MIDINote> events;
        events.reserve(10000);

        uint32_t time          = 0;
        uint8_t  runningStatus = 0;
        std::map<uint8_t, std::pair<uint32_t, uint8_t>> activeNotes;

        while (m_pos < trackEnd && m_pos < m_data.size()) {
            uint32_t delta = readVarLen();
            time += delta;

            uint8_t status = m_data[m_pos];
            if (status < 0x80) {
                status = runningStatus;
            } else {
                ++m_pos;
            }
            runningStatus = status;

            uint8_t type = status & 0xf0;

            if (type == 0x90 || type == 0x80) {
                uint8_t note = m_data[m_pos++];
                uint8_t vel  = m_data[m_pos++];

                if (type == 0x90 && vel > 0) {
                    if (sustainNotes) {
                        activeNotes[note] = {time, vel};
                    } else if (vel >= static_cast<uint8_t>(minVelocity)) {
                        events.emplace_back(time, note, vel, 0u);
                    }
                } else {   // note-off (0x80 or 0x90 vel=0)
                    if (sustainNotes) {
                        auto it = activeNotes.find(note);
                        if (it != activeNotes.end() &&
                            it->second.second >= static_cast<uint8_t>(minVelocity)) {
                            uint32_t dur = time - it->second.first;
                            events.emplace_back(it->second.first, note, it->second.second, dur);
                        }
                        activeNotes.erase(note);
                    }
                }
            } else if (type == 0xb0 || type == 0xe0 || type == 0xa0) {
                m_pos += 2;
            } else if (type == 0xc0 || type == 0xd0) {
                m_pos += 1;
            } else if (status == 0xff) {
                uint8_t  metaType = m_data[m_pos++];
                uint32_t len      = readVarLen();

                if (metaType == 0x51 && len == 3) {
                    uint32_t uspqn = (m_data[m_pos] << 16) |
                                     (m_data[m_pos + 1] << 8) |
                                      m_data[m_pos + 2];
                    double newBPM = 60000000.0 / uspqn;
                    tempoChanges.emplace_back(time, newBPM);
                    if (tempoChanges.size() == 1) bpm = newBPM;
                }
                m_pos += len;
            } else if (status == 0xf0 || status == 0xf7) {
                uint32_t len = readVarLen();
                m_pos += len;
            }
        }

        if (!events.empty())
            tracks.push_back(std::move(events));
    }

    return true;
}

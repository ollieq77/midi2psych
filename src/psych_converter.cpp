#include "psych_converter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <execution>
#include <fstream>
#include <future>
#include <iomanip>
#include <sstream>
#include <cstdint>
#include <unordered_map>
#include <queue>

#include "gui_logger.h"
#include "progress_bar.h"
#include "utils.h"

// ─── ticksToMs ───────────────────────────────────────────────────────────────

double PsychConverter::ticksToMs(uint32_t ticks, double finalBPM, uint16_t ppq,
                                  const std::vector<TempoChange>& tempoChanges,
                                  double multiplier) const {
    if (tempoChanges.empty())
        return (ticks * (60000.0 / finalBPM)) / ppq;

    double   ms          = 0.0;
    uint32_t lastTick    = 0;
    double   currentBPM  = tempoChanges[0].bpm * multiplier;

    for (size_t i = 0; i < tempoChanges.size(); ++i) {
        const auto& change = tempoChanges[i];

        if (ticks <= change.tick) {
            ms += ((ticks - lastTick) * (60000.0 / currentBPM)) / ppq;
            return ms;
        }

        if (i + 1 >= tempoChanges.size() || ticks < tempoChanges[i + 1].tick) {
            ms += ((change.tick - lastTick) * (60000.0 / currentBPM)) / ppq;
            currentBPM = change.bpm * multiplier;
            ms += ((ticks - change.tick) * (60000.0 / currentBPM)) / ppq;
            return ms;
        }

        ms += ((change.tick - lastTick) * (60000.0 / currentBPM)) / ppq;
        lastTick   = change.tick;
        currentBPM = change.bpm * multiplier;
    }

    ms += ((ticks - lastTick) * (60000.0 / currentBPM)) / ppq;
    return ms;
}

// ─── getBPMAtTime ─────────────────────────────────────────────────────────────

double PsychConverter::getBPMAtTime(double time,
                                     const std::vector<std::pair<double, double>>& timeToBPM,
                                     double baseBPM) const {
    if (timeToBPM.empty()) return baseBPM;
    for (int i = static_cast<int>(timeToBPM.size()) - 1; i >= 0; --i) {
        if (time >= timeToBPM[i].first)
            return timeToBPM[i].second;
    }
    return baseBPM;
}

// ─── buildJSON ────────────────────────────────────────────────────────────────

std::string PsychConverter::buildJSON(const std::vector<Section>& sections,
                                       double finalBPM) const {
    std::ostringstream json;
    json << std::fixed;
    if (m_config.highPrecision)
        json << std::setprecision(m_config.decimalPlaces);

    json << R"({"song":{"song":")" << m_config.songName << R"(","notes":[)";

    // One "step" is 1/4 beat (there are 16 steps per 4-beat section).
    // stepMs = (60000 / bpm) * 0.25 = 15000 / bpm
    double stepMs = (finalBPM > 0.0) ? (15000.0 / finalBPM) : 0.0;

    for (size_t s = 0; s < sections.size(); ++s) {
        if (s > 0) json << ",";
        json << R"({"sectionNotes":[)";

        const auto& notes = sections[s].notes;
        for (size_t i = 0; i < notes.size(); ++i) {
            if (i > 0) json << ",";

            double time = notes[i].time;
            double dur  = notes[i].duration;

            if (m_config.roundTimesTo >= 0) {
                double mult = std::pow(10.0, m_config.roundTimesTo);
                time = std::round(time * mult) / mult;
                dur  = std::round(dur  * mult) / mult;
            }

            // Format: [ time, lane, holdLength, custom ]
            // holdLength replaces the old placeholder (index 2). custom (index 3)
            // is currently unused (0) but reserved for future data.
            json << "[" << smartNumToStr(time, m_config.decimalPlaces)
                 << "," << notes[i].lane << ",";

            // Only emit a hold length if it is at least one step long
            bool isHold = (dur >= stepMs);
            if (m_config.minifyJSON && !isHold) {
                // Both holdLength and custom are zero
                json << "0,0]";
            } else {
                if (isHold)
                    json << smartNumToStr(dur, m_config.decimalPlaces) << ",0]";
                else
                    json << "0,0]";
            }
        }

        json << R"(],"lengthInSteps":16,"mustHitSection":)"
             << (sections[s].mustHitSection ? "true" : "false")
             << R"(,"changeBPM":)" << (sections[s].changeBPM ? "true" : "false")
             << R"(,"bpm":)" << smartNumToStr(sections[s].bpm, m_config.decimalPlaces)
             << "}";
    }

    json << R"(],"bpm":)"      << smartNumToStr(finalBPM,           m_config.decimalPlaces)
         << R"(,"needsVoices":true,"speed":)" << smartNumToStr(m_config.speed, m_config.decimalPlaces);

    json << R"(,"player1":")"  << m_config.p1Char
         << R"(","player2":")" << m_config.p2Char
         << R"(","gfVersion":")" << m_config.gfChar
         << R"(","stage":")"   << m_config.stage << '"';

    if (m_config.mania != 3)
        json << R"(,"mania":)" << m_config.mania;

    json << R"(,"validScore":true}})";

    return json.str();
}

// ─── splitSections ────────────────────────────────────────────────────────────

std::vector<std::vector<Section>>
PsychConverter::splitSections(const std::vector<Section>& sections, int notesPerChunk) const {
    std::vector<std::vector<Section>> chunks;
    std::vector<Section> current;
    int count = 0;

    for (const auto& sec : sections) {
        int n = static_cast<int>(sec.notes.size());
        if (count + n > notesPerChunk && !current.empty()) {
            chunks.push_back(std::move(current));
            current.clear();
            count = 0;
        }
        current.push_back(sec);
        count += n;
    }
    if (!current.empty()) chunks.push_back(std::move(current));
    return chunks;
}

// ─── fixMustHitSectionAcrossChunks ────────────────────────────────────────────

// ─── redistributeSimultaneousNotes ────────────────────────────────────────────

void PsychConverter::redistributeSimultaneousNotes(std::vector<ChartNote>& notes,
                                                    double sectionLengthMs) const {
    if (notes.size() < 2) return;

    const double EPSILON = 0.001;  // 1ms threshold for "simultaneous"

    size_t i = 0;
    while (i < notes.size()) {
        int    currentLane = notes[i].lane;
        double currentTime = notes[i].time;

        // Collect all notes with the same lane at approximately the same time
        size_t groupStart = i;
        size_t groupEnd   = i + 1;
        while (groupEnd < notes.size() &&
               notes[groupEnd].lane == currentLane &&
               std::abs(notes[groupEnd].time - currentTime) < EPSILON) {
            ++groupEnd;
        }

        // Only bother if there are actually multiple simultaneous notes
        if (groupEnd - groupStart > 1) {
            // Find the next note on the same lane
            size_t nextSameIdx = groupEnd;
            while (nextSameIdx < notes.size() && notes[nextSameIdx].lane != currentLane)
                ++nextSameIdx;

            bool shouldRedistribute = false;
            if (nextSameIdx < notes.size()) {
                double nextTime = notes[nextSameIdx].time;
                double gap      = nextTime - currentTime;

                // Work out which section currentTime sits in, and whether
                // nextTime is in the same section.  A section starts at
                // floor(currentTime / sectionLengthMs) * sectionLengthMs.
                double currentSection = std::floor(currentTime / sectionLengthMs);
                double nextSection    = std::floor(nextTime    / sectionLengthMs);

                // Only redistribute if the target note is in the SAME section
                shouldRedistribute = (currentSection == nextSection);

                if (shouldRedistribute) {
                    size_t groupSize = groupEnd - groupStart;
                    double timeStep  = gap / static_cast<double>(groupSize + 1);

                    for (size_t j = groupStart; j < groupEnd; ++j)
                        notes[j].time = currentTime + timeStep * static_cast<double>(j - groupStart + 1);
                }
                // If cross-section: leave all notes at currentTime as-is
            }
            // If no next same-lane note exists at all: also leave as-is
        }

        i = groupEnd;
    }
}

// ─── smartPitchToLane ─────────────────────────────────────────────────────────

int PsychConverter::smartPitchToLane(uint8_t midiNote, int keyCount, uint8_t minPitch, uint8_t maxPitch) const {
    // Cycle through lanes based on pitch position
    // Spreads consecutive pitches across lanes for natural playability
    // Example with 4 keys: C→lane0, C#→lane1, D→lane2, D#→lane3, E→lane0, etc.
    int notePosition = midiNote - minPitch;
    return notePosition % keyCount;
}

// ─── convert ─────────────────────────────────────────────────────────────────

bool PsychConverter::convert(const std::string& p1File,
                              const std::string& p2File,
                              const std::string& outFile) {
    auto startTime = std::chrono::high_resolution_clock::now();

    guiLogger.logColored("\n================================================\n", CYAN);
    guiLogger.logColored("    MIDI -> Psych Engine Converter V1.0\n",            CYAN);
    guiLogger.logColored("================================================\n\n",  CYAN);

    // ── Parallel MIDI parsing ──────────────────────────────────────────────
    ProgressBar parseBar("Parsing MIDI", 40);
    parseBar.setHandle(m_progressHandle);

    MIDIParser p1Parser, p2Parser;
    std::atomic<int> p1Pct{0}, p2Pct{0};

    p1Parser.progressCallback = [&](double p) {
        p1Pct.store(static_cast<int>(p * 100), std::memory_order_relaxed);
        parseBar.update((p1Pct.load() + p2Pct.load()) * 0.005,
            "P1: " + std::to_string(p1Pct.load()) + "% | P2: " + std::to_string(p2Pct.load()) + "%");
    };
    p2Parser.progressCallback = [&](double p) {
        p2Pct.store(static_cast<int>(p * 100), std::memory_order_relaxed);
        parseBar.update((p1Pct.load() + p2Pct.load()) * 0.005,
            "P1: " + std::to_string(p1Pct.load()) + "% | P2: " + std::to_string(p2Pct.load()) + "%");
    };

    guiLogger.logColored("Launching parallel MIDI parse threads...\n", YELLOW);

    auto p1Future = std::async(std::launch::async, [&]() {
        return p1Parser.parse(p1File, m_config.sustainNotes, m_config.minVelocity);
    });
    auto p2Future = std::async(std::launch::async, [&]() {
        return p2Parser.parse(p2File, m_config.sustainNotes, m_config.minVelocity);
    });

    bool p1Ok = p1Future.get();
    bool p2Ok = p2Future.get();

    if (!p1Ok) { guiLogger.logColored("\n[X] Failed to parse P1 MIDI file!\n", RED); return false; }
    if (!p2Ok) { guiLogger.logColored("\n[X] Failed to parse P2 MIDI file!\n", RED); return false; }

    parseBar.finish("Both MIDIs parsed in parallel!");

    // ── Note processing ───────────────────────────────────────────────────
    ProgressBar convertBar("Converting", 40);
    convertBar.setHandle(m_progressHandle);
    convertBar.update(0.0, "Processing notes...");

    uint16_t ppq      = p1Parser.ppq;
    double   baseBPM  = p1Parser.bpm;
    double   finalBPM = baseBPM * m_config.bpmMultiplier;

    auto& tempoChanges = !p1Parser.tempoChanges.empty()
                         ? p1Parser.tempoChanges : p2Parser.tempoChanges;

    std::vector<std::pair<double, double>> timeToBPM;
    for (const auto& tc : tempoChanges) {
        double t = ticksToMs(tc.tick, baseBPM, ppq, tempoChanges, m_config.bpmMultiplier);
        timeToBPM.push_back({t, tc.bpm * m_config.bpmMultiplier});
    }

    std::vector<ChartNote> allNotes;
    allNotes.reserve(50000);
    uint32_t maxTick = 0;

    int keyCount = m_config.mania + 1;

    // Track pitch ranges for mapping and compute pitch frequencies
    uint8_t minPitch = 127, maxPitch = 0;
    std::unordered_map<uint8_t, int> freq;
    for (const auto& track : p1Parser.tracks) {
        for (const auto& evt : track) {
            minPitch = std::min(minPitch, evt.note);
            maxPitch = std::max(maxPitch, evt.note);
            ++freq[evt.note];
        }
    }
    for (const auto& track : p2Parser.tracks) {
        for (const auto& evt : track) {
            minPitch = std::min(minPitch, evt.note);
            maxPitch = std::max(maxPitch, evt.note);
            ++freq[evt.note];
        }
    }

    // Build pitch -> lane mapping using histogram balancing (greedy)
    std::unordered_map<uint8_t, int> pitchToLane;
    if (m_config.smartPitchMapping && !freq.empty()) {
        // Collect unique pitches into a vector and sort by descending frequency
        std::vector<std::pair<uint8_t,int>> items;
        items.reserve(freq.size());
        for (const auto& kv : freq) items.emplace_back(kv.first, kv.second);
        std::sort(items.begin(), items.end(), [](const auto& a, const auto& b){
            return a.second > b.second || (a.second == b.second && a.first < b.first);
        });

        // laneLoads holds current assigned note counts per lane
        std::vector<int> laneLoads(keyCount, 0);

        for (const auto& p : items) {
            // find lane with minimum load (first smallest index on ties)
            int bestLane = 0;
            int bestLoad = laneLoads[0];
            for (int l = 1; l < keyCount; ++l) {
                if (laneLoads[l] < bestLoad) { bestLoad = laneLoads[l]; bestLane = l; }
            }
            pitchToLane[p.first] = bestLane;
            laneLoads[bestLane] += p.second;
        }
    }

    // P1 tracks → lanes 0 to (keyCount-1)
    size_t totalP1 = p1Parser.tracks.size(), doneP1 = 0;
    for (const auto& track : p1Parser.tracks) {
        for (const auto& evt : track) {
            double ms = ticksToMs(evt.tick, finalBPM, ppq, tempoChanges, m_config.bpmMultiplier)
                        + m_config.noteOffset;
            double dur = 0.0;
            if (m_config.sustainNotes && evt.duration > 0) {
                double s = ticksToMs(evt.tick,               finalBPM, ppq, tempoChanges, m_config.bpmMultiplier);
                double e = ticksToMs(evt.tick + evt.duration, finalBPM, ppq, tempoChanges, m_config.bpmMultiplier);
                dur = e - s;
            }
            int lane = 0;
            if (m_config.smartPitchMapping && !pitchToLane.empty()) {
                auto it = pitchToLane.find(evt.note);
                lane = (it != pitchToLane.end()) ? it->second : (evt.note % keyCount);
            } else {
                lane = evt.note % keyCount;
            }
            allNotes.emplace_back(ms, lane, dur);
            maxTick = std::max(maxTick, evt.tick);
        }
        ++doneP1;
        if (totalP1 > 0)
            convertBar.update(static_cast<double>(doneP1) / totalP1 * 0.25,
                "P1 tracks " + std::to_string(doneP1) + "/" + std::to_string(totalP1));
    }

    convertBar.update(0.25, "Processing P2...");
    size_t p1Count = allNotes.size();

    // P2 tracks → lanes (keyCount) to (2*keyCount-1)
    size_t totalP2 = p2Parser.tracks.size(), doneP2 = 0;
    for (const auto& track : p2Parser.tracks) {
        for (const auto& evt : track) {
            double ms = ticksToMs(evt.tick, finalBPM, ppq, tempoChanges, m_config.bpmMultiplier)
                        + m_config.noteOffset;
            double dur = 0.0;
            if (m_config.sustainNotes && evt.duration > 0) {
                double s = ticksToMs(evt.tick,               finalBPM, ppq, tempoChanges, m_config.bpmMultiplier);
                double e = ticksToMs(evt.tick + evt.duration, finalBPM, ppq, tempoChanges, m_config.bpmMultiplier);
                dur = e - s;
            }
            int lane = 0;
            if (m_config.smartPitchMapping && !pitchToLane.empty()) {
                auto it = pitchToLane.find(evt.note);
                lane = ((it != pitchToLane.end()) ? it->second : (evt.note % keyCount)) + 100;
            } else {
                lane = (evt.note % keyCount) + 100;
            }
            allNotes.emplace_back(ms, lane, dur);
            maxTick = std::max(maxTick, evt.tick);
        }
        ++doneP2;
        if (totalP2 > 0)
            convertBar.update(0.25 + static_cast<double>(doneP2) / totalP2 * 0.25,
                "P2 tracks " + std::to_string(doneP2) + "/" + std::to_string(totalP2));
    }

    convertBar.update(0.50, "Sorting notes...");

    if (allNotes.size() > 10000) {
        guiLogger.logColored("Using parallel sort for large dataset...\n", YELLOW);
        std::sort(std::execution::par_unseq, allNotes.begin(), allNotes.end(),
            [](const ChartNote& a, const ChartNote& b) {
                return a.time < b.time || (a.time == b.time && a.lane < b.lane);
            });
    } else {
        std::sort(allNotes.begin(), allNotes.end(),
            [](const ChartNote& a, const ChartNote& b) {
                return a.time < b.time || (a.time == b.time && a.lane < b.lane);
            });
    }

    convertBar.update(0.60, "Redistributing simultaneous notes...");

    // Section length in ms at the current BPM (4 beats per section).
    // Used by redistributeSimultaneousNotes to detect cross-section gaps.
    double sectionLengthMs = (60000.0 / finalBPM) * 4.0;
    redistributeSimultaneousNotes(allNotes, sectionLengthMs);

    convertBar.update(0.75, "Building sections...");

    // ── Section building (two-pointer O(n)) ──────────────────────────────
    std::vector<Section> sections;
    double maxTime     = ticksToMs(maxTick, finalBPM, ppq, tempoChanges, m_config.bpmMultiplier);
    double currentTime = 0.0;
    double currentBPM  = finalBPM;

    int    totalSectionEst = static_cast<int>((maxTime / ((60000.0 / finalBPM) * 4)) + 1);
    int    sectionCount    = 0;
    size_t noteIdx         = 0;
    bool   lastMustHit     = true;

    while (currentTime < maxTime + (60000.0 / currentBPM) * 4) {
        currentBPM = getBPMAtTime(currentTime, timeToBPM, finalBPM);
        double sectionLen = (60000.0 / currentBPM) * 4;
        double sectionEnd = currentTime + sectionLen;

        Section section;
        section.bpm       = currentBPM;
        section.changeBPM = false;

        double lastBPMInSection = currentBPM;
        bool   foundChange      = false;
        for (const auto& [t, b] : timeToBPM) {
            if (t > currentTime && t < sectionEnd) { foundChange = true; lastBPMInSection = b; }
        }
        if (foundChange) { section.changeBPM = true; section.bpm = lastBPMInSection; }

        while (noteIdx < allNotes.size() && allNotes[noteIdx].time < currentTime)
            ++noteIdx;

        size_t sectionStart = noteIdx;
        size_t sectionEnd2  = noteIdx;
        while (sectionEnd2 < allNotes.size() && allNotes[sectionEnd2].time < sectionEnd)
            ++sectionEnd2;

        int p1Count2 = 0, p2Count2 = 0;
        for (size_t i = sectionStart; i < sectionEnd2; ++i)
            (allNotes[i].lane < 100 ? p1Count2 : p2Count2)++;

        if (p1Count2 == 0 && p2Count2 == 0)
            section.mustHitSection = lastMustHit;
        else
            section.mustHitSection = (p1Count2 >= p2Count2);
        lastMustHit = section.mustHitSection;

        for (size_t i = sectionStart; i < sectionEnd2; ++i) {
            const auto& note = allNotes[i];
            bool isP1        = (note.lane < 100);
            int  baseLane    = isP1 ? note.lane : (note.lane - 100);
            int  finalLane   = section.mustHitSection
                               ? (isP1 ? baseLane : baseLane + keyCount)
                               : (isP1 ? baseLane + keyCount : baseLane);
            section.notes.emplace_back(note.time, finalLane, note.duration);
        }

        noteIdx = sectionEnd2;
        sections.push_back(std::move(section));
        currentTime += sectionLen;

        if (++sectionCount % 10 == 0) {
            double prog = std::min(0.99, currentTime / maxTime);
            convertBar.update(0.75 + prog * 0.24,
                "Section " + std::to_string(sectionCount) + "/" + std::to_string(totalSectionEst));
        }
    }

    while (!sections.empty() && sections.back().notes.empty())
        sections.pop_back();

    convertBar.finish("Sections built!");

    // ── File output ───────────────────────────────────────────────────────
    std::vector<std::string> outputFiles;
    size_t totalFileSize = 0;

    if (m_config.splitOutput && m_config.notesPerSplit > 0) {
        guiLogger.logColored("Splitting chart into multiple files...\n", CYAN);

        auto chunks = splitSections(sections, m_config.notesPerSplit);

        size_t      dotPos    = outFile.find_last_of('.');
        std::string baseName  = (dotPos != std::string::npos) ? outFile.substr(0, dotPos) : outFile;
        std::string extension = (dotPos != std::string::npos) ? outFile.substr(dotPos)    : ".json";

        for (size_t i = 0; i < chunks.size(); ++i) {
            std::string fname = baseName + "-" + std::to_string(i + 1) + extension;
            std::string json  = buildJSON(chunks[i], finalBPM);

            std::ofstream out(fname);
            if (!out) {
                guiLogger.logColored("\n[X] Failed to write file: " + fname + "\n", RED);
                return false;
            }
            out << json;
            outputFiles.push_back(fname);
            totalFileSize += json.size();

            guiLogger.log("  Created: " + fname + " (" +
                          std::to_string(json.size() / 1024.0) + " KB)\n");
        }
    } else {
        guiLogger.logColored("Generating single JSON file...\n", CYAN);
        std::string json = buildJSON(sections, finalBPM);

        std::ofstream out(outFile);
        if (!out) {
            guiLogger.logColored("\n[X] Failed to write output file!\n", RED);
            return false;
        }
        out << json;
        outputFiles.push_back(outFile);
        totalFileSize = json.size();
    }

    // ── Stats ─────────────────────────────────────────────────────────────
    auto endTime = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    guiLogger.logColored("\n=== CONVERSION SUCCESSFUL ===\n\n", GREEN);
    guiLogger.log("Chart Statistics:\n");
    guiLogger.log("  Total Notes:   " + std::to_string(allNotes.size()) + "\n");
    guiLogger.log("  P1 Notes:      " + std::to_string(p1Count) + "\n");
    guiLogger.log("  P2 Notes:      " + std::to_string(allNotes.size() - p1Count) + "\n");
    guiLogger.log("  Sections:      " + std::to_string(sections.size()) + "\n\n");

    guiLogger.log("MIDI Info:\n");
    guiLogger.log("  PPQ:           " + std::to_string(ppq) + "\n");

    std::ostringstream bss;
    bss << std::fixed << std::setprecision(2) << baseBPM;
    guiLogger.log("  Base BPM:      " + bss.str() + "\n");
    bss.str(""); bss << finalBPM;
    guiLogger.log("  Final BPM:     " + bss.str() + "\n\n");

    if (tempoChanges.size() > 1) {
        guiLogger.log("BPM Changes (" + std::to_string(tempoChanges.size() - 1) + "):\n");
        for (size_t i = 1; i < std::min(size_t(6), tempoChanges.size()); ++i) {
            double t = ticksToMs(tempoChanges[i].tick, baseBPM, ppq, tempoChanges, m_config.bpmMultiplier);
            std::ostringstream cs;
            cs << std::fixed << std::setprecision(2);
            cs << "  @ " << std::setw(7) << (t / 1000.0)
               << "s -> " << std::setw(6) << (tempoChanges[i].bpm * m_config.bpmMultiplier) << " BPM\n";
            guiLogger.log(cs.str());
        }
        if (tempoChanges.size() > 6)
            guiLogger.log("  ... and " + std::to_string(tempoChanges.size() - 6) + " more\n");
        guiLogger.log("\n");
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "Output:\n";
    if (outputFiles.size() > 1)
        oss << "  Files Created: " << outputFiles.size() << "\n";
    oss << "  Total Size:    " << (totalFileSize / 1024.0) << " KB\n";
    oss << "  Process Time:  " << elapsed.count() << " ms\n";
    if (outputFiles.size() == 1)
        oss << "  Location:      " << outputFiles[0] << "\n\n";
    else
        oss << "  Base Name:     " << outFile << "\n\n";
    guiLogger.log(oss.str());

    return true;
}
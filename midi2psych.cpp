#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <map>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>

// Platform-specific
#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#define ENABLE_COLORS() { HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE); DWORD mode; GetConsoleMode(h, &mode); SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING); }
#else
#define ENABLE_COLORS()
#endif

// ANSI color codes
#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"
#define BOLD    "\033[1m"
#define DIM     "\033[2m"

// GUI Control IDs
#define ID_BTN_P1_BROWSE 1001
#define ID_BTN_P2_BROWSE 1002
#define ID_BTN_OUT_BROWSE 1003
#define ID_BTN_CONVERT 1004
#define ID_BTN_CLEAR_LOG 1005
#define ID_EDIT_P1 1010
#define ID_EDIT_P2 1011
#define ID_EDIT_OUT 1012
#define ID_EDIT_SONG 1013
#define ID_EDIT_BPM 1014
#define ID_EDIT_OFFSET 1015
#define ID_EDIT_VELOCITY 1016
#define ID_EDIT_PRECISION 1017
#define ID_EDIT_SPEED 1018
#define ID_EDIT_P1CHAR 1019
#define ID_EDIT_P2CHAR 1020
#define ID_EDIT_GFCHAR 1021
#define ID_EDIT_STAGE 1022
#define ID_CHECK_SUSTAIN 1030
#define ID_CHECK_PRECISION 1031
#define ID_PROGRESS 1040
#define ID_CONSOLE 1050

// GUI Logger class
class GUILogger {
private:
    HWND consoleHandle;
    std::mutex logMutex;
    
public:
    GUILogger() : consoleHandle(nullptr) {}
    
    void setConsole(HWND hwnd) {
        consoleHandle = hwnd;
    }
    
    void log(const std::string& text, COLORREF color = RGB(220, 220, 220)) {
        if (!consoleHandle) {
            std::cout << text;
            return;
        }
        
        std::lock_guard<std::mutex> lock(logMutex);
        
        // Get current text length
        int len = GetWindowTextLength(consoleHandle);
        
        // Select end
        SendMessage(consoleHandle, EM_SETSEL, len, len);
        
        // Set color
        CHARFORMAT2 cf = {0};
        cf.cbSize = sizeof(CHARFORMAT2);
        cf.dwMask = CFM_COLOR;
        cf.crTextColor = color;
        SendMessage(consoleHandle, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        
        // Append text
        SendMessage(consoleHandle, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
        
        // Scroll to bottom
        SendMessage(consoleHandle, WM_VSCROLL, SB_BOTTOM, 0);
    }
    
    void logColored(const std::string& text, const std::string& colorCode) {
        COLORREF color = RGB(220, 220, 220);
        
        if (colorCode == RED) color = RGB(255, 80, 80);
        else if (colorCode == GREEN) color = RGB(80, 255, 120);
        else if (colorCode == YELLOW) color = RGB(255, 220, 80);
        else if (colorCode == BLUE) color = RGB(100, 150, 255);
        else if (colorCode == CYAN) color = RGB(80, 220, 255);
        else if (colorCode == MAGENTA) color = RGB(255, 100, 255);
        
        log(text, color);
    }
};

GUILogger guiLogger;

// Fast number to string conversion
template<typename T>
inline std::string fastNumToStr(T num, int decimals = -1) {
    if (decimals < 0) {
        return std::to_string(static_cast<int64_t>(num));
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(decimals) << num;
    return ss.str();
}

struct MIDINote {
    uint32_t tick;
    uint8_t note;
    uint8_t velocity;
    uint32_t duration;
    
    MIDINote(uint32_t t, uint8_t n, uint8_t v, uint32_t d = 0) 
        : tick(t), note(n), velocity(v), duration(d) {}
};

struct TempoChange {
    uint32_t tick;
    double bpm;
    
    TempoChange(uint32_t t, double b) : tick(t), bpm(b) {}
};

struct ChartNote {
    double time;
    int lane;
    double duration;
    
    ChartNote(double t, int l, double d = 0.0) : time(t), lane(l), duration(d) {}
};

struct Section {
    std::vector<ChartNote> notes;
    bool mustHitSection;
    bool changeBPM;
    double bpm;
};

class ProgressBar {
private:
    int width;
    std::string label;
    HWND progressHandle;
    
public:
    ProgressBar(const std::string& lbl, int w = 40) : label(lbl), width(w), progressHandle(nullptr) {}
    
    void setHandle(HWND hwnd) {
        progressHandle = hwnd;
    }
    
    void update(double progress, const std::string& status = "") {
        if (progressHandle) {
            SendMessage(progressHandle, PBM_SETPOS, (int)(progress * 100), 0);
        }
        
        // Also log to console
        std::string progressStr = label + " [" + std::to_string((int)(progress * 100)) + "%] " + status + "\n";
        guiLogger.logColored(progressStr, CYAN);
    }
    
    void finish(const std::string& msg = "Done!") {
        update(1.0, msg);
    }
};

class MIDIParser {
private:
    std::vector<uint8_t> data;
    size_t pos;
    
    uint32_t readVarLen() {
        uint32_t val = 0;
        uint8_t byte;
        do {
            byte = data[pos++];
            val = (val << 7) | (byte & 0x7f);
        } while (byte & 0x80);
        return val;
    }
    
    uint16_t read16() {
        uint16_t val = (data[pos] << 8) | data[pos + 1];
        pos += 2;
        return val;
    }
    
    uint32_t read32() {
        uint32_t val = (data[pos] << 24) | (data[pos + 1] << 16) | 
                       (data[pos + 2] << 8) | data[pos + 3];
        pos += 4;
        return val;
    }
    
public:
    std::vector<std::vector<MIDINote>> tracks;
    std::vector<TempoChange> tempoChanges;
    uint16_t ppq;
    double bpm;
    std::function<void(double)> progressCallback;
    
    MIDIParser() : pos(0), ppq(480), bpm(120.0) {}
    
    bool parse(const std::string& filename, bool sustainNotes, int minVelocity) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) return false;
        
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        
        data.resize(fileSize);
        file.read(reinterpret_cast<char*>(data.data()), fileSize);
        file.close();
        
        pos = 0;
        
        if (read32() != 0x4D546864) return false;
        if (read32() != 6) return false;
        
        uint16_t format = read16();
        uint16_t numTracks = read16();
        ppq = read16();
        
        tracks.clear();
        tempoChanges.clear();
        
        for (uint16_t t = 0; t < numTracks && pos < data.size(); ++t) {
            if (progressCallback && numTracks > 0) {
                progressCallback(static_cast<double>(t) / numTracks);
            }
            
            if (read32() != 0x4D54726B) continue;
            
            uint32_t trackLen = read32();
            size_t trackEnd = pos + trackLen;
            
            std::vector<MIDINote> events;
            events.reserve(10000);
            
            uint32_t time = 0;
            uint8_t runningStatus = 0;
            std::map<uint8_t, std::pair<uint32_t, uint8_t>> activeNotes;
            
            while (pos < trackEnd && pos < data.size()) {
                uint32_t delta = readVarLen();
                time += delta;
                
                uint8_t status = data[pos];
                if (status < 0x80) {
                    status = runningStatus;
                } else {
                    pos++;
                }
                
                runningStatus = status;
                uint8_t type = status & 0xf0;
                
                if (type == 0x90 || type == 0x80) {
                    uint8_t note = data[pos++];
                    uint8_t vel = data[pos++];
                    
                    if (type == 0x90 && vel > 0) {
                        if (sustainNotes) {
                            activeNotes[note] = {time, vel};
                        } else {
                            if (vel >= minVelocity) {
                                events.emplace_back(time, note, vel, 0);
                            }
                        }
                    } else if (type == 0x80 || (type == 0x90 && vel == 0)) {
                        if (sustainNotes) {
                            auto it = activeNotes.find(note);
                            if (it != activeNotes.end() && it->second.second >= minVelocity) {
                                uint32_t duration = time - it->second.first;
                                events.emplace_back(it->second.first, note, it->second.second, duration);
                            }
                            activeNotes.erase(note);
                        }
                    }
                } else if (type == 0xb0 || type == 0xe0 || type == 0xa0) {
                    pos += 2;
                } else if (type == 0xc0 || type == 0xd0) {
                    pos += 1;
                } else if (status == 0xff) {
                    uint8_t metaType = data[pos++];
                    uint32_t len = readVarLen();
                    
                    if (metaType == 0x51 && len == 3) {
                        uint32_t uspqn = (data[pos] << 16) | (data[pos + 1] << 8) | data[pos + 2];
                        double newBPM = 60000000.0 / uspqn;
                        tempoChanges.emplace_back(time, newBPM);
                        if (tempoChanges.size() == 1) bpm = newBPM;
                    }
                    pos += len;
                } else if (status == 0xf0 || status == 0xf7) {
                    uint32_t len = readVarLen();
                    pos += len;
                }
            }
            
            if (!events.empty()) {
                tracks.push_back(std::move(events));
            }
        }
        
        return true;
    }
};

class PsychConverter {
private:
    struct Config {
        std::string songName = "Converted";
        std::string p1Char = "bf";
        std::string p2Char = "dad";
        std::string gfChar = "gf";
        std::string stage = "stage";
        double speed = 2.5;
        double bpmMultiplier = 1.0;
        double noteOffset = 0.0;
        int minVelocity = 0;
        int decimalPlaces = 6;
        bool highPrecision = true;
        bool sustainNotes = false;
    };
    
    Config config;
    HWND progressHandle;
    
    double ticksToMs(uint32_t ticks, double finalBPM, uint16_t ppq, 
                     const std::vector<TempoChange>& tempoChanges, double multiplier) {
        if (tempoChanges.empty()) {
            return (ticks * (60000.0 / finalBPM)) / ppq;
        }
        
        double ms = 0.0;
        uint32_t lastTick = 0;
        double currentBPM = tempoChanges[0].bpm * multiplier;
        
        for (size_t i = 0; i < tempoChanges.size(); ++i) {
            const auto& change = tempoChanges[i];
            
            if (ticks <= change.tick) {
                uint32_t tickDiff = ticks - lastTick;
                ms += (tickDiff * (60000.0 / currentBPM)) / ppq;
                return ms;
            }
            
            if (i + 1 >= tempoChanges.size() || ticks < tempoChanges[i + 1].tick) {
                uint32_t tickDiff = change.tick - lastTick;
                ms += (tickDiff * (60000.0 / currentBPM)) / ppq;
                
                uint32_t remainingTicks = ticks - change.tick;
                currentBPM = change.bpm * multiplier;
                ms += (remainingTicks * (60000.0 / currentBPM)) / ppq;
                return ms;
            }
            
            uint32_t tickDiff = change.tick - lastTick;
            ms += (tickDiff * (60000.0 / currentBPM)) / ppq;
            lastTick = change.tick;
            currentBPM = change.bpm * multiplier;
        }
        
        uint32_t tickDiff = ticks - lastTick;
        ms += (tickDiff * (60000.0 / currentBPM)) / ppq;
        return ms;
    }
    
    double getBPMAtTime(double time, const std::vector<std::pair<double, double>>& timeToBPM, double baseBPM) {
        if (timeToBPM.empty()) return baseBPM;
        
        for (int i = timeToBPM.size() - 1; i >= 0; --i) {
            if (time >= timeToBPM[i].first) {
                return timeToBPM[i].second;
            }
        }
        return baseBPM;
    }
    
    std::string buildJSON(const std::vector<Section>& sections, double finalBPM) {
        std::ostringstream json;
        json << std::fixed;
        
        if (config.highPrecision) {
            json << std::setprecision(config.decimalPlaces);
        }
        
        json << R"({"song":{"song":")" << config.songName << R"(","notes":[)";
        
        for (size_t s = 0; s < sections.size(); ++s) {
            if (s > 0) json << ",";
            json << R"({"sectionNotes":[)";
            
            const auto& notes = sections[s].notes;
            for (size_t i = 0; i < notes.size(); ++i) {
                if (i > 0) json << ",";
                
                double t = config.highPrecision ? notes[i].time : std::round(notes[i].time);
                double d = config.highPrecision ? notes[i].duration : std::round(notes[i].duration);
                
                json << "[" << t << "," << notes[i].lane << ",0," << d << "]";
            }
            
            json << R"(],"lengthInSteps":16,"mustHitSection":)" 
                 << (sections[s].mustHitSection ? "true" : "false")
                 << R"(,"changeBPM":)" << (sections[s].changeBPM ? "true" : "false")
                 << R"(,"bpm":)" << sections[s].bpm << "}";
        }
        
        json << R"(],"bpm":)" << finalBPM 
             << R"(,"needsVoices":true,"speed":)" << config.speed
             << R"(,"player1":")" << config.p1Char
             << R"(","player2":")" << config.p2Char
             << R"(","gfVersion":")" << config.gfChar
             << R"(","stage":")" << config.stage
             << R"(","validScore":true}})";
        
        return json.str();
    }
    
public:
    void setConfig(const Config& cfg) { config = cfg; }
    Config& getConfig() { return config; }
    void setProgressHandle(HWND hwnd) { progressHandle = hwnd; }
    
    bool convert(const std::string& p1File, const std::string& p2File, 
                 const std::string& outFile) {
        auto startTime = std::chrono::high_resolution_clock::now();
        
        guiLogger.logColored("\n================================================\n", CYAN);
        guiLogger.logColored("    MIDI -> Psych Engine Converter v2.1\n", CYAN);
        guiLogger.logColored("================================================\n\n", CYAN);
        
        ProgressBar parseBar("Parsing MIDI", 40);
        parseBar.setHandle(progressHandle);
        parseBar.update(0.0, "Reading P1...");
        
        MIDIParser p1Parser, p2Parser;
        
        p1Parser.progressCallback = [&parseBar](double progress) {
            parseBar.update(progress * 0.5, "P1 track " + std::to_string(static_cast<int>(progress * 100)) + "%");
        };
        
        if (!p1Parser.parse(p1File, config.sustainNotes, config.minVelocity)) {
            guiLogger.logColored("\n[X] Failed to parse P1 MIDI file!\n", RED);
            return false;
        }
        
        parseBar.update(0.5, "Reading P2...");
        
        p2Parser.progressCallback = [&parseBar](double progress) {
            parseBar.update(0.5 + (progress * 0.5), "P2 track " + std::to_string(static_cast<int>(progress * 100)) + "%");
        };
        
        if (!p2Parser.parse(p2File, config.sustainNotes, config.minVelocity)) {
            guiLogger.logColored("\n[X] Failed to parse P2 MIDI file!\n", RED);
            return false;
        }
        
        parseBar.finish("MIDI parsed!");
        
        ProgressBar convertBar("Converting", 40);
        convertBar.setHandle(progressHandle);
        convertBar.update(0.0, "Processing notes...");
        
        uint16_t ppq = p1Parser.ppq;
        double baseBPM = p1Parser.bpm;
        double finalBPM = baseBPM * config.bpmMultiplier;
        
        auto& tempoChanges = !p1Parser.tempoChanges.empty() ? 
                            p1Parser.tempoChanges : p2Parser.tempoChanges;
        
        std::vector<std::pair<double, double>> timeToBPM;
        for (const auto& tc : tempoChanges) {
            double time = ticksToMs(tc.tick, baseBPM, ppq, tempoChanges, config.bpmMultiplier);
            timeToBPM.push_back({time, tc.bpm * config.bpmMultiplier});
        }
        
        std::vector<ChartNote> allNotes;
        allNotes.reserve(50000);
        
        uint32_t maxTick = 0;
        
        size_t totalP1Tracks = p1Parser.tracks.size();
        size_t processedP1 = 0;
        
        for (const auto& track : p1Parser.tracks) {
            for (const auto& evt : track) {
                double ms = ticksToMs(evt.tick, finalBPM, ppq, tempoChanges, config.bpmMultiplier);
                ms += config.noteOffset;
                
                int lane = evt.note % 4;
                double duration = 0.0;
                
                if (config.sustainNotes && evt.duration > 0) {
                    double startMs = ticksToMs(evt.tick, finalBPM, ppq, tempoChanges, config.bpmMultiplier);
                    double endMs = ticksToMs(evt.tick + evt.duration, finalBPM, ppq, tempoChanges, config.bpmMultiplier);
                    duration = endMs - startMs;
                }
                
                allNotes.emplace_back(ms, lane, duration);
                maxTick = std::max(maxTick, evt.tick);
            }
            processedP1++;
            if (totalP1Tracks > 0) {
                double progress = static_cast<double>(processedP1) / totalP1Tracks * 0.25;
                convertBar.update(progress, "P1 tracks " + std::to_string(processedP1) + "/" + std::to_string(totalP1Tracks));
            }
        }
        
        convertBar.update(0.25, "Processing P2...");
        
        size_t p1Count = allNotes.size();
        
        size_t totalP2Tracks = p2Parser.tracks.size();
        size_t processedP2 = 0;
        
        for (const auto& track : p2Parser.tracks) {
            for (const auto& evt : track) {
                double ms = ticksToMs(evt.tick, finalBPM, ppq, tempoChanges, config.bpmMultiplier);
                ms += config.noteOffset;
                
                int lane = (evt.note % 4) + 10;
                double duration = 0.0;
                
                if (config.sustainNotes && evt.duration > 0) {
                    double startMs = ticksToMs(evt.tick, finalBPM, ppq, tempoChanges, config.bpmMultiplier);
                    double endMs = ticksToMs(evt.tick + evt.duration, finalBPM, ppq, tempoChanges, config.bpmMultiplier);
                    duration = endMs - startMs;
                }
                
                allNotes.emplace_back(ms, lane, duration);
                maxTick = std::max(maxTick, evt.tick);
            }
            processedP2++;
            if (totalP2Tracks > 0) {
                double progress = 0.25 + (static_cast<double>(processedP2) / totalP2Tracks * 0.25);
                convertBar.update(progress, "P2 tracks " + std::to_string(processedP2) + "/" + std::to_string(totalP2Tracks));
            }
        }
        
        convertBar.update(0.50, "Sorting notes...");
        std::sort(allNotes.begin(), allNotes.end(), 
                  [](const ChartNote& a, const ChartNote& b) {
                      return a.time < b.time || (a.time == b.time && a.lane < b.lane);
                  });
        
        convertBar.update(0.75, "Building sections...");
        
        std::vector<Section> sections;
        double currentTime = 0.0;
        double maxTime = ticksToMs(maxTick, finalBPM, ppq, tempoChanges, config.bpmMultiplier);
        double currentBPM = finalBPM;
        
        int totalSectionEstimate = static_cast<int>((maxTime / ((60000.0 / finalBPM) * 4)) + 1);
        int sectionCount = 0;
        
        while (currentTime < maxTime + (60000.0 / currentBPM) * 4) {
            currentBPM = getBPMAtTime(currentTime, timeToBPM, finalBPM);
            double sectionLength = (60000.0 / currentBPM) * 4;
            double sectionEnd = currentTime + sectionLength;
            
            Section section;
            section.bpm = currentBPM;
            section.changeBPM = false;
            
            double lastBPMInSection = currentBPM;
            bool foundAnyChange = false;
            
            for (const auto& [time, bpm] : timeToBPM) {
                if (time > currentTime && time < sectionEnd) {
                    foundAnyChange = true;
                    lastBPMInSection = bpm;
                }
            }
            
            if (foundAnyChange) {
                section.changeBPM = true;
                section.bpm = lastBPMInSection;
            }
            
            int p1NoteCount = 0, p2NoteCount = 0;
            
            for (const auto& note : allNotes) {
                if (note.time >= currentTime && note.time < sectionEnd) {
                    if (note.lane < 10) p1NoteCount++;
                    else p2NoteCount++;
                }
            }
            
            section.mustHitSection = (p1NoteCount >= p2NoteCount);
            
            for (const auto& note : allNotes) {
                if (note.time >= currentTime && note.time < sectionEnd) {
                    int finalLane;
                    bool isP1 = note.lane < 10;
                    int baseLane = note.lane % 10;
                    
                    if (section.mustHitSection) {
                        finalLane = isP1 ? baseLane : baseLane + 4;
                    } else {
                        finalLane = isP1 ? baseLane + 4 : baseLane;
                    }
                    
                    section.notes.emplace_back(note.time, finalLane, note.duration);
                }
            }
            
            sections.push_back(std::move(section));
            currentTime += sectionLength;
            
            sectionCount++;
            if (sectionCount % 10 == 0) {
                double progress = std::min(0.99, currentTime / maxTime);
                convertBar.update(0.75 + (progress * 0.24), 
                                "Section " + std::to_string(sectionCount) + "/" + std::to_string(totalSectionEstimate));
            }
        }
        
        convertBar.finish("Sections built!");
        
        guiLogger.logColored("Generating JSON structure...\n", CYAN);
        std::string jsonData = buildJSON(sections, finalBPM);
        
        guiLogger.logColored("Writing to file...\n", CYAN);
        std::ofstream out(outFile);
        if (!out) {
            guiLogger.logColored("\n[X] Failed to write output file!\n", RED);
            return false;
        }
        out << jsonData;
        out.close();
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        guiLogger.logColored("\n=== CONVERSION SUCCESSFUL ===\n\n", GREEN);
        guiLogger.log("Chart Statistics:\n");
        guiLogger.log("  Total Notes:   " + std::to_string(allNotes.size()) + "\n");
        guiLogger.log("  P1 Notes:      " + std::to_string(p1Count) + "\n");
        guiLogger.log("  P2 Notes:      " + std::to_string(allNotes.size() - p1Count) + "\n");
        guiLogger.log("  Sections:      " + std::to_string(sections.size()) + "\n\n");
        
        guiLogger.log("MIDI Info:\n");
        guiLogger.log("  PPQ:           " + std::to_string(ppq) + "\n");
        
        std::ostringstream bpmStr;
        bpmStr << std::fixed << std::setprecision(2) << baseBPM;
        guiLogger.log("  Base BPM:      " + bpmStr.str() + "\n");
        
        bpmStr.str("");
        bpmStr << finalBPM;
        guiLogger.log("  Final BPM:     " + bpmStr.str() + "\n\n");
        
        if (!tempoChanges.empty() && tempoChanges.size() > 1) {
            guiLogger.log("BPM Changes (" + std::to_string(tempoChanges.size() - 1) + "):\n");
            for (size_t i = 1; i < std::min(size_t(6), tempoChanges.size()); ++i) {
                double time = ticksToMs(tempoChanges[i].tick, baseBPM, ppq, tempoChanges, config.bpmMultiplier);
                std::ostringstream changeStr;
                changeStr << std::fixed << std::setprecision(2);
                changeStr << "  @ " << std::setw(7) << (time / 1000.0) << "s -> " 
                         << std::setw(6) << (tempoChanges[i].bpm * config.bpmMultiplier) << " BPM\n";
                guiLogger.log(changeStr.str());
            }
            if (tempoChanges.size() > 6) {
                guiLogger.log("  ... and " + std::to_string(tempoChanges.size() - 6) + " more\n");
            }
            guiLogger.log("\n");
        }
        
        std::ostringstream outInfo;
        outInfo << std::fixed << std::setprecision(2);
        outInfo << "Output:\n";
        outInfo << "  File Size:     " << (jsonData.size() / 1024.0) << " KB\n";
        outInfo << "  Process Time:  " << duration.count() << " ms\n";
        outInfo << "  Location:      " << outFile << "\n\n";
        guiLogger.log(outInfo.str());
        
        return true;
    }
};

#ifdef _WIN32

// Global variables for GUI
HWND g_hMainWnd = nullptr;
HWND g_hP1Edit, g_hP2Edit, g_hOutEdit, g_hConsole, g_hProgress;
HWND g_hSongEdit, g_hBPMEdit, g_hOffsetEdit, g_hVelEdit, g_hPrecEdit, g_hSpeedEdit;
HWND g_hP1CharEdit, g_hP2CharEdit, g_hGFCharEdit, g_hStageEdit;
HWND g_hSustainCheck, g_hPrecisionCheck;
HFONT g_hFont, g_hTitleFont, g_hConsoleFont;
bool g_converting = false;

std::string BrowseForFile(HWND hwnd, const char* filter, bool save = false) {
    OPENFILENAME ofn = {0};
    char filename[MAX_PATH] = "";
    
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.Flags = save ? (OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT) : (OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST);
    
    if (save ? GetSaveFileName(&ofn) : GetOpenFileName(&ofn)) {
        return std::string(filename);
    }
    return "";
}

std::string GetWindowTextStr(HWND hwnd) {
    int len = GetWindowTextLength(hwnd);
    if (len == 0) return "";
    
    std::vector<char> buffer(len + 1);
    GetWindowText(hwnd, buffer.data(), len + 1);
    return std::string(buffer.data());
}

void DoConversion() {
    g_converting = true;
    
    std::string p1File = GetWindowTextStr(g_hP1Edit);
    std::string p2File = GetWindowTextStr(g_hP2Edit);
    std::string outFile = GetWindowTextStr(g_hOutEdit);
    
    if (p1File.empty() || p2File.empty() || outFile.empty()) {
        MessageBox(g_hMainWnd, "Please specify all files!", "Error", MB_OK | MB_ICONERROR);
        g_converting = false;
        return;
    }
    
    PsychConverter converter;
    auto& config = converter.getConfig();
    
    config.songName = GetWindowTextStr(g_hSongEdit);
    config.bpmMultiplier = std::stod(GetWindowTextStr(g_hBPMEdit));
    config.noteOffset = std::stod(GetWindowTextStr(g_hOffsetEdit));
    config.minVelocity = std::stoi(GetWindowTextStr(g_hVelEdit));
    config.decimalPlaces = std::stoi(GetWindowTextStr(g_hPrecEdit));
    config.speed = std::stod(GetWindowTextStr(g_hSpeedEdit));
    config.p1Char = GetWindowTextStr(g_hP1CharEdit);
    config.p2Char = GetWindowTextStr(g_hP2CharEdit);
    config.gfChar = GetWindowTextStr(g_hGFCharEdit);
    config.stage = GetWindowTextStr(g_hStageEdit);
    config.sustainNotes = (SendMessage(g_hSustainCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    config.highPrecision = (SendMessage(g_hPrecisionCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    
    converter.setConfig(config);
    converter.setProgressHandle(g_hProgress);
    
    std::thread convThread([=]() mutable {
        bool success = converter.convert(p1File, p2File, outFile);
        
        g_converting = false;
        SendMessage(g_hProgress, PBM_SETPOS, 0, 0);
        
        if (success) {
            MessageBox(g_hMainWnd, "Conversion completed successfully!", "Success", MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBox(g_hMainWnd, "Conversion failed! Check the console for details.", "Error", MB_OK | MB_ICONERROR);
        }
    });
    
    convThread.detach();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // Create fonts
            g_hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
            
            g_hTitleFont = CreateFont(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
            
            g_hConsoleFont = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      FIXED_PITCH | FF_MODERN, "Consolas");
            
            int yPos = 10;
            
            // Title
            HWND hTitle = CreateWindow("STATIC", "MIDI to Psych Engine Converter",
                WS_VISIBLE | WS_CHILD | SS_CENTER,
                10, yPos, 760, 30, hwnd, NULL, NULL, NULL);
            SendMessage(hTitle, WM_SETFONT, (WPARAM)g_hTitleFont, TRUE);
            yPos += 40;
            
            // File inputs section
            CreateWindow("STATIC", "Input Files:", WS_VISIBLE | WS_CHILD,
                10, yPos, 200, 20, hwnd, NULL, NULL, NULL);
            yPos += 25;
            
            CreateWindow("STATIC", "Player 1 MIDI:", WS_VISIBLE | WS_CHILD,
                10, yPos, 100, 20, hwnd, NULL, NULL, NULL);
            g_hP1Edit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                110, yPos, 550, 22, hwnd, (HMENU)ID_EDIT_P1, NULL, NULL);
            CreateWindow("BUTTON", "Browse...", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                670, yPos, 100, 22, hwnd, (HMENU)ID_BTN_P1_BROWSE, NULL, NULL);
            SendMessage(g_hP1Edit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            yPos += 30;
            
            CreateWindow("STATIC", "Player 2 MIDI:", WS_VISIBLE | WS_CHILD,
                10, yPos, 100, 20, hwnd, NULL, NULL, NULL);
            g_hP2Edit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                110, yPos, 550, 22, hwnd, (HMENU)ID_EDIT_P2, NULL, NULL);
            CreateWindow("BUTTON", "Browse...", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                670, yPos, 100, 22, hwnd, (HMENU)ID_BTN_P2_BROWSE, NULL, NULL);
            SendMessage(g_hP2Edit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            yPos += 30;
            
            CreateWindow("STATIC", "Output JSON:", WS_VISIBLE | WS_CHILD,
                10, yPos, 100, 20, hwnd, NULL, NULL, NULL);
            g_hOutEdit = CreateWindow("EDIT", "chart.json", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                110, yPos, 550, 22, hwnd, (HMENU)ID_EDIT_OUT, NULL, NULL);
            CreateWindow("BUTTON", "Browse...", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                670, yPos, 100, 22, hwnd, (HMENU)ID_BTN_OUT_BROWSE, NULL, NULL);
            SendMessage(g_hOutEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            yPos += 40;
            
            // Settings section
            CreateWindow("STATIC", "Chart Settings:", WS_VISIBLE | WS_CHILD,
                10, yPos, 200, 20, hwnd, NULL, NULL, NULL);
            yPos += 25;
            
            // Row 1
            CreateWindow("STATIC", "Song Name:", WS_VISIBLE | WS_CHILD,
                10, yPos, 80, 20, hwnd, NULL, NULL, NULL);
            g_hSongEdit = CreateWindow("EDIT", "Converted", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                95, yPos, 120, 22, hwnd, (HMENU)ID_EDIT_SONG, NULL, NULL);
            SendMessage(g_hSongEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            
            CreateWindow("STATIC", "BPM Mult:", WS_VISIBLE | WS_CHILD,
                230, yPos, 70, 20, hwnd, NULL, NULL, NULL);
            g_hBPMEdit = CreateWindow("EDIT", "1.0", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                305, yPos, 60, 22, hwnd, (HMENU)ID_EDIT_BPM, NULL, NULL);
            SendMessage(g_hBPMEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            
            CreateWindow("STATIC", "Speed:", WS_VISIBLE | WS_CHILD,
                380, yPos, 50, 20, hwnd, NULL, NULL, NULL);
            g_hSpeedEdit = CreateWindow("EDIT", "2.5", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                435, yPos, 60, 22, hwnd, (HMENU)ID_EDIT_SPEED, NULL, NULL);
            SendMessage(g_hSpeedEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            
            CreateWindow("STATIC", "Offset (ms):", WS_VISIBLE | WS_CHILD,
                510, yPos, 80, 20, hwnd, NULL, NULL, NULL);
            g_hOffsetEdit = CreateWindow("EDIT", "0", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                595, yPos, 60, 22, hwnd, (HMENU)ID_EDIT_OFFSET, NULL, NULL);
            SendMessage(g_hOffsetEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            yPos += 30;
            
            // Row 2
            CreateWindow("STATIC", "Min Vel:", WS_VISIBLE | WS_CHILD,
                10, yPos, 80, 20, hwnd, NULL, NULL, NULL);
            g_hVelEdit = CreateWindow("EDIT", "0", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                95, yPos, 60, 22, hwnd, (HMENU)ID_EDIT_VELOCITY, NULL, NULL);
            SendMessage(g_hVelEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            
            CreateWindow("STATIC", "Precision:", WS_VISIBLE | WS_CHILD,
                170, yPos, 70, 20, hwnd, NULL, NULL, NULL);
            g_hPrecEdit = CreateWindow("EDIT", "6", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                245, yPos, 40, 22, hwnd, (HMENU)ID_EDIT_PRECISION, NULL, NULL);
            SendMessage(g_hPrecEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            
            g_hSustainCheck = CreateWindow("BUTTON", "Sustain Notes", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                300, yPos, 120, 22, hwnd, (HMENU)ID_CHECK_SUSTAIN, NULL, NULL);
            SendMessage(g_hSustainCheck, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            
            g_hPrecisionCheck = CreateWindow("BUTTON", "High Precision", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                430, yPos, 130, 22, hwnd, (HMENU)ID_CHECK_PRECISION, NULL, NULL);
            SendMessage(g_hPrecisionCheck, BM_SETCHECK, BST_CHECKED, 0);
            SendMessage(g_hPrecisionCheck, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            yPos += 30;
            
            // Character settings
            CreateWindow("STATIC", "P1 Char:", WS_VISIBLE | WS_CHILD,
                10, yPos, 60, 20, hwnd, NULL, NULL, NULL);
            g_hP1CharEdit = CreateWindow("EDIT", "bf", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                75, yPos, 80, 22, hwnd, (HMENU)ID_EDIT_P1CHAR, NULL, NULL);
            SendMessage(g_hP1CharEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            
            CreateWindow("STATIC", "P2 Char:", WS_VISIBLE | WS_CHILD,
                170, yPos, 60, 20, hwnd, NULL, NULL, NULL);
            g_hP2CharEdit = CreateWindow("EDIT", "dad", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                235, yPos, 80, 22, hwnd, (HMENU)ID_EDIT_P2CHAR, NULL, NULL);
            SendMessage(g_hP2CharEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            
            CreateWindow("STATIC", "GF Char:", WS_VISIBLE | WS_CHILD,
                330, yPos, 60, 20, hwnd, NULL, NULL, NULL);
            g_hGFCharEdit = CreateWindow("EDIT", "gf", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                395, yPos, 80, 22, hwnd, (HMENU)ID_EDIT_GFCHAR, NULL, NULL);
            SendMessage(g_hGFCharEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            
            CreateWindow("STATIC", "Stage:", WS_VISIBLE | WS_CHILD,
                490, yPos, 50, 20, hwnd, NULL, NULL, NULL);
            g_hStageEdit = CreateWindow("EDIT", "stage", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                545, yPos, 110, 22, hwnd, (HMENU)ID_EDIT_STAGE, NULL, NULL);
            SendMessage(g_hStageEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            yPos += 35;
            
            // Progress bar
            g_hProgress = CreateWindowEx(0, PROGRESS_CLASS, NULL,
                WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
                10, yPos, 660, 25, hwnd, (HMENU)ID_PROGRESS, NULL, NULL);
            SendMessage(g_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessage(g_hProgress, PBM_SETSTEP, 1, 0);
            
            // Convert button
            HWND hConvert = CreateWindow("BUTTON", "CONVERT", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                680, yPos, 90, 25, hwnd, (HMENU)ID_BTN_CONVERT, NULL, NULL);
            SendMessage(hConvert, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            yPos += 35;
            
            // Console output
            CreateWindow("STATIC", "Console Output:", WS_VISIBLE | WS_CHILD,
                10, yPos, 200, 20, hwnd, NULL, NULL, NULL);
            
            HWND hClearBtn = CreateWindow("BUTTON", "Clear", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                680, yPos, 90, 22, hwnd, (HMENU)ID_BTN_CLEAR_LOG, NULL, NULL);
            SendMessage(hClearBtn, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            yPos += 25;
            
            // Rich edit for console
            LoadLibrary("Msftedit.dll");
            g_hConsole = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
                WS_VISIBLE | WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                10, yPos, 760, 250, hwnd, (HMENU)ID_CONSOLE, NULL, NULL);
            
            SendMessage(g_hConsole, WM_SETFONT, (WPARAM)g_hConsoleFont, TRUE);
            
            // Set console background to dark
            SendMessage(g_hConsole, EM_SETBKGNDCOLOR, 0, RGB(20, 20, 30));
            
            guiLogger.setConsole(g_hConsole);
            
            guiLogger.logColored("MIDI to Psych Engine Converter v2.1\n", CYAN);
            guiLogger.logColored("Ready to convert!\n\n", GREEN);
            
            break;
        }
        
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_BTN_P1_BROWSE: {
                    std::string file = BrowseForFile(hwnd, "MIDI Files (*.mid)\0*.mid\0All Files\0*.*\0");
                    if (!file.empty()) {
                        SetWindowText(g_hP1Edit, file.c_str());
                    }
                    break;
                }
                
                case ID_BTN_P2_BROWSE: {
                    std::string file = BrowseForFile(hwnd, "MIDI Files (*.mid)\0*.mid\0All Files\0*.*\0");
                    if (!file.empty()) {
                        SetWindowText(g_hP2Edit, file.c_str());
                    }
                    break;
                }
                
                case ID_BTN_OUT_BROWSE: {
                    std::string file = BrowseForFile(hwnd, "JSON Files (*.json)\0*.json\0All Files\0*.*\0", true);
                    if (!file.empty()) {
                        SetWindowText(g_hOutEdit, file.c_str());
                    }
                    break;
                }
                
                case ID_BTN_CONVERT: {
                    if (!g_converting) {
                        DoConversion();
                    } else {
                        MessageBox(hwnd, "Conversion already in progress!", "Info", MB_OK | MB_ICONINFORMATION);
                    }
                    break;
                }
                
                case ID_BTN_CLEAR_LOG: {
                    SetWindowText(g_hConsole, "");
                    guiLogger.logColored("Console cleared.\n", CYAN);
                    break;
                }
            }
            break;
        }
        
        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, RGB(240, 240, 240));
            SetBkColor(hdcStatic, RGB(45, 45, 48));
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        
        case WM_DESTROY:
            DeleteObject(g_hFont);
            DeleteObject(g_hTitleFont);
            DeleteObject(g_hConsoleFont);
            PostQuitMessage(0);
            break;
            
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Check if run from command line with arguments
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    
    // If command line arguments exist (more than just the exe name), run CLI mode
    if (argc > 1) {
        // Convert arguments and run CLI version
        // Redirect to console mode
        AllocConsole();
        FILE* fDummy;
        freopen_s(&fDummy, "CONOUT$", "w", stdout);
        freopen_s(&fDummy, "CONOUT$", "w", stderr);
        freopen_s(&fDummy, "CONIN$", "r", stdin);
        
        ENABLE_COLORS();
        
        // Convert wide strings to regular strings
        std::vector<std::string> args;
        std::vector<char*> argPtrs;
        for (int i = 0; i < argc; ++i) {
            int len = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, NULL, 0, NULL, NULL);
            std::string str(len - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, &str[0], len, NULL, NULL);
            args.push_back(str);
            argPtrs.push_back(&args.back()[0]);
        }
        
        LocalFree(argv);
        
        // Run CLI version (reuse the logic from original main)
        if (argc < 3) {
            std::cout << RED << "Error: Need at least 2 MIDI files!\n" << RESET;
            std::cout << "Use -h for help\n";
            system("pause");
            return 1;
        }
        
        std::string p1File = args[1];
        std::string p2File = args[2];
        std::string outFile = "chart.json";
        
        if (argc > 3 && args[3][0] != '-') {
            outFile = args[3];
        }
        
        PsychConverter converter;
        auto& config = converter.getConfig();
        
        // Parse arguments
        for (int i = 3; i < argc; ++i) {
            std::string arg = args[i];
            
            if ((arg == "-s" || arg == "--song") && i + 1 < argc) {
                config.songName = args[++i];
            } else if ((arg == "-b" || arg == "--bpm") && i + 1 < argc) {
                config.bpmMultiplier = std::stod(args[++i]);
            } else if ((arg == "-o" || arg == "--offset") && i + 1 < argc) {
                config.noteOffset = std::stod(args[++i]);
            } else if ((arg == "-v" || arg == "--velocity") && i + 1 < argc) {
                config.minVelocity = std::stoi(args[++i]);
            } else if ((arg == "-p" || arg == "--precision") && i + 1 < argc) {
                config.decimalPlaces = std::stoi(args[++i]);
            } else if (arg == "--no-precision") {
                config.highPrecision = false;
            } else if (arg == "--sustain") {
                config.sustainNotes = true;
            } else if (arg == "--speed" && i + 1 < argc) {
                config.speed = std::stod(args[++i]);
            } else if (arg == "--p1" && i + 1 < argc) {
                config.p1Char = args[++i];
            } else if (arg == "--p2" && i + 1 < argc) {
                config.p2Char = args[++i];
            } else if (arg == "--gf" && i + 1 < argc) {
                config.gfChar = args[++i];
            } else if (arg == "--stage" && i + 1 < argc) {
                config.stage = args[++i];
            }
        }
        
        converter.setConfig(config);
        
        if (!converter.convert(p1File, p2File, outFile)) {
            system("pause");
            return 1;
        }
        
        system("pause");
        return 0;
    }
    
    // Otherwise, run GUI mode
    LocalFree(argv);
    
    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);
    
    // Register window class
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(45, 45, 48));
    wc.lpszClassName = "MIDI2PsychConverter";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    
    RegisterClassEx(&wc);
    
    // Create main window
    g_hMainWnd = CreateWindowEx(
        0,
        "MIDI2PsychConverter",
        "MIDI to Psych Engine Converter v2.1",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 650,
        NULL, NULL, hInstance, NULL
    );
    
    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);
    
    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return (int)msg.wParam;
}

#else
// For non-Windows platforms, just use CLI mode
int main(int argc, char* argv[]) {
    std::cout << "GUI mode is only available on Windows. Running in CLI mode.\n";
    // Add CLI implementation here if needed for cross-platform
    return 0;
}
#endif
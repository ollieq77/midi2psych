#include "psych_converter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <future>
#include <iomanip>
#include <memory>
#include <sstream>
#include <thread>

#include <charconv>

#include "gui_logger.h"
#include "progress_bar.h"
#include "utils.h"

// ─── Fast number formatting ───────────────────────────────────────────────────

namespace {

const double   kPow10[]  = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9};
const uint64_t kPow10i[] = {1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull,
                            10000000ull, 100000000ull, 1000000000ull};

// Writes the decimal digits of v at p; returns the new write pointer.
inline char* writeU64(char* p, uint64_t v) {
    char tmp[20];
    int  n = 0;
    do { tmp[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
    while (n) *p++ = tmp[--n];
    return p;
}

// Exact path: produces precisely what smartNumToStr() would (including its habit of
// leaving "2.000000" alone), via to_chars/snprintf. Used for anything the fast path
// in writeNum() can't prove it rounds identically. Writes at most ~dp+540 bytes.
char* writeNumExact(char* p, double num, int maxDecimals) {
    char buf[512];

    if (std::floor(num) == num) {
        auto r = std::to_chars(buf, buf + sizeof(buf), static_cast<int64_t>(num));
        const size_t n = static_cast<size_t>(r.ptr - buf);
        std::memcpy(p, buf, n);
        return p + n;
    }

    if (maxDecimals < 0) maxDecimals = 6;   // what a negative precision means to printf

#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    auto r = std::to_chars(buf, buf + sizeof(buf), num, std::chars_format::fixed, maxDecimals);
    if (r.ec != std::errc()) {
        const std::string s = smartNumToStr(num, maxDecimals);
        std::memcpy(p, s.data(), s.size());
        return p + s.size();
    }
    size_t len = static_cast<size_t>(r.ptr - buf);
#else
    int n = std::snprintf(buf, sizeof(buf), "%.*f", maxDecimals, num);
    if (n < 0 || n >= static_cast<int>(sizeof(buf))) {
        const std::string s = smartNumToStr(num, maxDecimals);
        std::memcpy(p, s.data(), s.size());
        return p + s.size();
    }
    size_t len = static_cast<size_t>(n);
#endif

    const void* dotp = std::memchr(buf, '.', len);
    if (dotp) {
        const size_t dotPos = static_cast<size_t>(static_cast<const char*>(dotp) - buf);
        size_t keep = len;
        while (keep > 0 && buf[keep - 1] == '0') --keep;       // keep = lastNonZero + 1
        if (keep > 0 && keep - 1 > dotPos) len = keep;          // trim zeros after the dot
        if (buf[len - 1] == '.') --len;
    }
    std::memcpy(p, buf, len);
    return p + len;
}

// Same output as writeNumExact() for every input, but ~2x cheaper for the common case.
//
// Fast path: scale by 10^dp, round to an integer, and print the digits ourselves.
// That multiply can be off by up to half an ulp, so when the fractional part lands
// too close to .5 for us to know which way printf would round, we hand the number to
// writeNumExact() instead. (Checked against ~39M awkward values - ties, 1 ulp either
// side of ties, negatives, near-integers - with zero differences.)
inline char* writeNum(char* p, double num, int dp) {
    if (std::floor(num) == num) {
        const int64_t iv = static_cast<int64_t>(num);
        if (iv < 0) { *p++ = '-'; return writeU64(p, uint64_t(0) - static_cast<uint64_t>(iv)); }
        return writeU64(p, static_cast<uint64_t>(iv));
    }

    const int d = (dp < 0) ? 6 : dp;
    if (d >= 1 && d <= 9) {
        const double v = std::fabs(num) * kPow10[d];
        if (v < 1e15) {                                   // keeps v well inside 2^52 so v - floor(v) is exact
            const double r   = std::floor(v);
            const double f   = v - r;
            const double eps = v * 4.5e-16 + 1e-9;        // generous bound on the multiply's error
            if (std::fabs(f - 0.5) > eps) {
                const uint64_t n  = static_cast<uint64_t>(r) + (f > 0.5 ? 1u : 0u);
                const uint64_t sc = kPow10i[d];
                uint64_t       fp = n % sc;

                if (num < 0) *p++ = '-';
                p = writeU64(p, n / sc);
                *p++ = '.';

                char fd[9];
                for (int i = d - 1; i >= 0; --i) { fd[i] = static_cast<char>('0' + fp % 10); fp /= 10; }

                // Trim trailing zeros - except when the whole fraction is zero, where the
                // original leaves "N.000000" untouched (see the comment on writeNumExact).
                int keep = d;
                if (n % sc != 0) while (fd[keep - 1] == '0') --keep;
                std::memcpy(p, fd, static_cast<size_t>(keep));
                return p + keep;
            }
        }
    }
    return writeNumExact(p, num, dp);
}

inline char* writeInt(char* p, int v) {
    if (v >= 0 && v < 10)  { *p++ = static_cast<char>('0' + v); return p; }
    if (v >= 0 && v < 100) { *p++ = static_cast<char>('0' + v / 10); *p++ = static_cast<char>('0' + v % 10); return p; }
    return std::to_chars(p, p + 16, v).ptr;
}

// Sorts [first, last) by `cmp`, which must order by tick first.
//
// A MIDI track is in tick order by construction, so a run built from one is usually
// already sorted by tick, with only the notes that share a tick out of order (chords).
// One linear scan then fixes just those tiny equal-tick blocks, instead of paying
// O(n log n) over tens of millions of notes. The moment the scan sees a tick go
// backwards (e.g. sustain mode emits notes at their note-off), it gives up and does
// the full sort, so the result is always the same as std::sort(first, last, cmp).
// (Notes that compare equal under cmp are identical in every field, so the output
// can't depend on which algorithm ordered them.)
template <class It, class Cmp>
void sortRun(It first, It last, Cmp cmp) {
    if (last - first < 2) return;

    It blockStart = first;
    for (It it = first + 1; it != last; ++it) {
        if (it->tick == blockStart->tick) continue;
        if (it->tick < blockStart->tick) {            // not tick-ordered: do it the slow way
            std::sort(first, last, cmp);
            return;
        }
        if (it - blockStart > 1) std::sort(blockStart, it, cmp);
        blockStart = it;
    }
    if (last - blockStart > 1) std::sort(blockStart, last, cmp);
}

// Sorts v (made of consecutive pre-cut runs) by `cmp`: every run is sorted on its
// own - several at once on multi-core machines - and then neighbouring runs are
// merged pairwise. `starts` holds each run's first index plus v.size() at the end.
// Way cheaper than one big sort when the input is already chunked by MIDI track.
// `cmp` must order by tick first (see sortRun).
template <class Cmp>
void sortRuns(std::vector<ChartNote>& v, std::vector<size_t> starts, Cmp cmp) {
    if (starts.size() <= 2) {   // zero or one run: nothing to merge
        sortRun(v.begin(), v.end(), cmp);
        return;
    }

    const size_t maxThreads = std::max<size_t>(1, std::thread::hardware_concurrency());

    // Runs `task(i)` for i in [0, count), never more than maxThreads at once.
    auto runAll = [&](size_t count, auto&& task) {
        std::vector<std::future<void>> pending;
        for (size_t i = 0; i < count; ++i) {
            pending.push_back(std::async(std::launch::async, [&task, i] { task(i); }));
            if (pending.size() >= maxThreads) {
                pending.front().get();
                pending.erase(pending.begin());
            }
        }
        for (auto& f : pending) f.get();
    };

    runAll(starts.size() - 1, [&](size_t r) {
        sortRun(v.begin() + starts[r], v.begin() + starts[r + 1], cmp);
    });

    while (starts.size() > 2) {
        const size_t runs = starts.size() - 1;
        const size_t pairs = runs / 2;
        runAll(pairs, [&](size_t p) {
            const size_t r = p * 2;
            std::inplace_merge(v.begin() + starts[r], v.begin() + starts[r + 1],
                               v.begin() + starts[r + 2], cmp);
        });

        std::vector<size_t> next;
        for (size_t r = 0; r < runs; r += 2) next.push_back(starts[r]);   // merged pair starts where its first run did
        next.push_back(v.size());
        starts = std::move(next);
    }
}

}  // namespace

// ─── writeJSON ────────────────────────────────────────────────────────────────

size_t PsychConverter::writeJSON(std::ostream& out, const ChartNote* notes,
                                  const Section* first, const Section* last,
                                  double finalBPM) const {
    // Flush to the stream whenever the buffer passes this. Keeps memory flat no
    // matter how big the chart is - the old all-in-one string was ~25 bytes per
    // note, ~1.7 GB for 67M notes.
    constexpr size_t kFlushAt = size_t(1) << 20;

    const int dp = m_config.decimalPlaces;

    // Everything below writes straight into a raw char buffer through a pointer, so
    // there is no per-append capacity check / realloc branch like std::string has.
    // That's only safe because we always leave `slack` spare bytes past kFlushAt:
    // the biggest thing written between two flush checks is one note, one section
    // tail, or the final footer. A number is at most ~(dp + 540) bytes (dp is user
    // input, so size from it).
    const size_t numMax = 24 + static_cast<size_t>(std::max(dp, 6)) + 512;
    const size_t slack  = 2 * numMax + 256
                        + m_config.songName.size() + m_config.p1Char.size()
                        + m_config.p2Char.size()   + m_config.gfChar.size()
                        + m_config.stage.size();

    std::unique_ptr<char[]> store(new char[kFlushAt + slack]);
    char* const base = store.get();
    char*       p    = base;
    size_t written   = 0;

    auto flush = [&]() {
        out.write(base, static_cast<std::streamsize>(p - base));
        written += static_cast<size_t>(p - base);
        p = base;
    };
    auto lit = [&](const char* s, size_t n) { std::memcpy(p, s, n); p += n; };
    auto str = [&](const std::string& s)    { std::memcpy(p, s.data(), s.size()); p += s.size(); };
#define M2P_LIT(s) lit(s, sizeof(s) - 1)

    M2P_LIT("{\"song\":{\"song\":\"");
    str(m_config.songName);
    M2P_LIT("\",\"notes\":[");

    // One "step" is 1/4 beat (there are 16 steps per 4-beat section).
    // stepMs = (60000 / bpm) * 0.25 = 15000 / bpm
    const double stepMs = (finalBPM > 0.0) ? (15000.0 / finalBPM) : 0.0;

    const bool   doRound  = (m_config.roundTimesTo >= 0);
    const double roundMul = doRound ? std::pow(10.0, m_config.roundTimesTo) : 1.0;

    for (const Section* s = first; s != last; ++s) {
        if (s != first) *p++ = ',';
        M2P_LIT("{\"sectionNotes\":[");

        for (size_t i = s->firstNote; i < s->lastNote; ++i) {
            const ChartNote& n = notes[i];
            if (i != s->firstNote) *p++ = ',';

            double time = n.time;
            double dur  = n.duration;

            if (doRound) {
                time = std::round(time * roundMul) / roundMul;
                dur  = std::round(dur  * roundMul) / roundMul;
            }

            // Format: [ time, lane, holdLength, custom ]
            // custom (index 3) is currently unused (0) but reserved for later.
            *p++ = '[';
            p = writeNum(p, time, dp);
            *p++ = ',';
            p = writeInt(p, n.lane);
            *p++ = ',';

            // Only emit a hold length if it is at least one step long
            if (dur >= stepMs) {
                p = writeNum(p, dur, dp);
                M2P_LIT(",0]");
            } else {
                M2P_LIT("0,0]");
            }

            if (static_cast<size_t>(p - base) >= kFlushAt) flush();
        }

        M2P_LIT("],\"lengthInSteps\":16,\"mustHitSection\":");
        if (s->mustHitSection) M2P_LIT("true"); else M2P_LIT("false");
        M2P_LIT(",\"changeBPM\":");
        if (s->changeBPM) M2P_LIT("true"); else M2P_LIT("false");
        M2P_LIT(",\"bpm\":");
        p = writeNum(p, s->bpm, dp);
        *p++ = '}';

        if (static_cast<size_t>(p - base) >= kFlushAt) flush();
    }

    M2P_LIT("],\"bpm\":");
    p = writeNum(p, finalBPM, dp);
    M2P_LIT(",\"needsVoices\":true,\"speed\":");
    p = writeNum(p, m_config.speed, dp);

    M2P_LIT(",\"player1\":\"");    str(m_config.p1Char);
    M2P_LIT("\",\"player2\":\"");   str(m_config.p2Char);
    M2P_LIT("\",\"gfVersion\":\""); str(m_config.gfChar);
    M2P_LIT("\",\"stage\":\"");     str(m_config.stage);
    *p++ = '"';

    if (m_config.mania != 3) {
        M2P_LIT(",\"mania\":");
        p = writeInt(p, m_config.mania);
    }

    M2P_LIT(",\"validScore\":true}}");
    flush();
#undef M2P_LIT
    return written;
}

// ─── splitSections ────────────────────────────────────────────────────────────

std::vector<std::pair<size_t, size_t>>
PsychConverter::splitSections(const std::vector<Section>& sections, int notesPerChunk) const {
    std::vector<std::pair<size_t, size_t>> chunks;
    size_t begin = 0;
    int    count = 0;

    for (size_t i = 0; i < sections.size(); ++i) {
        int n = static_cast<int>(sections[i].noteCount());
        if (count + n > notesPerChunk && i > begin) {
            chunks.emplace_back(begin, i);
            begin = i;
            count = 0;
        }
        count += n;
    }
    if (begin < sections.size()) chunks.emplace_back(begin, sections.size());
    return chunks;
}

// ─── redistributeSimultaneousNotes ────────────────────────────────────────────

// Notes on the same lane + same MIDI tick get spread across the span of that
// single tick (tickStart .. tickStart + 1 tick). The first note stays exactly
// on the tick, the rest are spaced evenly inside it, so nothing ever drifts
// into the next tick and the groove stays put.
//
// `notes` comes in sorted by (tick, lane). A spread note never leaves its own
// tick, so the only thing that can get out of order is inside a single tick's
// block - that's all we re-sort, instead of the whole chart.
void PsychConverter::redistributeSimultaneousNotes(std::vector<ChartNote>& notes,
                                                    const TempoMap& tempoMap) const {
    const size_t n = notes.size();
    if (n < 2) return;

    auto timeOrder = [](const ChartNote& a, const ChartNote& b) {
        if (a.time != b.time) return a.time < b.time;
        if (a.lane != b.lane) return a.lane < b.lane;
        return a.duration < b.duration;
    };

    size_t blockStart = 0;
    while (blockStart < n) {
        const uint32_t tick = notes[blockStart].tick;

        size_t blockEnd = blockStart + 1;
        while (blockEnd < n && notes[blockEnd].tick == tick) ++blockEnd;

        if (blockEnd - blockStart > 1) {
            bool   spread = false;
            double tickMs = 0.0;   // how long one tick is right here (tempo-map aware), computed lazily

            size_t i = blockStart;
            while (i < blockEnd) {
                const int lane = notes[i].lane;
                size_t groupEnd = i + 1;
                while (groupEnd < blockEnd && notes[groupEnd].lane == lane) ++groupEnd;

                const size_t groupSize = groupEnd - i;
                if (groupSize > 1) {
                    if (!spread) tickMs = tempoMap.toMs(tick + 1) - tempoMap.toMs(tick);
                    spread = true;

                    const double base = notes[i].time;
                    for (size_t j = 1; j < groupSize; ++j)
                        notes[i + j].time = base + tickMs * static_cast<double>(j) / static_cast<double>(groupSize);
                }
                i = groupEnd;
            }

            if (spread)
                std::sort(notes.begin() + blockStart, notes.begin() + blockEnd, timeOrder);
        }

        blockStart = blockEnd;
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
    guiLogger.logColored("    MIDI -> Psych Engine Converter V1.1\n",            CYAN);
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

    // Local copy, kept sorted by tick (tempo events found in several tracks can
    // arrive out of order, and the tempo map needs them in order).
    std::vector<TempoChange> tempoChanges = !p1Parser.tempoChanges.empty()
                                            ? p1Parser.tempoChanges : p2Parser.tempoChanges;
    auto byTick = [](const TempoChange& a, const TempoChange& b) { return a.tick < b.tick; };
    if (!std::is_sorted(tempoChanges.begin(), tempoChanges.end(), byTick))
        std::stable_sort(tempoChanges.begin(), tempoChanges.end(), byTick);

    const TempoMap tempoMap(tempoChanges, finalBPM, ppq, m_config.bpmMultiplier);

    std::vector<std::pair<double, double>> timeToBPM;
    timeToBPM.reserve(tempoChanges.size());
    for (const auto& tc : tempoChanges)
        timeToBPM.push_back({tempoMap.toMs(tc.tick), tc.bpm * m_config.bpmMultiplier});

    uint32_t maxTick  = 0;
    const int keyCount = m_config.mania + 1;

    // Count how often each pitch shows up (and how many notes there are in total).
    int    freq[256] = {};
    size_t totalEvents = 0;
    for (const MIDIParser* parser : {&p1Parser, &p2Parser})
        for (const auto& track : parser->tracks)
            for (const auto& evt : track) { ++freq[evt.note]; ++totalEvents; }

    // Pitch -> lane table (a flat array; no hashing per note).
    int pitchToLane[256];
    for (int p = 0; p < 256; ++p) pitchToLane[p] = p % keyCount;

    if (m_config.smartPitchMapping && totalEvents > 0) {
        // Histogram balancing: most-used pitches first, each one goes to the
        // lane that currently has the fewest notes (lowest index on ties).
        std::vector<std::pair<uint8_t, int>> items;
        for (int p = 0; p < 256; ++p)
            if (freq[p] > 0) items.emplace_back(static_cast<uint8_t>(p), freq[p]);
        std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
            return a.second > b.second || (a.second == b.second && a.first < b.first);
        });

        std::vector<int> laneLoads(keyCount, 0);
        for (const auto& it : items) {
            int bestLane = 0;
            int bestLoad = laneLoads[0];
            for (int l = 1; l < keyCount; ++l) {
                if (laneLoads[l] < bestLoad) { bestLoad = laneLoads[l]; bestLane = l; }
            }
            pitchToLane[it.first] = bestLane;
            laneLoads[bestLane] += it.second;
        }
    }

    std::vector<ChartNote> allNotes;
    allNotes.reserve(totalEvents);   // one allocation instead of ~dozens of regrowths

    // Where each MIDI track's notes start in allNotes (plus the end). Each track
    // is already close to time-ordered, which the sort below takes advantage of.
    std::vector<size_t> runStarts{0};

    // Turns one player's parsed notes into ChartNotes. P1 gets lanes 0..keyCount-1,
    // P2 gets lane+100 (the section builder maps them back later).
    //
    // Each track's parsed events are released the moment they've been converted, so
    // the 12-byte MIDINotes drain away while the 24-byte ChartNotes fill up instead
    // of both sets sitting in memory at the same time.
    auto addPlayerNotes = [&](MIDIParser& parser, int laneOffset,
                              double progressBase, const char* tag) {
        const size_t totalTracks = parser.tracks.size();
        size_t doneTracks = 0;
        for (auto& track : parser.tracks) {
            for (const auto& evt : track) {
                const double startMs = tempoMap.toMs(evt.tick);
                double dur = 0.0;
                if (m_config.sustainNotes && evt.duration > 0)
                    dur = tempoMap.toMs(evt.tick + evt.duration) - startMs;

                allNotes.emplace_back(startMs + m_config.noteOffset,
                                      pitchToLane[evt.note] + laneOffset, dur, evt.tick);
                maxTick = std::max(maxTick, evt.tick);
            }
            runStarts.push_back(allNotes.size());
            std::vector<MIDINote>().swap(track);   // give this track's events back
            ++doneTracks;
            if (totalTracks > 0)
                convertBar.update(progressBase + static_cast<double>(doneTracks) / totalTracks * 0.25,
                    std::string(tag) + " tracks " + std::to_string(doneTracks) + "/" + std::to_string(totalTracks));
        }
    };

    addPlayerNotes(p1Parser, 0, 0.0, "P1");

    convertBar.update(0.25, "Processing P2...");
    size_t p1Count = allNotes.size();

    addPlayerNotes(p2Parser, 100, 0.25, "P2");

    convertBar.update(0.50, "Sorting notes...");

    // Order by (tick, lane, length). Time only ever goes up with the tick, so this
    // is time order too - but integer compares, and ties come out the same way
    // every run (the old sort left them up to the algorithm).
    auto tickLess = [](const ChartNote& a, const ChartNote& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.lane != b.lane) return a.lane < b.lane;
        return a.duration < b.duration;
    };
    sortRuns(allNotes, std::move(runStarts), tickLess);

    convertBar.update(0.60, "Spreading same-tick notes...");

    // Same lane + same tick -> spread inside that tick's span (and put each
    // tick's little block back in time order).
    redistributeSimultaneousNotes(allNotes, tempoMap);

    convertBar.update(0.75, "Building sections...");

    // ── Section building (two-pointer O(n)) ──────────────────────────────
    std::vector<Section> sections;
    double maxTime     = tempoMap.toMs(maxTick);
    double currentTime = 0.0;
    double currentBPM  = finalBPM;

    int    totalSectionEst = static_cast<int>((maxTime / ((60000.0 / finalBPM) * 4)) + 1);
    int    sectionCount    = 0;
    int    lastPct         = -1;
    size_t noteIdx         = 0;
    bool   lastMustHit     = true;

    sections.reserve(static_cast<size_t>(std::max(0, totalSectionEst)) + 2);

    // timeToBPM is sorted by time and the song only moves forward, so two
    // cursors replace the old "scan the whole list" lookups for every section.
    const size_t nChanges = timeToBPM.size();
    long   bpmCursor   = -1;   // last tempo change at or before currentTime
    size_t nextChange  = 0;    // first tempo change after currentTime

    while (currentTime < maxTime + (60000.0 / currentBPM) * 4) {
        while (bpmCursor + 1 < static_cast<long>(nChanges) &&
               timeToBPM[bpmCursor + 1].first <= currentTime)
            ++bpmCursor;
        currentBPM = (bpmCursor >= 0) ? timeToBPM[bpmCursor].second : finalBPM;

        double sectionLen = (60000.0 / currentBPM) * 4;
        double sectionEnd = currentTime + sectionLen;

        Section section;
        section.bpm       = currentBPM;
        section.changeBPM = false;

        while (nextChange < nChanges && timeToBPM[nextChange].first <= currentTime)
            ++nextChange;
        for (size_t j = nextChange; j < nChanges && timeToBPM[j].first < sectionEnd; ++j) {
            section.changeBPM = true;
            section.bpm       = timeToBPM[j].second;   // the last change inside the section wins
        }

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

        // Move each note to its final lane right where it sits; the section only
        // records which slice of allNotes it covers (no per-section copy).
        for (size_t i = sectionStart; i < sectionEnd2; ++i) {
            ChartNote& note = allNotes[i];
            bool isP1       = (note.lane < 100);
            int  baseLane   = isP1 ? note.lane : (note.lane - 100);
            note.lane       = section.mustHitSection
                              ? (isP1 ? baseLane : baseLane + keyCount)
                              : (isP1 ? baseLane + keyCount : baseLane);
        }
        section.firstNote = sectionStart;
        section.lastNote  = sectionEnd2;

        noteIdx = sectionEnd2;
        sections.push_back(std::move(section));
        currentTime += sectionLen;

        // Only touch the log/progress bar when the percentage actually moves.
        // (Each update is a cross-thread SendMessage into the GUI.)
        ++sectionCount;
        double prog = std::min(0.99, currentTime / maxTime);
        int    pct  = static_cast<int>(prog * 100);
        if (pct != lastPct) {
            lastPct = pct;
            convertBar.update(0.75 + prog * 0.24,
                "Section " + std::to_string(sectionCount) + "/" + std::to_string(totalSectionEst));
        }
    }

    while (!sections.empty() && sections.back().noteCount() == 0)
        sections.pop_back();

    convertBar.finish("Sections built!");

    // ── File output ───────────────────────────────────────────────────────
    std::vector<std::string> outputFiles;
    size_t totalFileSize = 0;

    // Writes sections [first, last) to `path`; false (after logging why) if it fails.
    auto writeChart = [&](const std::string& path, const Section* first, const Section* last,
                          size_t& bytesOut) -> bool {
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            guiLogger.logColored("\n[X] Failed to write file: " + path + "\n", RED);
            return false;
        }
        bytesOut = writeJSON(out, allNotes.data(), first, last, finalBPM);
        out.flush();
        if (!out) {
            guiLogger.logColored("\n[X] Error while writing " + path + " (disk full?)\n", RED);
            return false;
        }
        return true;
    };

    if (m_config.splitOutput && m_config.notesPerSplit > 0) {
        guiLogger.logColored("Splitting chart into multiple files...\n", CYAN);

        auto chunks = splitSections(sections, m_config.notesPerSplit);

        size_t      dotPos    = outFile.find_last_of('.');
        std::string baseName  = (dotPos != std::string::npos) ? outFile.substr(0, dotPos) : outFile;
        std::string extension = (dotPos != std::string::npos) ? outFile.substr(dotPos)    : ".json";

        for (size_t i = 0; i < chunks.size(); ++i) {
            std::string fname = baseName + "-" + std::to_string(i + 1) + extension;
            size_t bytes = 0;
            if (!writeChart(fname, sections.data() + chunks[i].first,
                                   sections.data() + chunks[i].second, bytes))
                return false;
            outputFiles.push_back(fname);
            totalFileSize += bytes;

            guiLogger.log("  Created: " + fname + " (" +
                          std::to_string(bytes / 1024.0) + " KB)\n");
        }
    } else {
        guiLogger.logColored("Generating single JSON file...\n", CYAN);
        size_t bytes = 0;
        if (!writeChart(outFile, sections.data(), sections.data() + sections.size(), bytes))
            return false;
        outputFiles.push_back(outFile);
        totalFileSize = bytes;
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
            double t = tempoMap.toMs(tempoChanges[i].tick);
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
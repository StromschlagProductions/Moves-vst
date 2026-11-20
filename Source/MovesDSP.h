#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

// MOVES - Klang-Engine (reines C++, kein JUCE).
//
// Grundidee: menschliche Mikropraesenz (Koerper-/Kleidungsbewegungen, selten ein
// Atemhauch) als duenne Folge einzelner Sample-Ereignisse, nicht als Dauergeraeusch.
//
// Determinismus/Reproduzierbarkeit (Pflichtanforderung, siehe Entwicklungsauftrag Abschnitt 10):
// Es gibt KEINEN fortlaufenden Zufallsgenerator, dessen Zustand von der Wiedergabe-
// geschichte abhaengt. Stattdessen wird die Zeitachse in feste "Zellen" (kCellSeconds)
// eingeteilt; der Inhalt jeder Zelle (ob/was/wie laut/wie schnell ein Ereignis beginnt)
// ist eine reine Funktion aus (Seed, Zellindex) ueber eine Hash-Funktion - unabhaengig
// davon, ob man ab Projektanfang durchspielt, mitten in die Szene hineinspringt, einen
// Loop wiederholt oder offline rendert. Einzige Voraussetzung ist eine gleichbleibende
// Abtastrate waehrend der Wiedergabe (siehe getrennte Dokumentation zu Bekannten Grenzen).
//
// Eine kleine, auf eine "Epoche" begrenzte Rueckschau (kEpochCells Zellen) sorgt fuer
// Mindestabstaende zwischen Ereignissen und vermeidet unmittelbare Wiederholungen,
// ohne dass man dafuer die gesamte Zeitachse von Null an abspielen muesste: die Rueckschau
// kostet bei jeder Zelle hoechstens kEpochCells billige Hash-Auswertungen - das ist auch
// fuer sehr lange Szenen (Stunden) und fuer beliebige Sprungstellen schnell genug fuer
// den Audio-Thread.
namespace mv
{

// ---------------------------------------------------------------------------
// Kleine Hilfsfunktionen (Stil wie in den anderen Stromschlag-DSP-Headern)
inline float finiteClamp (float x, float lo, float hi, float fallback)
{
    return std::isfinite (x) ? std::clamp (x, lo, hi) : fallback;
}
inline double validRate (double x) { return std::isfinite (x) && x >= 8000.0 && x <= 384000.0 ? x : 48000.0; }
inline float dbToLin (float db) { return db <= -90.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }

// splitmix64: schneller, gut durchmischender Zaehler-Hash. Keine fortlaufende Zustandskette -
// jeder Aufruf mit demselben (seed, index) liefert immer dieselben Zahlen.
inline uint64_t splitmix64 (uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// Mehrere unabhaengig wirkende Zufallswerte [0,1) fuer eine Zelle, rein aus (seed, cellIndex, salt).
inline float cellRandom (uint32_t seed, int64_t cellIndex, uint32_t salt)
{
    uint64_t state = (uint64_t) seed * 0x9E3779B97F4A7C15ULL
                      ^ (uint64_t) (cellIndex + 1) * 0xD1B54A32D192ED03ULL
                      ^ (uint64_t) salt * 0x2545F4914F6CDD1DULL;
    const uint64_t z = splitmix64 (state);
    return (float) ((z >> 11) * (1.0 / 9007199254740992.0));   // 53 Bit -> [0,1)
}

//==============================================================================
// Ein einzelnes, dekodiertes Sample (mono, Originalabtastrate der Aufnahme).
struct SampleClip
{
    std::vector<float> data;
    double sourceRate = 48000.0;
};

// Die komplette Samplebibliothek, nach dem Laden unveraendert (read-only fuer die Engine).
struct SampleBank
{
    std::vector<SampleClip> movements;
    std::vector<SampleClip> breaths;
};

enum class Category { movement, breath };

struct Params
{
    float movement = 0.4f;   // MOVEMENT 0..1: Aktivitaet der simulierten Person
    float levelDb = 0.0f;     // LEVEL in dB: Gesamtlautstaerke
};

inline Params sanitize (Params p)
{
    p.movement = finiteClamp (p.movement, 0.0f, 1.0f, 0.4f);
    p.levelDb = finiteClamp (p.levelDb, -60.0f, 12.0f, 0.0f);
    return p;
}

//==============================================================================
class Engine
{
public:
    static constexpr float kCellSeconds = 0.2f;      // Rasterbreite der Zeitachse (fein genug, damit kein Raster hoerbar wird)
    static constexpr int kEpochCells = 450;          // ~90 s; begrenzt die Rueckschau-Kosten
    static constexpr int kMaxVoices = 4;              // gleichzeitig hoerbare Ereignisse
    static constexpr float kBreathShare = 0.12f;      // Anteil der Ereignisse, die ein Atmer/Schniefer sein duerfen
    static constexpr float kMinGapMovementSec = 0.9f; // Mindestabstand zwischen zwei Bewegungs-Ereignissen
    static constexpr float kMinGapBreathSec = 9.0f;   // Mindestabstand zwischen zwei Atmer-Ereignissen ("wesentlich seltener")
    static constexpr float kSafetyRampMs = 6.0f;      // kurzer Fade an Stimmbeginn/-ende (Click-Schutz zusaetzlich zu den Sample-Fades)
    static constexpr float kLevelSmoothMs = 60.0f;    // Glaettung der LEVEL-Aenderung (kein Sprung bei Automation)

    void prepare (double sampleRateIn)
    {
        sampleRate = validRate (sampleRateIn);
        cellSamples = std::max<int64_t> (1, (int64_t) std::llround (sampleRate * (double) kCellSeconds));
        levelSmoothCoeff = (float) (1.0 - std::exp (-1.0 / (((double) kLevelSmoothMs / 1000.0) * sampleRate)));
        // Kapazitaet vorab reservieren: ensureEpoch() darf im Audio-Thread nicht (neu) allozieren.
        epochEvents.reserve (kMaxEventsPerEpoch);
        reset();
    }

    void setSeed (uint32_t newSeed) { seed = newSeed; }

    void setBank (std::shared_ptr<const SampleBank> newBank) { bank = std::move (newBank); }

    void reset()
    {
        for (auto& v : voices) v = Voice();
        lastHandledCell = -1;
        cachedEpoch = -1;
        smoothedLevelLin = -1.0f;   // beim naechsten process() sofort auf den Zielwert springen
    }

    // outL/outR: bereits auf 0 vorbereitete Ausgabepuffer (die Engine ersetzt sie vollstaendig).
    // blockStartSample: absolute Projekt-Samplepositon des ersten Samples in diesem Block
    //   (vom Host; falls der Host keine gueltige Position liefert, uebergibt der Aufrufer
    //   stattdessen einen intern fortlaufend gezaehlten Ersatzwert - siehe PluginProcessor).
    void process (float* outL, float* outR, int numSamples, const Params& pIn, int64_t blockStartSample)
    {
        const auto p = sanitize (pIn);
        const float targetLevelLin = dbToLin (p.levelDb);
        if (smoothedLevelLin < 0.0f) smoothedLevelLin = targetLevelLin;

        int64_t pos = std::max<int64_t> (0, blockStartSample);
        int remaining = numSamples;
        int outIdx = 0;

        while (remaining > 0)
        {
            const int64_t cellIndex = pos / cellSamples;
            const int64_t cellStart = cellIndex * cellSamples;
            const int64_t cellEnd = cellStart + cellSamples;
            const int chunk = (int) std::min<int64_t> (remaining, cellEnd - pos);

            if (cellIndex != lastHandledCell)
            {
                lastHandledCell = cellIndex;
                maybeSpawnEvent (cellIndex, p.movement);
            }

            for (int i = 0; i < chunk; ++i)
            {
                smoothedLevelLin += (targetLevelLin - smoothedLevelLin) * levelSmoothCoeff;
                float mono = 0.0f;
                for (auto& v : voices)
                    if (v.active)
                        mono += renderVoiceSample (v);
                mono *= smoothedLevelLin;
                outL[outIdx] = mono;
                outR[outIdx] = mono;
                ++outIdx;
            }

            pos += chunk;
            remaining -= chunk;
        }
    }

private:
    struct EventRecord
    {
        int64_t cell = -1;
        Category category = Category::movement;
        int sampleIndex = 0;
        float levelJitterDb = 0.0f;
        float rateMultiplier = 1.0f;
    };

    struct Voice
    {
        bool active = false;
        const SampleClip* clip = nullptr;
        double readPos = 0.0;
        double rateInc = 1.0;
        float gainLin = 1.0f;
        int64_t sampleCounter = 0;   // seit Stimmbeginn, fuer den kurzen Einsatz-Fade
    };

    // Reine Funktion: produziert fuer eine Zelle, ausgehend vom Epochenanfang, die Liste
    // tatsaechlich feuernder Ereignisse (mit Mindestabstaenden). Rein von (seed, epochStartCell)
    // abhaengig - bei gleichem Seed und gleicher Epoche immer dasselbe Ergebnis.
    void ensureEpoch (int64_t epochIndex, float fireProbability)
    {
        // Neu aufbauen, wenn entweder die Epoche wechselt ODER sich MOVEMENT seit dem
        // letzten Aufbau sichtbar geaendert hat (sonst wuerde eine Live-Aenderung des
        // Reglers bis zu ~90 s lang nichts bewirken).
        if (epochIndex == cachedEpoch && std::abs (fireProbability - builtWithProbability) < 1.0e-6f)
            return;
        cachedEpoch = epochIndex;
        builtWithProbability = fireProbability;
        epochEvents.clear();

        const int64_t epochStartCell = epochIndex * (int64_t) kEpochCells;
        const int minGapMovement = std::max (1, (int) std::lround (kMinGapMovementSec / kCellSeconds));
        const int minGapBreath = std::max (1, (int) std::lround (kMinGapBreathSec / kCellSeconds));
        int64_t lastMovementCell = epochStartCell - minGapMovement - 1;
        int64_t lastBreathCell = epochStartCell - minGapBreath - 1;
        int lastMovementIndex = -1, lastBreathIndex = -1;

        for (int i = 0; i < kEpochCells; ++i)
        {
            const int64_t cell = epochStartCell + i;
            const float r0 = cellRandom (seed, cell, 0);   // Einsatzschwelle
            if (r0 >= fireProbability) continue;

            const float r1 = cellRandom (seed, cell, 1);   // Kategorie
            const bool wantBreath = r1 < kBreathShare;
            const Category cat = wantBreath ? Category::breath : Category::movement;

            if (cat == Category::movement)
            {
                if (cell - lastMovementCell < minGapMovement) continue;
            }
            else
            {
                if (cell - lastBreathCell < minGapBreath) continue;
            }
            if (movementCount == 0 && cat == Category::movement) continue;
            if (breathCount == 0 && cat == Category::breath) continue;

            EventRecord ev;
            ev.cell = cell;
            ev.category = cat;
            const int poolSize = cat == Category::movement ? movementCount : breathCount;
            int& lastIndex = cat == Category::movement ? lastMovementIndex : lastBreathIndex;
            int idx = (int) (cellRandom (seed, cell, 2) * (float) poolSize);
            idx = std::clamp (idx, 0, poolSize - 1);
            if (poolSize > 1 && idx == lastIndex) idx = (idx + 1) % poolSize;
            lastIndex = idx;
            ev.sampleIndex = idx;
            ev.levelJitterDb = lerp (-1.5f, 1.5f, cellRandom (seed, cell, 4));
            ev.rateMultiplier = lerp (0.985f, 1.015f, cellRandom (seed, cell, 5));

            if (cat == Category::movement) lastMovementCell = cell; else lastBreathCell = cell;

            if ((int) epochEvents.size() < kMaxEventsPerEpoch)
                epochEvents.push_back (ev);
        }
    }

    void maybeSpawnEvent (int64_t cellIndex, float movementParam)
    {
        movementCount = bank ? (int) bank->movements.size() : 0;
        breathCount = bank ? (int) bank->breaths.size() : 0;
        if (movementCount == 0 && breathCount == 0) return;

        const float fireProbability = lerp (0.012f, 0.32f, movementParam);
        ensureEpoch (cellIndex / (int64_t) kEpochCells, fireProbability);

        for (const auto& ev : epochEvents)
        {
            if (ev.cell != cellIndex) continue;
            const auto& pool = ev.category == Category::movement ? bank->movements : bank->breaths;
            if (ev.sampleIndex < 0 || ev.sampleIndex >= (int) pool.size()) return;
            startVoice (pool[(size_t) ev.sampleIndex], ev);
            return;   // hoechstens ein neues Ereignis pro Zelle
        }
    }

    void startVoice (const SampleClip& clip, const EventRecord& ev)
    {
        Voice* slot = nullptr;
        for (auto& v : voices)
            if (! v.active) { slot = &v; break; }
        if (slot == nullptr) return;   // alle Stimmen belegt: Ereignis faellt still aus (Polyphonie bewusst begrenzt)

        *slot = Voice();
        slot->active = true;
        slot->clip = &clip;
        slot->readPos = 0.0;
        const double clipRate = clip.sourceRate > 0.0 ? clip.sourceRate : sampleRate;
        slot->rateInc = (clipRate / sampleRate) * (double) ev.rateMultiplier;
        slot->gainLin = dbToLin (ev.levelJitterDb);
    }

    // kubische Interpolation (Catmull-Rom), gleicher Stil wie in anderen Stromschlag-DSP-Engines
    static float readCubic (const std::vector<float>& y, double pos)
    {
        const int n = (int) y.size();
        if (n == 0) return 0.0f;
        const int i = (int) pos;
        const float f = (float) (pos - (double) i);
        auto at = [&] (int k) { return y[(size_t) std::clamp (k, 0, n - 1)]; };
        const float ym1 = at (i - 1), y0 = at (i), y1 = at (i + 1), y2 = at (i + 2);
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + y0;
    }

    float renderVoiceSample (Voice& v)
    {
        if (v.clip == nullptr || v.clip->data.empty()) { v.active = false; return 0.0f; }
        if (v.readPos >= (double) v.clip->data.size() - 2.0) { v.active = false; return 0.0f; }

        float s = readCubic (v.clip->data, v.readPos) * v.gainLin;

        // kurzer zusaetzlicher Sicherheits-Fade am Stimmbeginn (die Samples haben zwar schon
        // eigene Fades, das hier schuetzt zusaetzlich vor jeglichem Rest-Klick bei Stimmstart)
        const int safetySamples = std::max (1, (int) std::lround ((double) kSafetyRampMs / 1000.0 * sampleRate));
        if (v.sampleCounter < safetySamples)
            s *= (float) v.sampleCounter / (float) safetySamples;
        ++v.sampleCounter;

        v.readPos += v.rateInc;
        return s;
    }

    static constexpr int kMaxEventsPerEpoch = 96;

    double sampleRate = 48000.0;
    int64_t cellSamples = 16800;
    uint32_t seed = 0x4D6F7665u;   // "Move" als Start-Seed; per setSeed() veraenderbar
    std::shared_ptr<const SampleBank> bank;

    int64_t lastHandledCell = -1;
    int64_t cachedEpoch = -1;
    float builtWithProbability = -1.0f;
    int movementCount = 0, breathCount = 0;
    std::vector<EventRecord> epochEvents;

    std::array<Voice, kMaxVoices> voices {};
    float smoothedLevelLin = -1.0f;
    float levelSmoothCoeff = 0.01f;
};

} // namespace mv

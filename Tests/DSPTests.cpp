// Reine Engine-Tests ohne JUCE (siehe CMake-Option AUDIO_DSP_ONLY).
// Pruefen die fuer MOVES entscheidenden Eigenschaften: keine Audio-Thread-Allokationen,
// Reproduzierbarkeit bei identischer Position/Parametern, und Positions-Unabhaengigkeit
// (ein Sprung mitten in die Szene liefert dasselbe Ergebnis wie durchgehendes Abspielen).
#include "TestSupport.h"
#include "../Source/MovesDSP.h"
#include <random>
#include <vector>

static mv::SampleClip makeClip (int lengthSamples, uint32_t seed)
{
    mv::SampleClip clip;
    clip.sourceRate = 48000.0;
    clip.data.resize ((size_t) lengthSamples);
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-0.3f, 0.3f);
    for (int i = 0; i < lengthSamples; ++i)
    {
        // einfache Huelle (ansteigend/abfallend) auf Zufallsrauschen, damit das "Sample"
        // nicht an den Raendern bei voller Lautstaerke abrupt beginnt/endet
        const float env = std::sin ((float) i / (float) lengthSamples * 3.14159265f);
        clip.data[(size_t) i] = dist (rng) * env;
    }
    return clip;
}

static std::shared_ptr<mv::SampleBank> makeTestBank()
{
    auto bank = std::make_shared<mv::SampleBank>();
    for (int i = 0; i < 4; ++i) bank->movements.push_back (makeClip (2000 + i * 300, 100u + (uint32_t) i));
    for (int i = 0; i < 2; ++i) bank->breaths.push_back (makeClip (4000 + i * 500, 900u + (uint32_t) i));
    return bank;
}

static void renderRange (mv::Engine& engine, const mv::Params& p, int64_t startSample, int64_t numSamples,
                          std::vector<float>& outL, std::vector<float>& outR, int blockSize)
{
    outL.assign ((size_t) numSamples, 0.0f);
    outR.assign ((size_t) numSamples, 0.0f);
    int64_t done = 0;
    while (done < numSamples)
    {
        const int count = (int) std::min<int64_t> (blockSize, numSamples - done);
        engine.process (outL.data() + done, outR.data() + done, count, p, startSample + done);
        done += count;
    }
}

int main()
{
    auto bank = makeTestBank();

    // 1) Keine Allokationen im Audio-Thread nach dem Aufwaermen
    {
        mv::Engine engine;
        engine.prepare (48000.0);
        engine.setBank (bank);
        mv::Params p; p.movement = 0.8f; p.levelDb = 0.0f;   // hohe Aktivitaet: viele Stimmstarts
        std::vector<float> l ((size_t) 512), r ((size_t) 512);
        for (int i = 0; i < 50; ++i) engine.process (l.data(), r.data(), 512, p, (int64_t) i * 512); // aufwaermen
        {
            NoAllocations guard;
            for (int i = 50; i < 400; ++i)
                engine.process (l.data(), r.data(), 512, p, (int64_t) i * 512);
        }
        std::cout << "OK: keine Allokationen im Audio-Thread\n";
    }

    // 2) Reproduzierbarkeit: zwei frische Engines, gleicher Seed/Bank/Parameter -> identische Samples
    {
        mv::Params p; p.movement = 0.5f; p.levelDb = -6.0f;
        mv::Engine e1; e1.prepare (48000.0); e1.setBank (bank);
        mv::Engine e2; e2.prepare (48000.0); e2.setBank (bank);
        std::vector<float> l1, r1, l2, r2;
        const int64_t n = 48000 * 30;   // 30 Sekunden
        renderRange (e1, p, 0, n, l1, r1, 777);
        renderRange (e2, p, 0, n, l2, r2, 193);   // andere Blockgroesse: darf das Ergebnis nicht aendern
        CHECK (l1 == l2);
        CHECK (r1 == r2);
        bool anyNonZero = false;
        for (float v : l1) if (v != 0.0f) { anyNonZero = true; break; }
        CHECK (anyNonZero);   // bei 30 s und Movement 0.5 muss mindestens ein Ereignis klingen
        std::cout << "OK: reproduzierbar (gleiche Position/Parameter -> gleiches Ergebnis, blockgroessenunabhaengig)\n";
    }

    // 3) Positions-Unabhaengigkeit: ein direkter "Sprung" an Position X liefert denselben
    //    Verlauf wie durchgehendes Abspielen von 0 bis X (siehe Entwicklungsauftrag Abschnitt 10).
    {
        mv::Params p; p.movement = 0.6f; p.levelDb = 0.0f;
        const int64_t jumpAt = 48000 * 47;     // mitten in einer (fiktiven) Szene
        const int64_t windowLen = 48000 * 5;

        mv::Engine full; full.prepare (48000.0); full.setBank (bank);
        std::vector<float> lFull, rFull;
        renderRange (full, p, 0, jumpAt + windowLen, lFull, rFull, 480);

        mv::Engine seek; seek.prepare (48000.0); seek.setBank (bank);
        std::vector<float> lSeek, rSeek;
        renderRange (seek, p, jumpAt, windowLen, lSeek, rSeek, 480);

        for (int64_t i = 0; i < windowLen; ++i)
        {
            CHECK (lFull[(size_t) (jumpAt + i)] == lSeek[(size_t) i]);
            CHECK (rFull[(size_t) (jumpAt + i)] == rSeek[(size_t) i]);
        }
        std::cout << "OK: positionsunabhaengig (Sprung mitten in die Szene == durchgehende Wiedergabe)\n";
    }

    // 4) Keine NaN/Inf, Ausgabe bleibt in einem plausiblen Pegelbereich
    {
        mv::Params p; p.movement = 1.0f; p.levelDb = 12.0f;   // Extremfall: maximale Aktivitaet und Pegel
        mv::Engine engine; engine.prepare (44100.0); engine.setBank (bank);
        std::vector<float> l, r;
        renderRange (engine, p, 0, 44100 * 20, l, r, 1024);
        for (size_t i = 0; i < l.size(); ++i)
        {
            CHECK (std::isfinite (l[i]) && std::isfinite (r[i]));
            CHECK (std::fabs (l[i]) < 8.0f);   // grosszuegige Sicherheitsgrenze, kein Aufschaukeln
        }
        std::cout << "OK: endliche, begrenzte Ausgabe auch bei Extremwerten\n";
    }

    // 5) Leere Bibliothek: Engine bleibt stumm, stuerzt nicht ab
    {
        auto emptyBank = std::make_shared<mv::SampleBank>();
        mv::Engine engine; engine.prepare (48000.0); engine.setBank (emptyBank);
        mv::Params p; p.movement = 1.0f;
        std::vector<float> l, r;
        renderRange (engine, p, 0, 48000 * 5, l, r, 512);
        for (float v : l) CHECK (v == 0.0f);
        std::cout << "OK: ohne Samplebibliothek bleibt die Ausgabe stumm\n";
    }

    std::cout << "Alle MOVES-DSP-Tests erfolgreich.\n";
    return 0;
}

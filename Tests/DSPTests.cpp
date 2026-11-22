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

    // Long samples make seek tests meaningful: jumps inside attacks, tails and cells,
    // both sides of the former epoch boundary, negative preroll and same-cell loops.
    {
        auto longBank = std::make_shared<mv::SampleBank>();
        longBank->movements.push_back (makeClip (110000, 123));
        longBank->breaths.push_back (makeClip (76000, 456));
        mv::Params p; p.movement = 1;
        mv::Engine full; full.prepare (48000); full.setBank (longBank);
        std::vector<float> l, r;
        renderRange (full, p, 0, 48000 * 94, l, r, 777);
        for (int64_t start : { int64_t(123), int64_t(341280), int64_t(48000*89+17), int64_t(48000*90+31) })
        {
            mv::Engine seek; seek.prepare (48000); seek.setBank (longBank);
            std::vector<float> a, b;
            renderRange (seek, p, start, 48000, a, b, 193);
            for (size_t i=0; i<a.size(); ++i) CHECK (a[i] == l[size_t(start)+i]);
            renderRange (seek, p, start, 48000, a, b, 511);
            for (size_t i=0; i<a.size(); ++i) CHECK (a[i] == l[size_t(start)+i]);
        }
        mv::Engine loop; loop.prepare (48000); loop.setBank (longBank);
        std::vector<float> a,b,c,d;
        renderRange (loop,p,123,2400,a,b,512);
        renderRange (loop,p,123,2400,c,d,31);
        CHECK (a == c);
        mv::Engine pre; pre.prepare (48000); pre.setBank (longBank);
        renderRange (pre,p,-1000,2000,a,b,137);
        for (int i=0;i<1000;++i) CHECK (a[size_t(i)] == 0);
        for (int i=0;i<1000;++i) CHECK (a[size_t(i+1000)] == l[size_t(i)]);
        std::cout << "OK: lange Ausklaenge, beliebige Spruenge, kurze Loops und negativer Vorlauf\n";
    }
    // Minimum distances apply globally, including across arbitrary 90-second marks.
    {
        mv::Engine e; e.prepare (48000); e.setBank (bank);
        int64_t lastMove=-1000000000, lastBreath=lastMove;
        int lowCount=0, highCount=0;
        for (int64_t cell=0;cell<36000;++cell)
        {
            mv::Engine::Event ev;
            if (e.eventAt(cell,0,ev)) ++lowCount;
            if (!e.eventAt(cell,1,ev)) continue;
            ++highCount;
            auto& last=ev.category==mv::Category::movement ? lastMove : lastBreath;
            const double gap=ev.category==mv::Category::movement ? mv::Engine::kMinGapMovementSec : mv::Engine::kMinGapBreathSec;
            CHECK (double(ev.start-last)+.01 >= gap*48000);
            last=ev.start;
        }
        CHECK (highCount > lowCount*5);
        std::cout << "OK: globale Mindestabstaende und deutlich nutzbarer Aktivitaetsbereich\n";
    }
    // Isolated impacts must have a lower crest after conditioning, finite audio and
    // genuinely soft endpoints. Quiet movements gain a usable internal reference level.
    {
        auto clip=makeClip(48000,77);
        for(auto& v:clip.data) v*=.01f;
        clip.data[24000]=.8f;
        auto crest=[](const std::vector<float>& x) { double sum=0;float peak=0;for(float v:x){sum+=double(v)*v;peak=std::max(peak,std::abs(v));}return peak/std::sqrt(sum/double(x.size())); };
        const double before=crest(clip.data);
        mv::conditionClip(clip,mv::Category::movement);
        CHECK (crest(clip.data)<before*.5);
        CHECK (crest(clip.data)<8.0); // below 18.1 dB crest, not just quieter than the source
        float bodyPeak=0;
        for(size_t i=0;i<clip.data.size();++i) if(i!=24000) bodyPeak=std::max(bodyPeak,std::abs(clip.data[i]));
        CHECK (std::abs(clip.data[24000])<bodyPeak); // isolated impulse no longer dominates
        CHECK (clip.data.front()==0 && clip.data.back()==0);
        for(float v:clip.data) CHECK (std::isfinite(v) && std::abs(v)<=mv::dbToLin(-16.f)+1e-6f);
        std::cout << "OK: impulsarme Sampleaufbereitung, weiche Raender, begrenzter Pegel\n";
    }
    std::cout << "Alle MOVES-DSP-Tests erfolgreich.\n";
    return 0;
}

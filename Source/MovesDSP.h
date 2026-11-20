#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace mv
{
inline float finiteClamp(float x, float lo, float hi, float fallback)
{ return std::isfinite(x) ? std::clamp(x, lo, hi) : fallback; }
inline double validRate(double x) { return std::isfinite(x) && x >= 8000 && x <= 384000 ? x : 48000; }
inline float dbToLin(float db) { return db <= -90 ? 0 : std::pow(10.0f, db * .05f); }
inline float lerp(float a, float b, float t) { return a + (b-a)*t; }
inline uint64_t splitmix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}
inline float cellRandom(uint32_t seed, int64_t cell, uint32_t salt)
{
    const auto x = uint64_t(seed) * 0x9E3779B97F4A7C15ULL
                 ^ (uint64_t(cell) + 1) * 0xD1B54A32D192ED03ULL
                 ^ uint64_t(salt) * 0x2545F4914F6CDD1DULL;
    return float((splitmix64(x) >> 40) * (1.0 / 16777216.0));
}
enum class Category { movement, breath };
struct SampleClip { std::vector<float> data; double sourceRate = 48000; };
struct SampleBank { std::vector<SampleClip> movements, breaths; };
struct Params { float movement = .4f, levelDb = -6; };
inline Params sanitize(Params p)
{
    p.movement = finiteClamp(p.movement, 0, 1, .4f);
    p.levelDb = finiteClamp(p.levelDb, -60, 12, -6);
    return p;
}
inline float cosineFade(double x)
{ return .5f - .5f * float(std::cos(3.141592653589793 * std::clamp(x, 0.0, 1.0))); }

// Runs once on loading, never in processBlock. Preserve source WAVs. A bidirectional
// peak envelope anticipates isolated impulses; smooth gain reduces their crest before
// bounded active-RMS matching. Breaths deliberately sit 6 dB below movements.
inline void conditionClip(SampleClip& clip, Category category)
{
    auto& x = clip.data;
    if (x.empty()) return;
    const double rate = validRate(clip.sourceRate);
    float peak = 0;
    for (auto& s : x) { if (!std::isfinite(s)) s = 0; peak = std::max(peak, std::abs(s)); }
    if (peak < 1.0e-8f) { std::fill(x.begin(), x.end(), 0); return; }
    double energy = 0; size_t active = 0;
    for (float s : x) { energy += double(s)*s; ++active; }
    const float rms = float(std::sqrt(energy / double(std::max<size_t>(1, active))));
    const float threshold = std::max(1.0e-8f, rms * 2.8f);
    std::vector<float> envelope(x.size());
    const float ahead = float(std::exp(-1.0 / (.025 * rate)));
    float e = 0;
    for (size_t i = x.size(); i-- > 0;) { e = std::max(std::abs(x[i]), e*ahead); envelope[i] = e; }
    const float release = float(std::exp(-1.0 / (.090 * rate)));
    const float smoothing = float(1-std::exp(-1.0/(.004*rate)));
    float gain = 1; e = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        e = std::max(envelope[i], e*release);
        const float desired = e > threshold ? std::pow(threshold/e, .75f) : 1;
        gain += (desired-gain)*smoothing;
        const double fadeIn = std::min(.035*rate, double(x.size())*.2);
        const double fadeOut = std::min(.080*rate, double(x.size())*.25);
        x[i] *= gain * cosineFade(double(i)/std::max(1.0,fadeIn))
                     * cosineFade(double(x.size()-1-i)/std::max(1.0,fadeOut));
    }
    peak = 0; for (float s : x) peak = std::max(peak,std::abs(s));
    energy = 0; active = 0;
    for (float s : x) if (std::abs(s)>peak*.032f) { energy += double(s)*s; ++active; }
    const float shapedRms = float(std::sqrt(energy/double(std::max<size_t>(1,active))));
    const float target = dbToLin(category == Category::movement ? -30.f : -36.f);
    const float makeup = std::min({target/std::max(shapedRms,1.0e-8f),
                                  dbToLin(-16.f)/std::max(peak,1.0e-8f), dbToLin(36.f)});
    for (auto& s : x) s *= makeup;
}

// A local-priority point process: an eligible cell wins only if no higher-priority
// eligible neighbour lies within the category's minimum distance. This is a pure,
// bounded local query, including across former 90-second boundaries. No epoch resets,
// RNG history, allocation or locks. Voices use absolute sample offsets, so seeks and
// loops reconstruct the same tails as uninterrupted playback at constant parameters.
class Engine
{
public:
    static constexpr double kCellSeconds = .1;
    static constexpr float kMinGapMovementSec = .32f, kMinGapBreathSec = 12.f;
    static constexpr float kLevelSmoothMs = 60;
    static constexpr int kMaxVoices = 32;
    struct Event { int64_t cell = -1, start = 0; Category category = Category::movement; int index = 0; float gain = 1; double speed = 1, duration = 0; };
    void prepare(double rate) { sampleRate=validRate(rate); cellSamples=std::max<int64_t>(1,std::llround(sampleRate*kCellSeconds)); levelCoeff=float(1-std::exp(-1/(.060*sampleRate))); reset(); }
    void setSeed(uint32_t value) { seed=value; reset(); }
    void setBank(std::shared_ptr<const SampleBank> value)
    {
        bank=std::move(value); maxDuration=0;
        if (bank) for (const auto* pool : {&bank->movements,&bank->breaths})
            for (const auto& c : *pool) maxDuration=std::max(maxDuration,double(c.data.size())/validRate(c.sourceRate)/.985);
        reset();
    }
    void reset() { voices={}; lastCell=-1; expectedPosition=-1; level=-1; activeMask=0; }
    int getActiveMask() const { return activeMask; }
    double getSampleRate() const { return sampleRate; }
    // Safe for a UI-owned preview Engine sharing only the immutable bank.
    bool eventAt(int64_t cell, float activity, Event& out) const
    {
        if (cell<0 || !bank) return false;
        const Category cat=categoryAt(cell);
        const auto& pool=cat==Category::movement ? bank->movements : bank->breaths;
        if (pool.empty() || !eligible(cell,activity)) return false;
        const int64_t start=startAt(cell);
        const double gap=(cat==Category::movement ? double(kMinGapMovementSec) : double(kMinGapBreathSec))*sampleRate;
        const int neighbours=int(std::ceil(gap/double(cellSamples)))+1;
        const float priority=cellRandom(seed,cell,7);
        for (int d=-neighbours; d<=neighbours; ++d)
        {
            const int64_t other=cell+d;
            if (!d || other<0 || categoryAt(other)!=cat || !eligible(other,activity)) continue;
            if (std::abs(double(startAt(other)-start))>=gap) continue;
            const float op=cellRandom(seed,other,7);
            if (op<priority || (op==priority && other<cell)) return false;
        }
        out.cell=cell; out.start=start; out.category=cat;
        out.index=std::min(int(pool.size())-1,int(cellRandom(seed,cell,2)*float(pool.size())));
        out.gain=dbToLin(lerp(-1.5f,1.5f,cellRandom(seed,cell,4)));
        out.speed=double(lerp(.985f,1.015f,cellRandom(seed,cell,5)));
        const auto& clip=pool[size_t(out.index)];
        out.duration=double(clip.data.size())/validRate(clip.sourceRate)/out.speed;
        return true;
    }
    void process(float* left,float* right,int count,const Params& input,int64_t start)
    {
        const auto p=sanitize(input); const float target=dbToLin(p.levelDb);
        const bool jumped=start!=expectedPosition;
        if (jumped) { voices={}; lastCell=-1; level=target; }
        if (level<0) level=target;
        activeMask=0;
        for (int i=0;i<count;++i)
        {
            const int64_t pos=start+i;
            if (pos<0) {left[i]=right[i]=0; continue;}
            const int64_t cell=pos/cellSamples;
            if (cell!=lastCell)
            {
                if (lastCell<0) reconstruct(pos,p.movement);
                else { Event ev; if(eventAt(cell,p.movement,ev)) add(ev,pos); }
                lastCell=cell;
            }
            float mono=0;
            for (auto& voice:voices)
            {
                if (!voice.used) continue;
                const auto& ev=voice.event;
                const auto& pool=ev.category==Category::movement ? bank->movements : bank->breaths;
                const auto& clip=pool[size_t(ev.index)];
                const double offset=double(pos-ev.start);
                const double duration=ev.duration*sampleRate;
                if(offset>=duration) { voice.used=false; continue; }
                if(offset<0) continue;
                const double read=offset*validRate(clip.sourceRate)/sampleRate*ev.speed;
                const float envelope=cosineFade(offset/(.025*sampleRate))*cosineFade((duration-1-offset)/(.065*sampleRate));
                mono+=readCubic(clip.data,read)*ev.gain*envelope;
                activeMask|=ev.category==Category::movement ? 1 : 2;
            }
            level+=(target-level)*levelCoeff;
            mono*=level;
            // Continuous soft knee, no hard clipping even with dense overlaps at +12 dB.
            const float magnitude=std::abs(mono);
            if(magnitude>.5f) mono=std::copysign(.5f+.39f*std::tanh((magnitude-.5f)/.39f),mono);
            left[i]=right[i]=mono;
        }
        expectedPosition=start+count;
    }
private:
    struct Voice { bool used=false; Event event; };
    Category categoryAt(int64_t cell) const { return cellRandom(seed,cell,1)<.075f ? Category::breath : Category::movement; }
    bool eligible(int64_t cell,float activity) const
    {
        const float m=finiteClamp(activity,0,1,.4f);
        const float probability=categoryAt(cell)==Category::breath ? lerp(.018f,.065f,m) : lerp(.006f,.95f,std::pow(m,2.4f));
        return cellRandom(seed,cell,0)<probability;
    }
    int64_t startAt(int64_t cell) const { return cell*cellSamples+int64_t(cellRandom(seed,cell,3)*float(cellSamples)*.8f); }
    void add(const Event& ev,int64_t pos)
    {
        if(double(pos-ev.start)>=ev.duration*sampleRate) return;
        for(auto& v:voices) if(!v.used)
        {
            v.used=true;v.event=ev;
            // Stable chronological summation also makes floating-point overlap sums
            // bit-identical after reconstruction, regardless of former slot reuse.
            std::sort(voices.begin(),voices.end(),[](const Voice& a,const Voice& b) {
                if(a.used!=b.used) return a.used;
                return a.used && a.event.cell<b.event.cell;
            });
            return;
        }
    }
    void reconstruct(int64_t pos,float activity)
    {
        voices={}; const int64_t cell=pos/cellSamples;
        const int64_t lookback=int64_t(std::ceil(maxDuration/kCellSeconds))+1;
        for(int64_t c=std::max<int64_t>(0,cell-lookback);c<=cell;++c) {Event ev;if(eventAt(c,activity,ev))add(ev,pos);}
    }
    static float readCubic(const std::vector<float>& y,double pos)
    {
        if(y.empty()) return 0;
        const int n=int(y.size()),i=int(pos);const float f=float(pos-double(i));
        auto at=[&](int k){return y[size_t(std::clamp(k,0,n-1))];};
        const float a=at(i-1),b=at(i),c=at(i+1),d=at(i+2);
        return ((.5f*(d-a)+1.5f*(b-c))*f+(a-2.5f*b+2*c-.5f*d))*f*f+.5f*(c-a)*f+b;
    }
    std::shared_ptr<const SampleBank> bank;
    std::array<Voice,kMaxVoices> voices {};
    uint32_t seed=0x4D6F7665u;
    double sampleRate=48000,maxDuration=0;
    int64_t cellSamples=4800,lastCell=-1,expectedPosition=-1;
    float level=-1,levelCoeff=.0003f;
    int activeMask=0;
};
} // namespace mv

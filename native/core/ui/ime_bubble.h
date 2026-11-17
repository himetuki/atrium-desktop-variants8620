// The one-shot IME bubble (issue #56, demo): while the bar of the display a
// window types in is hidden, a change of the input method or its Chinese/
// English state announces itself as a liquid glass bubble at the bottom of
// that display — it swells in, drifts up a little, bursts into foam and is
// gone. Same material and adaptive ink as every popup (the bar's render
// thread draws the glass, the bubble itself being several pieces like the
// stage's drag carrier); the window never takes focus or input.
//
// Quick successive changes merge the bubble rather than restart it, as real
// bubbles do: each merge is one step larger (a logarithmic size in the number
// of merges, up to ten — a volume argument in spirit, the asked-for log in
// shape), bounces softly with a springy overshoot, pushes its pop back, and
// the bubble shows only the newest state. A change never queues behind the
// last one: it launches its own newcomer at once, so a rapid run rises as
// several small bubbles side by side and fuses them into the skin together.
#pragma once

#include "clock.h"
#include "motion.h"
#include "shell/ime_status.h"
#include "ui/surface.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace atrium {

// The feel of the merge, tunable from the settings UI: how deeply the skin
// compresses at a hit, how far the spring rebounds, the liquid wobble's base
// amplitude and rhythm, how long one bubble lives, the size the first bubble is
// born at and the one ten merges reach, where the two kinds of bubble are born
// and how a newcomer's absorb divides into its three acts.
// The defaults are the original shape, so an untuned bubble behaves as before.
struct ImeBubbleTuning {
    float compression = 0.10f; // the hit squashes the skin by this fraction
    float rebound = 0.31f;     // the spring passes its target by this much
    float wobbleAmp = 0.875f;  // the liquid wobble's base amplitude
    float wobbleHz = 4.3f;     // the liquid wobble's base rhythm, Hz
    float lifeMs = 1060.f;     // how long the bubble lives before it bursts (kEmergeMs + kFloatMs)
    float firstBirthDip = 18;  // the first bubble is born this far below its resting centre, DIP
    float lobeBirthDip = 10;   // a newcomer's centre is born this far below the window's bottom, DIP
    float lobeRiseMs = 380;    // the newcomer's slow rise, ms
    float lobeAttachMs = 243;  // its fast approach into the rim, ms
    float lobeSwallowMs = 137; // the swallow into the skin, ms
    // The bubble's own size, DIP of radius: the first bubble's and the one ten
    // merges reach. The merges in between are interpolated in the merge count's
    // logarithm, the same shape the size has always had.
    float startRadius = 32;
    float endRadius = 51;
};

// The bubble's one-shot timeline, all analytic: every frame is a pure function
// of the time since Begin and the merges drawn at Fuse (the burst's randomness
// is drawn once, at Begin and at each merge), so a late frame lands where a
// punctual one would. DIP and ms. Emerge: it swells up from below with a
// springy overshoot. Float: it drifts up a little and breathes. Pop: one more
// swell of pressure, then the skin tears at its rim into foam — a handful of
// drops and a dust of smaller ones, every pop of its own sizes, directions,
// wobble and pace, slowing, drooping and dissolving.
class ImeBubbleModel {
public:
    static constexpr int kW = 320, kH = 240; // the window around it, DIP
    static constexpr float kCx = kW / 2.f, kCyRest = 172; // the resting centre
    static constexpr float kRadius = 32;     // the nominal bubble the model is laid out around (ScaleAt's base)
    static constexpr float kFloatRise = 10; // DIP further up during the float
    static constexpr unsigned kEmergeMs = 360, kFloatMs = 700, kSwellMs = 46, kBurstMs = 440;
    static constexpr unsigned kPopAt = kEmergeMs + kFloatMs; // the first possible pop
    static constexpr int kMaxFusions = 10;   // past this the size stops growing
    // The newcomer's absorb, in three acts: it floats up slowly from off the
    // screen to a hover point below the skin, rushes the last stretch into the
    // rim, then is drawn in. The three lengths are the tuning's lobeRiseMs,
    // lobeAttachMs and lobeSwallowMs (380 + 243 + 137 ms by default); where in
    // the absorb its rim meets the skin, from there it presses the skin wider
    // and rocks it: the touch is rise + attach into the whole absorb.
    static constexpr double kDipSpan = 0.20; // the hit's squash decays over this share of the absorb
    static constexpr double kWobbleMs = 900; // the skin's liquid wobble after a merge
    // How many newcomers one frame carries. A rapid run may launch more at once
    // than an eye can tell apart; a frame shows the newest ones.
    static constexpr int kMaxFrameLobes = 6;
    // The pieces of glass one shape may melt (kMaxGlassParts in
    // render/glass_renderer.h, which the test asserts): the skin's one or two
    // and the drawn newcomers, all in one body.
    static constexpr int kMaxMeltParts = 6;
    // The resting geometry, in model DIP. A newcomer is no longer born on a
    // line inside the window: its centre is born below the window's bottom edge
    // (the tuning's lobeBirthDip past it) and floats up into the frame. A lone
    // bubble throws 9..13 drops; a fully merged one (ten merges) throws the cap,
    // so a big bubble's foam reads as plainly more than a small one's.
    static constexpr int kMinBeads = 9, kMaxBeads = 30;

    // What one frame's melt publishes: the skin as one piece or two (squashed
    // across the impact), and how many of the newest newcomers fit beside it.
    struct MeltBudget {
        int skinParts = 1;
        int lobes = 0;
    };
    static MeltBudget MeltFor(int lobeCount, bool split)
    {
        const int n = std::clamp(lobeCount, 0, kMaxFrameLobes);
        MeltBudget b;
        if (split && n <= kMaxMeltParts - 2) {
            b.skinParts = 2;
            b.lobes = n;
        } else {
            b.lobes = std::min(n, kMaxMeltParts - 1); // the split dropped: one more slot for a newcomer
        }
        return b;
    }
    static int DrawnLobes(int lobeCount, bool split) { return MeltFor(lobeCount, split).lobes; }

    // How many melt shapes a run of drops needs. The glass renderer melts at
    // most kMaxMeltParts parts per shape and drops the rest silently, so a swarm
    // larger than that is published as several shapes, each carrying up to six
    // drops; this is the count of them, ceil(parts / kMaxMeltParts). Nought
    // drops need nought shapes. A pure function, so the count is testable
    // without a device (the paint groups the drops by it).
    static int ChunksFor(int parts)
    {
        return parts <= 0 ? 0 : (parts + kMaxMeltParts - 1) / kMaxMeltParts;
    }

    // The number of drops a burst throws: the base swarm, plus more from a
    // bubble that merged to a larger size — its skin holds more liquid — less
    // from a bubble set smaller than the nominal, but never under the base.
    // `roll` (0..4) is the burst's own temper.
    int BeadsFor(int fusions, int roll) const
    {
        const int base = kMinBeads + std::min(roll, 4);
        return std::clamp(base + int((ScaleAt(fusions) - 1) * 40), kMinBeads, kMaxBeads); // +-0..24
    }
    // The swarm's spread: faster and wider with more drops, and every burst
    // has its own temper (0.88..1.12).
    static float SpreadFor(int beads, float temper)
    {
        const float more = float(beads - kMinBeads) / float(kMaxBeads - kMinBeads);
        return temper * (1 + 0.25f * std::clamp(more, 0.f, 1.f)); // up to 1.25
    }

    // The radius after `fusions` merges, DIP: the tuning's startRadius at the
    // first bubble, its endRadius at ten, and the merges between interpolated
    // in the count's logarithm (a real merge adds volume, so the growth tapers
    // instead of running away). Never past either end.
    float RadiusAt(int fusions) const
    {
        const float t = std::log(float(std::clamp(fusions, 1, kMaxFusions))) / std::log(float(kMaxFusions));
        return tuning_.startRadius + (tuning_.endRadius - tuning_.startRadius) * t;
    }
    // The same curve as the scale the glass and the letter ride: 1 at the first
    // bubble when the start is the nominal kRadius, and endRadius/kRadius at ten.
    float ScaleAt(int fusions) const { return RadiusAt(fusions) / kRadius; }

    // The chosen feel; a Begin or a Cancel keeps it across the restart. The two
    // sizes are kept sane here too: a radius of nothing, or an end below the
    // start, would fold the curve on itself.
    void SetTuning(const ImeBubbleTuning& t)
    {
        tuning_ = t;
        tuning_.startRadius = std::max(1.f, t.startRadius);
        tuning_.endRadius = std::max(tuning_.startRadius, std::max(1.f, t.endRadius));
    }

    // The newcomer's timeline, derived from the tuning's three acts. A zero
    // length is not a stage of nothing but is skipped by its own fraction, and
    // the absorb never falls to nothing, so no stage divides by zero. The touch
    // — where the rim meets the skin — is the end of the fast approach, and at
    // most one millisecond short of the swallow's end so the skin never has to
    // read a touch past its own absorb.
    double RiseEndMs() const { return std::max(0.f, tuning_.lobeRiseMs); }
    double AttachEndMs() const { return TouchMs(); }
    double AbsorbMs() const { return std::max(60.0, RiseEndMs() + std::max(0.f, tuning_.lobeAttachMs) +
                                                       std::max(0.f, tuning_.lobeSwallowMs)); }
    double TouchMs() const { return std::min(RiseEndMs() + std::max(0.f, tuning_.lobeAttachMs), AbsorbMs() - 1); }
    double TouchFrac() const { return TouchMs() / AbsorbMs(); }

    // 0 -> 1 with a decaying springy overshoot, all but settled by 900 ms. The
    // rebound is how far the first swing passes its target (0.31: about 31 %,
    // the old feel); the decay follows from it, so 0.31 gives the old
    // exp(-6 t) cos(16 t). Used both for the size and tested as the feel.
    static float softBounce(double ms, float rebound)
    {
        const double r = std::clamp(double(rebound), 0.02, 0.6);
        const double k = std::max(2.0, -16.0 * std::log(r) / 3.14159265358979323846);
        const double t = ms / 1000;
        return 1 - float(std::exp(-k * t) * std::cos(16 * t));
    }

    struct Bead {
        float x, y, r;
        float glint; // 0..1: its last sparkle, just before it dissolves
    };
    // One merge's newcomer as this frame draws it: its place and size, and how
    // far its absorb has come (0: just launched, 1: swallowed).
    struct LobeFrame {
        float x = 0, y = 0, r = 0, u = 0;
    };
    struct Frame {
        float cx = kCx, cy = kCyRest, scale = 1, grow = 1, opacity = 0;
        // The merges in progress, oldest first: every newcomer still rising or
        // being swallowed (0 of them: no merge now), and the skin's liquid
        // wobble — the sum of their own rhythms.
        LobeFrame lobes[kMaxFrameLobes];
        int lobeCount = 0;
        float wob = 0;
        Bead beads[kMaxBeads]{};
        int beadCount = 0, foamCount = 0; // the mist trails after the foam beads
        float beadOpacity = 0, mistOpacity = 0;
        bool active = false, animating = false;
    };

    bool active() const { return active_; }
    int fusions() const { return fusions_; }
    // The bubble is out there and has not begun to burst: a newer state can
    // still merge into it.
    bool Mergeable(double now) const { return active_ && std::isfinite(now) && now >= since_ && now < popAt_; }
    double PopAt() const { return popAt_; }
    double EndMs() const { return popAt_ + kSwellMs + kBurstMs; }

    void Begin(double now)
    {
        const ImeBubbleTuning tuning = tuning_; // the chosen feel survives a restart
        *this = ImeBubbleModel{};
        tuning_ = tuning;
        since_ = std::isfinite(now) && now > 0 ? now : 0;
        if (!since_)
            return;
        active_ = true;
        popAt_ = since_ + tuning_.lifeMs;
        fusions_ = 1;
        rng_.seed(static_cast<unsigned>(since_) ^ 0x9E3779B9u);
        PlanBurst();
    }

    // A newer state arrived while the bubble lives: it merges — one step
    // larger (up to ten), its burst re-rolled for the new size, its pop pushed
    // back so the newest state stays readable. Every merge is a newcomer of its
    // own, launched at once: a rapid run shows several rise side by side and
    // fuse together, instead of waiting for the last one to be swallowed.
    // False once the burst has begun.
    bool Fuse(double now)
    {
        if (!Mergeable(now))
            return false;
        ++fusions_;
        popAt_ = std::max({popAt_, now + double(tuning_.lifeMs), now + AbsorbMs()});
        lobes_.push_back({now, kCx + U(-50.f, 50.f), // where it is born, below the window
                          tuning_.wobbleHz * U(0.85f, 1.15f),  // its own liquid rhythm
                          tuning_.wobbleAmp * U(0.85f, 1.15f), // and strength
                          fusions_});                          // the size its touch adds
        PlanBurst();
        return true;
    }

    void Cancel()
    {
        const ImeBubbleTuning tuning = tuning_;
        *this = ImeBubbleModel{};
        tuning_ = tuning;
    }

    Frame Step(double now)
    {
        if (!active_ || !std::isfinite(now))
            return {};
        const double t = now - since_;
        if (t < 0 || now >= EndMs())
            return {};
        const double popRel = popAt_ - since_; // where the pop sits in the life
        // The newcomer's absorb and its two stage boundaries, from the tuning:
        // [0, uRise) the slow rise, [uRise, uAttach) the fast approach into the
        // rim, [uAttach, 1) the swallow.
        const double absorb = AbsorbMs();
        const double uRise = RiseEndMs() / absorb;
        const double uAttach = TouchFrac();
        Frame f;
        f.active = f.animating = true;
        // The merges as one elastic body: every newcomer whose touch has come
        // adds its own step to the size — optimistically, so a newer touch
        // never takes an older step back — and its own liquid rhythm to the
        // wobble. Before any touch the size is the base.
        float grow = ScaleAt(1), wob = 0;
        for (const auto& l : lobes_) {
            const double sinceTouch = now - (l.launch + TouchMs());
            if (sinceTouch < 0)
                continue;
            grow += (ScaleAt(l.step) - ScaleAt(l.step - 1)) * softBounce(sinceTouch, tuning_.rebound);
            if (sinceTouch < kWobbleMs)
                wob += WobbleAt(sinceTouch, l.amp, l.freq);
        }
        const float wobLimit = std::max(0.f, 1.15f * tuning_.wobbleAmp);
        f.grow = grow;
        f.wob = std::clamp(wob, -wobLimit, wobLimit);
        // The centre: up from below during the emerge, then the float carries
        // it a little past its rest, upwards. How far below it is born is the
        // tuning's firstBirthDip.
        float rise = float(tuning_.firstBirthDip) * float(1 - ease(t / kEmergeMs));
        if (t > kEmergeMs)
            rise -= kFloatRise * easeInOut(float((t - kEmergeMs) / kFloatMs));
        f.cy = kCyRest + rise;
        // The size: a springy swell in, a breath while floating, the pressure
        // before the burst — which swells once more, this time for real, and
        // pushes the skin up as it leaves. A merge rides along as `grow`.
        float phase;
        if (t < kEmergeMs)
            phase = 0.5f + 0.5f * motion::EaseOutBack(float(t / kEmergeMs), 1.70158f);
        else if (t < popRel)
            phase = 1 + 0.012f * float(std::sin(6.2831853f * float((t - kEmergeMs) / kFloatMs)));
        else if (t - popRel < kSwellMs)
            phase = 1 + (swellPeak_ - 1) * ease(float(t - popRel) / kSwellMs);
        else
            phase = swellPeak_ + (burstScale_ - swellPeak_) * ease(float((t - popRel - kSwellMs) / 110));
        // The newcomers, oldest first: each rises from the emerge line as a
        // bubble of its own, touches the skin and is swallowed. A rapid run
        // shows them side by side — a frame carries the newest ones — and where
        // one meets the skin it squashes it, the squashes adding up.
        int live = 0;
        for (const auto& l : lobes_)
            live += now >= l.launch && now < l.launch + absorb;
        float dip = 0;
        for (const auto& l : lobes_) {
            if (now < l.launch || now >= l.launch + absorb)
                continue;
            const bool room = live <= kMaxFrameLobes;
            --live;
            if (!room)
                continue; // an older newcomer the frame has no room for
            const float u = float((now - l.launch) / absorb);
            const float rMain = kRadius * grow; // the skin it meets (path/touch geometry keeps using this)
            // Its own size does not follow the skin's growth: every newcomer is born
            // the size of the first newcomer after the initial bubble — the bubble's
            // start radius (ScaleAt(1) · kRadius), not the skin's current radius.
            const float rRef = tuning_.startRadius;
            const float rStart = rRef * 0.30f, rTouch = rRef * 0.55f;
            // Born below the window: its centre the tuning's lobeBirthDip past
            // the window's bottom, so the first frames are clipped and it then
            // drills up out of the screen's very bottom edge.
            const float startX = l.startX, startY = kH + tuning_.lobeBirthDip;
            // The line from the main centre out to the birth point; every point
            // of the journey lies on it, so the newcomer only ever rises.
            float dx = startX - kCx, dy = startY - f.cy;
            const float len = std::max(1.f, std::hypot(dx, dy));
            const float ux = dx / len, uy = dy / len;
            // P1, the hover it floats up to: below the skin, on that line. P2,
            // the rim: their rims meet at touchDist — but never past the birth
            // point, so a big main bubble (which sits near the bottom) cannot
            // push the touch point below the start and off the window.
            const float hoverDist = rMain + rTouch * 2.2f;
            const float hx = kCx + ux * hoverDist, hy = f.cy + uy * hoverDist;
            const float touchDist = std::min(rMain + rTouch * 0.10f, len);
            const float tx = kCx + ux * touchDist, ty = f.cy + uy * touchDist;
            LobeFrame& lobe = f.lobes[f.lobeCount++];
            lobe.u = u;
            if (u < float(uRise)) { // the slow rise: off-screen up to the hover
                const float e = easeInOut(u / float(uRise));
                lobe.x = startX + (hx - startX) * e;
                lobe.y = startY + (hy - startY) * e;
                lobe.r = rStart + (rTouch * 0.8f - rStart) * e;
            } else if (u < float(uAttach)) { // the fast approach: the hover into the rim
                const float e = easeIn((u - float(uRise)) / float(uAttach - uRise));
                lobe.x = hx + (tx - hx) * e;
                lobe.y = hy + (ty - hy) * e;
                lobe.r = rTouch * 0.8f + (rTouch - rTouch * 0.8f) * e;
            } else { // the swallow: into the middle, shrinking away
                const float e = ease((u - float(uAttach)) / float(1 - uAttach));
                lobe.x = tx + (kCx - tx) * e;
                lobe.y = ty + (f.cy - ty) * e;
                lobe.r = rTouch * (1 - 0.96f * e);
            }
            const float d = (u - float(uAttach)) / float(kDipSpan);
            dip += float(std::exp(-d * d)); // the hit compresses the skin
        }
        phase *= 1 - tuning_.compression * std::min(dip, 1.f);
        f.scale = grow * phase;
        if (t < popRel)
            f.opacity = t < 220 ? ease(float(t) / 220) : 1;
        else {
            const float tb = float((t - popRel - kSwellMs) / 1000); // s since the skin gave way
            f.opacity = 1 - ease(tb * 1000 / 120);
            f.cy -= 3 * ease(tb * 1000 / 120);
            Tear(tb, grow, f);
        }
        return f;
    }

private:
    // One merge's newcomer: when it leaves the emerge line, where, its own
    // liquid rhythm and strength, and the size step its touch adds (fusions_
    // after that merge). Launches ascend, one per merge; several may be in
    // flight at once.
    struct Lobe {
        double launch;
        float startX, freq, amp;
        int step;
    };
    // One drop's own character, drawn when the burst begins: where it leaves
    // the skin, how fast and far it flies, how it wobbles, when it dies.
    struct BeadPlan {
        float deg, speed, r0, life, wobbleAmp, wobbleHz, phase, droopAfter;
    };

    void PlanBurst()
    {
        beadCount_ = std::max(kMinBeads, BeadsFor(fusions_, int(rng_() % 5)));
        spread_ = SpreadFor(beadCount_, U(0.88f, 1.12f));
        foamCount_ = std::clamp(beadCount_ - (2 + int(rng_() % 3)), 5, beadCount_ - 2); // the rest is mist
        auto plan = [&](float degA, float degB, float speedA, float speedB, float rA, float rB, float lifeA, float lifeB) {
            BeadPlan b;
            b.deg = U(degA, degB);
            b.speed = U(speedA, speedB);
            b.r0 = U(rA, rB);
            b.life = U(lifeA, lifeB);
            b.wobbleAmp = U(0.8f, 3.2f);
            b.wobbleHz = U(6, 13);
            b.phase = U(0, 6.2831853f);
            b.droopAfter = U(0.08f, 0.16f);
            return b;
        };
        // A jet at the top (the liquid spike of a real pop), a few drops, then mist.
        beads_[kJet] = plan(-104.f, -76.f, 175.f, 215.f, 5.0f, 6.8f, 330.f, 420.f);
        for (int i = 1; i < foamCount_; ++i)
            beads_[i] = plan(-175.f, -5.f, i < 4 ? 115.f : 80.f, i < 4 ? 170.f : 130.f, i < 4 ? 4.0f : 2.6f,
                             i < 4 ? 5.6f : 4.0f, 300.f, 420.f);
        for (int i = foamCount_; i < beadCount_; ++i)
            beads_[i] = plan(-175.f, -5.f, 45.f, 105.f, 1.1f, 2.2f, 170.f, 290.f);
        swellPeak_ = U(1.15f, 1.22f);
        burstScale_ = U(1.42f, 1.58f);
        jitterX_ = U(-2, 2);
        jitterY_ = U(-1.5f, 1.5f);
    }

    void Tear(float tb, float grow, Frame& f)
    {
        const float rSkin = kRadius * grow * swellPeak_; // where the skin stood when it burst
        const float bx = kCx + jitterX_, by = kCyRest - kFloatRise + jitterY_;
        const float glide = std::min(tb, kGlideS);
        const float travel = glide - glide * glide / (2 * kGlideS); // v*t with a constant deceleration
        const float size = (0.75f + 0.25f * grow) * spread_; // a bigger, fuller swarm scatters further
        f.beadCount = beadCount_;
        f.foamCount = foamCount_;
        for (int i = 0; i < beadCount_; ++i) {
            const auto& b = beads_[i];
            const float ms = tb * 1000;
            const float u = std::clamp(ms / b.life, 0.f, 1.f); // its own lifetime, 0..1
            const float rad = b.deg * (3.1415926f / 180);
            // The swarm scatters a touch wider sideways than along the pop's own
            // vertical: a drop flying level (or at any angle off straight up)
            // carries this extra reach, while the jet at the top keeps its own
            // pace (it already reaches the window's top edge). 1.20 level,
            // 1.06 at 45 degrees, 1.00 straight up.
            const float dirSpread = 1 + 0.20f * (1 - std::abs(std::sin(rad)));
            const float away = rSkin + b.speed * dirSpread * size * travel; // drops leave from the rim, not the middle
            const float wob = std::sin(6.2831853f * b.wobbleHz * tb + b.phase) * b.wobbleAmp * float(1.0 - u);
            f.beads[i].x = bx + float(std::cos(rad) * away - std::sin(rad) * wob);
            f.beads[i].y = by + float(std::sin(rad) * away + std::cos(rad) * wob) +
                           kDroop * std::max(0.f, tb - b.droopAfter) * std::max(0.f, tb - b.droopAfter);
            // It thins away to nothing over its own life — never blinks off.
            f.beads[i].r = u >= 1 ? 0 : b.r0 * grow * (1 - u * u);
            // And its last sparkle: the final 30 % of its life.
            const float g = (u - 0.70f) / 0.30f;
            f.beads[i].glint = (u >= 0.70f && u < 1) ? std::sin(3.1415926f * std::clamp(g, 0.f, 1.f)) : 0.f;
        }
        // The foam lives longer than the mist dissolving behind it.
        f.beadOpacity = 1 - ease(tb * 1000 / 420);
        f.mistOpacity = 1 - ease(tb * 1000 / 300);
    }

    static float ease(double t)
    {
        const float p = float(std::clamp(t, 0.0, 1.0));
        return 1 - (1 - p) * (1 - p) * (1 - p);
    }
    // Accelerating: still at first, quickest at the end (the rush into the rim).
    static float easeIn(double t)
    {
        const float p = float(std::clamp(t, 0.0, 1.0));
        return p * p * p;
    }
    static float easeInOut(double t)
    {
        const float p = float(std::clamp(t, 0.0, 1.0));
        return p < 0.5f ? 2 * p * p : 1 - float(std::pow(-2 * p + 2, 2)) / 2;
    }
    // One newcomer's own liquid rhythm, `ms` after its touch: a decaying sine
    // at its own pace and strength. The frame's wobble is the sum of these.
    static float WobbleAt(double ms, float amp, float freqHz)
    {
        return amp * float(std::exp(-ms / 260.0)) * float(std::sin(6.2831853f * freqHz * float(ms / 1000)));
    }

    float U(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng_); }

    static constexpr float kGlideS = 0.55f;      // the drops slow to a stop in this long
    static constexpr float kDroop = 240;         // DIP/s²: gravity's pull
    static constexpr int kJet = 0;               // beads_[0] is the pop's spike
    ImeBubbleTuning tuning_; // the chosen feel; kept across Begin and Cancel
    double since_ = 0, popAt_ = 0;
    bool active_ = false;
    int fusions_ = 1;
    float spread_ = 1;
    std::vector<Lobe> lobes_; // one per merge, launches ascending
    std::mt19937 rng_;
    BeadPlan beads_[kMaxBeads];
    int beadCount_ = 0, foamCount_ = 0;
    float swellPeak_ = 1.18f, burstScale_ = 1.5f, jitterX_ = 0, jitterY_ = 0;
};


// The surface: a small fixed window at the bottom of the work area, whose
// content is the bubble's letter and whose glass follows the model frame by
// frame. It streams its frames (no waiting for the glass layer's present), so
// a stalled render thread can never freeze it, and a watchdog takes it down if
// its frames stop anyway — nothing stays stuck on the screen. Lives on the UI
// thread.
class ImeBubble {
public:
    explicit ImeBubble(HINSTANCE instance) : bubble_(instance, L"Atrium IME Bubble", false, 0)
    {
        bubble_.SetGlassLockstep(false); // a transient animation: stream, never wait
        bubble_.paint = [this](Canvas& c) { Paint(c); };
        bubble_.message = [this](UINT m, WPARAM w, LPARAM, LRESULT& result) {
            if (m == WM_TIMER && w == kEndTimer) {
                const double now = ClockMs();
                if (!model_.Step(now).active)
                    Stop(); // outside Paint; a stale timer cannot cut a newer bubble short
                else
                    ArmEndTimer(now); // a merge pushed the pop back after the timer was armed
                return true;
            }
            if (m != WM_NCHITTEST)
                return false;
            result = HTTRANSPARENT;
            return true;
        };
    }

    bool Visible() const { return bubble_.Visible(); }

    // The chosen merge feel: applied to the live model too, and kept for the
    // next Begin.
    void SetTuning(const ImeBubbleTuning& t)
    {
        tuning_ = t;
        model_.SetTuning(t);
    }

    // The freshly settled state: merges with a live bubble (one step larger,
    // newest state shown), or starts a new one. `bottomSpacingDip` is the bar's
    // margin (kept across the call) and `screenBottom` the display's physical
    // bottom edge: the window's bottom edge sits there, so the resting bubble
    // hangs just above the screen's bottom and a newcomer can be born off the
    // screen below it. `bottomSpacingDip` is kept for the settings round trip.
    void Show(const ImeStatus& status, RECT work, float bottomSpacingDip = 10, int screenBottom = 0)
    {
        status_ = status;
        glyph_ = ImeGlyph(status); // the OS answers for this language, once per announce
        const bool moved = !EqualRect(&work_, &work) || bottomSpacingDip != bottomSpacing_ ||
                           screenBottom != screenBottom_;
        work_ = work;
        bottomSpacing_ = bottomSpacingDip;
        screenBottom_ = screenBottom;
        const double now = ClockMs();
        if (!bubble_.Visible() || !model_.Fuse(now)) {
            model_.Begin(now);
            const RECT rect = RectFor();
            if (bubble_.Visible())
                bubble_.Move(rect);
            else
                bubble_.Show(rect, false, POINT{(rect.left + rect.right) / 2, rect.bottom}, false);
        } else if (moved) {
            bubble_.Move(RectFor()); // the display or the spacing changed with the bubble up
        }
        bubble_.Invalidate();
        ArmEndTimer(now);
    }

    // Self-healing, from the probe's heartbeat: a bubble that outlived its
    // model, or one whose frames stopped (a stalled device or render thread),
    // is taken down at once; the next announcement shows again — nothing stays
    // stuck on the screen.
    void Watch(double now)
    {
        if (!bubble_.Visible())
            return;
        const bool ended = now >= model_.EndMs() + 250;
        const bool stalled = model_.active() && lastPaint_ > 0 && now - lastPaint_ > 500;
        if (!ended && !stalled)
            return;
        Stop();
    }

private:
    static constexpr UINT_PTR kEndTimer = 1;

    // The window: centred on the display, its bottom edge on the display's own
    // physical bottom (`screenBottom`, from GetMonitorInfo's rcMonitor.bottom),
    // tall enough for the whole model (kH*scale) and growing upward from there.
    // The resting bubble therefore hangs just above the screen's bottom edge,
    // and a newcomer can be born below the window and rise up out of the screen.
    RECT RectFor() const
    {
        const float scale = PopupScale(work_);
        const int w = int(std::lround(ImeBubbleModel::kW * scale)), h = int(std::lround(ImeBubbleModel::kH * scale));
        const int bottom = screenBottom_;
        return RECT{(work_.left + work_.right - w) / 2, bottom - h,
                    (work_.left + work_.right + w) / 2, bottom};
    }

    void ArmEndTimer(double now)
    {
        const double ms = model_.EndMs() - now + 60;
        SetTimer(bubble_.Hwnd(), kEndTimer, UINT(std::max(60.0, ms)), nullptr);
    }

    void Stop()
    {
        KillTimer(bubble_.Hwnd(), kEndTimer);
        model_.Cancel();
        bubble_.Hide(false); // the content is already transparent; no second animation
        lastPaint_ = 0;
    }

    void Paint(Canvas& c)
    {
        lastPaint_ = c.now;
        const auto f = model_.Step(c.now);
        if (!f.active) { // done: every piece of glass gone; the end timer or the watchdog hides the window
            // A 1x1 faded shape, not an empty rectangle: the glass renderer
            // reads a degenerate shape as "the whole window" and would flash
            // a panel-sized rectangle.
            std::vector<ShapeDip> gone{ShapeDip{D2D1::RectF(0, 0, 1, 1), 0}};
            gone[0].fade = 1;
            bubble_.SetShapes(gone);
            return;
        }
        // The bubble as one circle, then the burst as two fusing pieces — the
        // foam drops and the mist behind them. Only shapes with real geometry
        // are published: an empty one would be read as the whole window by the
        // glass renderer (the square flash), and the groups never fade as a
        // whole anyway (each drop dies by thinning its own radius to zero).
        const float r = ImeBubbleModel::kRadius * f.scale;
        // A still, whole skin — no newcomer, no wobble, no burst yet: its glass
        // is published rounded to whole DIPs, so the frames of the slow float
        // are byte-identical and the bar's render thread skips that glass
        // Present. Frames that merge, wobble or burst are never rounded.
        const float wob = std::abs(f.wob);
        const bool steady = f.lobeCount == 0 && f.beadCount == 0 && f.opacity >= 1 && wob < 0.04f;
        std::vector<ShapeDip> shapes;
        shapes.reserve(3);
        ShapeDip main{};
        const auto budget = ImeBubbleModel::MeltFor(f.lobeCount, wob > 0.04f);
        if (budget.lobes > 0 || budget.skinParts == 2) {
            // The merge, as one liquid body: the skin squashed wider by its
            // wobble (two melt-fused lobes), the newcomers swallowed into it —
            // the glass renderer's melt keeps them a single blob throughout.
            auto part = [](float px, float py, float rad) {
                ShapeDip::Part p;
                p.rect = D2D1::RectF(px - rad, py - rad, px + rad, py + rad);
                std::fill(std::begin(p.radii), std::end(p.radii), rad);
                return p;
            };
            std::vector<ShapeDip::Part> parts;
            if (budget.skinParts == 2) {
                const float d = r * 0.42f * wob;       // stretch across the impact
                const float rr = r * (1 - 0.10f * wob); // and thin along it
                parts.push_back(part(f.cx - d, f.cy, rr));
                parts.push_back(part(f.cx + d, f.cy, rr));
            } else {
                parts.push_back(part(f.cx, f.cy, r));
            }
            // The newest newcomers are the ones with room in the melt budget.
            for (int i = f.lobeCount - budget.lobes; i < f.lobeCount; ++i)
                if (f.lobes[i].r > 0.5f)
                    parts.push_back(part(f.lobes[i].x, f.lobes[i].y, f.lobes[i].r));
            main.rect = parts.front().rect;
            for (const auto& p : parts) {
                main.rect.left = std::min(main.rect.left, p.rect.left);
                main.rect.top = std::min(main.rect.top, p.rect.top);
                main.rect.right = std::max(main.rect.right, p.rect.right);
                main.rect.bottom = std::max(main.rect.bottom, p.rect.bottom);
            }
            main.parts = std::move(parts);
            main.melt = 12; // a liquid neck, not separate beads
            main.radius = r;
        } else {
            const float rr = steady ? std::round(r) : r;
            const float cx = steady ? std::round(f.cx) : f.cx;
            const float cy = steady ? std::round(f.cy) : f.cy;
            main.rect = D2D1::RectF(cx - rr, cy - rr, cx + rr, cy + rr);
            main.radius = rr;
        }
        main.fade = 1 - f.opacity;
        const D2D1_RECT_F mainRect = main.rect;
        shapes.push_back(std::move(main));
        // The burst's drops, foam first and the mist behind it. The glass
        // renderer melts at most kMaxMeltParts (6) parts per shape and drops
        // the rest without a word, so a bubble whose swarm is larger than that
        // (a merged one throws up to kMaxBeads drops) would show only its first
        // six — and a small bubble then looks exactly as full as a big one.
        // Every full group of drops is published as its own melt shape, so the
        // whole swarm is drawn, and the count finally reads.
        std::vector<ShapeDip::Part> foamParts, mistParts;
        foamParts.reserve(size_t(f.foamCount));
        mistParts.reserve(size_t(f.beadCount - f.foamCount));
        for (int i = 0; i < f.beadCount; ++i) {
            const auto& bead = f.beads[i];
            if (bead.r < 0.35f)
                continue; // dissolved: it thinned away, it did not blink off
            ShapeDip::Part part;
            part.rect = D2D1::RectF(bead.x - bead.r, bead.y - bead.r, bead.x + bead.r, bead.y + bead.r);
            std::fill(std::begin(part.radii), std::end(part.radii), bead.r);
            (i < f.foamCount ? foamParts : mistParts).push_back(part);
        }
        // One melt shape per group of at most kMaxMeltParts drops, its rectangle
        // the union of the group it carries (ChunksFor gives the count).
        auto publish = [&](const std::vector<ShapeDip::Part>& parts, float melt) {
            for (int done = 0; done < ImeBubbleModel::ChunksFor(int(parts.size())); ++done) {
                const int first = done * ImeBubbleModel::kMaxMeltParts;
                const int last = std::min(first + ImeBubbleModel::kMaxMeltParts, int(parts.size()));
                ShapeDip shape{};
                shape.parts.assign(parts.begin() + first, parts.begin() + last);
                shape.rect = parts[size_t(first)].rect;
                for (int i = first; i < last; ++i) {
                    const auto& p = parts[size_t(i)].rect;
                    shape.rect.left = std::min(shape.rect.left, p.left);
                    shape.rect.top = std::min(shape.rect.top, p.top);
                    shape.rect.right = std::max(shape.rect.right, p.right);
                    shape.rect.bottom = std::max(shape.rect.bottom, p.bottom);
                }
                shape.melt = melt;
                shape.fade = 0;
                shapes.push_back(std::move(shape));
            }
        };
        publish(foamParts, 2.5f + (f.grow - 1) * 2); // a larger bubble fuses a larger foam
        publish(mistParts, 1.5f);
        bubble_.SetShapes(shapes, !steady); // a still skin publishes once, not on every frame

        // The letter, over the glass — or a plain stand-in while the glass is on its way.
        const auto theme = c.panel ? Theme::Panel() : c.theme;
        if (c.panel) {
            c.Circle(f.cx, f.cy, r, theme.panel);
            for (int i = 0; i < f.lobeCount; ++i)
                if (f.lobes[i].r > 0.5f)
                    c.Circle(f.lobes[i].x, f.lobes[i].y, f.lobes[i].r, theme.panel);
            c.Stroke(mainRect, r, theme.border);
            for (int i = 0; i < f.beadCount; ++i)
                if (f.beads[i].r >= 0.35f)
                    c.Circle(f.beads[i].x, f.beads[i].y, f.beads[i].r, theme.panel);
        }
        // A drop's last sparkle, as two layers so it reads on light and dark
        // alike: a wide contrasting halo, then a bright white core.
        for (int i = 0; i < f.beadCount; ++i) {
            const auto& bead = f.beads[i];
            if (bead.glint <= 0.02f)
                continue;
            const float radius = std::max(1.8f, bead.r * 0.9f + 0.8f);
            D2D1_COLOR_F haloInk = theme.halo;
            haloInk.a *= 0.35f * bead.glint;
            c.Circle(bead.x, bead.y, radius * 1.7f, haloInk);
            c.Circle(bead.x, bead.y, radius, D2D1::ColorF(1.f, 1.f, 1.f, 0.95f * bead.glint));
        }
        if (f.opacity <= 0.01f) {
            c.Continue(); // still fading in (or out): the chain of frames must not stop
            return;
        }
        // Drawn at the resting place, carried to the frame's centre and size.
        // The liquid wobble rocks the letter with the glass: a small sideways
        // shove, an opposing nudge down, and a matching breath in scale.
        c.BeginGroup(f.opacity, f.scale * (1 + 0.035f * f.wob), f.cx, f.cy,
                     f.cx - ImeBubbleModel::kCx + f.wob * 2.5f, f.cy - ImeBubbleModel::kCyRest - f.wob * 0.8f);
        // The glyph is the OS's for this language (中/a/A/その言語の印). A
        // lowercase latin letter rides the baseline, and a line box centred on
        // the bubble leaves its body a little low: lift it so 'a' sits in the
        // middle. Capitals and the scripts' own marks already fill theirs.
        const bool lowLatin = glyph_.size() == 1 && glyph_[0] >= L'a' && glyph_[0] <= L'z';
        const float lift = lowLatin ? 2.5f : 0.f;
        c.Text(glyph_, D2D1::RectF(ImeBubbleModel::kCx - 32, ImeBubbleModel::kCyRest - 17 - lift,
                                  ImeBubbleModel::kCx + 32, ImeBubbleModel::kCyRest + 17 - lift),
               26, theme.text, DWRITE_FONT_WEIGHT_DEMI_BOLD, 1);
        c.EndGroup();
        c.Continue();
    }

    ImeStatus status_;
    std::wstring glyph_; // the OS's mark for this language and mode, from Show
    RECT work_{};
    float bottomSpacing_ = 10; // the bar's margin above the work area's bottom, DIP (kept for the settings round trip)
    int screenBottom_ = 0;     // the display's physical bottom edge, from rcMonitor.bottom: the window's own bottom
    ImeBubbleTuning tuning_; // the chosen merge feel, handed to the model
    ImeBubbleModel model_;
    double lastPaint_ = 0;
    Surface bubble_;
};

} // namespace atrium

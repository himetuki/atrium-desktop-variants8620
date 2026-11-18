#include "shell/ime_status.h"
#include "render/glass_renderer.h"
#include "render/ime_swap.h"
#include "ui/ime_bubble.h"
#include "ui/ime_bubble_gate.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace atrium;

// The bubble's melt rides in the glass renderer's own parts budget: one shape
// carries the skin's pieces and every newcomer drawn with it.
static_assert(ImeBubbleModel::kMaxMeltParts == kMaxGlassParts, "the bubble's melt fits the renderer's parts");

namespace {

ImeStatus Status(DWORD layout, bool ime, bool open, bool native, bool upper = false)
{
    ImeStatus s;
    s.layout = layout;
    s.ime = ime;
    s.open = open;
    s.native = native;
    s.upper = upper;
    return s;
}

} // namespace

int main()
{
    int failures = 0;
    auto require = [](bool condition, const char* message) {
        if (!condition)
            throw std::runtime_error(message);
    };
    auto run = [&](const char* name, auto test) {
        try {
            test();
            std::cout << "PASS: " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
        }
    };

    run("the glyph follows the conversion state and the case", [&] {
        require(ImeGlyph(Status(0x08040804, true, true, true)) == L"中", "Chinese native composition writes 中");
        require(ImeGlyph(Status(0x08040804, true, true, false)) == L"a", "Chinese alphanumeric mode writes latin");
        require(ImeGlyph(Status(0x08040804, true, true, false, true)) == L"A", "alphanumeric with Shift writes capital");
        require(ImeGlyph(Status(0x08040804, true, false, true)) == L"a", "an IME that is off writes small latin");
        require(ImeGlyph(Status(0x04090409, false, false, false, true)) == L"A", "a plain keyboard writes the case");
        require(ImeGlyph(Status(0x04110411, true, true, true)) == L"あ", "Japanese native composition writes あ");
        require(ImeGlyph(Status(0x04120412, true, true, true)) == L"가", "Korean native composition writes 가");
        require(ImeGlyph(ImeStatus{}) == L"a", "an unreadable foreground writes small latin");
    });
    run("CapsLock overrides a native method; Shift gives the script back", [&] {
        const ImeStatus zh = Status(0x08040804, true, true, true);
        ImeStatus caps = zh;
        caps.caps = caps.upper = true; // CapsLock on, Shift up
        require(ImeGlyph(caps) == L"A", "CapsLock types uppercase Latin over Chinese, for every IME");
        require(!zh.SameAs(caps), "and that is news");
        ImeStatus capsShift = caps;
        capsShift.upper = false; // Shift held under CapsLock
        require(ImeGlyph(capsShift) == L"中", "Shift under CapsLock gives the Chinese letters back, not a lowercase a");
        require(caps.SameAs(capsShift), "but that temporary mode is typing, not news");
        ImeStatus shiftOnly = zh;
        shiftOnly.upper = true; // Shift with CapsLock off
        require(ImeGlyph(shiftOnly) == L"中", "Shift alone leaves a native method its own letters");
        require(zh.SameAs(shiftOnly), "nor is it news");
        ImeStatus capsOff = caps;
        capsOff.caps = capsOff.upper = false;
        require(ImeGlyph(capsOff) == L"中", "releasing CapsLock brings the Chinese letters back");
    });
    run("every language gets its own mark from the OS, nothing from this machine", [&] {
        require(ImeGlyph(Status(0x04040404, true, true, true)) == L"中", "zh-TW composes 中 too");
        // Other scripts' text services: the language's name in its own script
        // answers, so the bubble never claims a Chinese 中 for them.
        require(ImeGlyph(Status(0x04070407, true, true, true)) == L"D", "German native writes its own first letter (Deutsch)");
        require(ImeGlyph(Status(0x041E041E, true, true, true))[0] > 0x7F, "Thai native writes a Thai letter, not latin");
        require(ImeGlyph(Status(0x042A042A, true, true, true)) == L"T", "Vietnamese native writes T (Tiếng Việt)");
        require(ImeGlyph(Status(0x04190419, true, true, true))[0] > 0x7F, "Russian native writes a Cyrillic letter");
        require(ImeGlyph(Status(0x04190419, true, true, true)) != L"中", "a Cyrillic IME is not announced as Chinese");
        require(ImeGlyph(Status(0x04070407, false, false, false)) == L"a", "a German direct-input layout writes latin");
    });
    run("only a change the bubble announces counts", [&] {
        const ImeStatus base = Status(0x08040804, true, true, true);
        require(base.SameAs(Status(0x08040804, true, true, true)), "the same state is not news");
        require(!base.SameAs(Status(0x08040804, true, true, false)), "switching to English is news");
        require(!base.SameAs(Status(0x04090409, false, false, false)), "another layout is news");
        const ImeStatus lower = Status(0x08040804, true, true, false);
        require(lower.SameAs(Status(0x08040804, true, true, false, true)), "Shift in latin mode is typing, not news");
        require(base.SameAs(Status(0x08040804, true, true, true, true)), "Shift means nothing while composing 中");
    });
    run("the bubble swells in from below with a springy overshoot", [&] {
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        const auto emerging = bubble.Step(1030);
        require(emerging.active && emerging.animating, "the announce is running");
        require(emerging.opacity > 0 && emerging.opacity < 1, "the bubble fades in");
        require(emerging.cy > ImeBubbleModel::kCyRest && emerging.scale < 1, "it starts below its rest, small");
        const auto sprung = bubble.Step(1000 + 300);
        require(sprung.scale > 1, "the emerge passes its end by a little before settling");
    });
    run("the float drifts up gently while fully visible", [&] {
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        const auto rested = bubble.Step(1000 + ImeBubbleModel::kEmergeMs + 1);
        require(std::abs(rested.cy - ImeBubbleModel::kCyRest) < 0.5f, "the emerge ends at the resting place");
        require(rested.opacity == 1 && std::abs(rested.scale - 1) < 0.02f, "a settled bubble is whole");
        const auto floated = bubble.Step(1000 + ImeBubbleModel::kPopAt - 1);
        require(floated.cy < ImeBubbleModel::kCyRest - 5, "the float rises the rest of the way");
        require(floated.beadCount == 0, "nothing bursts early");
    });
    run("the pop tears the skin into foam and mist", [&] {
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        const auto burst = bubble.Step(1000 + ImeBubbleModel::kPopAt + ImeBubbleModel::kSwellMs + 10);
        require(burst.beadCount >= ImeBubbleModel::kMinBeads && burst.beadCount <= ImeBubbleModel::kMaxBeads, "every pop has its own number of drops");
        require(burst.foamCount >= 5 && burst.foamCount < burst.beadCount, "drops lead, mist follows");
        require(burst.beadOpacity > 0.9f, "the foam starts fully visible");
        require(burst.opacity < 1 && burst.opacity > 0, "the bubble's skin is leaving");
        require(burst.scale > 1.15f, "the burst inflates the skin as it goes");
        // The drops leave the rim: all of them real, none grown yet.
        for (int i = 0; i < burst.beadCount; ++i)
            require(burst.beads[i].r > 0, "every drop of the foam is a real one");
        const auto flying = bubble.Step(1000 + ImeBubbleModel::kPopAt + ImeBubbleModel::kSwellMs + 40);
        const float by = ImeBubbleModel::kCyRest - ImeBubbleModel::kFloatRise;
        int above = 0;
        for (int i = 0; i < flying.beadCount; ++i)
            above += flying.beads[i].y < by - 1;
        require(above >= 3, "the foam scatters upwards, like a bubble breaking");
    });
    run("every pop of its own pattern", [&] {
        ImeBubbleModel first, second;
        first.Begin(1000);
        second.Begin(3100); // another seed: another burst
        const auto a = first.Step(1000 + ImeBubbleModel::kPopAt + ImeBubbleModel::kSwellMs + 60);
        const auto b = second.Step(3100 + ImeBubbleModel::kPopAt + ImeBubbleModel::kSwellMs + 60);
        bool same = a.beadCount == b.beadCount && a.foamCount == b.foamCount;
        for (int i = 0; same && i < a.beadCount; ++i)
            same = std::abs(a.beads[i].x - b.beads[i].x) < 0.5f && std::abs(a.beads[i].y - b.beads[i].y) < 0.5f;
        require(!same, "two pops cannot share one frozen pattern");
    });
    run("the whole life stays inside the small fixed window", [&] {
        for (int seed = 0; seed < 3; ++seed) { // several patterns must all fit
            const double begin = 1000 + seed * 7919;
            ImeBubbleModel bubble;
            bubble.Begin(begin);
            for (double t = begin + 8; t < bubble.EndMs(); t += 8) {
                const auto f = bubble.Step(t);
                require(f.active && f.animating, "every mid-life frame animates");
                require(std::isfinite(f.cx) && std::isfinite(f.cy) && std::isfinite(f.scale) && std::isfinite(f.opacity) && f.opacity >= 0 && f.opacity <= 1 &&
                            f.scale >= 0.4f && f.scale <= 1.7f,
                        "the bubble stays visible and bounded");
                const float r = ImeBubbleModel::kRadius * f.scale;
                require(f.cy - r >= 0 && f.cy + r <= ImeBubbleModel::kH, "the skin never leaves the window");
                for (int i = 0; i < f.beadCount; ++i) {
                    const auto& bead = f.beads[i];
                    require(std::isfinite(bead.x) && std::isfinite(bead.y) && std::isfinite(bead.r) && bead.r >= 0,
                            "every bead is a real drop, or one that has thinned away");
                    require(bead.x - bead.r >= 0 && bead.x + bead.r <= ImeBubbleModel::kW && bead.y - bead.r >= 0 && bead.y + bead.r <= ImeBubbleModel::kH,
                            "the burst stays inside the window");
                }
            }
        }
    });
    run("the announce ends bounded, and a restart starts clean", [&] {
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        require(!bubble.Step(1000 + ImeBubbleModel::kPopAt + ImeBubbleModel::kSwellMs + ImeBubbleModel::kBurstMs + 1).active,
                "the bubble is done after its total");
        require(!bubble.Step(1000 + ImeBubbleModel::kPopAt + ImeBubbleModel::kSwellMs + ImeBubbleModel::kBurstMs + 500).animating, "it never animates again");
        bubble.Begin(3000);
        const auto fresh = bubble.Step(3050);
        require(fresh.active && fresh.opacity < 1 && fresh.cy > ImeBubbleModel::kCyRest, "a restarted bubble emerges like the first");
        bubble.Cancel();
        require(!bubble.Step(3100).active, "cancel removes it at once");
    });
    run("a late frame lands where a punctual one would", [&] {
        ImeBubbleModel slow, jumped;
        slow.Begin(1000);
        jumped.Begin(1000);
        const double at = 1000 + 617;
        for (double t = 1008; t < at; t += 8)
            slow.Step(t);
        const auto a = slow.Step(at), b = jumped.Step(at);
        require(std::abs(a.cy - b.cy) < 0.01f && std::abs(a.scale - b.scale) < 0.01f && std::abs(a.opacity - b.opacity) < 0.01f && a.beadCount == b.beadCount,
                "no integration state: the frame is a pure function of time");
        for (int i = 0; i < a.beadCount; ++i)
            require(std::abs(a.beads[i].x - b.beads[i].x) < 0.01f && std::abs(a.beads[i].y - b.beads[i].y) < 0.01f,
                    "the drawn burst pattern is the same for late and punctual frames");
    });
    run("a late frame lands where a punctual one would, even mid-merge", [&] {
        ImeBubbleModel slow, jumped;
        slow.Begin(1000);
        jumped.Begin(1000);
        slow.Fuse(1200);
        jumped.Fuse(1200);
        slow.Fuse(1300);
        jumped.Fuse(1300);
        const double at = 1700; // both newcomers in flight, both already touched
        for (double t = 1008; t < at; t += 8)
            slow.Step(t);
        const auto a = slow.Step(at), b = jumped.Step(at);
        require(a.lobeCount == 2 && b.lobeCount == 2, "both rising newcomers are in the frame");
        for (int i = 0; i < a.lobeCount; ++i)
            require(std::abs(a.lobes[i].x - b.lobes[i].x) < 0.01f && std::abs(a.lobes[i].y - b.lobes[i].y) < 0.01f &&
                        std::abs(a.lobes[i].r - b.lobes[i].r) < 0.01f && std::abs(a.lobes[i].u - b.lobes[i].u) < 0.0001f,
                    "each rising newcomer is the same");
        require(std::abs(a.grow - b.grow) < 0.01f && std::abs(a.wob - b.wob) < 0.01f && std::abs(a.scale - b.scale) < 0.01f,
                "the size and wobble are the same");
        require(a.beadCount == b.beadCount, "no burst comes from integrating");
        for (int i = 0; i < a.beadCount; ++i)
            require(std::abs(a.beads[i].x - b.beads[i].x) < 0.01f && std::abs(a.beads[i].y - b.beads[i].y) < 0.01f, "any drawn drops are the same");
    });
    run("merges grow the bubble logarithmically and stop at ten", [&] {
        const ImeBubbleModel defaults{};
        require(std::abs(defaults.ScaleAt(1) - 1.f) < 1e-6f, "one bubble is the nominal size");
        require(std::abs(defaults.RadiusAt(1) - ImeBubbleModel::kRadius) < 1e-6f, "at the start radius, in DIP");
        float last = defaults.ScaleAt(1);
        for (int x = 2; x <= ImeBubbleModel::kMaxFusions; ++x) {
            const float s = defaults.ScaleAt(x);
            require(s > last, "every merge is larger than the last");
            last = s;
        }
        require(std::abs(defaults.RadiusAt(ImeBubbleModel::kMaxFusions) - 51) < 1e-4f &&
                    std::abs(defaults.ScaleAt(ImeBubbleModel::kMaxFusions) - 51.f / ImeBubbleModel::kRadius) < 1e-5f,
                "ten merges reach the default end radius: 51/32 of the base");
        require(defaults.ScaleAt(14) == defaults.ScaleAt(ImeBubbleModel::kMaxFusions), "past ten the size stays");
        require(defaults.ScaleAt(0) == defaults.ScaleAt(1), "the count clamps at one");
        require(defaults.ScaleAt(2) - defaults.ScaleAt(1) > defaults.ScaleAt(ImeBubbleModel::kMaxFusions) - defaults.ScaleAt(ImeBubbleModel::kMaxFusions - 1),
                "the growth tapers, as a logarithmic size must");
    });
    run("the two size settings set the curve, and the letter rides its scale", [&] {
        ImeBubbleTuning smaller;
        smaller.startRadius = 20;
        smaller.endRadius = 48;
        ImeBubbleModel bubble;
        bubble.SetTuning(smaller);
        require(std::abs(bubble.ScaleAt(1) - 20.f / 32) < 1e-5f, "the start setting is the first bubble's scale");
        require(std::abs(bubble.ScaleAt(ImeBubbleModel::kMaxFusions) - 48.f / 32) < 1e-5f, "the end setting is ten merges': 48/32");
        require(std::abs(bubble.RadiusAt(1) - 20) < 1e-4f && std::abs(bubble.RadiusAt(10) - 48) < 1e-4f, "and the radii themselves are the settings, in DIP");
        // Still the logarithm between the two ends: every merge grows, by less
        // than the one before.
        for (int x = 2; x <= ImeBubbleModel::kMaxFusions; ++x)
            require(bubble.ScaleAt(x) > bubble.ScaleAt(x - 1), "the tuned curve only grows");
        require(bubble.ScaleAt(2) - bubble.ScaleAt(1) > bubble.ScaleAt(ImeBubbleModel::kMaxFusions) - bubble.ScaleAt(ImeBubbleModel::kMaxFusions - 1),
                "and keeps the logarithm's taper");
        // The glyph is drawn inside the content group, which the frame's scale
        // carries: a small setting opens small, ten merges reach the end size,
        // so the letter grows with the bubble.
        bubble.Begin(1000);
        const auto opened = bubble.Step(1000 + ImeBubbleModel::kEmergeMs + 1);
        require(std::abs(opened.scale - 20.f / 32) < 0.02f, "an unmerged frame rides the start setting");
        for (int i = 0; i < ImeBubbleModel::kMaxFusions; ++i)
            require(bubble.Fuse(1150 + i * 120.0), "every switch merges");
        const auto grown = bubble.Step(bubble.PopAt() - 1);
        require(std::abs(grown.scale - 48.f / 32) < 0.02f, "ten merges ride the end setting");
        // A pair that would fold the curve on itself (an end below the start)
        // keeps the start: the bubble grows or stays, never shrinks.
        ImeBubbleTuning folded;
        folded.startRadius = 40;
        folded.endRadius = 24;
        ImeBubbleModel sane;
        sane.SetTuning(folded);
        require(sane.ScaleAt(10) == sane.ScaleAt(1) && std::abs(sane.RadiusAt(10) - 40) < 1e-4f, "an end below the start folds to the start");
    });
    run("the merge feel and the life are tunable, and a restart keeps them", [&] {
        // The default rebound is the old spring: ~31 % over its target, about
        // 1.31 of the way at 196 ms.
        require(std::abs(ImeBubbleModel::softBounce(196, 0.31f) - 1.308f) < 0.01f, "the default bounce matches the old one");
        const ImeBubbleTuning defaults;
        require(defaults.compression == 0.10f && defaults.rebound == 0.31f && defaults.wobbleAmp == 0.875f && defaults.wobbleHz == 4.3f &&
                    defaults.lifeMs == 1060.f && defaults.startRadius == 32.f && defaults.endRadius == 51.f,
                "the defaults are the original feel, life and size");
        ImeBubbleModel plain;
        plain.Begin(1000);
        require(std::abs(plain.EndMs() - (1000 + 1060 + 46 + 440)) < 0.5, "the default life is the old one");

        ImeBubbleTuning t;
        t.compression = 0.2f;
        t.rebound = 0.05f;
        t.wobbleAmp = 0.f;
        t.wobbleHz = 8.f;
        t.lifeMs = 1500.f;
        ImeBubbleModel bubble;
        bubble.SetTuning(t);
        bubble.Begin(1000);
        require(std::abs(bubble.EndMs() - (1000 + 1500 + 46 + 440)) < 0.5, "a longer life is honored");
        bubble.Begin(2500); // Begin keeps the tuning
        require(std::abs(bubble.EndMs() - (2500 + 1500 + 46 + 440)) < 0.5, "Begin keeps the tuning");
        bubble.Cancel(); // and so does Cancel
        bubble.Begin(4000);
        require(std::abs(bubble.EndMs() - (4000 + 1500 + 46 + 440)) < 0.5, "Cancel keeps the tuning");

        // The compression deepens the hit's squash; a zero wobble amp is silent.
        ImeBubbleModel shallow, deep;
        ImeBubbleTuning ts, td;
        ts.compression = 0.f;
        ts.wobbleAmp = 0.f;
        td.compression = 0.2f;
        td.wobbleAmp = 0.f;
        shallow.SetTuning(ts);
        deep.SetTuning(td);
        shallow.Begin(1000);
        deep.Begin(1000);
        require(shallow.Fuse(1200) && deep.Fuse(1200), "both merge");
        const double hit = 1200 + deep.TouchMs();
        require(deep.Step(hit).scale < shallow.Step(hit).scale * 0.95f, "the compression is the tuned one");
        require(std::abs(deep.Step(hit + 60).wob) < 1e-4f, "a zero wobble amp keeps the letter still");

        // A tiny rebound (0.05) barely passes its target: the swell after a
        // touch stays under 10 % past the new size.
        ImeBubbleModel gentle;
        ImeBubbleTuning tg;
        tg.rebound = 0.05f;
        gentle.SetTuning(tg);
        gentle.Begin(1000);
        require(gentle.Fuse(1200), "it merges");
        const double hitAt = 1200 + gentle.TouchMs();
        float top = 0;
        for (int ms = 0; ms <= 900; ms += 5)
            top = std::max(top, gentle.Step(hitAt + ms).grow);
        require(top < gentle.ScaleAt(2) * 1.10f, "a tiny rebound overshoots by under ten per cent");
    });
    run("the birth dip and the newcomer's three acts come from the tuning", [&] {
        // The defaults are the old feel: the first bubble 18 DIP below its
        // rest, a newcomer 10 DIP below the window's bottom, and an absorb of
        // 380 + 243 + 137 = 760 ms whose touch (rise + attach) is at 623 ms.
        const ImeBubbleModel defaults{};
        const ImeBubbleTuning d;
        require(d.firstBirthDip == 18 && d.lobeBirthDip == 10 && d.lobeRiseMs == 380 && d.lobeAttachMs == 243 && d.lobeSwallowMs == 137,
                "the tuning's own defaults are the old constants");
        require(std::abs(defaults.RiseEndMs() - 380) < 1e-9 && std::abs(defaults.AbsorbMs() - 760) < 1e-9 && std::abs(defaults.AttachEndMs() - 623) < 1e-9 &&
                    std::abs(defaults.TouchMs() - 623) < 1e-9 && std::abs(defaults.TouchFrac() - 623.0 / 760.0) < 1e-9,
                "the defaults are the old birth points and the old absorb");

        ImeBubbleTuning t;
        t.firstBirthDip = 42;
        t.lobeBirthDip = 24;
        t.lobeRiseMs = 120;
        t.lobeAttachMs = 40;
        t.lobeSwallowMs = 240;
        ImeBubbleModel bubble;
        bubble.SetTuning(t);
        bubble.Begin(1000);
        // The first bubble is born firstBirthDip below its resting centre.
        require(std::abs(bubble.Step(1000).cy - (ImeBubbleModel::kCyRest + 42)) < 1e-4f, "the first bubble is born below its resting centre by firstBirthDip");
        // The derived timeline: rise + attach + swallow, the touch at rise + attach.
        require(std::abs(bubble.AbsorbMs() - 400) < 1e-9 && std::abs(bubble.RiseEndMs() - 120) < 1e-9 && std::abs(bubble.AttachEndMs() - 160) < 1e-9 &&
                    std::abs(bubble.TouchMs() - 160) < 1e-9 && std::abs(bubble.TouchFrac() - 0.4) < 1e-9,
                "the absorb is its three acts, the touch the end of rise + attach");
        // The newcomer's centre is born lobeBirthDip below the window's bottom,
        // at the start of the radius rhythm.
        require(bubble.Fuse(1200), "it merges");
        const auto born = bubble.Step(1200);
        require(std::abs(born.lobes[0].y - (ImeBubbleModel::kH + 24)) < 1e-4f, "the newcomer is born below the window's bottom edge by lobeBirthDip");
        require(std::abs(born.lobes[0].r - 0.30f * ImeBubbleTuning{}.startRadius) < 0.01f,
                "it is born at the start of its own radius rhythm, off the bubble's start size");
        // The three acts are the tuning's own lengths, not fixed fractions: the
        // slow rise ends at 120 ms, the fast approach at the touch (160 ms), the
        // swallow into the skin by 400 ms. The radius marks each boundary.
        const float rTouch = 0.55f * ImeBubbleTuning{}.startRadius;
        require(std::abs(bubble.Step(1200 + 119.5).lobes[0].r - 0.8f * rTouch) < 0.5f, "the slow rise ends on the hover radius at its own time");
        require(bubble.Step(1200 + 159.5).lobes[0].r > 0.95f * rTouch, "the fast approach reaches the rim by the touch");
        require(std::abs(bubble.Step(1200 + 159).grow - 1.f) < 1e-4f, "the touch comes at rise + attach, not later");
        require(bubble.Step(1200 + 161).grow > 1.f, "and the size steps at once");
        // At 5/8 of the absorb the newcomer is well into the swallow, where a
        // fixed 0.82 touch fraction would still have it in the fast approach.
        require(bubble.Step(1200 + 250).lobes[0].r < 0.5f * rTouch, "the swallow begins where the tuning says");
        require(bubble.Step(1200 + 300).lobes[0].r < 0.25f * rTouch, "and is far in by 3/4 of the absorb");
        require(bubble.Step(1200 + 401).lobeCount == 0, "and gone at the absorb's own end");

        // A restart keeps the tuning's birth points and acts, as it keeps the rest.
        bubble.Begin(2500);
        require(std::abs(bubble.Step(2500).cy - (ImeBubbleModel::kCyRest + 42)) < 1e-4f && std::abs(bubble.AbsorbMs() - 400) < 1e-9,
                "Begin keeps the birth dip and the three acts");
        bubble.Cancel();
        require(std::abs(bubble.AbsorbMs() - 400) < 1e-9, "Cancel keeps them too");
        bubble.Begin(4000);
        require(bubble.Fuse(4200) && std::abs(bubble.Step(4200).lobes[0].y - (ImeBubbleModel::kH + 24)) < 1e-4f,
                "and a newcomer after the restart is still born lobeBirthDip below the window");
    });
    run("an absorb with a zero-length act still runs, and never divides by zero", [&] {
        // Each of the three acts may be zero: the others still carry the
        // newcomer, and every frame stays finite.
        for (int zero = 0; zero < 3; ++zero) {
            ImeBubbleTuning t;
            t.lobeRiseMs = zero == 0 ? 0.f : 380.f;
            t.lobeAttachMs = zero == 1 ? 0.f : 243.f;
            t.lobeSwallowMs = zero == 2 ? 0.f : 137.f;
            ImeBubbleModel bubble;
            bubble.SetTuning(t);
            bubble.Begin(1000);
            require(bubble.Fuse(1200), "it merges");
            const double A = bubble.AbsorbMs();
            require(std::isfinite(A) && A >= 60.0, "the absorb keeps its floor");
            bool sawLobe = false;
            for (double dt = 0; dt < A; dt += 8) {
                const auto f = bubble.Step(1200 + dt);
                if (!f.active)
                    break;
                if (f.lobeCount > 0) {
                    sawLobe = true;
                    require(std::isfinite(f.lobes[0].x) && std::isfinite(f.lobes[0].y) && std::isfinite(f.lobes[0].r),
                            "a zero-length act still leaves a real newcomer");
                }
                require(std::isfinite(f.cx) && std::isfinite(f.cy) && std::isfinite(f.scale) && std::isfinite(f.wob), "every frame of it is finite");
            }
            require(sawLobe, "the newcomer was drawn");
        }
        // Every act zero: the absorb is the floor and the newcomer still rises
        // and is swallowed — the whole act a short swallow.
        ImeBubbleTuning allZero;
        allZero.lobeRiseMs = allZero.lobeAttachMs = allZero.lobeSwallowMs = 0;
        ImeBubbleModel bubble;
        bubble.SetTuning(allZero);
        require(std::abs(bubble.AbsorbMs() - 60.0) < 1e-9 && std::abs(bubble.TouchMs()) < 1e-9, "an all-zero absorb is the 60 ms floor, the touch at once");
        bubble.Begin(1000);
        require(bubble.Fuse(1200), "it merges");
        const auto mid = bubble.Step(1230);
        require(mid.active && mid.lobeCount == 1 && std::abs(mid.lobes[0].u - 0.5f) < 1e-4f, "the newcomer still rises over the floor and is drawn");
        require(bubble.Step(1261).lobeCount == 0, "and is swallowed by the floor's own end");
    });
    run("a birth dip of zero puts a newcomer's centre on the window's bottom edge", [&] {
        ImeBubbleTuning t;
        t.lobeBirthDip = 0;
        ImeBubbleModel bubble;
        bubble.SetTuning(t);
        bubble.Begin(1000);
        require(bubble.Fuse(1200), "it merges");
        const auto born = bubble.Step(1200);
        require(std::abs(born.lobes[0].y - ImeBubbleModel::kH) < 1e-4f, "with no birth dip its centre sits on the window's bottom edge");
        require(born.lobes[0].y - born.lobes[0].r < ImeBubbleModel::kH, "so half of it is still unseen below the window");
    });
    run("the pop is one life out, and the burst follows it", [&] {
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        require(std::abs(bubble.PopAt() - (1000 + 1060)) < 0.5, "the default life sets the pop");
        require(bubble.Mergeable(bubble.PopAt() - 5), "a state just before the pop still merges");
        require(!bubble.Mergeable(bubble.PopAt() + 5), "and none once the pop is due");
        require(std::abs(bubble.EndMs() - (bubble.PopAt() + 486)) < 0.5, "the swell and burst follow the pop");
        // The pop is the longest of the merge's own life and a fresh state's,
        // so a merge never shortens the life it was already promised.
        const double before = bubble.EndMs();
        require(bubble.Fuse(bubble.PopAt() - 100), "a late state still merges");
        require(bubble.EndMs() > before, "the newcomer is absorbed and the pop is pushed back");
    });
    run("a short life still waits for its newcomer to be swallowed", [&] {
        ImeBubbleTuning t;
        t.lifeMs = 400; // shorter than one absorb
        ImeBubbleModel bubble;
        bubble.SetTuning(t);
        bubble.Begin(1000);
        require(bubble.Fuse(1200), "it merges");
        require(bubble.PopAt() >= 1000 + t.lifeMs, "the life it was promised is kept");
        require(bubble.PopAt() >= 1200 + bubble.AbsorbMs(), "and the pop waits for the swallow");
        const auto atPop = bubble.Step(bubble.PopAt() - 0.5);
        require(atPop.active, "the bubble is still there just before the pop");
        for (int i = 0; i < atPop.lobeCount; ++i)
            require(atPop.lobes[i].r < 1.f, "its newcomer is all but swallowed when the skin bursts");
        require(bubble.Step(bubble.PopAt() + ImeBubbleModel::kSwellMs + 10).lobeCount == 0, "and none is left once the burst begins");
        require(!bubble.Mergeable(bubble.PopAt()), "no merge comes after the pop");
    });
    run("a live bubble merges: soft, springy, alive, one step larger", [&] {
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        const double fuseAt = 1000 + ImeBubbleModel::kPopAt - 100;
        const double end1 = bubble.EndMs();
        const auto before = bubble.Step(fuseAt);
        require(bubble.fusions() == 1 && before.active, "one bubble, out there");
        require(bubble.Fuse(fuseAt), "a state while it lives merges");
        require(bubble.fusions() == 2, "the count moves");
        require(bubble.EndMs() > end1, "the pop is pushed back so the newest state reads");
        const auto at = bubble.Step(fuseAt);
        require(std::abs(at.scale - before.scale) < 0.01f, "the size cannot jump at the merge");
        require(std::abs(at.grow - 1.f) < 1e-4f, "the size waits for the newcomer to touch");
        const double touch = fuseAt + bubble.TouchMs();
        require(std::abs(bubble.Step(touch - 30).grow - 1.f) < 1e-3f, "it holds until the touch");
        require(bubble.Step(touch + 60).grow > 1.f, "then the touch presses the skin wider");
        float peak = 0;
        for (int ms = 0; ms <= 900; ms += 10)
            peak = std::max(peak, bubble.Step(touch + ms).grow);
        require(peak > bubble.ScaleAt(2) + 0.05f, "a real merge overshoots its new size, visibly");
        // The spring has not fully settled by the pop — the touch now comes late
        // enough in the absorb (about 0.82 of it by default) that a whole life
        // leaves it at about 94 %, not all but done — but it has clearly all but
        // reached it, and it settles fully just after (below).
        const auto nearPop = bubble.Step(bubble.PopAt() - 1);
        require(nearPop.active && std::abs(nearPop.grow - bubble.ScaleAt(2)) < 0.02f, "by the pop it has all but reached the new size");
        const auto settled = bubble.Step(touch + 920);
        require(std::abs(settled.grow - bubble.ScaleAt(2)) < 0.001f, "and settles at it");
        require(settled.active, "still the same bubble");
    });
    run("a lone merge keeps the old feel: size, squash and wobble", [&] {
        // The feel anchor: with one newcomer the frame is exactly the old
        // closed form — the base plus one springy step, the hit's own squash,
        // one decaying rhythm — however the merges are added up.
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        require(bubble.Fuse(1200), "it merges");
        const double touch = 1200 + bubble.TouchMs();
        const float step = bubble.ScaleAt(2) - bubble.ScaleAt(1);
        for (double t = 1200; t < bubble.PopAt(); t += 7) {
            const float want = t < touch ? bubble.ScaleAt(1) : bubble.ScaleAt(1) + step * ImeBubbleModel::softBounce(t - touch, 0.31f);
            require(std::abs(bubble.Step(t).grow - want) < 1e-5f, "the size is the base plus one step");
        }
        // The squash is the old hit's, at the old moment: the frame against a
        // skin that cannot be squashed at all.
        ImeBubbleModel soft, stiff;
        ImeBubbleTuning firm;
        firm.compression = 0;
        stiff.SetTuning(firm);
        soft.Begin(1000);
        stiff.Begin(1000);
        require(soft.Fuse(1200) && stiff.Fuse(1200), "both merge");
        const double absorb = soft.AbsorbMs();
        const float touchFrac = float(soft.TouchFrac());
        for (double t = 1200; t < 1200 + absorb; t += 7) {
            const float u = float((t - 1200) / absorb);
            const float d = (u - touchFrac) / float(ImeBubbleModel::kDipSpan);
            const float want = 1 - 0.10f * float(std::exp(-d * d));
            require(std::abs(soft.Step(t).scale / stiff.Step(t).scale - want) < 1e-5f, "the hit squashes the skin by the old amount");
        }
        // The wobble is one decaying sine of the newcomer's own strength and
        // pace: doubling either tuning field is that of its own frame — and the
        // doubled pace at a moment is the plain one half as far along, decaying
        // half as fast.
        ImeBubbleTuning longLife, louderTuning, quickerTuning;
        longLife.lifeMs = louderTuning.lifeMs = quickerTuning.lifeMs = 3000; // the pop cannot cut the rhythm short
        louderTuning.wobbleAmp = 2 * longLife.wobbleAmp;
        quickerTuning.wobbleHz = 2 * longLife.wobbleHz;
        ImeBubbleModel steady, louder, quicker;
        steady.SetTuning(longLife);
        louder.SetTuning(louderTuning);
        quicker.SetTuning(quickerTuning);
        steady.Begin(1000);
        louder.Begin(1000);
        quicker.Begin(1000);
        require(steady.Fuse(1200) && louder.Fuse(1200) && quicker.Fuse(1200), "all three merge");
        for (int ms = 20; ms <= 420; ms += 20) {
            const double s = ms;
            require(std::abs(louder.Step(touch + s).wob - 2 * steady.Step(touch + s).wob) < 1e-6f, "the wobble's strength is the newcomer's own");
            require(std::abs(quicker.Step(touch + s).wob - steady.Step(touch + 2 * s).wob * float(std::exp(s / 260.0))) < 1e-4f,
                    "its pace is its own, and it decays by the same spring");
        }
    });
    run("merges chain, extend the life, and stop growing at ten", [&] {
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        double now = 1200;
        for (int i = 0; i < 30; ++i, now += 100)
            require(bubble.Fuse(now), "each state inside the life merges");
        require(bubble.fusions() > ImeBubbleModel::kMaxFusions, "the count itself may pass ten");
        require(bubble.EndMs() > now, "the last merge still owns the bubble");
        require(bubble.EndMs() - 1000 >= ImeBubbleModel::kPopAt + ImeBubbleModel::kSwellMs + ImeBubbleModel::kBurstMs - 1,
                "the bubble outlives at least one whole life");
        // Every newcomer's step lands on its own touch; the frame before the
        // burst has had all of them, and none past the tenth adds size.
        const auto grown = bubble.Step(bubble.PopAt() - 1);
        require(std::abs(grown.grow - bubble.ScaleAt(ImeBubbleModel::kMaxFusions)) < 0.02f, "the size stops growing past ten merges");
        require(grown.lobeCount == 0, "no newcomer is still in flight when the skin bursts");
        require(!bubble.Fuse(bubble.PopAt() + 100), "once the burst begins, no merge exists");
    });
    run("a bubble merged to ten still lives inside the window", [&] {
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        for (int i = 0; i < 12; ++i)
            bubble.Fuse(1100 + i * 100.0);
        // Every newcomer, however big the bubble and however many are already
        // in flight, has its centre born the newcomer birth dip below the
        // window's bottom edge, at the start of its own radius rhythm.
        const float birthDip = ImeBubbleTuning{}.lobeBirthDip;
        for (int i = 0; i < 12; ++i) {
            const auto start = bubble.Step(1100 + i * 100.0);
            require(start.active && start.lobeCount > 0, "each newcomer is a visible bubble");
            const auto& own = start.lobes[start.lobeCount - 1];
            require(own.r > 0.5f, "the newcomer of this very switch is a real bubble");
            require(std::abs(own.y - (ImeBubbleModel::kH + birthDip)) < 0.01f, "its centre is born the birth dip below the window's bottom edge");
            require(std::abs(own.r - 0.30f * ImeBubbleTuning{}.startRadius) < 0.01f, "it is born the first bubble's size, not the grown skin's");
        }
        for (double t = 1008; t < bubble.EndMs(); t += 8) {
            const auto f = bubble.Step(t);
            if (!f.active)
                continue;
            const float r = ImeBubbleModel::kRadius * f.scale;
            require(f.cy - r >= 0 && f.cy + r <= ImeBubbleModel::kH, "the fused skin never leaves the window");
            require(f.lobeCount <= ImeBubbleModel::kMaxFrameLobes, "a frame carries no more than it can draw");
            // A newcomer born below the window is clipped until it rises in, so
            // only one already in the frame is held to the frame; its top edge
            // never crosses the window's own top.
            for (int i = 0; i < f.lobeCount; ++i) {
                const auto& lobe = f.lobes[i];
                require(lobe.y - lobe.r >= 0, "no newcomer ever leaves the window through the top");
                if (lobe.y + lobe.r <= ImeBubbleModel::kH)
                    require(lobe.x - lobe.r >= 0 && lobe.x + lobe.r <= ImeBubbleModel::kW && lobe.y - lobe.r >= 0,
                            "a newcomer inside the window stays inside it");
            }
            for (int i = 0; i < f.beadCount; ++i) {
                const auto& bead = f.beads[i];
                require(bead.x - bead.r >= 0 && bead.x + bead.r <= ImeBubbleModel::kW && bead.y - bead.r >= 0 && bead.y + bead.r <= ImeBubbleModel::kH,
                        "even the largest burst stays inside the window");
            }
        }
    });
    run("a newcomer keeps the first bubble's size, never the grown skin's", [&] {
        // A tuning whose two ends differ plainly: the skin grows from 20 to 51
        // DIP, but every newcomer is still born the first bubble's size — its
        // touch radius does not inherit the skin's growth.
        ImeBubbleTuning tuning;
        tuning.startRadius = 20;
        tuning.endRadius = 51;
        ImeBubbleModel bubble;
        bubble.SetTuning(tuning);
        bubble.Begin(1000);
        for (int i = 0; i < 4; ++i)
            require(bubble.Fuse(1150 + i * 120.0), "each switch merges");
        const double launch = 1150 + 4 * 120.0; // a fresh newcomer, long after four fusions
        require(bubble.Fuse(launch), "one more switch merges");
        const auto atTouch = bubble.Step(launch + bubble.TouchMs());
        require(atTouch.grow > 1.2f, "the skin has grown well past its start by now");
        require(atTouch.lobeCount >= 1, "the newcomer is still in the frame at its own touch");
        const auto& own = atTouch.lobes[atTouch.lobeCount - 1];
        require(std::abs(own.r - 0.55f * tuning.startRadius) < 0.5f, "at its touch it is the first bubble's rim, not the grown skin's");
        require(ImeBubbleModel::kRadius * atTouch.grow > tuning.startRadius + 5, "while the skin it meets is clearly larger than the first bubble");
    });
    run("the size settings keep every frame inside the window", [&] {
        struct Case {
            float start, end;
            int merges;
        };
        // The clamp's own extremes: the smallest pair, the default, and the
        // largest — the end is capped at 52 for the very reason that a fully
        // merged burst at the top end sits a touch past the 240-DIP window, so
        // the largest pair is checked at the merges that still fit, while the
        // default is checked fully merged.
        const Case cases[] = {{16, 24, 10}, {32, 51, 10}, {40, 52, 6}};
        for (const Case& kase : cases) {
            ImeBubbleTuning tuning;
            tuning.startRadius = kase.start;
            tuning.endRadius = kase.end;
            ImeBubbleModel bubble;
            bubble.SetTuning(tuning);
            bubble.Begin(1000);
            for (int i = 0; i < kase.merges; ++i)
                require(bubble.Fuse(1150 + i * 120.0), "every switch merges");
            for (double at = 1008; at < bubble.EndMs(); at += 8) {
                const auto f = bubble.Step(at);
                if (!f.active)
                    continue;
                const float r = ImeBubbleModel::kRadius * f.scale;
                require(f.cy - r >= 0 && f.cy + r <= ImeBubbleModel::kH, "the skin never leaves the window");
                for (int i = 0; i < f.lobeCount; ++i) {
                    const auto& lobe = f.lobes[i];
                    require(lobe.y - lobe.r >= 0, "no newcomer leaves the window through the top");
                    if (lobe.y + lobe.r <= ImeBubbleModel::kH)
                        require(lobe.x - lobe.r >= 0 && lobe.x + lobe.r <= ImeBubbleModel::kW, "a newcomer inside the window stays inside it");
                }
                for (int i = 0; i < f.beadCount; ++i) {
                    const auto& bead = f.beads[i];
                    require(bead.x - bead.r >= 0 && bead.x + bead.r <= ImeBubbleModel::kW && bead.y - bead.r >= 0 && bead.y + bead.r <= ImeBubbleModel::kH,
                            "the burst stays inside the window");
                }
            }
        }
    });

    run("the status entry swaps its symbol for the language and back", [&] {
        const auto start = ImeSwapAt(0);
        require(start.running && start.out == 0 && start.in == 0, "the swap starts from the symbol");
        const auto out = ImeSwapAt(60);
        require(out.out > 0 && out.out < 1 && out.in == 0, "the symbol fades out first, quickly");
        const auto in = ImeSwapAt(200);
        require(in.out == 1 && in.in > 0 && in.in < 1, "then the language fades in");
        require(ImeSwapAt(600).out == 1 && ImeSwapAt(600).in == 1, "the announcement holds");
        const auto leaving = ImeSwapAt(1200);
        require(leaving.out == 1 && leaving.in > 0 && leaving.in < 1, "the language leaves before the symbol returns");
        const auto back = ImeSwapAt(1300);
        require(back.out > 0 && back.out < 1 && back.in == 0, "the symbol fades back in last");
        const auto done = ImeSwapAt(1400);
        require(!done.running && done.out == 0 && done.in == 0, "and the entry settles where it was");
        require(!ImeSwapAt(-1).running, "clock skew shows nothing");
        for (double at : {0.0, 60.0, 110.0, 200.0, 600.0, 1000.0, 1200.0, 1300.0})
            require(ImeSwapAt(at).prev == 0, "a normal pass has no outgoing glyph");
    });
    run("a new announcement crossfades from a glyph still showing", [&] {
        require(!ImeSwapShowing(-1) && !ImeSwapShowing(0) && !ImeSwapShowing(60) && !ImeSwapShowing(109.9), "nothing shows while the symbol fades out");
        require(ImeSwapShowing(110) && ImeSwapShowing(200) && ImeSwapShowing(600) && ImeSwapShowing(1099.9),
                "the glyph is on screen from its fade-in until its own fade-out");
        require(!ImeSwapShowing(1100) && !ImeSwapShowing(1400), "and not after that");

        const auto start = ImeSwapAt(0, true);
        require(start.running && start.out == 1 && start.in == 0 && start.prev == 1, "a continued pass keeps the symbol away and starts on the old glyph");
        const auto mid = ImeSwapAt(75, true);
        require(mid.out == 1 && mid.in > 0 && mid.in < 1 && mid.prev > 0 && mid.prev < 1, "the old glyph crossfades into the new one");
        const auto arrived = ImeSwapAt(150, true);
        require(arrived.out == 1 && arrived.in == 1 && arrived.prev == 0, "the new glyph alone holds");
        require(ImeSwapAt(600, true).out == 1 && ImeSwapAt(600, true).in == 1, "the continued pass holds");
        const auto leaving = ImeSwapAt(1050, true);
        require(leaving.out == 1 && leaving.in < 1, "the new glyph leaves with the symbol still away");
        const auto back = ImeSwapAt(1150, true);
        require(back.out > 0 && back.out < 1, "then the symbol returns");
        const auto ending = ImeSwapAt(1230, true);
        require(ending.out < 1, "almost back to the symbol");
        require(!ImeSwapAt(1240, true).running, "the continued pass is shorter: no fade-out first");
    });
    run("a switch announces at its first sample, never twice", [&] {
        ImeWatch watch;
        const HWND w = reinterpret_cast<HWND>(1);
        const auto zh = Status(0x08040804, true, true, true);
        const auto en = Status(0x08040804, true, true, false);
        require(!watch.Sample(w, zh, 1000), "the first poll only seeds the baseline");
        require(watch.Sample(w, en, 1120), "the switch follows the hand at its first sample");
        require(!watch.state().native, "the announced state is the newest one");
        require(!watch.Sample(w, en, 1380), "the same state never announces twice");
        require(watch.Sample(w, zh, 1500), "switching back announces too");
    });
    run("the two dwells are the user's; a switch is instant by default", [&] {
        ImeWatch immediate;
        const HWND w = reinterpret_cast<HWND>(1);
        const auto zh = Status(0x08040804, true, true, true);
        const auto en = Status(0x08040804, true, true, false);
        immediate.SetSettle(0, 0);
        immediate.Sample(w, zh, 1000);
        require(immediate.Sample(w, en, 1010), "zero dwell follows the hand at once");
        ImeWatch slow;
        slow.SetSettle(600, 900);
        slow.Sample(w, zh, 1000);
        require(!slow.Sample(w, en, 1500), "a longer dwell the user chose is respected");
        require(!slow.Sample(w, en, 2000), "still inside the dwell");
        require(slow.Sample(w, en, 2120), "and then announces");
        ImeWatch defaults;
        defaults.Sample(w, zh, 1000);
        require(defaults.Sample(w, en, 1010), "the switch default is instant");
        // The case default still waits for the CapsLock latch.
        ImeWatch latin;
        const HWND w2 = reinterpret_cast<HWND>(2);
        const auto lower = Status(0x04090409, false, false, false, false);
        ImeStatus caps = lower;
        caps.caps = caps.upper = true;
        latin.Sample(w2, lower, 1000);
        require(!latin.Sample(w2, caps, 1120), "a CapsLock flip is not instant");
        require(latin.Sample(w2, caps, 1400), "the case default is 250 ms");
    });
    run("a switch that returns inside its dwell is not news", [&] {
        // A -> B -> A with a nonzero state dwell: the latest sample is the
        // baseline again, so the window closes silently instead of announcing B.
        ImeWatch watch;
        const HWND w = reinterpret_cast<HWND>(1);
        const auto zh = Status(0x08040804, true, true, true);
        const auto en = Status(0x08040804, true, true, false);
        watch.SetSettle(600, 250);
        watch.Sample(w, zh, 1000);
        require(!watch.Sample(w, en, 1100), "the state dwell holds the announcement");
        require(!watch.Sample(w, zh, 1200), "back at the baseline inside the window");
        require(!watch.Sample(w, zh, 1900), "nothing to announce at the deadline");
        require(watch.state().native, "the baseline stayed shown");
        ImeWatch again;
        again.SetSettle(600, 250);
        again.Sample(w, zh, 2000);
        require(!again.Sample(w, en, 2100), "a differing state opens the window");
        require(again.Sample(w, en, 2800), "and still announces when it lasts");
    });
    run("a Shift never announces, however long it is held", [&] {
        ImeWatch watch;
        const HWND w = reinterpret_cast<HWND>(1);
        const auto lower = Status(0x04090409, false, false, false, false);
        const auto capital = Status(0x04090409, false, false, false, true);
        watch.Sample(w, lower, 1000);
        require(!watch.Sample(w, capital, 1120), "a capital's Shift is not news");
        require(!watch.Sample(w, capital, 2280), "a long press is still only a temporary mode");
        require(!watch.Sample(w, lower, 2400), "letting go is not news either");
        // The case dwell now belongs to CapsLock, whose latch is the state.
        ImeWatch latch;
        latch.SetSettle(250, 0);
        ImeStatus caps = lower;
        caps.caps = caps.upper = true;
        latch.Sample(w, lower, 1000);
        require(latch.Sample(w, caps, 1120), "a case dwell of zero announces the CapsLock at the first sample");
    });
    run("CapsLock is news in every mode, a Shift is never", [&] {
        // CapsLock latches: a flip announces even while Chinese composes 中
        // (the glyph stays 中, but the announcement still shows). Shift only
        // opens a temporary mode — in every IME and every mode — so it is
        // never the news itself.
        const ImeStatus zh = Status(0x08040804, true, true, true);
        ImeStatus zhCaps = zh;
        zhCaps.caps = zhCaps.upper = true;
        require(!zh.SameAs(zhCaps), "CapsLock on is news while Chinese composes");
        require(!zhCaps.SameAs(zh), "and so is CapsLock off");
        ImeStatus zhShift = zh;
        zhShift.upper = true; // Shift alone: caps stays as it was
        require(zh.SameAs(zhShift), "a Shift alone is not news while Chinese composes");
        const ImeStatus en = Status(0x04090409, false, false, false);
        ImeStatus enCaps = en;
        enCaps.caps = enCaps.upper = true;
        require(!en.SameAs(enCaps), "CapsLock in latin is news");
        ImeStatus enShift = en;
        enShift.upper = true;
        require(en.SameAs(enShift), "a Shift capital in latin is typing, not news");
    });
    run("a CapsLock flip settles like a case, and a quick double flip clears", [&] {
        ImeWatch watch;
        const HWND w = reinterpret_cast<HWND>(7);
        const ImeStatus zh = Status(0x08040804, true, true, true);
        ImeStatus caps = zh;
        caps.caps = caps.upper = true;
        watch.Sample(w, zh, 1000);
        require(!watch.Sample(w, caps, 1100), "the case dwell holds the announcement");
        require(watch.Sample(w, caps, 1400), "a held CapsLock announces at the case deadline");
        require(watch.state().caps, "the announced state is the locked one");
        ImeWatch quick;
        quick.Sample(w, zh, 2000);
        require(!quick.Sample(w, caps, 2100), "locked inside the window");
        require(!quick.Sample(w, zh, 2200), "unlocked again inside the window");
        require(!quick.Sample(w, zh, 2600), "nothing to announce");
    });
    run("another window brings its own layout without news", [&] {
        ImeWatch watch;
        const HWND a = reinterpret_cast<HWND>(1), b = reinterpret_cast<HWND>(2);
        watch.Sample(a, Status(0x08040804, true, true, true), 1000);
        require(!watch.Sample(b, Status(0x04090409, false, false, false), 1200), "focus alone is not a switch");
        require(watch.state().layout == 0x04090409, "the new window seeds the baseline");
        require(watch.Sample(b, Status(0x08040804, true, true, true), 1400), "its own switch announces at the next poll");
    });
    run("a stalled poll is not a state", [&] {
        ImeWatch watch;
        const HWND w = reinterpret_cast<HWND>(1);
        const auto zh = Status(0x08040804, true, true, true);
        watch.Sample(w, zh, 1000);
        ImeStatus stalled = zh; // what a timed-out control message leaves behind
        stalled.open = false;
        stalled.reliable = false;
        require(!watch.Sample(w, stalled, 1200), "a stalled reading neither announces nor is believed");
        require(!watch.Sample(w, zh, 1400), "the state stands unchanged");
    });
    run("a stalled poll of the same layout carries the last conversion state", [&] {
        ImeStatus last = Status(0x08040804, true, true, true);
        last.fullShape = true;
        // What a timed-out read leaves: the identity read, the state defaults.
        ImeStatus now = Status(0x08040804, true, false, false);
        now.reliable = false;
        ImeCarryForward(now, last);
        require(now.open && now.native && now.fullShape, "the last conversion state is carried");
        require(now.layout == last.layout && now.ime, "the identity stays as it was read");
        require(!now.reliable, "the carried sample is still not believed");
        ImeStatus other = Status(0x04090409, true, false, false);
        other.reliable = false;
        ImeCarryForward(other, last);
        require(!other.open && !other.native && !other.fullShape, "another layout is not the old state");
        ImeStatus confirmed = Status(0x08040804, true, false, false);
        ImeCarryForward(confirmed, last);
        require(!confirmed.open && !confirmed.native && !confirmed.fullShape, "a complete read is believed as it is");
        ImeStatus plain = Status(0x08040804, false, false, false);
        plain.reliable = false;
        ImeCarryForward(plain, last);
        require(!plain.open && !plain.native && !plain.fullShape, "a plain layout carries no IME state");
        ImeStatus blank;
        ImeCarryForward(blank, last);
        require(blank.empty() && !blank.open, "an empty read is not filled");
        ImeStatus unreadable = Status(0x08040804, true, false, false);
        unreadable.reliable = false;
        ImeCarryForward(unreadable, ImeStatus{});
        require(!unreadable.open, "nor is one carried from an empty last state");
    });
    run("each switch of a rapid run announces — a bubble per switch", [&] {
        ImeWatch watch;
        const HWND w = reinterpret_cast<HWND>(1);
        const auto zh = Status(0x08040804, true, true, true);
        const auto en = Status(0x08040804, true, true, false);
        watch.Sample(w, zh, 1000);
        require(watch.Sample(w, en, 1060), "the first switch");
        require(watch.Sample(w, zh, 1120), "the second, 60 ms later");
        require(watch.Sample(w, en, 1180), "the third");
        require(watch.Sample(w, zh, 1240), "the fourth — none is swallowed by the next");
        // The model starts each as its own newcomer (its tests); a typed
        // capital is not news at the default case dwell.
        ImeWatch latin;
        const HWND w2 = reinterpret_cast<HWND>(2);
        latin.Sample(w2, Status(0x04090409, false, false, false), 1000);
        require(!latin.Sample(w2, Status(0x04090409, false, false, false, true), 1120), "a typed capital is not news");
    });
    run("a merge is born below the window, rises, wobbles and lengthens the life", [&] {
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        const double fuseAt = 1200;
        require(bubble.Mergeable(fuseAt), "a fresh bubble is mergeable");
        require(bubble.Fuse(fuseAt), "the merge happens");
        require(!bubble.Mergeable(bubble.PopAt()), "and closes when the burst begins");
        const double touch = fuseAt + bubble.TouchMs();
        const auto first = bubble.Step(fuseAt);
        require(first.lobeCount == 1, "one newcomer");
        require(first.lobes[0].r > 2, "the newcomer is a visible bubble");
        require(first.lobes[0].y - first.lobes[0].r >= ImeBubbleModel::kH - 0.5f, "it is born below the window's bottom edge, off the screen");
        // It only ever rises until it meets the skin.
        float prevY = first.lobes[0].y;
        for (double t = fuseAt + 8; t < touch; t += 8) {
            const auto f = bubble.Step(t);
            require(f.lobes[0].y <= prevY + 0.001f, "the newcomer rises monotonically to the skin");
            prevY = f.lobes[0].y;
        }
        const auto later = bubble.Step(fuseAt + 300);
        require(later.lobes[0].r > 0.5f, "the lobe is still being swallowed");
        require(std::hypot(later.lobes[0].x - later.cx, later.lobes[0].y - later.cy) < std::hypot(first.lobes[0].x - first.cx, first.lobes[0].y - first.cy),
                "it slides into the skin");
        require(std::abs(bubble.Step(touch + 50).wob) > 0.3f, "the touch rocks the skin like liquid");
        const auto doneFrame = bubble.Step(fuseAt + bubble.AbsorbMs() + 20);
        require(doneFrame.lobeCount == 0, "and is gone once swallowed");
        require(std::abs(bubble.Step(touch + ImeBubbleModel::kWobbleMs + 40).wob) < 0.01f, "the wobble dies down");
        const double hold = bubble.EndMs() - fuseAt;
        require(hold >= ImeBubbleModel::kPopAt + ImeBubbleModel::kSwellMs + ImeBubbleModel::kBurstMs - 1,
                "a merge keeps the bubble a whole life longer than its first");
        const auto before = bubble.EndMs();
        require(bubble.Fuse(2000), "the next merge lands while it lives");
        require(bubble.EndMs() > before, "and lengthens the life further");
    });
    run("a newcomer is born off the screen and then only rises", [&] {
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        require(bubble.Fuse(1200), "it merges");
        const auto born = bubble.Step(1201);
        require(born.lobeCount == 1, "the newcomer is drawn at once");
        require(born.lobes[0].y - born.lobes[0].r >= ImeBubbleModel::kH - 0.5f, "it is born entirely below the window, off the screen");
        require(born.lobes[0].y + born.lobes[0].r > ImeBubbleModel::kH, "its whole body is below the window's bottom");
        float prevY = born.lobes[0].y;
        int frames = 0;
        for (double t = 1208; t < 1200 + bubble.AbsorbMs(); t += 8) {
            const auto f = bubble.Step(t);
            if (f.lobeCount == 0)
                break;
            require(f.lobes[0].y <= prevY + 0.05f, "the newcomer only ever rises into the frame");
            prevY = f.lobes[0].y;
            ++frames;
        }
        require(frames > 0 && prevY < ImeBubbleModel::kCyRest + ImeBubbleModel::kRadius, "by the end it has climbed right up to the skin");
    });
    run("the slow rise gives way to a far quicker approach into the skin", [&] {
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        require(bubble.Fuse(1200), "it merges");
        // The newcomer's centre y, sampled across each act: how far it moves
        // per millisecond there.
        auto avgSpeed = [&](double from, double to) {
            float prev = bubble.Step(1200 + from).lobes[0].y;
            double moved = 0;
            int n = 0;
            for (double t = from + 8; t <= to; t += 8) {
                const float y = bubble.Step(1200 + t).lobes[0].y;
                moved += std::abs(prev - y);
                prev = y;
                ++n;
            }
            return n ? float(moved / (n * 8.0)) : 0.f;
        };
        const float slow = avgSpeed(0, bubble.RiseEndMs());
        const float fast = avgSpeed(bubble.RiseEndMs(), bubble.TouchMs());
        require(slow > 0, "the newcomer is moving during its slow rise");
        require(fast >= 2.f * slow, "the fast approach is at least twice the slow rise's pace");
    });
    run("the touch pivots the whole hit: size, squash and wobble await it", [&] {
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        require(bubble.Fuse(1200), "it merges");
        const double touch = 1200 + bubble.TouchMs();
        require(std::abs(bubble.TouchFrac() - 0.82) < 0.001, "the touch sits at about 0.82 of the absorb by default");
        require(std::abs(bubble.Step(touch - 1).grow - 1.f) < 1e-4f, "the size waits for the touch");
        require(bubble.Step(touch + 40).grow > 1.f, "the touch then presses the skin wider");
        require(std::abs(bubble.Step(touch - 1).wob) < 1e-6f, "the skin is still before the touch");
        require(std::abs(bubble.Step(touch + 50).wob) > 0.3f, "the touch rocks it, like liquid");
        // The squash is deepest at the touch, not before it: a firmer skin holds
        // more of its size at that very moment.
        ImeBubbleModel stiff;
        ImeBubbleTuning firm;
        firm.compression = 0.f;
        stiff.SetTuning(firm);
        stiff.Begin(1000);
        require(stiff.Fuse(1200), "a skin that cannot be squashed merges too");
        const float dipAtTouch = bubble.Step(touch).scale / stiff.Step(touch).scale;
        const float dipBefore = bubble.Step(1200 + 0.3 * bubble.AbsorbMs()).scale / stiff.Step(1200 + 0.3 * bubble.AbsorbMs()).scale;
        require(dipAtTouch < dipBefore, "the hit compresses the skin deepest at the touch");
    });
    run("a newcomer's whole journey: off the screen, into the frame, swallowed", [&] {
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        require(bubble.Fuse(1200), "it merges");
        const double A = bubble.AbsorbMs();
        const auto away = bubble.Step(1200 + 0.02 * A);
        require(away.lobeCount == 1 && away.lobes[0].y - away.lobes[0].r >= ImeBubbleModel::kH - 0.5f, "it starts off the screen");
        const auto within = bubble.Step(1200 + 0.78 * A);
        require(within.lobes[0].y - within.lobes[0].r >= 0 && within.lobes[0].y + within.lobes[0].r <= ImeBubbleModel::kH,
                "by the fast approach it is wholly inside the frame");
        require(std::hypot(within.lobes[0].x - within.cx, within.lobes[0].y - within.cy) < std::hypot(away.lobes[0].x - away.cx, away.lobes[0].y - away.cy),
                "and nearer the skin than it began");
        require(std::abs(bubble.Step(1200 + A - 0.5).lobes[0].r) < 1.f, "its last frame is a sliver");
        require(bubble.Step(1200 + A + 1).lobeCount == 0, "and it is swallowed with nothing left");
    });
    run("rapid merges rise together and fuse side by side", [&] {
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        const double f1 = 1200, f2 = 1300; // 100 ms apart, far faster than one absorb
        require(bubble.Fuse(f1) && bubble.Fuse(f2), "both rapid states merge");
        const auto first = bubble.Step(f1); // the first newcomer, exactly at its launch
        require(first.lobeCount == 1 && first.lobes[0].r > 2, "the first newcomer rises at once");
        require(first.lobes[0].y - first.lobes[0].r >= ImeBubbleModel::kH - 0.5f, "it is born below the window's bottom edge");
        const auto both = bubble.Step(f2);
        require(both.lobeCount == 2, "the second rises too: it does not wait for the first swallow");
        require(both.lobes[1].y - both.lobes[1].r >= ImeBubbleModel::kH - 0.5f, "its own birth is below the window too");
        require(both.lobes[1].y > both.lobes[0].y, "the first is already climbing above it");
        require(std::abs(both.lobes[0].x - both.lobes[1].x) > 0.01f, "each newcomer draws its own place on the emerge line");
        // Each touches its own touch-time after its own launch.
        const double touch1 = f1 + bubble.TouchMs(), touch2 = f2 + bubble.TouchMs();
        require(std::abs(bubble.Step(touch1 - 1).grow - 1.f) < 1e-4f, "no touch has come yet");
        require(bubble.Step(touch1 + 40).grow > 1.f, "the first touch presses the skin wider");
        require(bubble.Step(touch2 - 1).grow > bubble.ScaleAt(2), "and has passed the first step's size before the second touch");
        require(bubble.Step(touch2 + 40).grow > bubble.Step(touch2 - 1).grow, "the second touch adds its own");
        // A launch adds nothing to the size, and a later touch never takes back
        // the step an earlier one took: the sizes are added up, not replaced.
        require(std::abs(bubble.Step(f2).grow - bubble.Step(f2 - 1).grow) < 1e-6f, "a launch changes no size at all");
        ImeBubbleModel solo;
        solo.Begin(1000);
        solo.Fuse(f1);
        for (double t = f1 + 1; t < bubble.PopAt(); t += 13)
            require(bubble.Step(t).grow >= solo.Step(t).grow - 1e-5f, "the second rise never takes the first one's step back");
        float peak = 0;
        for (int ms = 0; ms <= 340; ms += 5) // the first touch's own spring, still overshooting
            peak = std::max(peak, bubble.Step(touch1 + ms).grow);
        require(peak > bubble.ScaleAt(2) + 0.05f, "the first touch still overshoots visibly");
        // Both steps have landed by the burst: three bubbles' size.
        const auto nearPop = bubble.Step(bubble.PopAt() - 1);
        require(std::abs(nearPop.grow - bubble.ScaleAt(3)) < 0.01f, "once both have settled the skin is two steps larger");
    });
    run("two states in the same millisecond both rise", [&] {
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        require(bubble.Fuse(1200) && bubble.Fuse(1200), "both merge, at the very same time");
        require(bubble.fusions() == 3, "both count");
        const auto both = bubble.Step(1200);
        require(both.lobeCount == 2, "both newcomers rise");
        const float birthDip = ImeBubbleTuning{}.lobeBirthDip;
        for (int i = 0; i < both.lobeCount; ++i) {
            require(both.lobes[i].r > 1, "each is a real bubble");
            require(std::abs(both.lobes[i].u) < 1e-6f, "and both have just launched");
            require(std::abs(both.lobes[i].y - (ImeBubbleModel::kH + birthDip)) < 0.01f,
                    "both are born with their centre the birth dip below the window's bottom edge");
        }
        const double touch = 1200 + bubble.TouchMs();
        require(std::abs(bubble.Step(touch - 1).grow - 1.f) < 1e-4f, "neither has touched yet");
        require(bubble.Step(touch + 1).grow > 1.f, "both touches press at once");
        // Their squashes add up, but the skin can only be squashed so far.
        ImeBubbleModel firm;
        ImeBubbleTuning stiff;
        stiff.compression = 0;
        firm.SetTuning(stiff);
        firm.Begin(1000);
        require(firm.Fuse(1200) && firm.Fuse(1200), "a skin that cannot be squashed merges too");
        require(std::abs(bubble.Step(touch).scale / firm.Step(touch).scale - 0.90f) < 1e-4f, "two hits at once squash to the limit, not twice as deep");
        // Both steps have all but landed by the pop (the later touch in the
        // longer absorb leaves the spring at about 94 % of the way, as above).
        require(std::abs(bubble.Step(bubble.PopAt() - 1).grow - bubble.ScaleAt(3)) < 0.02f, "and both steps are the skin's by the pop");
    });
    run("ten merges, ten newcomers, one newest state at a time", [&] {
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        double now = 1200;
        const float birthDip = ImeBubbleTuning{}.lobeBirthDip;
        for (int i = 1; i <= 10; ++i, now += 150) {
            require(bubble.Fuse(now), "every switch before the burst merges");
            const auto own = bubble.Step(now + 1);
            require(own.lobeCount >= 1 && own.lobeCount <= ImeBubbleModel::kMaxFrameLobes, "the frame carries the newcomers in flight");
            const auto& newest = own.lobes[own.lobeCount - 1];
            require(newest.r > 1 && std::abs(newest.y - (ImeBubbleModel::kH + birthDip)) < 0.01f,
                    "the newcomer of this very switch is born below the window at once, not after the others");
        }
        require(bubble.fusions() == 11, "ten merges on top of the first bubble");
        require(bubble.Step(now - 150 + 1).lobeCount == 6, "at 150 ms apart six rise side by side");
        const auto nearPop = bubble.Step(bubble.PopAt() - 1);
        require(std::abs(nearPop.grow - bubble.ScaleAt(ImeBubbleModel::kMaxFusions)) < 0.02f, "the size has reached its ten-merge cap");
        require(nearPop.lobeCount == 0, "and every newcomer is swallowed before the burst");
    });
    run("a frame carries the newest newcomers, the model counts them all", [&] {
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        // Eight switches within 40 ms: more newcomers at once than a frame can
        // draw, all of them real merges.
        for (int i = 0; i < 8; ++i)
            require(bubble.Fuse(1200 + i * 5.0), "every switch merges");
        require(bubble.fusions() == 9, "the model counts them all");
        const auto f = bubble.Step(1240);
        require(f.lobeCount == ImeBubbleModel::kMaxFrameLobes, "the frame is full");
        const float thirdU = float((1240 - (1200 + 2 * 5.0)) / bubble.AbsorbMs());
        const float newestU = float((1240 - (1200 + 7 * 5.0)) / bubble.AbsorbMs());
        require(std::abs(f.lobes[0].u - thirdU) < 1e-5f, "the oldest newcomers made room");
        require(std::abs(f.lobes[f.lobeCount - 1].u - newestU) < 1e-5f, "the newest is always there");
        require(f.lobes[0].u > f.lobes[f.lobeCount - 1].u, "oldest first, newest last");
        require(std::abs(bubble.Step(bubble.PopAt() - 1).grow - bubble.ScaleAt(9)) < 0.02f, "the size takes every one of the eight, drawn or not");
    });
    run("the melt stays inside the renderer's six parts", [&] {
        static_assert(ImeBubbleModel::kMaxMeltParts == kMaxGlassParts, "the renderer's own parts budget");
        for (int n = 0; n <= ImeBubbleModel::kMaxFrameLobes; ++n) {
            for (bool split : {false, true}) {
                const auto b = ImeBubbleModel::MeltFor(n, split);
                require(b.skinParts >= 1 && b.skinParts <= 2, "the skin is one piece or two");
                require(b.lobes == ImeBubbleModel::DrawnLobes(n, split), "one budget, one drawn count");
                require(b.lobes >= 0 && b.lobes <= n, "never more newcomers than the frame carries");
                require(b.skinParts + b.lobes <= ImeBubbleModel::kMaxMeltParts, "the skin and the newcomers fit the one melt");
                require(n <= 4 ? b.lobes == n : b.lobes == ImeBubbleModel::kMaxMeltParts - b.skinParts,
                        "every newcomer fits while there is room, then the newest do");
                if (split && b.skinParts == 1)
                    require(n >= ImeBubbleModel::kMaxMeltParts - 1, "the split only goes for a newcomer's slot");
            }
        }
        require(ImeBubbleModel::DrawnLobes(0, true) == 0, "no newcomer, nothing to draw");
        require(ImeBubbleModel::DrawnLobes(4, true) == 4, "four newcomers still split the skin");
        require(ImeBubbleModel::DrawnLobes(5, true) == 5, "a fifth newcomer drops the split, not itself");
        require(ImeBubbleModel::DrawnLobes(6, true) == 5, "six newcomers: the oldest is dropped, the newest kept");
        require(ImeBubbleModel::DrawnLobes(6, false) == 5, "a whole skin has no room for a sixth either");
    });
    run("ten rapid switches keep every frame inside the window", [&] {
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        for (int i = 0; i < 10; ++i)
            bubble.Fuse(1150 + i * 120.0);
        require(bubble.fusions() == 11, "ten switches on top of the first bubble");
        for (double t = 1008; t < bubble.EndMs(); t += 8) {
            const auto f = bubble.Step(t);
            if (!f.active)
                continue;
            require(f.lobeCount <= ImeBubbleModel::kMaxFrameLobes, "a frame never overfills");
            const float r = ImeBubbleModel::kRadius * f.scale;
            require(f.cy - r >= 0 && f.cy + r <= ImeBubbleModel::kH, "the skin stays in the window");
            for (int i = 0; i < f.lobeCount; ++i) {
                const auto& lobe = f.lobes[i];
                require(std::isfinite(lobe.x) && std::isfinite(lobe.y) && std::isfinite(lobe.r) && lobe.r > 0, "every newcomer drawn is a real bubble");
                require(lobe.u >= 0 && lobe.u < 1, "each has its own place in its absorb");
                // A newcomer is born below the window and clipped until it has
                // risen in; once in the frame it stays in the frame.
                require(lobe.y - lobe.r >= 0, "no newcomer ever leaves the window through the top");
                if (lobe.y + lobe.r <= ImeBubbleModel::kH)
                    require(lobe.x - lobe.r >= 0 && lobe.x + lobe.r <= ImeBubbleModel::kW && lobe.y - lobe.r >= 0, "and stays inside the window");
            }
            for (int i = 0; i < f.beadCount; ++i) {
                const auto& bead = f.beads[i];
                require(bead.x - bead.r >= 0 && bead.x + bead.r <= ImeBubbleModel::kW && bead.y - bead.r >= 0 && bead.y + bead.r <= ImeBubbleModel::kH,
                        "the burst stays inside the window too");
            }
        }
    });
    run("the foam thins away to nothing with a last sparkle", [&] {
        ImeBubbleModel bubble;
        bubble.Begin(1000);
        const double burstAt = 1000 + ImeBubbleModel::kPopAt + ImeBubbleModel::kSwellMs;
        const int n = bubble.Step(burstAt + 4).beadCount;
        require(n >= ImeBubbleModel::kMinBeads, "the burst has its foam");
        float prev[ImeBubbleModel::kMaxBeads], lastAlive[ImeBubbleModel::kMaxBeads], peak[ImeBubbleModel::kMaxBeads];
        for (int i = 0; i < n; ++i) {
            prev[i] = -1;
            lastAlive[i] = 0;
            peak[i] = 0;
        }
        for (double t = burstAt + 4; t < bubble.EndMs(); t += 4) {
            const auto f = bubble.Step(t);
            if (!f.active)
                break;
            for (int i = 0; i < n; ++i) {
                const auto& bead = f.beads[i];
                require(bead.r <= (prev[i] < 0 ? 1e9f : prev[i] + 0.001f), "a drop only ever thins, never jumps back");
                if (bead.r > 0)
                    lastAlive[i] = bead.r;
                peak[i] = std::max(peak[i], bead.glint);
                prev[i] = bead.r;
            }
        }
        for (int i = 0; i < n; ++i) {
            require(peak[i] > 0.3f, "every drop sparkles once just before it goes");
            require(prev[i] == 0, "every drop is gone by the end");
            require(lastAlive[i] < 2.f, "it thinned to a sliver first — it did not blink off");
        }
    });
    run("a bigger bubble throws more foam, and a fuller swarm scatters wider", [&] {
        const ImeBubbleModel defaults{};
        require(defaults.BeadsFor(1, 0) >= ImeBubbleModel::kMinBeads, "the base swarm");
        for (int fusions = 2; fusions <= ImeBubbleModel::kMaxFusions; ++fusions)
            require(defaults.BeadsFor(fusions, 2) >= defaults.BeadsFor(fusions - 1, 2), "never fewer drops with more merges");
        require(ImeBubbleModel::kMaxBeads == 30, "the cap is thirty drops");
        require(defaults.BeadsFor(1, 4) <= 13, "a lone bubble's swarm stays small");
        require(defaults.BeadsFor(ImeBubbleModel::kMaxFusions, 0) == ImeBubbleModel::kMaxBeads, "ten merges reach the cap even at the calmest temper");
        require(defaults.BeadsFor(ImeBubbleModel::kMaxFusions, 4) == ImeBubbleModel::kMaxBeads, "up to the cap");
        // The size settings carry into the foam: a smaller pair never drops
        // below the base swarm, a larger first bubble already holds more, and
        // the cap still holds at the top.
        ImeBubbleTuning smallTuning, largeTuning;
        smallTuning.startRadius = 16;
        smallTuning.endRadius = 24;
        largeTuning.startRadius = 40;
        largeTuning.endRadius = 52;
        ImeBubbleModel smaller, bigger;
        smaller.SetTuning(smallTuning);
        bigger.SetTuning(largeTuning);
        require(smaller.BeadsFor(1, 4) == ImeBubbleModel::kMinBeads, "a bubble set small still throws the base swarm, never fewer");
        require(smaller.BeadsFor(ImeBubbleModel::kMaxFusions, 0) == ImeBubbleModel::kMinBeads, "and keeps it: its skin holds no more liquid");
        require(bigger.BeadsFor(1, 4) > defaults.BeadsFor(1, 4), "a bubble born larger holds more foam at once");
        require(bigger.BeadsFor(ImeBubbleModel::kMaxFusions, 0) == ImeBubbleModel::kMaxBeads, "and the cap still holds");
        require(ImeBubbleModel::SpreadFor(ImeBubbleModel::kMaxBeads, 1.f) > ImeBubbleModel::SpreadFor(ImeBubbleModel::kMinBeads, 1.f),
                "more drops, a wider scatter");
        require(std::abs(ImeBubbleModel::SpreadFor(ImeBubbleModel::kMinBeads, 1.f) - 1.f) < 0.001f, "the base swarm keeps its temper");
        // And a merged bubble's burst really carries more drops than a plain one.
        ImeBubbleModel plain;
        plain.Begin(1000);
        const int plainCount = plain.Step(1000 + ImeBubbleModel::kPopAt + ImeBubbleModel::kSwellMs + 10).beadCount;
        ImeBubbleModel fused;
        fused.Begin(1000);
        for (int i = 0; i < 9; ++i)
            fused.Fuse(1150 + i * 120.0);
        const auto burst = fused.Step(fused.PopAt() + ImeBubbleModel::kSwellMs + 10);
        require(burst.beadCount > plainCount, "a merged bubble's skin holds more foam");
        require(burst.beadCount >= ImeBubbleModel::kMinBeads + 5, "and the difference is visible in the count");
    });
    run("a ten-merge bubble's foam is plainly larger than a lone bubble's", [&] {
        ImeBubbleModel lone;
        lone.Begin(1000);
        const int loneCount = lone.Step(1000 + ImeBubbleModel::kPopAt + ImeBubbleModel::kSwellMs + 10).beadCount;
        ImeBubbleModel merged;
        merged.Begin(1000);
        for (int i = 0; i < 10; ++i)
            merged.Fuse(1150 + i * 120.0);
        require(merged.fusions() == 11, "ten merges on top of the first bubble");
        const int mergedCount = merged.Step(merged.PopAt() + ImeBubbleModel::kSwellMs + 10).beadCount;
        require(mergedCount - loneCount >= 15, "a ten-merge bubble throws at least fifteen drops more than a lone one");
        require(mergedCount == ImeBubbleModel::kMaxBeads, "and it reaches the cap");
        require(loneCount >= ImeBubbleModel::kMinBeads && loneCount <= 13, "while a lone bubble's swarm stays small");
    });
    run("a fuller swarm flies further than a lone bubble's", [&] {
        // The bigger skin throws from a wider rim, on a fuller swarm's spread, so
        // its drops' mean reach from the burst's own centre is the larger — the
        // scatter grows with the bubble, not just the count.
        auto meanReach = [](ImeBubbleModel& bubble) {
            const auto burst = bubble.Step(bubble.PopAt() + ImeBubbleModel::kSwellMs + 40);
            const float bx = ImeBubbleModel::kCx, by = ImeBubbleModel::kCyRest - ImeBubbleModel::kFloatRise;
            double sum = 0;
            for (int i = 0; i < burst.beadCount; ++i)
                sum += std::hypot(double(burst.beads[i].x - bx), double(burst.beads[i].y - by));
            return burst.beadCount > 0 ? sum / burst.beadCount : 0.0;
        };
        ImeBubbleModel lone;
        lone.Begin(1000);
        ImeBubbleModel merged;
        merged.Begin(1000);
        for (int i = 0; i < 10; ++i)
            merged.Fuse(1150 + i * 120.0);
        require(meanReach(merged) > meanReach(lone), "the merged swarm scatters further on the whole");
    });
    run("every drop the burst throws gets a melt shape to ride in", [&] {
        require(ImeBubbleModel::kMaxMeltParts == kMaxGlassParts, "the shapes ride the renderer's own parts budget");
        require(ImeBubbleModel::ChunksFor(0) == 0 && ImeBubbleModel::ChunksFor(1) == 1 && ImeBubbleModel::ChunksFor(6) == 1 &&
                    ImeBubbleModel::ChunksFor(7) == 2 && ImeBubbleModel::ChunksFor(30) == 5,
                "one shape per six drops: 0/1/6/7/30 -> 0/1/1/2/5");
        for (int parts = 0; parts <= ImeBubbleModel::kMaxBeads; ++parts) {
            const int chunks = ImeBubbleModel::ChunksFor(parts);
            require(chunks * ImeBubbleModel::kMaxMeltParts >= parts, "the shapes carry every drop — the renderer's silent six-drop cut can never bite");
            require(chunks == 0 || (chunks - 1) * ImeBubbleModel::kMaxMeltParts < parts, "and no shape sits empty: the last one is the one with room");
            require(chunks <= ImeBubbleModel::ChunksFor(ImeBubbleModel::kMaxBeads), "no swarm ever needs more shapes than the largest bubble");
        }
    });
    run("the no-bubble list follows the stage games", [&] {
        // First sync: every stage entry is copied, trimmed, and only once.
        std::vector<std::wstring> games = SyncNoBubbleGames({}, {}, {L"gameA.exe", L"tencent.qq_", L"  gameB.exe  "});
        require(games == std::vector<std::wstring>{L"gameA.exe", L"tencent.qq_", L"gameB.exe"}, "the first sync copies every stage entry, trimmed");
        games = SyncNoBubbleGames({}, {}, {L"", L"   ", L"d:\\games\\foo\\"});
        require(games == std::vector<std::wstring>{L"d:\\games\\foo\\"}, "empty and blank entries are dropped");
        games = SyncNoBubbleGames({L"GameA.exe"}, {}, {L"gamea.exe", L"gameC.exe"});
        require(games == std::vector<std::wstring>{L"GameA.exe", L"gameC.exe"},
                "a case-only duplicate keeps the first spelling and adds nothing, a new one is appended");
        // A pinned entry is the user's own edit: a sync never re-adds it.
        games = SyncNoBubbleGames({L"gameA.exe"}, {L"gameb.exe"}, {L"gameA.exe", L"gameB.exe"});
        require(games == std::vector<std::wstring>{L"gameA.exe"}, "a pinned game is not re-added");
        // Additions only: an entry the stage list has since dropped stays here.
        games = SyncNoBubbleGames({L"keep.exe"}, {}, {L"other.exe"});
        require(games == std::vector<std::wstring>{L"keep.exe", L"other.exe"}, "an already-listed game is kept and a missing stage entry is appended");
        games = SyncNoBubbleGames({L"  a.exe ", L"A.EXE"}, {}, {});
        require(games == std::vector<std::wstring>{L"a.exe"}, "the games list itself is trimmed and deduplicated");
    });
    run("the bubble is muted only over a listed fullscreen game", [&] {
        const std::vector<std::wstring> games{L"game.exe", L"d:\\games\\foo\\"};
        require(!ImeBubbleMuted(false, games, L"c:\\games\\game.exe"), "the pause off never mutes");
        require(!ImeBubbleMuted(true, {}, L"c:\\games\\game.exe"), "an empty list never mutes");
        require(ImeBubbleMuted(true, games, L"c:\\games\\game.exe"), "an exe name matches at any path");
        require(ImeBubbleMuted(true, games, L"c:\\games\\GAME.EXE"), "the match ignores case");
        require(ImeBubbleMuted(true, games, L"d:\\games\\foo\\bin\\game.exe"), "a folder entry matches the programs under it");
        require(!ImeBubbleMuted(true, games, L"c:\\tools\\editor.exe"), "an unrelated path is not muted");
    });
    return failures ? 1 : 0;
}

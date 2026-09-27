// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The builtin BLUR-CHAIN metadata lints of the validator's surface arm: the Kawase
// pyramid's positional and base-scale checks, the separable gaussian pair's positional
// check, the needsBackdrop gate, and the customParams[0].x radius-slot arm with its four
// ways of getting the first scalar wrong.
//
// Its own executable because the family outgrew its host. test_surface_pack_validator.cpp
// covers everything else a surface pack's metadata can get wrong, and it sat fifteen lines
// under the file-size ceiling while this family kept growing: nine builtin passes, two
// families that read different things, and one lint per ordering mistake that still
// compiles and still resolves. The seam mirrors the production one, since
// packvalidator_surface_lints.cpp was split out of packvalidator_surface.cpp for the same
// reason, so the blur lints and the tests for the blur lints now move together.
//
// Every slot builds a deliberately-broken pack in a temp dir and asserts the diagnostic.
// The positive control is theCorrectKawaseChainIsNotLinted, which is what catches a lint
// that fires on a CORRECT chain; the sibling executable's aCleanSurfacePackReportsNoErrors
// is the broader one for a lint that fires on everything.
//
// Every negative pins an error COUNT as well as a substring, and the fixtures are built so
// that count is the lint under test plus nothing else. Two of these lints can fire on one
// fixture, so a membership assertion alone cannot tell a fixture that draws the lint under
// test from one that draws it plus another — and several fixtures did quietly start drawing a
// second lint when the gaussian family was added to two of the arms.
//
// "Plus nothing else" is the part that took a correction. An earlier version of this sentence
// claimed every negative was counted while two were not, and the worst of them
// (aReorderedKawaseChainIsLinted) drew FOUR errors behind an `errors > 0`: the lint under
// test plus three pieces of collateral this file exists to stop hiding. So a fixture here
// declares the canonical bufferScales, needsBackdrop and a leading blurRadius unless the slot
// is specifically about their absence.

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "packvalidatortesthelpers.h"

using namespace PackValidatorTest;

class TestSurfaceBlurChainLints : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// The builtin Kawase passes are bound to iChannel<index> BY POSITION and the
    /// seven frags hardcode which channel they read, so the chain composes in one
    /// order only. Every token here resolves and every file compiles, so nothing
    /// but an order check can catch a pack that lists them wrongly.
    ///
    /// Otherwise CLEAN by construction — canonical bufferScales, needsBackdrop and a
    /// leading blurRadius — so the count is the order lint alone. It used to declare
    /// none of the three and drew FOUR errors behind an `errors > 0`, three of them
    /// nothing to do with ordering. The assertion is the full clause too, because
    /// "positional" alone is also a substring of the buffer-array length lint's "aligned
    /// positionally". (It used to be a substring of the gaussian arm's message as well; the
    /// commit that wrote this sentence rewrote both gaussian messages and neither carries the
    /// word now, so the full clause is right for one reason rather than two.)
    void aReorderedKawaseChainIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(
            QStringLiteral("sf-kawase-order"),
            QJsonArray{surfaceParam(QStringLiteral("blurRadius"), QStringLiteral("float"), 32.0, 0.0, 256.0)});
        obj.insert(QStringLiteral("multipass"), true);
        // The correct seven with two DOWN levels transposed.
        obj.insert(QStringLiteral("bufferShaders"),
                   QJsonArray{QStringLiteral("builtin:kawase-down-1"), QStringLiteral("builtin:kawase-down-0"),
                              QStringLiteral("builtin:kawase-down-2"), QStringLiteral("builtin:kawase-down-3"),
                              QStringLiteral("builtin:kawase-up-0"), QStringLiteral("builtin:kawase-up-1"),
                              QStringLiteral("builtin:kawase-up-2")});
        obj.insert(QStringLiteral("bufferScales"), QJsonArray{0.25, 0.125, 0.0625, 0.03125, 0.0625, 0.125, 0.25});
        obj.insert(QStringLiteral("needsBackdrop"), true);

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-kawase-order"), obj,
                                             surfaceBodyReading({QStringLiteral("blurRadius")}));
        QVERIFY2(r.report.contains(QStringLiteral("are positional and must appear as bufferShaders[0..6]")),
                 qPrintable(r.report));
        QCOMPARE(r.errors, 1);
    }

    /// The positive control for the slot above: the chain in its declared order
    /// must NOT trip the order lint, or the lint would fail every blur pack.
    ///
    /// The pack declares a real `blurRadius` FIRST. It used to pass an empty
    /// parameters array, which tripped the no-scalar radius lint below. Then it
    /// declared no bufferScales, tripping the pyramid lint in that same block.
    /// Both times the name promised a clean pack, the pack WAS linted, and the
    /// substring assertions could not see it because each named a different arm.
    /// A positive control has to be clean on every arm it controls, and counting
    /// errors is the only assertion that holds that without being re-audited.
    void theCorrectKawaseChainIsNotLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(
            QStringLiteral("sf-kawase-ok"),
            QJsonArray{surfaceParam(QStringLiteral("blurRadius"), QStringLiteral("float"), 32.0, 0.0, 256.0)});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"),
                   QJsonArray{QStringLiteral("builtin:kawase-down-0"), QStringLiteral("builtin:kawase-down-1"),
                              QStringLiteral("builtin:kawase-down-2"), QStringLiteral("builtin:kawase-down-3"),
                              QStringLiteral("builtin:kawase-up-0"), QStringLiteral("builtin:kawase-up-1"),
                              QStringLiteral("builtin:kawase-up-2")});
        // The canonical pyramid, because the SAME `if (anyKawase)` block lints a
        // chain that declares none ("every pass renders at the pack-wide
        // bufferScale and the pyramid is not a pyramid").
        obj.insert(QStringLiteral("bufferScales"), QJsonArray{0.25, 0.125, 0.0625, 0.03125, 0.0625, 0.125, 0.25});
        // And the backdrop flag, for the same reason: without it the chain's first
        // pass samples a backdrop nothing captured, which that block also lints.
        // Leaving it off made this a "correct" pack that renders a transparent pane.
        obj.insert(QStringLiteral("needsBackdrop"), true);

        // The body READS blurRadius, which is surfaceBodyReading's stated purpose ("so the
        // declared-but-unread sweep stays quiet"). Passing {} against a pack that declares
        // one is harmless today, because the surface arm has no declared-but-unread sweep,
        // and it would fail loudly the day one is added — which is the right failure, but
        // only if the control is asking the question the helper is for.
        const PackResult r = validateSurface(tmp, QStringLiteral("sf-kawase-ok"), obj,
                                             surfaceBodyReading({QStringLiteral("blurRadius")}));
        QVERIFY2(!r.report.contains(QStringLiteral("positional")), qPrintable(r.report));
        QVERIFY2(!r.report.contains(QStringLiteral("customParams[0].x")), qPrintable(r.report));
        // COUNT the errors, do not grep for the arms you remembered. Both earlier
        // versions of this control built a linted pack and passed anyway, because
        // each asserted the absence of arms other than the one it tripped.
        QCOMPARE(r.errors, 0);
    }

    /// THE RADIUS SLOT, wrong-name arm. The builtin passes read the radius as
    /// customParams[0].x, which is the first SCALAR declared, so a pack that
    /// leads with some other scalar blurs by that control instead. Untested until
    /// now: a mutation deleting the lint failed nothing.
    void aKawaseChainNotLeadingWithBlurRadiusIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(
            QStringLiteral("sf-kawase-slot"),
            QJsonArray{surfaceParam(QStringLiteral("cornerRadius"), QStringLiteral("float"), 8.0, 0.0, 32.0),
                       surfaceParam(QStringLiteral("blurRadius"), QStringLiteral("float"), 32.0, 0.0, 256.0)});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"),
                   QJsonArray{QStringLiteral("builtin:kawase-down-0"), QStringLiteral("builtin:kawase-down-1"),
                              QStringLiteral("builtin:kawase-down-2"), QStringLiteral("builtin:kawase-down-3"),
                              QStringLiteral("builtin:kawase-up-0"), QStringLiteral("builtin:kawase-up-1"),
                              QStringLiteral("builtin:kawase-up-2")});
        // The canonical pyramid, so the count below is the radius arm and nothing else.
        obj.insert(QStringLiteral("bufferScales"), QJsonArray{0.25, 0.125, 0.0625, 0.03125, 0.0625, 0.125, 0.25});
        obj.insert(QStringLiteral("needsBackdrop"), true);

        const PackResult r =
            validateSurface(tmp, QStringLiteral("sf-kawase-slot"), obj,
                            surfaceBodyReading({QStringLiteral("cornerRadius"), QStringLiteral("blurRadius")}));
        QVERIFY2(r.report.contains(QStringLiteral("customParams[0].x")), qPrintable(r.report));
        // The full clause, not the bare id: this fixture declares cornerRadius and
        // no roundBottomCorners, which trips a separate NOTE naming cornerRadius, so
        // a bare-id assertion passed with the lint's .arg() deleted.
        QVERIFY2(r.report.contains(QStringLiteral("and that is 'cornerRadius' here")), qPrintable(r.report));
        // ONE, the radius arm alone. This used to pin 2 and disclose that the second was the
        // pyramid lint for an absent bufferScales — a count that would have failed, naming the
        // radius slot, if anyone narrowed a lint about buffer scales. Declaring the canonical
        // pyramid costs one line and makes the count mean what the slot is named for.
        QCOMPARE(r.errors, 1);
    }

    /// THE RADIUS SLOT, bool-first. paramPreamble pools everything that is not a
    /// "color" as a scalar, so a BOOL takes customParams[0].x in declaration order
    /// exactly as a float would and the chain blurs by it. The lint used to test
    /// `type == "float" || type == "int"`, skip the bool, and then report the SECOND
    /// declaration as "the first scalar" — so a pack leading with roundBottomCorners,
    /// the likeliest bool an author reaches for, validated clean and blurred by a
    /// corner switch. The validator's own slot-budget block counted bool as a scalar
    /// the whole time.
    void aKawaseChainLeadingWithABoolIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(
            QStringLiteral("sf-kawase-bool"),
            QJsonArray{surfaceParam(QStringLiteral("roundBottomCorners"), QStringLiteral("bool"), false, 0.0, 0.0),
                       surfaceParam(QStringLiteral("blurRadius"), QStringLiteral("float"), 32.0, 0.0, 256.0)});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"),
                   QJsonArray{QStringLiteral("builtin:kawase-down-0"), QStringLiteral("builtin:kawase-down-1"),
                              QStringLiteral("builtin:kawase-down-2"), QStringLiteral("builtin:kawase-down-3"),
                              QStringLiteral("builtin:kawase-up-0"), QStringLiteral("builtin:kawase-up-1"),
                              QStringLiteral("builtin:kawase-up-2")});
        obj.insert(QStringLiteral("bufferScales"), QJsonArray{0.25, 0.125, 0.0625, 0.03125, 0.0625, 0.125, 0.25});
        obj.insert(QStringLiteral("needsBackdrop"), true);

        const PackResult r =
            validateSurface(tmp, QStringLiteral("sf-kawase-bool"), obj,
                            surfaceBodyReading({QStringLiteral("roundBottomCorners"), QStringLiteral("blurRadius")}));
        QVERIFY2(r.report.contains(QStringLiteral("and that is 'roundBottomCorners' here")), qPrintable(r.report));
        QCOMPARE(r.errors, 1);
    }

    /// THE BACKDROP FLAG. kawase_down_0 has backdropTexel() as its only source, so a
    /// chain pack that omits `needsBackdrop` captures nothing and every pass
    /// composites a fully transparent pane. kawase_down_0.frag names the flag as one
    /// of the keys opting in, and it was the one key in that list with no lint:
    /// `multipass` and `bufferShaders` are checked in both directions and the pyramid
    /// is checked per index, while this went to the shipped-pack convention alone.
    void aKawaseChainWithoutNeedsBackdropIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(
            QStringLiteral("sf-kawase-nobackdrop"),
            QJsonArray{surfaceParam(QStringLiteral("blurRadius"), QStringLiteral("float"), 32.0, 0.0, 256.0)});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"),
                   QJsonArray{QStringLiteral("builtin:kawase-down-0"), QStringLiteral("builtin:kawase-down-1"),
                              QStringLiteral("builtin:kawase-down-2"), QStringLiteral("builtin:kawase-down-3"),
                              QStringLiteral("builtin:kawase-up-0"), QStringLiteral("builtin:kawase-up-1"),
                              QStringLiteral("builtin:kawase-up-2")});
        obj.insert(QStringLiteral("bufferScales"), QJsonArray{0.25, 0.125, 0.0625, 0.03125, 0.0625, 0.125, 0.25});

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-kawase-nobackdrop"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("\"needsBackdrop\" is not true")), qPrintable(r.report));
        // The message names the PASS that samples the backdrop, which is down-0 here.
        // Asserted, because the wording it replaced ("in its first pass") was false for two
        // reachable shapes and a substring test on the flag name could not tell them apart.
        QVERIFY2(r.report.contains(QStringLiteral("builtin:kawase-down-0 pass samples the backdrop")),
                 qPrintable(r.report));
        QCOMPARE(r.errors, 1);
    }

    /// THE GAUSSIAN FAMILY, which both blur lints missed for a round. They were scoped to
    /// `anyKawase`, and gaussian_h's only source is backdropTexel() exactly as
    /// kawase_down_0's is — so a pack declaring the gaussian pair without `needsBackdrop`
    /// validated CLEAN and rendered a transparent pane, which is the very failure the
    /// backdrop lint was added to prevent, one family over.
    void aGaussianChainWithoutNeedsBackdropIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(
            QStringLiteral("sf-gauss-nobackdrop"),
            QJsonArray{surfaceParam(QStringLiteral("blurRadius"), QStringLiteral("float"), 32.0, 0.0, 256.0)});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"),
                   QJsonArray{QStringLiteral("builtin:gaussian-h"), QStringLiteral("builtin:gaussian-v")});

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-gauss-nobackdrop"), obj,
                                             surfaceBodyReading({QStringLiteral("blurRadius")}));
        QVERIFY2(r.report.contains(QStringLiteral("\"needsBackdrop\" is not true")), qPrintable(r.report));
        // And it names THIS family's backdrop pass, not the Kawase one.
        QVERIFY2(r.report.contains(QStringLiteral("builtin:gaussian-h pass samples the backdrop")),
                 qPrintable(r.report));
        QCOMPARE(r.errors, 1);
    }

    /// The gaussian twin of the bool-first radius slot. BOTH gaussian helpers read
    /// customParams[0].x as the radius (surfaceGaussianBackdropH and
    /// surfaceGaussianChannelV), so the whole slot argument applies to them verbatim.
    void aGaussianChainLeadingWithABoolIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(
            QStringLiteral("sf-gauss-bool"),
            QJsonArray{surfaceParam(QStringLiteral("roundBottomCorners"), QStringLiteral("bool"), false, 0.0, 0.0),
                       surfaceParam(QStringLiteral("blurRadius"), QStringLiteral("float"), 32.0, 0.0, 256.0)});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"),
                   QJsonArray{QStringLiteral("builtin:gaussian-h"), QStringLiteral("builtin:gaussian-v")});
        obj.insert(QStringLiteral("needsBackdrop"), true);

        const PackResult r =
            validateSurface(tmp, QStringLiteral("sf-gauss-bool"), obj,
                            surfaceBodyReading({QStringLiteral("roundBottomCorners"), QStringLiteral("blurRadius")}));
        QVERIFY2(r.report.contains(QStringLiteral("and that is 'roundBottomCorners' here")), qPrintable(r.report));
        QCOMPARE(r.errors, 1);
    }

    /// gaussian_v ALONE reads iChannel0 and no backdrop, so the backdrop lint must NOT fire
    /// on it while the radius lint still must. That asymmetry is why there are two
    /// predicates rather than one, and without this slot the backdrop lint could be widened
    /// to every buffer token with the suite still green.
    void aGaussianVerticalOnlyPackTakesTheRadiusLintAndNotTheBackdropOne()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(
            QStringLiteral("sf-gaussv-only"),
            QJsonArray{surfaceParam(QStringLiteral("roundBottomCorners"), QStringLiteral("bool"), false, 0.0, 0.0),
                       surfaceParam(QStringLiteral("blurRadius"), QStringLiteral("float"), 32.0, 0.0, 256.0)});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"), QJsonArray{QStringLiteral("builtin:gaussian-v")});

        const PackResult r =
            validateSurface(tmp, QStringLiteral("sf-gaussv-only"), obj,
                            surfaceBodyReading({QStringLiteral("roundBottomCorners"), QStringLiteral("blurRadius")}));
        QVERIFY2(r.report.contains(QStringLiteral("and that is 'roundBottomCorners' here")), qPrintable(r.report));
        QVERIFY2(!r.report.contains(QStringLiteral("needsBackdrop")), qPrintable(r.report));
        // The positional lint fires here too, which is the whole point of it: a lone vertical
        // half is bufferShaders[0], so what it reads is nothing this chain has written — the
        // 1x1 transparent fallback the hosts bind for a channel at or past the current pass
        // index, or on the daemon with `bufferFeedback` set the pass's own previous frame — and
        // it writes a blank pane either way. (It does NOT read the FBO it is writing within the
        // frame; the fallback is there precisely to stop that, and an earlier version of this
        // comment said otherwise.)
        QVERIFY2(r.report.contains(QStringLiteral("builtin:gaussian-v is bufferShaders[0]")), qPrintable(r.report));
        QCOMPARE(r.errors, 2);
    }

    /// THE VERTICAL HALF NEEDS THE HORIZONTAL ONE AT PASS 0. gaussian_v samples iChannel0,
    /// which is buffer pass 0's output, so written the other way round it blurs the transparent
    /// fallback while the horizontal half re-blurs the backdrop into a channel nothing
    /// consumes — and every token still resolves, every frag still compiles, and the pack
    /// validated CLEAN. That was the gap the round which taught the backdrop and radius arms
    /// about the gaussian family left behind: covered for what it READS, not for what ORDER.
    ///
    /// What it does NOT require is index 1 exactly, which the first version of this lint
    /// demanded. iChannelN is pass N's output for every later pass, so the slot below pins the
    /// shape that reading wrongly rejected.
    ///
    /// Arm-unique by construction: needsBackdrop is set and blurRadius leads, so neither of
    /// the other two gaussian arms can fire, and the count pins that.
    void aGaussianPairDeclaredVerticalFirstIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(
            QStringLiteral("sf-gauss-reversed"),
            QJsonArray{surfaceParam(QStringLiteral("blurRadius"), QStringLiteral("float"), 32.0, 0.0, 256.0)});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"),
                   QJsonArray{QStringLiteral("builtin:gaussian-v"), QStringLiteral("builtin:gaussian-h")});
        obj.insert(QStringLiteral("needsBackdrop"), true);

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-gauss-reversed"), obj,
                                             surfaceBodyReading({QStringLiteral("blurRadius")}));
        // The INDEX-0 arm, not the h-not-at-0 one: with the pair reversed the vertical half is
        // itself bufferShaders[0], so the truthful complaint is that it reads the transparent
        // fallback. The other arm is what [filler, h, v] draws, and the slot below pins that
        // the same rule leaves [h, filler, v] alone.
        QVERIFY2(r.report.contains(QStringLiteral("builtin:gaussian-v is bufferShaders[0]")), qPrintable(r.report));
        QCOMPARE(r.errors, 1);

        // The correct order is silent, which is what stops the lint being satisfiable by
        // any pack that names the two tokens at all.
        obj.insert(QStringLiteral("bufferShaders"),
                   QJsonArray{QStringLiteral("builtin:gaussian-h"), QStringLiteral("builtin:gaussian-v")});
        const PackResult ok = validateSurface(tmp, QStringLiteral("sf-gauss-reversed"), obj,
                                              surfaceBodyReading({QStringLiteral("blurRadius")}));
        QVERIFY2(!ok.report.contains(QStringLiteral("samples iChannel0")), qPrintable(ok.report));
        QCOMPARE(ok.errors, 0);
    }

    /// BOTH BACKDROP PASSES IN ONE CHAIN, which is the only thing that exercises the backdrop
    /// message's join. Nothing else in the tree declares a Kawase token AND builtin:gaussian-h
    /// — no other fixture and no bundled pack — so before this slot the two-name branch could
    /// be deleted, or left reading "X and Y pass samples", with the whole suite green. An
    /// untested defensive branch is what this file exists to stop.
    void aChainDeclaringBothBackdropPassesNamesBothAndAgrees()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(
            QStringLiteral("sf-both-backdrop"),
            QJsonArray{surfaceParam(QStringLiteral("blurRadius"), QStringLiteral("float"), 32.0, 0.0, 256.0)});
        obj.insert(QStringLiteral("multipass"), true);
        // The gaussian pair in its correct order, then the full pyramid: both kawase-down-0 and
        // gaussian-h sample the backdrop, and needsBackdrop is deliberately omitted.
        obj.insert(QStringLiteral("bufferShaders"),
                   QJsonArray{QStringLiteral("builtin:gaussian-h"), QStringLiteral("builtin:gaussian-v"),
                              QStringLiteral("builtin:kawase-down-0"), QStringLiteral("builtin:kawase-down-1"),
                              QStringLiteral("builtin:kawase-down-2"), QStringLiteral("builtin:kawase-down-3"),
                              QStringLiteral("builtin:kawase-up-0"), QStringLiteral("builtin:kawase-up-1")});

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-both-backdrop"), obj,
                                             surfaceBodyReading({QStringLiteral("blurRadius")}));
        // Both names, in declaration order, with a PLURAL verb. The message is built from two
        // predicates rather than a ternary precisely so it can say both.
        QVERIFY2(r.report.contains(QStringLiteral("builtin:kawase-down-0 and builtin:gaussian-h passes sample")),
                 qPrintable(r.report));
        // The Kawase pyramid is incomplete here (up-2 absent), so the order lint fires too —
        // asserted rather than left as unnamed collateral, since this fixture cannot be made
        // Kawase-clean while also being gaussian-bearing at index 0.
        QVERIFY2(r.report.contains(QStringLiteral("are positional and must appear as bufferShaders[0..6]")),
                 qPrintable(r.report));
        QCOMPARE(r.errors, 3);
    }

    /// THE SHAPE THE FIRST VERSION OF THAT LINT WRONGLY REJECTED, pinned so it cannot be
    /// rejected again. iChannelN is buffer pass N's output for EVERY later pass — both hosts
    /// bind channels 0..i-1 to the prior outputs, and kawase_up_2 reads iChannel0 from index
    /// 6 — so the vertical half composes at any index behind the horizontal one, not at index
    /// 1 only. [gaussian-h, filler, gaussian-v] runs the same three computations as
    /// [gaussian-h, gaussian-v, filler]; rejecting one and accepting the other was the defect.
    ///
    /// Also pins the OTHER half of the narrowed rule: a pack shipping its own horizontal half
    /// is left alone, because an arbitrary frag cannot be identified as a horizontal Gaussian.
    void aVerticalHalfBehindTheHorizontalOneIsCleanAtAnyIndex()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(
            QStringLiteral("sf-gauss-spaced"),
            QJsonArray{surfaceParam(QStringLiteral("blurRadius"), QStringLiteral("float"), 32.0, 0.0, 256.0)});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"),
                   QJsonArray{QStringLiteral("builtin:gaussian-h"), surfaceFillerBufferName(),
                              QStringLiteral("builtin:gaussian-v")});
        obj.insert(QStringLiteral("needsBackdrop"), true);

        const PackResult spaced = validateSurfaceWithFillerPass(tmp, QStringLiteral("sf-gauss-spaced"), obj,
                                                                surfaceBodyReading({QStringLiteral("blurRadius")}));
        QVERIFY2(!spaced.report.contains(QStringLiteral("samples iChannel0")), qPrintable(spaced.report));
        QCOMPARE(spaced.errors, 0);

        // The pack's own horizontal half at pass 0, builtin vertical behind it: unidentifiable
        // and therefore unlinted, rather than guessed at and rejected.
        obj.insert(QStringLiteral("bufferShaders"),
                   QJsonArray{surfaceFillerBufferName(), QStringLiteral("builtin:gaussian-v")});
        const PackResult own = validateSurfaceWithFillerPass(tmp, QStringLiteral("sf-gauss-spaced"), obj,
                                                             surfaceBodyReading({QStringLiteral("blurRadius")}));
        QVERIFY2(!own.report.contains(QStringLiteral("samples iChannel0")), qPrintable(own.report));
        QCOMPARE(own.errors, 0);
    }

    /// THE RADIUS SCAN MUST DROP A REPEATED ID the way the loader does. fromJson keeps the
    /// FIRST entry for an id and discards every later one, and the slot assignment then runs
    /// over the survivors — so a pack declaring the same id first as a colour and then as a
    /// float has no scalar parameter at all. The raw scan named that id as the first scalar
    /// while --emit-preamble put it in customColors[0] and wrote no scalar define, which is
    /// two outputs of one binary contradicting each other about the same pack.
    ///
    /// The pack is rejected either way, because a duplicate id draws its own lint; what this
    /// pins is that the radius arm agrees with the preamble about WHICH arm is true.
    void aDuplicateParameterIdIsDroppedByTheRadiusScanAsTheLoaderDropsIt()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj =
            surfacePack(QStringLiteral("sf-gauss-dupe"),
                        QJsonArray{surfaceParam(QStringLiteral("tint"), QStringLiteral("color"),
                                                QStringLiteral("#ffffffff"), 0.0, 0.0),
                                   surfaceParam(QStringLiteral("tint"), QStringLiteral("float"), 4.0, 0.0, 32.0)});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"),
                   QJsonArray{QStringLiteral("builtin:gaussian-h"), QStringLiteral("builtin:gaussian-v")});
        obj.insert(QStringLiteral("needsBackdrop"), true);

        const PackResult r =
            validateSurface(tmp, QStringLiteral("sf-gauss-dupe"), obj, surfaceBodyReading({QStringLiteral("tint")}));
        // The truthful arm: the surviving `tint` is the COLOUR, so the chain has no scalar.
        QVERIFY2(r.report.contains(QStringLiteral("blurs by 0")), qPrintable(r.report));
        QVERIFY2(!r.report.contains(QStringLiteral("and that is 'tint' here")), qPrintable(r.report));
        // Two: the no-scalar arm above, plus the duplicate-id lint that makes the pack
        // rejected either way.
        QCOMPARE(r.errors, 2);
    }

    /// THE NOT-FOUND SENTINEL, which a widened predicate broke. The scan used to test the
    /// first scalar's TYPE for emptiness, sound only while the predicate was float-or-int:
    /// once it became "not a color", a parameter whose `type` key is MISSING set the id and
    /// left the type empty, so the lint took the no-scalar arm and told an author whose slot
    /// 0 was CORRECT that the chain blurs by 0. Two outputs of one binary disagreed —
    /// --emit-preamble wrote the right slot while -s denied it existed.
    ///
    /// Asserts the message the fix makes it print, not merely that something was reported:
    /// a typeless parameter also trips the unknown-type lint, so `errors > 0` would pass
    /// with the sentinel still broken.
    void aKawaseChainLeadingWithATypelessParameterNamesThatParameter()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        // By hand rather than through surfaceParam, which always writes a type.
        QJsonObject typeless;
        typeless.insert(QStringLiteral("id"), QStringLiteral("mystery"));
        typeless.insert(QStringLiteral("name"), QStringLiteral("mystery"));
        typeless.insert(QStringLiteral("default"), 1.0);

        QJsonObject obj = surfacePack(
            QStringLiteral("sf-kawase-notype"),
            QJsonArray{typeless,
                       surfaceParam(QStringLiteral("blurRadius"), QStringLiteral("float"), 32.0, 0.0, 256.0)});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"),
                   QJsonArray{QStringLiteral("builtin:kawase-down-0"), QStringLiteral("builtin:kawase-down-1"),
                              QStringLiteral("builtin:kawase-down-2"), QStringLiteral("builtin:kawase-down-3"),
                              QStringLiteral("builtin:kawase-up-0"), QStringLiteral("builtin:kawase-up-1"),
                              QStringLiteral("builtin:kawase-up-2")});
        obj.insert(QStringLiteral("bufferScales"), QJsonArray{0.25, 0.125, 0.0625, 0.03125, 0.0625, 0.125, 0.25});
        obj.insert(QStringLiteral("needsBackdrop"), true);

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-kawase-notype"), obj,
                                             surfaceBodyReading({QStringLiteral("blurRadius")}));
        QVERIFY2(r.report.contains(QStringLiteral("and that is 'mystery' here")), qPrintable(r.report));
        QVERIFY2(!r.report.contains(QStringLiteral("blurs by 0")), qPrintable(r.report));
        // Two, both intrinsic to a typeless parameter: the radius wrong-name arm under test
        // and the unknown-type lint. Counted as well as named, so a third cannot slip in.
        QCOMPARE(r.errors, 2);
    }

    /// THE RADIUS SLOT, no-scalar arm. With no scalar parameter at all the slot reads
    /// 0 and the chain blurs by nothing, which looks like a broken pack rather than a
    /// misdeclared one. Also untested until now.
    void aKawaseChainWithNoScalarParameterIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-kawase-noscalar"), QJsonArray{});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"),
                   QJsonArray{QStringLiteral("builtin:kawase-down-0"), QStringLiteral("builtin:kawase-down-1"),
                              QStringLiteral("builtin:kawase-down-2"), QStringLiteral("builtin:kawase-down-3"),
                              QStringLiteral("builtin:kawase-up-0"), QStringLiteral("builtin:kawase-up-1"),
                              QStringLiteral("builtin:kawase-up-2")});
        // The canonical pyramid, so the count below is the no-scalar arm and nothing else.
        obj.insert(QStringLiteral("bufferScales"), QJsonArray{0.25, 0.125, 0.0625, 0.03125, 0.0625, 0.125, 0.25});
        obj.insert(QStringLiteral("needsBackdrop"), true);

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-kawase-noscalar"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("blurs by 0")), qPrintable(r.report));
        QCOMPARE(r.errors, 1);
    }
};

QTEST_MAIN(TestSurfaceBlurChainLints)
#include "test_surface_blur_chain_lints.moc"

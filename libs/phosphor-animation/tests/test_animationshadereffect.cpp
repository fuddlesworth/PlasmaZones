// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationShaderEffect.h>

#include <QJsonDocument>
#include <QTest>

using PhosphorAnimationShaders::AnimationShaderEffect;

class TestAnimationShaderEffect : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testDefaultIsInvalid()
    {
        AnimationShaderEffect e;
        QVERIFY(!e.isValid());
    }

    void testValidWithIdAndFragmentShader()
    {
        AnimationShaderEffect e;
        e.id = QStringLiteral("dissolve");
        e.fragmentShaderPath = QStringLiteral("effect.frag");
        QVERIFY(e.isValid());
    }

    void testJsonRoundTrip()
    {
        AnimationShaderEffect original;
        original.id = QStringLiteral("slide");
        original.name = QStringLiteral("Slide");
        original.description = QStringLiteral("Slide transition");
        original.author = QStringLiteral("PlasmaZones");
        original.version = QStringLiteral("1.0");
        original.category = QStringLiteral("Geometric");
        original.fragmentShaderPath = QStringLiteral("effect.frag");
        original.vertexShaderPath = QStringLiteral("effect.vert");
        original.previewPath = QStringLiteral("preview.png");

        AnimationShaderEffect::ParameterInfo p;
        p.id = QStringLiteral("direction");
        p.name = QStringLiteral("Direction");
        p.type = QStringLiteral("float");
        p.defaultValue = 0.0;
        p.minValue = 0.0;
        p.maxValue = 3.0;
        original.parameters.append(p);

        const QJsonObject json = original.toJson();
        const AnimationShaderEffect restored = AnimationShaderEffect::fromJson(json);

        QCOMPARE(restored, original);
    }

    void testJsonPreservesParameters()
    {
        AnimationShaderEffect e;
        e.id = QStringLiteral("glitch");
        e.fragmentShaderPath = QStringLiteral("effect.frag");

        AnimationShaderEffect::ParameterInfo p1;
        p1.id = QStringLiteral("intensity");
        p1.name = QStringLiteral("Intensity");
        p1.type = QStringLiteral("float");
        p1.defaultValue = 0.5;

        AnimationShaderEffect::ParameterInfo p2;
        p2.id = QStringLiteral("blockSize");
        p2.name = QStringLiteral("Block Size");
        p2.type = QStringLiteral("int");
        p2.defaultValue = 8;
        p2.minValue = 1;
        p2.maxValue = 64;

        e.parameters = {p1, p2};

        const AnimationShaderEffect restored = AnimationShaderEffect::fromJson(e.toJson());
        QCOMPARE(restored.parameters.size(), 2);
        QCOMPARE(restored.parameters[0].id, QStringLiteral("intensity"));
        QCOMPARE(restored.parameters[1].id, QStringLiteral("blockSize"));
    }

    void testFromJsonMissingFields()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("test"));

        const AnimationShaderEffect e = AnimationShaderEffect::fromJson(obj);
        QCOMPARE(e.id, QStringLiteral("test"));
        QVERIFY(e.fragmentShaderPath.isEmpty());
        QVERIFY(!e.isValid());
    }

    void testEquality()
    {
        AnimationShaderEffect a;
        a.id = QStringLiteral("dissolve");
        a.fragmentShaderPath = QStringLiteral("effect.frag");

        AnimationShaderEffect b = a;
        QCOMPARE(a, b);

        b.id = QStringLiteral("morph");
        QVERIFY(a != b);
    }

    /// Multipass / wallpaper / depth / buffer fields survive a full
    /// `toJson()` → `fromJson()` round-trip. The PR that promoted the
    /// animation UBO to the full BaseUniforms layout added every field
    /// listed below; without coverage here, a regression that dropped
    /// (say) `bufferWraps` from the JSON serialisation would silently
    /// degrade multipass animation packs to single-pass on the daemon
    /// path with no test signal.
    void testJsonPreservesMultipassFields()
    {
        AnimationShaderEffect original;
        original.id = QStringLiteral("multipass-test");
        original.fragmentShaderPath = QStringLiteral("effect.frag");
        original.isMultipass = true;
        original.bufferShaderPaths = {QStringLiteral("buffer-a.frag"), QStringLiteral("buffer-b.frag")};
        original.useWallpaper = true;
        original.bufferFeedback = true;
        original.bufferScale = 0.5;
        original.bufferWrap = QStringLiteral("repeat");
        original.bufferWraps = {QStringLiteral("clamp"), QStringLiteral("mirror")};
        original.bufferFilter = QStringLiteral("linear");
        original.bufferFilters = {QStringLiteral("nearest"), QStringLiteral("linear")};
        original.useDepthBuffer = true;
        original.fboExtentRing = 0.25;

        const AnimationShaderEffect restored = AnimationShaderEffect::fromJson(original.toJson());
        QCOMPARE(restored, original);
    }

    /// `bufferScale` is clamped to `[0.125, 1.0]` at parse time so a
    /// metadata.json author can't accidentally allocate gigabyte-sized
    /// FBOs by writing a >1.0 multiplier or render at sub-pixel scales
    /// that produce visible aliasing. The field still round-trips
    /// for any in-range value (covered above); pin the clamp here so a
    /// regression that removed it surfaces directly.
    void testFromJsonClampsBufferScale()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("test"));
        obj.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        obj.insert(QLatin1String("bufferScale"), 5.0);
        const AnimationShaderEffect overshoot = AnimationShaderEffect::fromJson(obj);
        QCOMPARE(overshoot.bufferScale, qreal(1.0));

        obj.insert(QLatin1String("bufferScale"), 0.001);
        const AnimationShaderEffect undershoot = AnimationShaderEffect::fromJson(obj);
        QCOMPARE(undershoot.bufferScale, qreal(0.125));
    }

    /// `fboExtent` grammar parser coverage. The Phase 3 refactor
    /// replaced the split `boundsExtent` enum + `boundsPadding` qreal
    /// JSON keys with a single string field; the parser at
    /// `parseFboExtent` in animationshadereffect.cpp accepts four forms
    /// (`"anchor"`, `"anchor+0.5"` fraction, `"anchor+50%"` percent,
    /// `"surface"`) and rejects malformed values with a journal warning
    /// + struct-defaults fallback.
    ///
    /// Driven by a `_data()` table so a regression that silently drops
    /// a form (e.g. percent support) surfaces in CI rather than waiting
    /// on a metadata round-trip. Each row pins (input, expected kind,
    /// expected ring) and any new rule (e.g. negative-value rejection)
    /// gets a single new row instead of a fresh slot.
    ///
    /// Coverage of the runtime fallback for parentless / windowless
    /// anchors lives in `clampPaddingToParent` (anonymous-namespace
    /// helper in surfaceanimator.cpp) and is exercised end-to-end via
    /// `tests/test_surface_animator.cpp`. The math contract for that
    /// path is "fall back to the global `kMaxFboExtentRing` ceiling",
    /// which IS pinned here directly via `kMaxFboExtentRing` row below.
    void testFromJsonFboExtent_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<AnimationShaderEffect::FboExtentKind>("expectedKind");
        QTest::addColumn<qreal>("expectedRing");

        const auto kAnchor = AnimationShaderEffect::FboExtentKind::Anchor;
        const auto kSurface = AnimationShaderEffect::FboExtentKind::Surface;
        const qreal kMax = AnimationShaderEffect::kMaxFboExtentRing;

        // Accepted grammar.
        QTest::newRow("anchor") << QStringLiteral("anchor") << kAnchor << qreal(0.0);
        QTest::newRow("anchor+0.5") << QStringLiteral("anchor+0.5") << kAnchor << qreal(0.5);
        QTest::newRow("anchor+50%") << QStringLiteral("anchor+50%") << kAnchor << qreal(0.5);
        QTest::newRow("anchor+0") << QStringLiteral("anchor+0") << kAnchor << qreal(0.0);
        QTest::newRow("surface") << QStringLiteral("surface") << kSurface << qreal(0.0);
        QTest::newRow("anchor-uppercase") << QStringLiteral("ANCHOR") << kAnchor << qreal(0.0);
        QTest::newRow("surface-mixed") << QStringLiteral("Surface") << kSurface << qreal(0.0);
        QTest::newRow("anchor+ws") << QStringLiteral("anchor + 0.5") << kAnchor << qreal(0.5);
        QTest::newRow("leading-ws") << QStringLiteral("  anchor") << kAnchor << qreal(0.0);

        // Clamped to the runtime ceiling: a metadata value above
        // kMaxFboExtentRing is treated as the ceiling instead of
        // allocating gigabyte FBOs.
        QTest::newRow("clamp-over") << QStringLiteral("anchor+10.0") << kAnchor << kMax;

        // Malformed values: parser falls back to struct defaults
        // (Anchor, ring=0) and emits a journal warning.
        QTest::newRow("empty") << QString() << kAnchor << qreal(0.0);
        QTest::newRow("whitespace-only") << QStringLiteral("   ") << kAnchor << qreal(0.0);
        QTest::newRow("garbage") << QStringLiteral("foo") << kAnchor << qreal(0.0);
        QTest::newRow("anchor-bad-frac") << QStringLiteral("anchor+abc") << kAnchor << qreal(0.0);
        QTest::newRow("anchor-only-plus") << QStringLiteral("anchor+") << kAnchor << qreal(0.0);
        QTest::newRow("anchor-only-pct") << QStringLiteral("anchor+%") << kAnchor << qreal(0.0);
        QTest::newRow("anchor-double-pct") << QStringLiteral("anchor+50%%") << kAnchor << qreal(0.0);

        // NaN / Inf rejection at the parse boundary so downstream
        // consumers (osd.cpp::resolveOsdShaderPadding feeding QML's
        // `shaderBoundsPadding`) can't NaN out window dimensions.
        QTest::newRow("nan") << QStringLiteral("anchor+nan") << kAnchor << qreal(0.0);
        QTest::newRow("inf") << QStringLiteral("anchor+inf") << kAnchor << qreal(0.0);
        QTest::newRow("neg-inf") << QStringLiteral("anchor+-inf") << kAnchor << qreal(0.0);

        // Negative ring rejection. A typo like "anchor+-0.5" would
        // otherwise silently clamp to 0 (parse "succeeds", qBound
        // floors to 0) and the operator loses the chance to spot the
        // bad value. Parser now fails-loud on negatives.
        QTest::newRow("neg-fraction") << QStringLiteral("anchor+-0.5") << kAnchor << qreal(0.0);
        QTest::newRow("neg-percent") << QStringLiteral("anchor+-50%") << kAnchor << qreal(0.0);
    }

    void testFromJsonFboExtent()
    {
        QFETCH(QString, input);
        QFETCH(AnimationShaderEffect::FboExtentKind, expectedKind);
        QFETCH(qreal, expectedRing);

        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("test"));
        obj.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        obj.insert(QLatin1String("fboExtent"), input);
        const AnimationShaderEffect e = AnimationShaderEffect::fromJson(obj);
        QCOMPARE(e.fboExtentKind, expectedKind);
        QCOMPARE(e.fboExtentRing, expectedRing);
    }

    /// Round-trip Surface extent through JSON. toJson emits
    /// `"fboExtent": "surface"`; fromJson reads it back.
    void testFboExtentSurfaceRoundTrip()
    {
        AnimationShaderEffect original;
        original.id = QStringLiteral("fly-in");
        original.fragmentShaderPath = QStringLiteral("effect.frag");
        original.fboExtentKind = AnimationShaderEffect::FboExtentKind::Surface;

        const AnimationShaderEffect restored = AnimationShaderEffect::fromJson(original.toJson());
        QCOMPARE(restored.fboExtentKind, AnimationShaderEffect::FboExtentKind::Surface);
        QCOMPARE(restored.fboExtentRing, qreal(0.0));
    }

    /// `formatFboExtent` writes the ring with 17 sig digits so a
    /// programmatically-assigned non-round value (1.0/3.0, sqrt(0.5),
    /// etc.) survives toJson -> fromJson without losing precision.
    /// Default `arg(double)` truncates to 6 sig digits, which collapses
    /// 0.333... -> "0.333333" and breaks strict-equality compare.
    void testFboExtentRingPrecisionRoundTrip()
    {
        AnimationShaderEffect original;
        original.id = QStringLiteral("test");
        original.fragmentShaderPath = QStringLiteral("effect.frag");
        original.fboExtentKind = AnimationShaderEffect::FboExtentKind::Anchor;
        original.fboExtentRing = 1.0 / 3.0;

        const AnimationShaderEffect restored = AnimationShaderEffect::fromJson(original.toJson());
        QCOMPARE(restored.fboExtentKind, AnimationShaderEffect::FboExtentKind::Anchor);
        QCOMPARE(restored.fboExtentRing, original.fboExtentRing);
    }
};

QTEST_MAIN(TestAnimationShaderEffect)
#include "test_animationshadereffect.moc"

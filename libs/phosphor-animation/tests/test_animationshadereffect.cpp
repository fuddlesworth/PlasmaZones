// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationShaderEffect.h>

#include <QJsonArray>
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
        original.author = QStringLiteral("Phosphor");
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

    /// `fboExtent` grammar parser coverage. Accepts exactly two forms
    /// — `"anchor"` and `"surface"` — case-insensitive. Anything else
    /// (including the legacy `anchor+N` ring-fraction grammar) is
    /// rejected with a journal warning and falls back to the default
    /// (Anchor). Driven by a `_data()` table so a regression that
    /// silently accepts a malformed form surfaces in CI rather than
    /// waiting on a visual regression.
    void testFromJsonFboExtent_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<AnimationShaderEffect::FboExtentKind>("expectedKind");

        const auto kAnchor = AnimationShaderEffect::FboExtentKind::Anchor;
        const auto kSurface = AnimationShaderEffect::FboExtentKind::Surface;

        // Accepted grammar.
        QTest::newRow("anchor") << QStringLiteral("anchor") << kAnchor;
        QTest::newRow("surface") << QStringLiteral("surface") << kSurface;
        QTest::newRow("anchor-uppercase") << QStringLiteral("ANCHOR") << kAnchor;
        QTest::newRow("surface-mixed") << QStringLiteral("Surface") << kSurface;
        QTest::newRow("leading-ws") << QStringLiteral("  anchor") << kAnchor;

        // Malformed / unsupported values: parser falls back to the
        // struct default (Anchor) and emits a journal warning. The
        // legacy `anchor+N` ring grammar lives in this bucket since
        // ring expansion is no longer supported on either runtime.
        QTest::newRow("empty") << QString() << kAnchor;
        QTest::newRow("whitespace-only") << QStringLiteral("   ") << kAnchor;
        QTest::newRow("garbage") << QStringLiteral("foo") << kAnchor;
        QTest::newRow("legacy-anchor+0.5") << QStringLiteral("anchor+0.5") << kAnchor;
        QTest::newRow("legacy-anchor+50%") << QStringLiteral("anchor+50%") << kAnchor;
    }

    void testFromJsonFboExtent()
    {
        QFETCH(QString, input);
        QFETCH(AnimationShaderEffect::FboExtentKind, expectedKind);

        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("test"));
        obj.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        obj.insert(QLatin1String("fboExtent"), input);
        const AnimationShaderEffect e = AnimationShaderEffect::fromJson(obj);
        QCOMPARE(e.fboExtentKind, expectedKind);
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
    }

    /// `appliesTo` round-trips through JSON, and an unset list stays empty
    /// (universal) without emitting the key.
    void testAppliesToRoundTrip()
    {
        AnimationShaderEffect original;
        original.id = QStringLiteral("window-morph");
        original.fragmentShaderPath = QStringLiteral("effect.frag");
        original.appliesTo = QStringList{QStringLiteral("geometry")};

        const QJsonObject json = original.toJson();
        QVERIFY(json.contains(QLatin1String("appliesTo")));
        const AnimationShaderEffect restored = AnimationShaderEffect::fromJson(json);
        QCOMPARE(restored.appliesTo, original.appliesTo);

        // Universal (empty) effect omits the key entirely.
        AnimationShaderEffect universal;
        universal.id = QStringLiteral("fade");
        universal.fragmentShaderPath = QStringLiteral("effect.frag");
        QVERIFY(!universal.toJson().contains(QLatin1String("appliesTo")));
        QVERIFY(AnimationShaderEffect::fromJson(universal.toJson()).appliesTo.isEmpty());
    }

    /// Unknown / duplicate tokens are dropped at parse time; a list that
    /// validates down to empty is treated as universal.
    void testAppliesToValidation()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("x"));
        obj.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        QJsonArray arr;
        arr.append(QStringLiteral("geometry"));
        arr.append(QStringLiteral("geometry")); // duplicate
        arr.append(QStringLiteral("teleport")); // unknown
        obj.insert(QLatin1String("appliesTo"), arr);

        const AnimationShaderEffect e = AnimationShaderEffect::fromJson(obj);
        QCOMPARE(e.appliesTo, (QStringList{QStringLiteral("geometry")}));

        QJsonObject allBad = obj;
        QJsonArray bad;
        bad.append(QStringLiteral("nonsense"));
        allBad.insert(QLatin1String("appliesTo"), bad);
        QVERIFY(AnimationShaderEffect::fromJson(allBad).appliesTo.isEmpty());
    }

    /// The (effect × path) predicate: a geometry-only effect is compatible
    /// with geometry legs, incompatible with appearance legs, and a
    /// universal effect is compatible everywhere. An ambiguous row (the
    /// mixed `window` root) is never reported incompatible.
    void testShaderEffectAppliesToEventPath()
    {
        using PhosphorAnimationShaders::shaderEffectAppliesToEventPath;

        AnimationShaderEffect morph;
        morph.id = QStringLiteral("window-morph");
        morph.fragmentShaderPath = QStringLiteral("effect.frag");
        morph.appliesTo = QStringList{QStringLiteral("geometry")};

        QVERIFY(shaderEffectAppliesToEventPath(morph, QStringLiteral("window.move")));
        QVERIFY(shaderEffectAppliesToEventPath(morph, QStringLiteral("window.snapIn")));
        QVERIFY(shaderEffectAppliesToEventPath(morph, QStringLiteral("window.maximize")));
        QVERIFY(!shaderEffectAppliesToEventPath(morph, QStringLiteral("window.open")));
        QVERIFY(!shaderEffectAppliesToEventPath(morph, QStringLiteral("popup.layoutPicker.show")));
        // Mixed ancestor → unclassified → not provably incompatible.
        QVERIFY(shaderEffectAppliesToEventPath(morph, QStringLiteral("window")));

        AnimationShaderEffect fade; // universal (no appliesTo)
        fade.id = QStringLiteral("fade");
        fade.fragmentShaderPath = QStringLiteral("effect.frag");
        QVERIFY(shaderEffectAppliesToEventPath(fade, QStringLiteral("window.open")));
        QVERIFY(shaderEffectAppliesToEventPath(fade, QStringLiteral("window.move")));
    }
};

QTEST_MAIN(TestAnimationShaderEffect)
#include "test_animationshadereffect.moc"

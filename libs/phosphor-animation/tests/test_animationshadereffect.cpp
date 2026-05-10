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
};

QTEST_MAIN(TestAnimationShaderEffect)
#include "test_animationshadereffect.moc"

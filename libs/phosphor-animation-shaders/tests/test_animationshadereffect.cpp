// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationShaders/AnimationShaderEffect.h>

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
        original.kwinFragmentShaderPath = QStringLiteral("effect_kwin.frag");
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
};

QTEST_MAIN(TestAnimationShaderEffect)
#include "test_animationshadereffect.moc"

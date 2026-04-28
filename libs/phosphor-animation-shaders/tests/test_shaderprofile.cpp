// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationShaders/ShaderProfile.h>

#include <QJsonDocument>
#include <QTest>

using PhosphorAnimationShaders::ShaderProfile;

class TestShaderProfile : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testDefaultFieldsAreUnset()
    {
        ShaderProfile p;
        QVERIFY(!p.effectId.has_value());
        QVERIFY(!p.parameters.has_value());
    }

    void testEffectiveDefaults()
    {
        ShaderProfile p;
        QVERIFY(p.effectiveEffectId().isEmpty());
        QVERIFY(p.effectiveParameters().isEmpty());
    }

    void testWithDefaults()
    {
        ShaderProfile p;
        const ShaderProfile filled = p.withDefaults();
        QVERIFY(filled.effectId.has_value());
        QVERIFY(filled.parameters.has_value());
        QVERIFY(filled.effectId->isEmpty());
        QVERIFY(filled.parameters->isEmpty());
    }

    void testJsonRoundTripEngagedFields()
    {
        ShaderProfile original;
        original.effectId = QStringLiteral("dissolve");
        QVariantMap params;
        params.insert(QStringLiteral("grain"), 0.5);
        params.insert(QStringLiteral("threshold"), 0.8);
        original.parameters = params;

        const QJsonObject json = original.toJson();
        const ShaderProfile restored = ShaderProfile::fromJson(json);

        QCOMPARE(restored, original);
        QVERIFY(restored.effectId.has_value());
        QCOMPARE(*restored.effectId, QStringLiteral("dissolve"));
        QVERIFY(restored.parameters.has_value());
        QCOMPARE(restored.parameters->size(), 2);
    }

    void testJsonRoundTripUnsetFields()
    {
        ShaderProfile original;
        const QJsonObject json = original.toJson();
        QVERIFY(json.isEmpty());

        const ShaderProfile restored = ShaderProfile::fromJson(json);
        QVERIFY(!restored.effectId.has_value());
        QVERIFY(!restored.parameters.has_value());
    }

    void testJsonRoundTripEmptyEffectId()
    {
        ShaderProfile original;
        original.effectId = QString();
        const QJsonObject json = original.toJson();
        QVERIFY(json.contains(QLatin1String("effectId")));

        const ShaderProfile restored = ShaderProfile::fromJson(json);
        QVERIFY(restored.effectId.has_value());
        QVERIFY(restored.effectId->isEmpty());
    }

    void testOverlayEngagedFieldsWin()
    {
        ShaderProfile dst;
        dst.effectId = QStringLiteral("slide");

        ShaderProfile src;
        src.effectId = QStringLiteral("dissolve");

        ShaderProfile::overlay(dst, src);
        QCOMPARE(*dst.effectId, QStringLiteral("dissolve"));
    }

    void testOverlayUnsetFieldsSkipped()
    {
        ShaderProfile dst;
        dst.effectId = QStringLiteral("slide");
        dst.parameters = QVariantMap({{QStringLiteral("dir"), 1}});

        ShaderProfile src;
        // src has no engaged fields

        ShaderProfile::overlay(dst, src);
        QCOMPARE(*dst.effectId, QStringLiteral("slide"));
        QCOMPARE(dst.parameters->size(), 1);
    }

    void testOverlayEmptyEffectIdOverridesNonEmpty()
    {
        ShaderProfile dst;
        dst.effectId = QStringLiteral("dissolve");

        ShaderProfile src;
        src.effectId = QString();

        ShaderProfile::overlay(dst, src);
        QVERIFY(dst.effectId.has_value());
        QVERIFY(dst.effectId->isEmpty());
    }

    void testEquality()
    {
        ShaderProfile a;
        a.effectId = QStringLiteral("dissolve");

        ShaderProfile b;
        b.effectId = QStringLiteral("dissolve");
        QCOMPARE(a, b);

        b.effectId = QStringLiteral("slide");
        QVERIFY(a != b);
    }
};

QTEST_MAIN(TestShaderProfile)
#include "test_shaderprofile.moc"

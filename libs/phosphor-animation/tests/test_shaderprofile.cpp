// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/ShaderProfile.h>

#include <QJsonDocument>
#include <QTest>
#include <QVariantMap>

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

    // ─────── Preset reference ───────

    void testPresetIdDefaultsUnset()
    {
        // A config written before presets existed must load with the field
        // INHERIT, not "explicitly no preset" — different statements in the
        // cascade, exactly like effectId and parameters.
        ShaderProfile p;
        QVERIFY(!p.presetId.has_value());
        QVERIFY(p.effectivePresetId().isEmpty());
    }

    void testPresetIdRoundTrips()
    {
        ShaderProfile p;
        p.effectId = QStringLiteral("dissolve");
        p.presetId = QStringLiteral("{neon}");
        p.parameters = QVariantMap{{QStringLiteral("glow"), 0.8}};

        const ShaderProfile back = ShaderProfile::fromJson(p.toJson());
        QCOMPARE(back, p);
        QCOMPARE(back.effectivePresetId(), QStringLiteral("{neon}"));
    }

    void testAbsentPresetIdStaysUnsetThroughJson()
    {
        ShaderProfile p;
        p.effectId = QStringLiteral("dissolve");
        const ShaderProfile back = ShaderProfile::fromJson(p.toJson());
        QVERIFY(!back.presetId.has_value());
        QCOMPARE(back, p);
    }

    void testEngagedEmptyPresetIdSurvives()
    {
        // Engaged-but-empty is "explicitly no preset", which a leaf uses to
        // stop inheriting an ancestor's. It has to round-trip distinctly from
        // absent or that statement is unwritable.
        ShaderProfile p;
        p.presetId = QString();
        const ShaderProfile back = ShaderProfile::fromJson(p.toJson());
        QVERIFY(back.presetId.has_value());
        QVERIFY(back.presetId->isEmpty());
        QCOMPARE(back, p);
    }

    void testOverlayInheritsAndOverridesPresetId()
    {
        ShaderProfile parent;
        parent.presetId = QStringLiteral("{from-parent}");
        ShaderProfile child;
        child.effectId = QStringLiteral("dissolve");

        ShaderProfile merged = parent;
        ShaderProfile::overlay(merged, child);
        QCOMPARE(merged.effectivePresetId(), QStringLiteral("{from-parent}"));

        child.presetId = QStringLiteral("{from-child}");
        merged = parent;
        ShaderProfile::overlay(merged, child);
        QCOMPARE(merged.effectivePresetId(), QStringLiteral("{from-child}"));
    }

    void testPresetIdParticipatesInEquality()
    {
        ShaderProfile a;
        a.effectId = QStringLiteral("dissolve");
        ShaderProfile b = a;
        QCOMPARE(a, b);
        b.presetId = QStringLiteral("{neon}");
        QVERIFY(a != b);
    }

    void testWithDefaultsEngagesPresetId()
    {
        const ShaderProfile filled = ShaderProfile().withDefaults();
        QVERIFY(filled.presetId.has_value());
        QVERIFY(filled.presetId->isEmpty());
    }
};

QTEST_MAIN(TestShaderProfile)
#include "test_shaderprofile.moc"

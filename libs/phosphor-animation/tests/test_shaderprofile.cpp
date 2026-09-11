// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/ShaderProfile.h>

#include <PhosphorShaders/ShaderPresetRegistry.h>

#include <QJsonObject>

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

    // ─────── withPresetsResolved ───────
    //
    // The animation flatten had no test anywhere: it lived as two hand-written
    // copies in the daemon and the compositor, so neither tree could reach it.
    // Now that it is a library function, these pin its contract.

    void testFlattenAppliesThePresetUnderOwnEdits()
    {
        PhosphorShaders::ShaderPresetRegistry registry;
        PhosphorShaders::ShaderPreset soft;
        soft.id = QStringLiteral("soft");
        soft.name = QStringLiteral("Soft");
        soft.packId = QStringLiteral("dissolve");
        soft.params = QVariantMap{{QStringLiteral("speed"), 1.0}, {QStringLiteral("glow"), 0.2}};
        registry.setUserPresets(PhosphorShaders::ShaderFamily::Animation, {soft});

        ShaderProfile p;
        p.effectId = QStringLiteral("dissolve");
        p.presetId = QStringLiteral("soft");
        p.parameters = QVariantMap{{QStringLiteral("speed"), 9.0}};

        const ShaderProfile flat = withPresetsResolved(p, registry);
        // The preset supplies what the assignment did not touch...
        QCOMPARE(flat.effectiveParameters().value(QStringLiteral("glow")).toDouble(), 0.2);
        // ...and the assignment's own edit wins where it did.
        QCOMPARE(flat.effectiveParameters().value(QStringLiteral("speed")).toDouble(), 9.0);
        // The reference is CLEARED, which is what makes a second pass a no-op.
        QVERIFY(!flat.presetId.has_value());
        QCOMPARE(withPresetsResolved(flat, registry), flat);
    }

    void testFlattenResolvesAgainstTheWalkedUpPack()
    {
        // The pack comes from effectiveEffectId(), which the tree walk-up has
        // already filled in. A preset is keyed by (family, packId, presetId), so
        // flattening before the walk-up would look it up against an empty pack id
        // and resolve nothing — which is why this runs after it, never per node.
        PhosphorShaders::ShaderPresetRegistry registry;
        PhosphorShaders::ShaderPreset soft;
        soft.id = QStringLiteral("soft");
        soft.name = QStringLiteral("Soft");
        soft.packId = QStringLiteral("dissolve");
        soft.params = QVariantMap{{QStringLiteral("glow"), 0.5}};
        registry.setUserPresets(PhosphorShaders::ShaderFamily::Animation, {soft});

        ShaderProfile inherited;
        inherited.presetId = QStringLiteral("soft"); // no effectId: pack not resolved yet
        QVERIFY(withPresetsResolved(inherited, registry).effectiveParameters().isEmpty());

        inherited.effectId = QStringLiteral("dissolve");
        QCOMPARE(
            withPresetsResolved(inherited, registry).effectiveParameters().value(QStringLiteral("glow")).toDouble(),
            0.5);
    }

    void testFlattenWithAnUnknownPresetKeepsOwnParameters()
    {
        // An assignment can outlive the preset it points at. It then renders the
        // way it did before it pointed at one, rather than losing its own values.
        PhosphorShaders::ShaderPresetRegistry registry;
        ShaderProfile p;
        p.effectId = QStringLiteral("dissolve");
        p.presetId = QStringLiteral("gone");
        p.parameters = QVariantMap{{QStringLiteral("speed"), 2.0}};

        const ShaderProfile flat = withPresetsResolved(p, registry);
        QCOMPARE(flat.effectiveParameters().value(QStringLiteral("speed")).toDouble(), 2.0);
        QVERIFY(!flat.presetId.has_value());
    }

    void testEqualityNormalisesParameterValuesButNotEngagement()
    {
        // The settings setters gate their write and their signal on this
        // operator, comparing a map BUILT in C++ against one read back from
        // disk. Normalised through JSON, the same way the overlay profile does,
        // so a value whose type changed on the way through still compares equal
        // and the no-op gate does not fail open — the signal behind it reaches
        // the daemon and the compositor.
        ShaderProfile built;
        built.effectId = QStringLiteral("dissolve");
        built.parameters = QVariantMap{{QStringLiteral("count"), 3}};

        ShaderProfile readBack = built;
        readBack.parameters = QVariantMap{{QStringLiteral("count"), 3.0}};
        QVERIFY(built == readBack);

        // A different VALUE still differs, so the normalisation is not simply
        // making everything equal.
        ShaderProfile other = built;
        other.parameters = QVariantMap{{QStringLiteral("count"), 4}};
        QVERIFY(built != other);

        // ENGAGEMENT is not normalised away: nullopt and engaged-empty are
        // different statements on this type, and JSON-normalising an absent map
        // would flatten both to {}.
        ShaderProfile absent;
        absent.effectId = QStringLiteral("dissolve");
        ShaderProfile engagedEmpty = absent;
        engagedEmpty.parameters = QVariantMap{};
        QVERIFY(absent != engagedEmpty);
        QVERIFY(absent == ShaderProfile{.effectId = QStringLiteral("dissolve")});
    }

    void testFlattenDoesNotEngageParametersItHasNothingToPutIn()
    {
        // nullopt and engaged-empty are different statements: engaged-empty is
        // "no parameters here, and do not inherit any". A flatten that turned
        // one into the other would invent a block the user never wrote, and it
        // reached this exact case — an assignment naming a preset that no longer
        // exists, with no parameters of its own, resolves to an empty map.
        PhosphorShaders::ShaderPresetRegistry registry;
        ShaderProfile p;
        p.effectId = QStringLiteral("dissolve");
        p.presetId = QStringLiteral("gone");
        QVERIFY(!p.parameters.has_value());

        const ShaderProfile flat = withPresetsResolved(p, registry);
        QVERIFY(!flat.parameters.has_value());
        QVERIFY(!flat.presetId.has_value());

        // An assignment that DID engage an empty map keeps it engaged: that one
        // is the user's statement and the flatten must not revoke it either.
        ShaderProfile blocking = p;
        blocking.parameters = QVariantMap{};
        const ShaderProfile flatBlocking = withPresetsResolved(blocking, registry);
        QVERIFY(flatBlocking.parameters.has_value());
        QVERIFY(flatBlocking.parameters->isEmpty());
    }

    void testFlattenLeavesAProfileWithNoPresetAlone()
    {
        PhosphorShaders::ShaderPresetRegistry registry;
        ShaderProfile p;
        p.effectId = QStringLiteral("dissolve");
        p.parameters = QVariantMap{{QStringLiteral("speed"), 2.0}};
        QCOMPARE(withPresetsResolved(p, registry), p);
    }

    void testNonStringPresetIdIsDropped()
    {
        // Mirror of the decoration twin: a hand-edited config carrying a
        // non-string presetId must not coerce to the string "42".
        const QJsonObject obj{{QStringLiteral("effectId"), QStringLiteral("dissolve")},
                              {QStringLiteral("presetId"), 42}};
        const ShaderProfile p = ShaderProfile::fromJson(obj);
        QCOMPARE(*p.effectId, QStringLiteral("dissolve"));
        QVERIFY(!p.presetId.has_value());
    }
};

QTEST_MAIN(TestShaderProfile)
#include "test_shaderprofile.moc"

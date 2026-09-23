// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// The PRESET half of DecorationProfileTree: per-pack preset references, their
// JSON round trip and inheritance, and `withPresetsResolved` — the flatten that
// turns a resolved profile plus its preset ids into the parameter map a consumer
// renders with. Split from test_decorationprofiletree.cpp when that file reached the
// size ceiling; the seed-injection and plain resolve/round-trip slots stay there.
//
// Same fixtures, same namespace helper, because the two halves assert against the
// same value type and a second spelling of makeProfile would be a second contract.

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>
#include <PhosphorSurface/DecorationSupportedPaths.h>

#include <PhosphorShaders/ShaderPresetRegistry.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QtTest/QtTest>

using namespace PhosphorSurfaceShaders;

namespace {

/// A profile carrying an explicit chain plus per-pack parameters, for round-trip and
/// inheritance assertions. `[[maybe_unused]]` because only some of the slots below need a
/// pre-built chain; the helper is kept identical to its twin in the sibling file so the
/// two halves cannot drift on what a fixture profile is.
[[maybe_unused]] DecorationProfile makeProfile(const QStringList& chain, double borderWidth, const QString& borderColor)
{
    DecorationProfile p;
    p.chain = chain;
    QVariantMap borderParams;
    borderParams.insert(QStringLiteral("width"), borderWidth);
    borderParams.insert(QStringLiteral("color"), borderColor);
    QVariantMap params;
    params.insert(QStringLiteral("border"), borderParams);
    p.parameters = params;
    return p;
}

} // namespace
class TestDecorationProfileTreePresets : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ─────── Per-pack preset references ───────

    void testPresetIdsDefaultUnset()
    {
        DecorationProfile p;
        QVERIFY(!p.presetIds.has_value());
        QVERIFY(p.effectivePresetIds().isEmpty());
        QVERIFY(p.presetIdFor(QStringLiteral("border")).isEmpty());
    }

    void testPresetIdsRoundTrip()
    {
        DecorationProfile p;
        p.chain = QStringList{QStringLiteral("border"), QStringLiteral("glow")};
        p.presetIds = QVariantMap{{QStringLiteral("border"), QStringLiteral("{thick}")}};
        p.parameters = QVariantMap{{QStringLiteral("border"), QVariantMap{{QStringLiteral("width"), 4}}}};

        const DecorationProfile back = DecorationProfile::fromJson(p.toJson());
        QCOMPARE(back, p);
        QCOMPARE(back.presetIdFor(QStringLiteral("border")), QStringLiteral("{thick}"));
        // A layer that named no preset reports none.
        QVERIFY(back.presetIdFor(QStringLiteral("glow")).isEmpty());
    }

    void testAbsentPresetIdsStayUnsetThroughJson()
    {
        // A config written before presets existed loads with every layer on
        // its own parameters, and the field inheriting rather than engaged.
        DecorationProfile p;
        p.chain = QStringList{QStringLiteral("border")};
        const DecorationProfile back = DecorationProfile::fromJson(p.toJson());
        QVERIFY(!back.presetIds.has_value());
        QCOMPARE(back, p);
    }

    void testNonStringPresetIdIsDropped()
    {
        // A preset id is a lookup key; coercing a malformed value to "" would
        // mean "no preset", a different statement from what the author wrote.
        QJsonObject obj;
        QJsonArray chain;
        chain.append(QStringLiteral("border"));
        obj.insert(QLatin1String("chain"), chain);
        QJsonObject presets;
        presets.insert(QStringLiteral("border"), 42);
        presets.insert(QStringLiteral("glow"), QStringLiteral("{ok}"));
        obj.insert(QLatin1String("presetIds"), presets);

        const DecorationProfile p = DecorationProfile::fromJson(obj);
        QVERIFY(p.presetIds.has_value());
        QVERIFY(p.presetIdFor(QStringLiteral("border")).isEmpty());
        QCOMPARE(p.presetIdFor(QStringLiteral("glow")), QStringLiteral("{ok}"));
    }

    void testPresetIdsInheritAndOverride()
    {
        DecorationProfile parent;
        parent.presetIds = QVariantMap{{QStringLiteral("border"), QStringLiteral("{from-parent}")}};
        DecorationProfile child;
        child.chain = QStringList{QStringLiteral("border")};

        DecorationProfile merged = parent;
        DecorationProfile::overlay(merged, child);
        QCOMPARE(merged.presetIdFor(QStringLiteral("border")), QStringLiteral("{from-parent}"));

        child.presetIds = QVariantMap{{QStringLiteral("border"), QStringLiteral("{from-child}")}};
        merged = parent;
        DecorationProfile::overlay(merged, child);
        QCOMPARE(merged.presetIdFor(QStringLiteral("border")), QStringLiteral("{from-child}"));
    }

    void testPresetIdsParticipateInEquality()
    {
        DecorationProfile a;
        a.chain = QStringList{QStringLiteral("border")};
        DecorationProfile b = a;
        QCOMPARE(a, b);
        b.presetIds = QVariantMap{{QStringLiteral("border"), QStringLiteral("{thick}")}};
        QVERIFY(a != b);
    }

    // ─────── withPresetsResolved ───────

    void testFlattenAppliesPresetUnderLayerEdits()
    {
        PhosphorShaders::ShaderPresetRegistry registry;
        PhosphorShaders::ShaderPreset thick;
        thick.id = QStringLiteral("{thick}");
        thick.name = QStringLiteral("Thick");
        thick.packId = QStringLiteral("border");
        thick.params = QVariantMap{{QStringLiteral("width"), 8}, {QStringLiteral("radius"), 12}};
        registry.setUserPresets(PhosphorShaders::ShaderFamily::Surface, {thick});

        DecorationProfile p;
        p.chain = QStringList{QStringLiteral("border"), QStringLiteral("glow")};
        p.presetIds = QVariantMap{{QStringLiteral("border"), QStringLiteral("{thick}")}};
        // This layer overrides only the radius; width must follow the preset.
        p.parameters = QVariantMap{{QStringLiteral("border"), QVariantMap{{QStringLiteral("radius"), 2}}},
                                   {QStringLiteral("glow"), QVariantMap{{QStringLiteral("strength"), 5}}}};

        const DecorationProfile flat = withPresetsResolved(p, registry, PhosphorShaders::ShaderFamily::Surface);
        const QVariantMap border = flat.effectiveParameters().value(QStringLiteral("border")).toMap();
        QCOMPARE(border.value(QStringLiteral("width")).toInt(), 8);
        QCOMPARE(border.value(QStringLiteral("radius")).toInt(), 2);
        // A layer naming no preset is untouched.
        QCOMPARE(
            flat.effectiveParameters().value(QStringLiteral("glow")).toMap().value(QStringLiteral("strength")).toInt(),
            5);
        // Cleared, so a second flatten cannot apply twice.
        QVERIFY(!flat.presetIds.has_value());
    }

    void testFlattenIsIdempotent()
    {
        PhosphorShaders::ShaderPresetRegistry registry;
        PhosphorShaders::ShaderPreset thick;
        thick.id = QStringLiteral("{thick}");
        thick.name = QStringLiteral("Thick");
        thick.packId = QStringLiteral("border");
        thick.params = QVariantMap{{QStringLiteral("width"), 8}};
        registry.setUserPresets(PhosphorShaders::ShaderFamily::Surface, {thick});

        DecorationProfile p;
        p.presetIds = QVariantMap{{QStringLiteral("border"), QStringLiteral("{thick}")}};
        p.parameters = QVariantMap{{QStringLiteral("border"), QVariantMap{{QStringLiteral("width"), 3}}}};

        const DecorationProfile once = withPresetsResolved(p, registry, PhosphorShaders::ShaderFamily::Surface);
        const DecorationProfile twice = withPresetsResolved(once, registry, PhosphorShaders::ShaderFamily::Surface);
        QCOMPARE(once, twice);
        // The CLEARED reference is what makes a second pass a no-op, so assert it
        // directly. `once == twice` alone does not: with the reset deleted, the
        // second flatten re-applies the same preset under the same deltas and the
        // two still compare equal field for field, so this test passed while the
        // thing it is named for was gone.
        QVERIFY(!once.presetIds.has_value());
        QVERIFY(!twice.presetIds.has_value());
        // The layer's own edit wins over the preset, and stays won.
        QCOMPARE(
            once.effectiveParameters().value(QStringLiteral("border")).toMap().value(QStringLiteral("width")).toInt(),
            3);
    }

    void testFlattenRespectsTheFamily()
    {
        // The same pack id under two families is two different packs: a
        // surface chain must not pick up a pointer preset.
        PhosphorShaders::ShaderPresetRegistry registry;
        PhosphorShaders::ShaderPreset pointerPreset;
        pointerPreset.id = QStringLiteral("{p}");
        pointerPreset.name = QStringLiteral("P");
        pointerPreset.packId = QStringLiteral("glow");
        pointerPreset.params = QVariantMap{{QStringLiteral("strength"), 99}};
        registry.setUserPresets(PhosphorShaders::ShaderFamily::Pointer, {pointerPreset});

        DecorationProfile p;
        p.presetIds = QVariantMap{{QStringLiteral("glow"), QStringLiteral("{p}")}};

        const DecorationProfile asSurface = withPresetsResolved(p, registry, PhosphorShaders::ShaderFamily::Surface);
        QVERIFY(asSurface.effectiveParameters().value(QStringLiteral("glow")).toMap().isEmpty());

        const DecorationProfile asPointer = withPresetsResolved(p, registry, PhosphorShaders::ShaderFamily::Pointer);
        QCOMPARE(asPointer.effectiveParameters()
                     .value(QStringLiteral("glow"))
                     .toMap()
                     .value(QStringLiteral("strength"))
                     .toInt(),
                 99);
    }

    void testFlattenLeavesProfileWithNoPresetsAlone()
    {
        PhosphorShaders::ShaderPresetRegistry registry;
        DecorationProfile p;
        p.chain = QStringList{QStringLiteral("border")};
        p.parameters = QVariantMap{{QStringLiteral("border"), QVariantMap{{QStringLiteral("width"), 3}}}};
        QCOMPARE(withPresetsResolved(p, registry, PhosphorShaders::ShaderFamily::Surface), p);
    }

    void testFlattenWithMissingPresetKeepsOwnParameters()
    {
        // An assignment can outlive the preset it points at. It must degrade
        // to its own values, never to nothing.
        PhosphorShaders::ShaderPresetRegistry registry;
        DecorationProfile p;
        p.presetIds = QVariantMap{{QStringLiteral("border"), QStringLiteral("{deleted}")}};
        p.parameters = QVariantMap{{QStringLiteral("border"), QVariantMap{{QStringLiteral("width"), 3}}}};

        const DecorationProfile flat = withPresetsResolved(p, registry, PhosphorShaders::ShaderFamily::Surface);
        QCOMPARE(
            flat.effectiveParameters().value(QStringLiteral("border")).toMap().value(QStringLiteral("width")).toInt(),
            3);
    }

    void testFlattenDoesNotEngageParametersItHasNothingToPutIn()
    {
        // nullopt and engaged-empty are different statements: engaged-empty is
        // "no parameters here, and do not inherit any". Reachable whenever every
        // entry in presetIds holds an empty string, which is the sentinel an
        // assignment writes to BLOCK an inherited preset without naming one of
        // its own — so the flatten of a blocking-only profile silently also
        // blocked inherited parameters.
        PhosphorShaders::ShaderPresetRegistry registry;
        DecorationProfile p;
        p.chain = QStringList{QStringLiteral("border")};
        p.presetIds = QVariantMap{{QStringLiteral("border"), QString()}};
        QVERIFY(!p.parameters.has_value());

        const DecorationProfile flat = withPresetsResolved(p, registry, PhosphorShaders::ShaderFamily::Surface);
        QVERIFY(!flat.parameters.has_value());
        // The BLOCKING entry survives the flatten, and that is the point of this
        // assertion rather than an accident of it. The flatten clears the entries it
        // consumed so a second pass cannot double-apply, but an empty presetId was
        // never consumed: it is the user saying "this pack follows no preset".
        // Clearing the whole map revoked that, so a flattened-and-overlaid child
        // re-inherited the very preset it had blocked.
        QVERIFY(flat.presetIds.has_value());
        QCOMPARE(flat.presetIds->size(), 1);
        QVERIFY(flat.presetIds->value(QStringLiteral("border")).toString().isEmpty());

        // A profile that DID engage an empty map keeps it: that one is the
        // user's statement and the flatten must not revoke it either.
        DecorationProfile blocking = p;
        blocking.parameters = QVariantMap{};
        const DecorationProfile flatBlocking =
            withPresetsResolved(blocking, registry, PhosphorShaders::ShaderFamily::Surface);
        QVERIFY(flatBlocking.parameters.has_value());
        QVERIFY(flatBlocking.parameters->isEmpty());
    }

    void testFlattenKeepsBlockingEntriesAndDropsConsumedOnes()
    {
        // One profile blocking one pack and resolving another is the case neither an
        // early return nor a wholesale reset gets right, so pin both halves at once.
        PhosphorShaders::ShaderPresetRegistry registry;
        PhosphorShaders::PackPresets presets;
        presets.insert(QStringLiteral("Night"), QVariantMap{{QStringLiteral("width"), 7}});
        registry.setPackPresetsForFamily(PhosphorShaders::ShaderFamily::Surface, {{QStringLiteral("shadow"), presets}},
                                         {});

        DecorationProfile p;
        p.chain = QStringList{QStringLiteral("border"), QStringLiteral("shadow")};
        p.presetIds = QVariantMap{
            {QStringLiteral("border"), QString()},
            {QStringLiteral("shadow"), QStringLiteral("Night")},
        };

        const DecorationProfile flat = withPresetsResolved(p, registry, PhosphorShaders::ShaderFamily::Surface);
        // Consumed: gone, so a second flatten cannot apply it twice.
        QVERIFY(flat.presetIds.has_value());
        QVERIFY(!flat.presetIds->contains(QStringLiteral("shadow")));
        // Blocking: kept.
        QVERIFY(flat.presetIds->contains(QStringLiteral("border")));
        QVERIFY(flat.presetIds->value(QStringLiteral("border")).toString().isEmpty());
        QCOMPARE(
            flat.effectiveParameters().value(QStringLiteral("shadow")).toMap().value(QStringLiteral("width")).toInt(),
            7);

        // Idempotent: the second pass finds only the blocking entry, resolves
        // nothing and preserves it.
        const DecorationProfile again = withPresetsResolved(flat, registry, PhosphorShaders::ShaderFamily::Surface);
        QCOMPARE(again.presetIds, flat.presetIds);
        QCOMPARE(again.effectiveParameters(), flat.effectiveParameters());
    }

    void testFlattenClampsOwnValuesWithNoPresetEngaged()
    {
        // resolveParams is where a pack's declared min/max is enforced, and the
        // flatten used to early-return before reaching it when no preset was named.
        // A value that arrived by any door other than the settings slider — a
        // hand-edited config.json, a D-Bus write, a config predating a narrowed
        // range — therefore reached the uniform unbounded.
        PhosphorShaders::ShaderPresetRegistry registry;
        PhosphorShaders::PresetValueBounds bounds;
        bounds.insert(QStringLiteral("width"), {1, 10});
        registry.setPackPresetsForFamily(PhosphorShaders::ShaderFamily::Surface, {},
                                         {{QStringLiteral("border"), bounds}});

        DecorationProfile p;
        p.chain = QStringList{QStringLiteral("border")};
        p.parameters = QVariantMap{{QStringLiteral("border"), QVariantMap{{QStringLiteral("width"), 9999}}}};
        QVERIFY(!p.presetIds.has_value());

        const DecorationProfile flat = withPresetsResolved(p, registry, PhosphorShaders::ShaderFamily::Surface);
        QCOMPARE(
            flat.effectiveParameters().value(QStringLiteral("border")).toMap().value(QStringLiteral("width")).toInt(),
            10);
        // Still no preset axis invented, and still no engagement invented either.
        QVERIFY(!flat.presetIds.has_value());

        DecorationProfile bare;
        bare.chain = QStringList{QStringLiteral("border")};
        const DecorationProfile flatBare = withPresetsResolved(bare, registry, PhosphorShaders::ShaderFamily::Surface);
        QVERIFY(!flatBare.parameters.has_value());
    }
};

QTEST_MAIN(TestDecorationProfileTreePresets)
#include "test_decorationprofiletree_presets.moc"

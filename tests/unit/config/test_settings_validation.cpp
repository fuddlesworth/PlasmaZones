// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_validation.cpp
 * @brief End-to-end validation tests for the PhosphorConfig::Store schema
 *
 * The tests here seed the backing JSON config with deliberately-invalid or
 * out-of-range values, then construct a Settings object and verify that the
 * schema validator coerces the value on read. Covers:
 *  1. clampInt validator -- out-of-range int snaps to the schema default.
 *  2. validColorOr validator -- invalid color string falls back to default.
 *  3. Trigger list JSON parse -- invalid JSON drops back to the default,
 *     max-size cap at MaxTriggersPerAction is enforced.
 *  4. validIntOr enum validator -- unknown enum value snaps to the safe
 *     default rather than the nearest in-range neighbour.
 */

#include <QTest>
#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include "../../../src/config/settings.h"
#include "../../../src/config/configdefaults.h"
#include "../../../src/config/configbackends.h"
#include "../../../src/core/constants.h"
#include "../../../src/core/enums.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestSettingsValidation : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // =========================================================================
    // Schema clampInt validator (out-of-range int)
    // =========================================================================

    /**
     * The clampInt validator wired into the PhosphorZones::Zone padding KeyDef must coerce
     * a hand-written 999 into the schema max, so the reader sees the
     * canonical default instead of the raw invalid value.
     *
     * Seeds at the v2 schema location (Snapping.Gaps/Inner) so the validator
     * is actually exercised. Seeding at the legacy v1 location would be
     * skipped by ensureJsonConfig's version-match short-circuit and the test
     * would pass for the wrong reason.
     */
    void testReadValidatedInt_outOfRange_returnsDefault()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto gaps = backend->group(ConfigDefaults::snappingGapsGroup());
            gaps->writeInt(ConfigDefaults::innerKey(), 999); // clamp max is zonePaddingMax()
            gaps.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.zonePadding(), ConfigDefaults::zonePaddingMax());
    }

    /**
     * The clampInt validators wired into the new snapped-window border KeyDefs
     * (Snapping.Appearance.Borders/Width and /Radius) must coerce an
     * out-of-range value to the schema bound. These keys share the same KeyDef
     * shape as the autotile border keys but can regress independently, so they
     * get their own coverage.
     */
    void testReadValidatedInt_snappingBorderWidth_outOfRange_returnsMax()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto borders = backend->group(ConfigDefaults::snappingAppearanceBordersGroup());
            borders->writeInt(ConfigDefaults::widthKey(), 999); // clamp max is snappingBorderWidthMax()
            borders.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.snappingBorderWidth(), ConfigDefaults::snappingBorderWidthMax());
    }

    void testReadValidatedInt_snappingBorderRadius_outOfRange_returnsMax()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto borders = backend->group(ConfigDefaults::snappingAppearanceBordersGroup());
            borders->writeInt(ConfigDefaults::radiusKey(), 999); // clamp max is snappingBorderRadiusMax()
            borders.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.snappingBorderRadius(), ConfigDefaults::snappingBorderRadiusMax());
    }

    void testReadValidatedInt_snappingBorderWidth_negative_returnsMin()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto borders = backend->group(ConfigDefaults::snappingAppearanceBordersGroup());
            borders->writeInt(ConfigDefaults::widthKey(), -5); // clamp min is snappingBorderWidthMin()
            borders.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.snappingBorderWidth(), ConfigDefaults::snappingBorderWidthMin());
    }

    void testReadValidatedInt_snappingBorderRadius_negative_returnsMin()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto borders = backend->group(ConfigDefaults::snappingAppearanceBordersGroup());
            borders->writeInt(ConfigDefaults::radiusKey(), -5); // clamp min is snappingBorderRadiusMin()
            borders.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.snappingBorderRadius(), ConfigDefaults::snappingBorderRadiusMin());
    }

    /**
     * The validColorOr validator on the snapped-window border color
     * (Snapping.Appearance.Colors/Active) must fall back to the schema default
     * for an unparseable string. useSystemBorderColors is disabled so
     * Settings::load() doesn't overwrite the validated value with the
     * accent-derived system color.
     */
    void testReadValidatedColor_snappingBorderColor_invalidColor_returnsDefault()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto colors = backend->group(ConfigDefaults::snappingAppearanceColorsGroup());
            colors->writeBool(ConfigDefaults::useSystemKey(), false);
            colors->writeString(ConfigDefaults::activeKey(), QStringLiteral("not-a-color"));
            colors.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.snappingBorderColor(), ConfigDefaults::snappingBorderColor());
    }

    /**
     * Sibling to the active-color test above: the validColorOr validator on the
     * snapped-window INACTIVE border color (Snapping.Appearance.Colors/Inactive)
     * must fall back to its schema default for an unparseable string. It shares
     * the active color's KeyDef shape but can regress independently, so it gets
     * its own coverage. useSystemBorderColors is disabled so Settings::load()
     * doesn't overwrite the validated value with the accent-derived color.
     */
    void testReadValidatedColor_snappingInactiveBorderColor_invalidColor_returnsDefault()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto colors = backend->group(ConfigDefaults::snappingAppearanceColorsGroup());
            colors->writeBool(ConfigDefaults::useSystemKey(), false);
            colors->writeString(ConfigDefaults::inactiveKey(), QStringLiteral("not-a-color"));
            colors.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.snappingInactiveBorderColor(), ConfigDefaults::snappingInactiveBorderColor());
    }

    /**
     * The inverse of the validator tests above: with useSystemBorderColors ENABLED,
     * Settings::load() routes through applySnappingBorderSystemColor(), overriding the
     * stored Active color with the accent-derived highlight color. A hand-seeded
     * explicit Active color must NOT survive the load — proving the system-color
     * override fires for the snap-window border (the load path the disabled tests
     * deliberately avoid).
     */
    void testReadValidatedColor_snappingBorderColor_systemColorsEnabled_overridesStored()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto colors = backend->group(ConfigDefaults::snappingAppearanceColorsGroup());
            colors->writeBool(ConfigDefaults::useSystemKey(), true);
            colors->writeString(ConfigDefaults::activeKey(), QStringLiteral("#010203"));
            colors.reset();
            backend->sync();
        }

        Settings settings;
        // System colors enabled → border adopts the zone highlight HUE but with
        // the near-opaque window-border alpha (not the translucent zone-fill alpha).
        QColor expectedBorder = settings.highlightColor();
        expectedBorder.setAlpha(::PhosphorZones::ZoneDefaults::WindowBorderActiveAlpha);
        QCOMPARE(settings.snappingBorderColor(), expectedBorder);
        QVERIFY(settings.snappingBorderColor() != QColor(QStringLiteral("#010203")));
    }

    // =========================================================================
    // Schema validColorOr validator (invalid color string)
    // =========================================================================

    /**
     * The validColorOr validator must fall back to the schema default when
     * the stored string fails to parse as a valid QColor. Seeds at
     * Snapping.Zones.Colors/Highlight and disables useSystemColors so
     * Settings::load() doesn't call applySystemColorScheme and overwrite the
     * validated value with a palette-derived tint.
     */
    void testReadValidatedColor_invalidColor_returnsDefault()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto appearance = backend->group(ConfigDefaults::snappingZonesColorsGroup());
            appearance->writeBool(ConfigDefaults::useSystemKey(), false);
            appearance->writeString(ConfigDefaults::highlightKey(), QStringLiteral("not-a-color"));
            appearance.reset();
            backend->sync();
        }

        Settings settings;
        // Must fall back to the schema default (which is always valid).
        QCOMPARE(settings.highlightColor(), ConfigDefaults::highlightColor());
    }

    // =========================================================================
    // Trigger list JSON parse (invalid JSON + max-size cap)
    // =========================================================================

    /**
     * Invalid JSON in the drag-activation trigger list must fall back to the
     * schema default rather than propagating a corrupt list upwards. Seeds
     * at the v2 location (Snapping.Behavior/Triggers).
     */
    void testParseTriggerListJson_invalidJson_returnsSchemaDefault()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto behavior = backend->group(ConfigDefaults::snappingBehaviorGroup());
            // writeString is always verbatim — the literal "{broken json["
            // survives the write as a string, and the Store's trigger-list
            // reader falls back to the schema default when parsing fails.
            behavior->writeString(ConfigDefaults::triggersKey(), QStringLiteral("{broken json["));
            behavior.reset();
            backend->sync();
        }

        Settings settings;

        const QVariantList triggers = settings.dragActivationTriggers();
        // Invalid JSON must fall back to the declarative schema default.
        QCOMPARE(triggers, ConfigDefaults::dragActivationTriggers());
    }

    /**
     * The setter must cap trigger lists at MaxTriggersPerAction so an
     * overlong list passed via the API (or the UI) can never persist more
     * than the cap.
     */
    void testSetDragActivationTriggers_capsAtMaxTriggers()
    {
        IsolatedConfigGuard guard;

        Settings settings;

        QVariantList overlong;
        for (int i = 0; i < Settings::MaxTriggersPerAction + 2; ++i) {
            QVariantMap trigger;
            trigger[ConfigDefaults::triggerModifierField()] = i;
            trigger[ConfigDefaults::triggerMouseButtonField()] = 0;
            overlong.append(trigger);
        }

        settings.setDragActivationTriggers(overlong);

        QCOMPARE(settings.dragActivationTriggers().size(), Settings::MaxTriggersPerAction);
    }

    // =========================================================================
    // Drag/Overflow behavior enum loading: unknown values must clamp to the
    // safe default (Float) rather than the highest known value. The earlier
    // qBound-based clamp would silently snap a future config value (e.g.
    // DragBehavior=2 for a hypothetical ReorderAcrossScreens) to Reorder, the
    // exact silent-misinterpretation pattern the effect-side cache loader
    // (plasmazoneseffect.cpp:loadCachedSettings) avoids. Both readers must
    // agree, and that agreement is pinned here.
    // =========================================================================

    void testAutotileDragBehavior_unknownValueClampsToFloat()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto tilingBehavior = backend->group(ConfigDefaults::tilingBehaviorGroup());
            tilingBehavior->writeInt(ConfigDefaults::dragBehaviorKey(), 99); // out of range
            tilingBehavior.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.autotileDragBehavior(), AutotileDragBehavior::Float);
    }

    void testAutotileDragBehavior_validReorderValueLoadsCorrectly()
    {
        // Sanity baseline: a valid Reorder=1 value must round-trip, so the
        // unknown-value test above isn't masking a broken setter path.
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto tilingBehavior = backend->group(ConfigDefaults::tilingBehaviorGroup());
            tilingBehavior->writeInt(ConfigDefaults::dragBehaviorKey(),
                                     static_cast<int>(AutotileDragBehavior::Reorder));
            tilingBehavior.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.autotileDragBehavior(), AutotileDragBehavior::Reorder);
    }

    void testAutotileOverflowBehavior_unknownValueClampsToFloat()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto tilingBehavior = backend->group(ConfigDefaults::tilingBehaviorGroup());
            tilingBehavior->writeInt(ConfigDefaults::overflowBehaviorKey(), 42); // out of range
            tilingBehavior.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.autotileOverflowBehavior(), PhosphorTiles::AutotileOverflowBehavior::Float);
    }

    void testAutotileOverflowBehavior_validUnlimitedValueLoadsCorrectly()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto tilingBehavior = backend->group(ConfigDefaults::tilingBehaviorGroup());
            tilingBehavior->writeInt(ConfigDefaults::overflowBehaviorKey(),
                                     static_cast<int>(PhosphorTiles::AutotileOverflowBehavior::Unlimited));
            tilingBehavior.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.autotileOverflowBehavior(), PhosphorTiles::AutotileOverflowBehavior::Unlimited);
    }
};

QTEST_MAIN(TestSettingsValidation)
#include "test_settings_validation.moc"

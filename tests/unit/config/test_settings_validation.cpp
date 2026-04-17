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
     * The clampInt validator wired into the Zone padding KeyDef must coerce
     * a hand-written 999 into the schema max, so the reader sees the
     * canonical default instead of the raw invalid value.
     */
    void testReadValidatedInt_outOfRange_returnsDefault()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto zones = backend->group(QStringLiteral("Zones"));
            zones->writeInt(QStringLiteral("Padding"), 999); // max is 50
            zones.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.zonePadding(), ConfigDefaults::zonePadding());
    }

    // =========================================================================
    // Schema validColorOr validator (invalid color string)
    // =========================================================================

    /**
     * The validColorOr validator must fall back to the schema default when
     * the stored string fails to parse as a valid QColor.
     */
    void testReadValidatedColor_invalidColor_returnsDefault()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto appearance = backend->group(QStringLiteral("Appearance"));
            appearance->writeString(QStringLiteral("HighlightColor"), QStringLiteral("not-a-color"));
            appearance.reset();
            backend->sync();
        }

        Settings settings;
        // The color should be valid (either default or system color)
        QVERIFY2(settings.highlightColor().isValid(), "Invalid color in config must fall back to a valid default");
    }

    // =========================================================================
    // Trigger list JSON parse (invalid JSON + max-size cap)
    // =========================================================================

    /**
     * Invalid JSON in the drag-activation trigger list must fall back to the
     * schema default rather than propagating a corrupt list upwards.
     */
    void testParseTriggerListJson_invalidJson_returnsNullopt()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto activation = backend->group(QStringLiteral("Activation"));
            // Write invalid JSON as the trigger list
            activation->writeString(QStringLiteral("DragActivationTriggers"), QStringLiteral("{broken json["));
            // Provide a legacy fallback modifier
            activation->writeInt(QStringLiteral("DragActivationModifier"), 3); // Alt
            activation.reset();
            backend->sync();
        }

        Settings settings;

        QVariantList triggers = settings.dragActivationTriggers();
        // Should have fallen back to legacy migration
        QVERIFY(!triggers.isEmpty());
        QVariantMap first = triggers.first().toMap();
        QCOMPARE(first.value(QStringLiteral("modifier")).toInt(), 3);
    }

    /**
     * Trigger lists must be capped at MaxTriggersPerAction (4), so a config
     * containing 6 triggers reads back with at most 4.
     */
    void testParseTriggerListJson_capsAtMaxTriggers()
    {
        IsolatedConfigGuard guard;

        // Build a JSON array with 6 triggers (above MaxTriggersPerAction=4)
        QJsonArray arr;
        for (int i = 0; i < 6; ++i) {
            QJsonObject obj;
            obj[QLatin1String("modifier")] = i;
            obj[QLatin1String("mouseButton")] = 0;
            arr.append(obj);
        }
        QString json = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto activation = backend->group(QStringLiteral("Activation"));
            activation->writeString(QStringLiteral("DragActivationTriggers"), json);
            activation.reset();
            backend->sync();
        }

        Settings settings;

        QVERIFY2(settings.dragActivationTriggers().size() <= Settings::MaxTriggersPerAction,
                 "Trigger list must be capped at MaxTriggersPerAction");
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
        QCOMPARE(settings.autotileOverflowBehavior(), AutotileOverflowBehavior::Float);
    }

    void testAutotileOverflowBehavior_validUnlimitedValueLoadsCorrectly()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto tilingBehavior = backend->group(ConfigDefaults::tilingBehaviorGroup());
            tilingBehavior->writeInt(ConfigDefaults::overflowBehaviorKey(),
                                     static_cast<int>(AutotileOverflowBehavior::Unlimited));
            tilingBehavior.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.autotileOverflowBehavior(), AutotileOverflowBehavior::Unlimited);
    }
};

QTEST_MAIN(TestSettingsValidation)
#include "test_settings_validation.moc"

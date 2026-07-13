// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_validation.cpp
 * @brief End-to-end validation tests for the PhosphorConfig::Store schema
 *
 * The tests here seed the backing JSON config with deliberately-invalid or
 * out-of-range values, then construct a Settings object and verify that the
 * schema validator coerces the value on read. Covers:
 *  1. clampInt validator -- out-of-range int snaps to the violated clamp bound.
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
     * The clampInt validator wired into a stored int KeyDef must coerce a
     * hand-written 999 into the schema max, so the reader sees the canonical
     * clamped value instead of the raw invalid value.
     *
     * Uses adjacentThreshold (Snapping.Gaps/AdjacentThreshold, clamp max 500).
     * The shared inner/outer gaps are no longer stored config keys (their global
     * default is rule-backed), so this exercises the validator on a key that is
     * still schema-backed.
     */
    void testReadValidatedInt_outOfRange_clampsToMax()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto gaps = backend->group(ConfigDefaults::snappingGapsGroup());
            gaps->writeInt(ConfigDefaults::adjacentThresholdKey(), 999); // clamp max is adjacentThresholdMax()
            gaps.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.adjacentThreshold(), ConfigDefaults::adjacentThresholdMax());
    }

    /**
     * Same clampInt contract on the decoration focus cross-fade duration
     * (Windows/FocusFadeDuration): a hand-written out-of-range value snaps to
     * the declared max rather than reaching the effect raw.
     */
    void testReadValidatedFocusFadeDuration_outOfRange_clampsToMax()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto windows = backend->group(ConfigDefaults::windowsAppearanceGroup());
            windows->writeInt(ConfigDefaults::focusFadeDurationKey(), 999999);
            windows.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.focusFadeDuration(), ConfigDefaults::focusFadeDurationMax());
    }

    /**
     * The min side of the same clamp: a negative on-disk value snaps up to the
     * declared minimum (0 = instant), so the effect never divides by a negative
     * duration. Without this case a validator that only clamps the upper bound
     * would pass the suite.
     */
    void testReadValidatedFocusFadeDuration_belowMin_clampsToMin()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto windows = backend->group(ConfigDefaults::windowsAppearanceGroup());
            windows->writeInt(ConfigDefaults::focusFadeDurationKey(), -50);
            windows.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.focusFadeDuration(), ConfigDefaults::focusFadeDurationMin());
    }

    /**
     * Same clampInt contract on the decoration idle timeout
     * (Decorations.Performance/IdleTimeoutSec). This one is load-bearing rather
     * than cosmetic: the daemon feeds the value straight into an
     * ext-idle-notify-v1 timeout as `value * 1000`, so an unclamped on-disk value
     * would arm a nonsensical timer. A zero or negative timeout is dropped by the
     * idle service, silently disabling the pause; a value at or above ~2147484
     * would overflow the int multiply outright.
     */
    void testReadValidatedDecorationIdleTimeout_outOfRange_clampsToMax()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto perf = backend->group(ConfigDefaults::decorationsPerformanceGroup());
            perf->writeInt(ConfigDefaults::idleTimeoutSecKey(), 999999999);
            perf.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.decorationIdleTimeoutSec(), ConfigDefaults::decorationIdleTimeoutSecMax());
    }

    void testReadValidatedDecorationIdleTimeout_belowMin_clampsToMin()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto perf = backend->group(ConfigDefaults::decorationsPerformanceGroup());
            perf->writeInt(ConfigDefaults::idleTimeoutSecKey(), -1);
            perf.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.decorationIdleTimeoutSec(), ConfigDefaults::decorationIdleTimeoutSecMin());
    }

    /**
     * A config with NO Decorations.Performance group at all must report the DEFAULTS,
     * and PauseWhenIdle's default is TRUE.
     *
     * This pins the bug that actually shipped. Every layer of the wiring looked
     * complete, but the key was missing from SettingsAdaptor's hand-maintained getter
     * registry, and getSetting answered an unknown key with a valid EMPTY STRING —
     * which QVariant::toBool() reads as false. So a default-true setting came back
     * false on every startup: not merely disabled, INVERTED. The registry hole itself
     * is guarded by test_settings_registry_contract; this guards the other half, that
     * an absent key still yields the default it is supposed to.
     */
    void testDecorationPerformance_missingGroup_yieldsDefaults()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        QVERIFY2(settings.decorationPauseWhenIdle(),
                 "PauseWhenIdle defaults to TRUE. A false here means something is reading the absent key as a "
                 "value rather than falling back — which is exactly how it shipped inverted once.");
        QCOMPARE(settings.decorationPauseWhenIdle(), ConfigDefaults::decorationPauseWhenIdle());
        QCOMPARE(settings.decorationAnimateFocusedOnly(), ConfigDefaults::decorationAnimateFocusedOnly());
        QCOMPARE(settings.decorationIdleTimeoutSec(), ConfigDefaults::decorationIdleTimeoutSec());
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
    // Schema validStringOr validator (unknown closed-set scope token)
    // =========================================================================

    /**
     * The validStringOr validator wired into the Windows group's BorderScope /
     * TitleBarScope keys must snap an unknown on-disk token to the default
     * ("tiled"). The scope is a closed set ("tiled" / "normal" / "all") the
     * Appearance page and the effect agree on, so a hand-edited garbage token
     * must never flow through to the effect.
     */
    void testReadValidatedScope_unknownToken_snapsToDefault()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto windows = backend->group(ConfigDefaults::windowsAppearanceGroup());
            windows->writeString(ConfigDefaults::borderScopeKey(), QStringLiteral("garbage"));
            windows->writeString(ConfigDefaults::titleBarScopeKey(), QStringLiteral("garbage"));
            windows.reset();
            backend->sync();
        }

        Settings settings;
        // Both scopes fall back to the schema default (=="tiled").
        QCOMPARE(settings.windowBorderScope(), ConfigDefaults::windowBorderScope());
        QCOMPARE(settings.windowTitleBarScope(), ConfigDefaults::windowTitleBarScope());
    }

    /**
     * Sanity baseline: a valid closed-set token round-trips untouched, so the
     * unknown-token test above isn't masking a validator that snaps everything
     * to the default.
     */
    void testReadValidatedScope_validToken_preserved()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto windows = backend->group(ConfigDefaults::windowsAppearanceGroup());
            windows->writeString(ConfigDefaults::borderScopeKey(), QStringLiteral("normal"));
            windows->writeString(ConfigDefaults::titleBarScopeKey(), QStringLiteral("all"));
            windows.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.windowBorderScope(), QStringLiteral("normal"));
        QCOMPARE(settings.windowTitleBarScope(), QStringLiteral("all"));
    }

    /**
     * A hand-edited garbage border colour (neither the "accent" sentinel nor a
     * valid QColor) snaps to the schema default so garbage can't flow to the
     * effect; a valid hex round-trips untouched.
     */
    void testReadValidatedBorderColor_garbageSnaps_hexPreserved()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto windows = backend->group(ConfigDefaults::windowsAppearanceGroup());
            windows->writeString(ConfigDefaults::borderColorActiveKey(), QStringLiteral("not-a-color"));
            windows->writeString(ConfigDefaults::borderColorInactiveKey(), QStringLiteral("#FF3DAEE9"));
            windows.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.windowBorderColorActive(), ConfigDefaults::windowBorderColorActive());
        QCOMPARE(settings.windowBorderColorInactive(), QStringLiteral("#FF3DAEE9"));
    }

    /**
     * The "accent" sentinel is a valid border-colour value (the effect resolves it
     * to the live system colour), so validation must leave it untouched.
     */
    void testReadValidatedBorderColor_accentPreserved()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto windows = backend->group(ConfigDefaults::windowsAppearanceGroup());
            windows->writeString(ConfigDefaults::borderColorActiveKey(), QStringLiteral("accent"));
            windows.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.windowBorderColorActive(), QStringLiteral("accent"));
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

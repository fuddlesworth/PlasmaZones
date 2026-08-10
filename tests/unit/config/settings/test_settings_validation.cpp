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
 *  2. canonicalThemeFallbackColor -- an invalid colour string snaps to the
 *     empty theme-fallback sentinel (all four zone keys).
 *  3. Trigger list JSON parse -- invalid JSON drops back to the default,
 *     max-size cap at MaxTriggersPerAction is enforced.
 *  4. validIntOr enum validator -- unknown enum value snaps to the safe
 *     default rather than the nearest in-range neighbour.
 *  5. clampDouble validator -- window opacity / tint strength scalars.
 *  6. validStringOr closed-set validator -- the three scope token settings.
 *  7. canonicalThemeFallbackColor on the Windows border/tint keys --
 *     "#AARRGGBB" or the empty follow-the-system sentinel; the legacy
 *     "accent" token snaps to the sentinel.
 *  8. Decorations.Performance -- absent-group defaults and reset() round-trip.
 */

#include <QTest>
#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include "config/settings.h"
#include "config/configdefaults.h"
#include "config/configbackends.h"
#include "core/types/constants.h"
#include "core/types/enums.h"
#include "helpers/IsolatedConfigGuard.h"

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
        QVERIFY2(settings.decorationAnimateFocusedOnly(),
                 "AnimateFocusedOnly defaults to TRUE (flipped in PR #872), putting it in the exact absent-key "
                 "inversion risk class the PauseWhenIdle pin above exists for. The symbolic QCOMPARE below cannot "
                 "catch a regression that flips the ConfigDefaults value itself.");
        QCOMPARE(settings.decorationAnimateFocusedOnly(), ConfigDefaults::decorationAnimateFocusedOnly());
        QCOMPARE(settings.decorationIdleTimeoutSec(), ConfigDefaults::decorationIdleTimeoutSec());
    }

    /**
     * reset() must restore every Decorations.Performance key to its default.
     *
     * NOTE what this does and does not pin. It catches a reset() that no-ops, or a
     * post-reset load() that fails to re-read. It does NOT pin the group's entry in
     * Settings::managedGroupNames(): "Decorations.Performance" nests under
     * root["Decorations"]["Performance"], and managedGroupNames already lists the
     * parent "Decorations", whose delete removes the whole subtree. The explicit
     * sub-group entry is defence-in-depth (it keeps working if the group is ever
     * un-nested), not the thing this test guards. Mirrors the sibling
     * testDecorationWindowFiltering_defaultsAndReset.
     */
    void testDecorationPerformance_defaultsAndReset()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        QCOMPARE(settings.decorationPauseWhenIdle(), ConfigDefaults::decorationPauseWhenIdle());
        QCOMPARE(settings.decorationAnimateFocusedOnly(), ConfigDefaults::decorationAnimateFocusedOnly());
        QCOMPARE(settings.decorationIdleTimeoutSec(), ConfigDefaults::decorationIdleTimeoutSec());

        // Flip AnimateFocusedOnly FIRST while PauseWhenIdle STAYS at its
        // default (both are true since PR #872): asserting the two bools on
        // different values at this point is what catches a cross-wired
        // getter/setter pair — with both flipped together, a copy-paste key
        // swap in the storescalars macros passes every compare. PauseWhenIdle
        // is flipped in a SECOND step so its reset leg below is exercised
        // too. (120 is in-range and distinct from the default of 30.)
        settings.setDecorationAnimateFocusedOnly(false);
        settings.setDecorationIdleTimeoutSec(120);
        QCOMPARE(settings.decorationPauseWhenIdle(), true);
        QCOMPARE(settings.decorationAnimateFocusedOnly(), false);
        QCOMPARE(settings.decorationIdleTimeoutSec(), 120);
        settings.setDecorationPauseWhenIdle(false);
        QCOMPARE(settings.decorationPauseWhenIdle(), false);

        settings.reset();
        QCOMPARE(settings.decorationPauseWhenIdle(), ConfigDefaults::decorationPauseWhenIdle());
        QCOMPARE(settings.decorationAnimateFocusedOnly(), ConfigDefaults::decorationAnimateFocusedOnly());
        QCOMPARE(settings.decorationIdleTimeoutSec(), ConfigDefaults::decorationIdleTimeoutSec());
    }

    // =========================================================================
    // Schema clampDouble validator (window opacity / tint strength scalars)
    //
    // Both are [0.0, 1.0] scalars fed straight into the effect's alpha/tint
    // modulation. The min leg is the load-bearing one: windowOpacity defaults to
    // 1.0 (== max) and windowTintStrength defaults to 0.0 (== min), so a
    // clamp-to-bound and a fall-back-to-default are indistinguishable on one side.
    // Testing the OTHER side of each proves the value snaps to the violated bound
    // rather than the default.
    // =========================================================================

    void testReadValidatedWindowOpacity_belowMin_clampsToMin()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto windows = backend->group(ConfigDefaults::windowsAppearanceGroup());
            windows->writeDouble(ConfigDefaults::opacityKey(), -1.0);
            windows.reset();
            backend->sync();
        }

        Settings settings;
        // 0.0 (min) is distinct from the 1.0 default, so a fall-back-to-default
        // validator would fail this.
        QCOMPARE(settings.windowOpacity(), ConfigDefaults::windowOpacityMin());
    }

    void testReadValidatedWindowOpacity_aboveMax_clampsToMax()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto windows = backend->group(ConfigDefaults::windowsAppearanceGroup());
            windows->writeDouble(ConfigDefaults::opacityKey(), 5.0);
            windows.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.windowOpacity(), ConfigDefaults::windowOpacityMax());
    }

    void testReadValidatedWindowTintStrength_aboveMax_clampsToMax()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto windows = backend->group(ConfigDefaults::windowsAppearanceGroup());
            windows->writeDouble(ConfigDefaults::tintStrengthKey(), 5.0);
            windows.reset();
            backend->sync();
        }

        Settings settings;
        // 1.0 (max) is distinct from the 0.0 default.
        QCOMPARE(settings.windowTintStrength(), ConfigDefaults::windowTintStrengthMax());
    }

    void testReadValidatedWindowTintStrength_belowMin_clampsToMin()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto windows = backend->group(ConfigDefaults::windowsAppearanceGroup());
            windows->writeDouble(ConfigDefaults::tintStrengthKey(), -1.0);
            windows.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.windowTintStrength(), ConfigDefaults::windowTintStrengthMin());
    }

    /**
     * Sanity baseline: a valid mid-range value round-trips untouched, so the
     * clamp tests above aren't masking a validator that snaps everything to a
     * bound.
     */
    void testReadValidatedWindowOpacityTint_validValue_preserved()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto windows = backend->group(ConfigDefaults::windowsAppearanceGroup());
            windows->writeDouble(ConfigDefaults::opacityKey(), 0.5);
            windows->writeDouble(ConfigDefaults::tintStrengthKey(), 0.5);
            windows.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.windowOpacity(), 0.5);
        QCOMPARE(settings.windowTintStrength(), 0.5);
    }

    // =========================================================================
    // Schema canonicalThemeFallbackColor validator (invalid color string)
    // =========================================================================

    /**
     * The canonicalThemeFallbackColor validator must snap a stored string that
     * fails to parse as a valid QColor back to the empty sentinel, so the
     * colour falls back to following the system palette rather than painting
     * black. Seeds garbage at ALL FOUR zone colour keys: each key carries its
     * own validator wiring, and covering only one would let a validator
     * silently fall off the other three (deleting it would leave the suite
     * green).
     */
    void testReadValidatedColor_invalidColor_fallsBackToSentinel()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto colors = backend->group(ConfigDefaults::snappingZonesColorsGroup());
            colors->writeString(ConfigDefaults::highlightKey(), QStringLiteral("not-a-color"));
            colors->writeString(ConfigDefaults::inactiveKey(), QStringLiteral("also-not-a-color"));
            colors->writeString(ConfigDefaults::borderKey(), QStringLiteral("#zz1122"));
            colors.reset();
            auto labels = backend->group(ConfigDefaults::snappingZonesLabelsGroup());
            labels->writeString(ConfigDefaults::fontColorKey(), QStringLiteral("nope"));
            labels.reset();
            backend->sync();
        }

        Settings settings;
        // The garbage snaps to the sentinel: each stored value reads empty
        // and the resolved colour follows the palette (valid either way).
        QCOMPARE(settings.highlightColorRaw(), QString());
        QCOMPARE(settings.inactiveColorRaw(), QString());
        QCOMPARE(settings.borderColorRaw(), QString());
        QCOMPARE(settings.labelFontColorRaw(), QString());
        QVERIFY(settings.highlightColor().isValid());
        QVERIFY(settings.inactiveColor().isValid());
        QVERIFY(settings.borderColor().isValid());
        QVERIFY(settings.labelFontColor().isValid());
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
            windows->writeString(ConfigDefaults::opacityTintScopeKey(), QStringLiteral("garbage"));
            windows.reset();
            backend->sync();
        }

        Settings settings;
        // Every closed-set scope falls back to its schema default (=="tiled").
        QCOMPARE(settings.windowBorderScope(), ConfigDefaults::windowBorderScope());
        QCOMPARE(settings.windowTitleBarScope(), ConfigDefaults::windowTitleBarScope());
        QCOMPARE(settings.windowOpacityTintScope(), ConfigDefaults::windowOpacityTintScope());
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
            // opacityTintScope shares the identical closed-set validator; without a
            // valid-token leg here a validator that wrongly snapped a legitimate
            // "normal"/"all" for THIS key would pass, since its default is also "tiled"
            // and so indistinguishable from a mis-snap in the garbage test above.
            windows->writeString(ConfigDefaults::opacityTintScopeKey(), QStringLiteral("normal"));
            windows.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.windowBorderScope(), QStringLiteral("normal"));
        QCOMPARE(settings.windowTitleBarScope(), QStringLiteral("all"));
        QCOMPARE(settings.windowOpacityTintScope(), QStringLiteral("normal"));
    }

    /**
     * A hand-edited garbage border colour (not a valid QColor) snaps to the
     * schema default so garbage can't flow to the effect; a valid hex
     * round-trips untouched. NOTE: the schema default IS the empty sentinel
     * now, so this and the accent-token test below assert the same OUTCOME
     * for different INPUTS — the distinguishing power of each test lives in
     * its hex-preserved leg.
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
     * "accent" is no longer a stored value for the window colour keys — their
     * v6 sentinel is the empty string, resolved by the daemon before the
     * value crosses D-Bus (rules keep the token, the config does not). A
     * hand-edited leftover "accent" is neither the sentinel nor a QColor, so
     * canonicalThemeFallbackColor snaps it to the sentinel and the key falls
     * back to following the system accent rather than painting a black
     * border.
     */
    void testReadValidatedBorderColor_accentTokenSnapsToSentinel()
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
        QCOMPARE(settings.windowBorderColorActive(), QString());
    }

    /**
     * windowTintColor carries the identical theme-fallback contract as the
     * border colours ("#AARRGGBB" or the empty follow-the-system sentinel),
     * and a missing or mis-wired canonicalThemeFallbackColor on its key
     * would ship silently. Pin both legs: garbage snaps to the sentinel
     * default, a valid hex round-trips untouched.
     */
    void testReadValidatedTintColor_garbageSnaps_hexPreserved()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto windows = backend->group(ConfigDefaults::windowsAppearanceGroup());
            windows->writeString(ConfigDefaults::tintColorKey(), QStringLiteral("not-a-color"));
            windows.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.windowTintColor(), ConfigDefaults::windowTintColor());
    }

    void testReadValidatedTintColor_hexPreserved()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto windows = backend->group(ConfigDefaults::windowsAppearanceGroup());
            windows->writeString(ConfigDefaults::tintColorKey(), QStringLiteral("#FF3DAEE9"));
            windows.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.windowTintColor(), QStringLiteral("#FF3DAEE9"));
    }

    /**
     * The tint colour shares the border keys' v6 contract: "accent" is not a
     * stored value any more, so a hand-edited leftover snaps to the empty
     * sentinel (see the border sibling above).
     */
    void testReadValidatedTintColor_accentTokenSnapsToSentinel()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto windows = backend->group(ConfigDefaults::windowsAppearanceGroup());
            windows->writeString(ConfigDefaults::tintColorKey(), QStringLiteral("accent"));
            windows.reset();
            backend->sync();
        }

        Settings settings;
        QCOMPARE(settings.windowTintColor(), QString());
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

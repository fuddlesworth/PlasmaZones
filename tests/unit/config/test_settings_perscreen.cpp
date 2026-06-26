// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_perscreen.cpp
 * @brief Unit tests for Settings: per-screen config, defaults, edge cases
 *
 * Split from test_settings.cpp. Tests cover:
 * 1. Per-screen zone selector set/clear
 * 2. Per-screen zone selector no-op-write emit/husk suppression
 * 3. Per-screen autotile validation
 * 4. Per-screen autotile gaps/algorithm sub-domain independence
 * 5. Per-screen snapping clear drops the (gaps-only) entry
 * 6. Fresh config defaults
 */

#include <QJsonValue>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>
#include <QVariantMap>
#include <memory>

#include "../../../src/config/settings.h"
#include "../../../src/config/configdefaults.h"
#include "../../../src/core/constants.h"
#include "../../../src/core/settings_interfaces.h"
#include "../helpers/IsolatedConfigGuard.h"
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorWindowRules/MatchExpression.h>
#include <PhosphorWindowRules/RuleAction.h>
#include <PhosphorWindowRules/WindowRule.h>
#include <PhosphorWindowRules/WindowRuleStore.h>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

// Build a per-monitor gap WindowRule keyed exactly as the Appearance page keys
// it: the deterministic v5 id namespaced under the baseline gap rule
// from the monitor's connector name, with a `ScreenId Equals <connector>` match
// and the inner/outer gap actions. The match value is irrelevant to the
// rule-backed accessors (which look up BY ID), but mirrors the real shape.
PhosphorWindowRules::WindowRule makePerScreenGapRule(const QString& connector, int innerGap, int outerGap)
{
    using namespace PhosphorWindowRules;
    const auto action = [](QLatin1StringView type, const QJsonValue& value) {
        RuleAction a;
        a.type = QString(type);
        a.params.insert(QString(ActionParam::Value), value);
        return a;
    };

    WindowRule rule;
    rule.id = QUuid::createUuidV5(ConfigDefaults::baselineGapRuleId(), connector.toUtf8());
    rule.name = QStringLiteral("Gaps (%1)").arg(connector);
    rule.enabled = true;
    rule.priority = 100;
    rule.match = MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, connector);
    rule.actions = {
        action(ActionType::SetInnerGap, innerGap),        action(ActionType::SetOuterGap, outerGap),
        action(ActionType::SetUsePerSideOuterGap, false), action(ActionType::SetOuterGapTop, outerGap),
        action(ActionType::SetOuterGapBottom, outerGap),  action(ActionType::SetOuterGapLeft, outerGap),
        action(ActionType::SetOuterGapRight, outerGap),
    };
    return rule;
}

} // namespace

class TestSettingsPerScreen : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // =========================================================================
    // Per-screen zone selector
    // =========================================================================

    /**
     * Per-screen zone selector: set an override, verify it resolves, then clear it.
     */
    void testPerScreenZoneSelector_setAndClear()
    {
        IsolatedConfigGuard guard;

        Settings settings;

        const QString screen = QStringLiteral("test-screen-1");

        QVERIFY(!settings.hasPerScreenZoneSelectorSettings(screen));

        settings.setPerScreenZoneSelectorSetting(screen, QStringLiteral("Position"), 3);
        QVERIFY(settings.hasPerScreenZoneSelectorSettings(screen));

        QVariantMap overrides = settings.getPerScreenZoneSelectorSettings(screen);
        QCOMPARE(overrides.value(QStringLiteral("Position")).toInt(), 3);

        // Clear and verify
        settings.clearPerScreenZoneSelectorSettings(screen);
        QVERIFY(!settings.hasPerScreenZoneSelectorSettings(screen));
    }

    /**
     * Per-screen zone selector resolver: an override stored on the physical
     * monitor must still resolve when queried with one of its virtual
     * sub-screen ids ("<physical>/vs:N"). Mirrors the snapping geometry path's
     * getPerScreenSnappingWithFallback() so the selector honors per-monitor
     * overrides on virtual screens (regression for discussion #661).
     */
    void testPerScreenZoneSelector_virtualScreenFallsBackToPhysical()
    {
        IsolatedConfigGuard guard;

        Settings settings;

        const QString physical = QStringLiteral("test-screen-1");
        const QString virtualId = PhosphorIdentity::VirtualScreenId::make(physical, 0);

        // Baseline: with no override, the virtual screen resolves to the global
        // default position (and that default is not the value we will set).
        const int defaultPosition = settings.resolvedZoneSelectorConfig(virtualId).position;
        QVERIFY(defaultPosition != 3);

        // Store the override on the PHYSICAL id (as the settings UI does).
        settings.setPerScreenZoneSelectorSetting(physical, QStringLiteral("Position"), 3);

        // Querying the VIRTUAL sub-screen must fall back to the physical entry.
        QCOMPARE(settings.resolvedZoneSelectorConfig(virtualId).position, 3);

        // An unrelated screen still resolves to the default (no spurious match).
        const QString otherVirtual = PhosphorIdentity::VirtualScreenId::make(QStringLiteral("other-screen"), 0);
        QCOMPARE(settings.resolvedZoneSelectorConfig(otherVirtual).position, defaultPosition);
    }

    /**
     * Per-screen zone selector setter: writing the same value twice emits the
     * change signal exactly once, and a no-op write against a screen with no
     * existing entry never default-inserts an empty husk (which hasPerScreen*
     * would otherwise read as a phantom override).
     */
    void testPerScreenZoneSelector_noOpWriteSuppressesEmitAndHusk()
    {
        IsolatedConfigGuard guard;

        Settings settings;

        const QString screen = QStringLiteral("test-screen-1");

        QSignalSpy spy(&settings, &Settings::perScreenZoneSelectorSettingsChanged);

        // First write of a real value emits once and creates the entry.
        settings.setPerScreenZoneSelectorSetting(screen, QStringLiteral("Position"), 3);
        QCOMPARE(spy.count(), 1);
        QVERIFY(settings.hasPerScreenZoneSelectorSettings(screen));

        // Re-writing the identical value is a no-op: no second emit.
        settings.setPerScreenZoneSelectorSetting(screen, QStringLiteral("Position"), 3);
        QCOMPARE(spy.count(), 1);

        // A different value updates in place and emits again.
        settings.setPerScreenZoneSelectorSetting(screen, QStringLiteral("Position"), 4);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(settings.getPerScreenZoneSelectorSettings(screen).value(QStringLiteral("Position")).toInt(), 4);

        // A rejected write (out-of-range value) against a screen with no
        // existing entry must not emit and must not default-insert an empty
        // husk that hasPerScreen* would misread as a phantom override.
        const QString freshScreen = QStringLiteral("test-screen-2");
        settings.setPerScreenZoneSelectorSetting(freshScreen, QStringLiteral("Position"), 9999);
        QCOMPARE(spy.count(), 2);
        QVERIFY(!settings.hasPerScreenZoneSelectorSettings(freshScreen));
    }

    // =========================================================================
    // Per-screen autotile validation
    // =========================================================================

    /**
     * Per-screen autotile: validation must reject out-of-range values.
     */
    void testPerScreenAutotile_validationRejectsBadValues()
    {
        IsolatedConfigGuard guard;

        Settings settings;

        const QString screen = QStringLiteral("test-screen-1");

        QSignalSpy spy(&settings, &Settings::perScreenAutotileSettingsChanged);

        // Valid value (long-form key -- normalized to short form internally)
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileMasterCount"), 3);
        QCOMPARE(spy.count(), 1);

        // Value is clamped (not rejected outright): 100 should clamp to MaxMasterCount (5)
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileMasterCount"), 100);
        QVariantMap overrides = settings.getPerScreenAutotileSettings(screen);
        // Key is stored in short form ("MasterCount") after normalization
        int stored = overrides.value(QStringLiteral("MasterCount")).toInt();
        QVERIFY2(stored >= PhosphorTiles::AutotileDefaults::MinMasterCount
                     && stored <= PhosphorTiles::AutotileDefaults::MaxMasterCount,
                 "Per-screen autotile value must be clamped to valid range");

        // Non-numeric payloads are REJECTED, not coerced: QVariant::toInt()
        // silently converts garbage to 0, which the validators would then
        // clamp/store as a real override (e.g. Position "garbage" -> 0 =
        // TopLeft). The D-Bus dispatch path delivers raw QVariants, making
        // this a genuine input boundary.
        const int before = spy.count();
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileMasterCount"), QStringLiteral("garbage"));
        QCOMPARE(spy.count(), before);
        QCOMPARE(settings.getPerScreenAutotileSettings(screen).value(QStringLiteral("MasterCount")).toInt(), stored);

        // Numeric STRINGS still convert (JSON string storage compatibility):
        // QVariant("3").toInt(&ok) sets ok=true.
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileMasterCount"), QStringLiteral("3"));
        QCOMPARE(settings.getPerScreenAutotileSettings(screen).value(QStringLiteral("MasterCount")).toInt(), 3);

        // Same rejection contract on the zone-selector validator.
        QSignalSpy zsSpy(&settings, &Settings::perScreenZoneSelectorSettingsChanged);
        const QString freshScreen = QStringLiteral("test-screen-nonnumeric");
        settings.setPerScreenZoneSelectorSetting(freshScreen, QStringLiteral("Position"), QStringLiteral("garbage"));
        QCOMPARE(zsSpy.count(), 0);
        QVERIFY(!settings.hasPerScreenZoneSelectorSettings(freshScreen));
    }

    /**
     * HideTitleBars is NOT a per-screen autotile key: title-bar hiding is a
     * global mode setting consumed by the effect's DecorationManager, and the
     * per-screen variant was dead config surface (no UI, no consumer). A
     * write must be rejected like any unknown key and never round-trip.
     */
    void testPerScreenAutotile_hideTitleBarsIsNotAPerScreenKey()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        const QString screen = QStringLiteral("test-screen-1");

        QSignalSpy spy(&settings, &Settings::perScreenAutotileSettingsChanged);
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileHideTitleBars"), true);
        QCOMPARE(spy.count(), 0);
        // The validator strips the "Autotile" prefix itself, so both
        // spellings converge on the same rejection branch — the short-form
        // write pins the second wire spelling rather than a distinct code
        // path (and would catch a future short-form-only whitelist entry).
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("HideTitleBars"), true);
        QCOMPARE(spy.count(), 0);
        const QVariantMap overrides = settings.getPerScreenAutotileSettings(screen);
        QVERIFY(!overrides.contains(QStringLiteral("HideTitleBars")));
        QVERIFY(!overrides.contains(QStringLiteral("AutotileHideTitleBars")));
    }

    /**
     * The Algorithm / AnimationEasingCurve validators canonicalize the
     * accepted value to QString so the in-memory type matches what the
     * backend round-trips (writeString → readString). A non-string payload
     * (e.g. an int arriving over D-Bus) must therefore store as a QString
     * immediately, keeping the observable type stable across restart — a
     * regression to pass-through QVariant(value) would store it int-typed
     * in the writing session but string-typed after reload.
     */
    void testPerScreenAutotile_algorithmValueIsCanonicalizedToString()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        const QString screen = QStringLiteral("test-screen-1");

        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileAlgorithm"), 5);
        const QVariant stored = settings.getPerScreenAutotileSettings(screen).value(QStringLiteral("Algorithm"));
        QCOMPARE(stored.typeId(), static_cast<int>(QMetaType::QString));
        QCOMPARE(stored.toString(), QStringLiteral("5"));

        // Empty string is still rejected (no meaningful per-screen override).
        QSignalSpy spy(&settings, &Settings::perScreenAutotileSettingsChanged);
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileAlgorithm"), QString());
        QCOMPARE(spy.count(), 0);
    }

    /**
     * Tripwire: every PerScreenAutotileKey constant must stay accepted by the
     * validator (which whitelists against the SHORT-form PhosphorEngine::
     * PerScreenKeys namespace after stripping the "Autotile" prefix). Nothing
     * structurally ties the two lists — a key renamed or added on one side
     * silently fails validation and the override is dropped, so this test
     * round-trips every long-form key with a type-appropriate value.
     */
    void testPerScreenAutotile_everyDeclaredKeyRoundTrips()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        const QString screen = QStringLiteral("test-screen-1");

        struct KeyProbe
        {
            const char* key;
            QVariant value;
        };
        // The inner/outer gap keys are intentionally absent: per-screen gaps are
        // rule-backed now, so the per-screen autotile validator rejects them
        // (covered by testPerScreenAutotile_gapKeysAreRejected). SmartGaps stays.
        const QList<KeyProbe> probes{
            {PerScreenAutotileKey::Algorithm, QStringLiteral("bsp")},
            {PerScreenAutotileKey::SplitRatio, 0.5},
            {PerScreenAutotileKey::MasterCount, 2},
            {PerScreenAutotileKey::FocusNewWindows, true},
            {PerScreenAutotileKey::SmartGaps, true},
            {PerScreenAutotileKey::MaxWindows, 3},
            {PerScreenAutotileKey::InsertPosition, 1},
            {PerScreenAutotileKey::FocusFollowsMouse, true},
            {PerScreenAutotileKey::RespectMinimumSize, true},
            {PerScreenAutotileKey::SplitRatioStep, 0.05},
            {PerScreenAutotileKey::AnimationsEnabled, true},
            {PerScreenAutotileKey::AnimationDuration, 200},
            {PerScreenAutotileKey::AnimationEasingCurve, QStringLiteral("linear")},
        };

        // Derive the short form the same way the implementation does
        // (kAutotilePrefix in perscreen.cpp) — no magic length literal.
        const QLatin1String autotilePrefix("Autotile");
        const auto shortForm = [autotilePrefix](const QString& longKey) {
            return longKey.startsWith(autotilePrefix) ? longKey.mid(autotilePrefix.size()) : longKey;
        };

        for (const KeyProbe& probe : probes) {
            const QString longKey = QString::fromLatin1(probe.key);
            settings.setPerScreenAutotileSetting(screen, longKey, probe.value);
            const QVariantMap overrides = settings.getPerScreenAutotileSettings(screen);
            QVERIFY2(overrides.contains(shortForm(longKey)),
                     qPrintable(QStringLiteral("validator rejected declared per-screen key: ") + longKey));
        }

        // Disk round-trip: the LOAD path whitelists against a separate
        // hand-maintained list (kPerScreenAutotileKeys in perscreen.cpp) —
        // a key the validator accepts but that list omits would round-trip
        // in memory yet be silently dropped on the next launch. Force a
        // save, construct a second Settings over the same config, and
        // assert every key survived.
        settings.save();
        Settings reloaded;
        const QVariantMap persisted = reloaded.getPerScreenAutotileSettings(screen);
        for (const KeyProbe& probe : probes) {
            const QString longKey = QString::fromLatin1(probe.key);
            QVERIFY2(persisted.contains(shortForm(longKey)),
                     qPrintable(QStringLiteral("per-screen key lost across save/reload: ") + longKey));
        }
    }

    /**
     * Per-screen autotile gaps and algorithm sub-domains are independent: the
     * Gaps card and the Algorithm card share one per-screen map but must report
     * and clear only their own keys, so resetting one never wipes the other.
     */
    void testPerScreenAutotile_gapsAndAlgorithmSubdomainsAreIndependent()
    {
        IsolatedConfigGuard guard;

        Settings settings;

        const QString screen = QStringLiteral("test-screen-1");

        // A gaps-sub-domain override (SmartGaps — the only remaining per-screen
        // gaps-card key now that the inner/outer gaps are rule-backed) and an
        // algorithm override (MasterCount) coexist in the one shared per-screen
        // autotile map.
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileSmartGaps"), true);
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileMasterCount"), 2);

        QVERIFY(settings.hasPerScreenAutotileGapsSettings(screen));
        QVERIFY(settings.hasPerScreenAutotileAlgorithmSettings(screen));

        // Spy from here so the two setter emits above don't count.
        QSignalSpy spy(&settings, &Settings::perScreenAutotileSettingsChanged);

        // Clearing the gaps sub-domain emits once and leaves the algorithm
        // override intact.
        settings.clearPerScreenAutotileGapsSettings(screen);
        QCOMPARE(spy.count(), 1);
        QVERIFY(!settings.hasPerScreenAutotileGapsSettings(screen));
        QVERIFY(settings.hasPerScreenAutotileAlgorithmSettings(screen));
        QVariantMap afterGapsClear = settings.getPerScreenAutotileSettings(screen);
        QVERIFY2(!afterGapsClear.contains(QStringLiteral("SmartGaps")), "gaps key must be cleared");
        QCOMPARE(afterGapsClear.value(QStringLiteral("MasterCount")).toInt(), 2);

        // A no-op gaps clear (no gaps remain) changes nothing and does not emit.
        settings.clearPerScreenAutotileGapsSettings(screen);
        QCOMPARE(spy.count(), 1);
        QVERIFY(settings.hasPerScreenAutotileAlgorithmSettings(screen));

        // Clearing the algorithm sub-domain removes the last remaining key, so
        // the whole per-screen entry is dropped.
        settings.clearPerScreenAutotileAlgorithmSettings(screen);
        QCOMPARE(spy.count(), 2);
        QVERIFY(!settings.hasPerScreenAutotileAlgorithmSettings(screen));
        QVERIFY(!settings.hasPerScreenAutotileSettings(screen));
    }

    /**
     * Per-screen inner/outer gap keys are rule-backed, not Settings-stored: the
     * per-screen autotile validator must reject them like any unknown key so a
     * write never round-trips into the per-screen map (the rule owns the value).
     */
    void testPerScreenAutotile_gapKeysAreRejected()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        const QString screen = QStringLiteral("test-screen-1");

        QSignalSpy spy(&settings, &Settings::perScreenAutotileSettingsChanged);
        for (const char* key :
             {"AutotileInnerGap", "AutotileOuterGap", "AutotileUsePerSideOuterGap", "AutotileOuterGapTop",
              "AutotileOuterGapBottom", "AutotileOuterGapLeft", "AutotileOuterGapRight"}) {
            settings.setPerScreenAutotileSetting(screen, QString::fromLatin1(key), 5);
        }
        QCOMPARE(spy.count(), 0);
        // No gap rule exists either, so the accessor returns no gap keys.
        const QVariantMap overrides = settings.getPerScreenAutotileSettings(screen);
        QVERIFY(!overrides.contains(QStringLiteral("InnerGap")));
        QVERIFY(!overrides.contains(QStringLiteral("OuterGap")));
    }

    // =========================================================================
    // Rule-backed per-screen gaps (autotile + snapping)
    // =========================================================================

    /**
     * getPerScreenAutotileSettings / getPerScreenSnappingSettings must surface a
     * per-monitor gap RULE's values for that screen (keyed the way the Appearance
     * page keys it: createUuidV5(baseline, connector)), and return no gap keys
     * when no such rule exists (consumer falls back to global). Non-gap autotile
     * keys still come from Settings storage and coexist with the rule gaps.
     */
    void testPerScreenGaps_ruleBacked()
    {
        IsolatedConfigGuard guard;
        namespace PSK = PhosphorEngine::PerScreenKeys;

        const QString screen = QStringLiteral("DP-rule-1");
        const QString other = QStringLiteral("DP-rule-2");

        // Author the per-monitor gap rule directly in a borrowed store, then
        // build Settings over it (mirrors the daemon's shared-store wiring).
        auto store = std::make_unique<PhosphorWindowRules::WindowRuleStore>(ConfigDefaults::windowRulesFilePath());
        QVERIFY(store->addRule(makePerScreenGapRule(screen, /*inner=*/13, /*outer=*/21)));

        Settings settings(store.get(), nullptr);

        // A non-gap per-screen autotile override coexists with the rule gaps.
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileMasterCount"), 2);

        const QVariantMap autotile = settings.getPerScreenAutotileSettings(screen);
        QCOMPARE(autotile.value(QString(PSK::InnerGap)).toInt(), 13);
        QCOMPARE(autotile.value(QString(PSK::OuterGap)).toInt(), 21);
        QCOMPARE(autotile.value(QString(PSK::OuterGapTop)).toInt(), 21);
        QCOMPARE(autotile.value(QString(PSK::UsePerSideOuterGap)).toBool(), false);
        QCOMPARE(autotile.value(QStringLiteral("MasterCount")).toInt(), 2);

        const QVariantMap snapping = settings.getPerScreenSnappingSettings(screen);
        QCOMPARE(snapping.value(QString(PSK::InnerGap)).toInt(), 13);
        QCOMPARE(snapping.value(QString(PSK::OuterGap)).toInt(), 21);

        // A screen with no gap rule gets no gap keys (falls through to global).
        const QVariantMap noRuleAutotile = settings.getPerScreenAutotileSettings(other);
        QVERIFY(!noRuleAutotile.contains(QString(PSK::InnerGap)));
        QVERIFY(settings.getPerScreenSnappingSettings(other).isEmpty());
    }

    /**
     * A per-monitor gap rule keyed by the physical monitor must also apply when
     * queried with one of its virtual sub-screen ids ("<physical>/vs:N"), via
     * the connector/physical resolution in perScreenGapRuleOverrides.
     */
    void testPerScreenGaps_ruleBackedVirtualFallback()
    {
        IsolatedConfigGuard guard;
        namespace PSK = PhosphorEngine::PerScreenKeys;

        const QString physical = QStringLiteral("DP-virtfb");
        const QString virtualId = PhosphorIdentity::VirtualScreenId::make(physical, 0);

        auto store = std::make_unique<PhosphorWindowRules::WindowRuleStore>(ConfigDefaults::windowRulesFilePath());
        QVERIFY(store->addRule(makePerScreenGapRule(physical, /*inner=*/7, /*outer=*/9)));

        Settings settings(store.get(), nullptr);

        const QVariantMap snapping = settings.getPerScreenSnappingSettings(virtualId);
        QCOMPARE(snapping.value(QString(PSK::InnerGap)).toInt(), 7);
        QCOMPARE(snapping.value(QString(PSK::OuterGap)).toInt(), 9);
    }

    // =========================================================================
    // P2: edge cases -- fresh config defaults
    // =========================================================================

    /**
     * A fresh config file (no entries) must produce all ConfigDefaults values.
     */
    void testLoad_freshConfig_allDefaults()
    {
        IsolatedConfigGuard guard;

        Settings settings;

        // Activation defaults
        QCOMPARE(settings.zoneSpanEnabled(), ConfigDefaults::zoneSpanEnabled());
        QCOMPARE(settings.toggleActivation(), ConfigDefaults::toggleActivation());
        QCOMPARE(settings.snappingEnabled(), ConfigDefaults::snappingEnabled());

        // Display defaults
        QCOMPARE(settings.showZonesOnAllMonitors(), ConfigDefaults::showOnAllMonitors());
        QCOMPARE(settings.showZoneNumbers(), ConfigDefaults::showNumbers());
        QCOMPARE(settings.flashZonesOnSwitch(), ConfigDefaults::flashOnSwitch());

        // Appearance defaults
        QCOMPARE(settings.borderWidth(), ConfigDefaults::borderWidth());
        QCOMPARE(settings.borderRadius(), ConfigDefaults::borderRadius());
        QCOMPARE(settings.enableBlur(), ConfigDefaults::enableBlur());
        QCOMPARE(settings.labelFontWeight(), ConfigDefaults::labelFontWeight());
        QVERIFY(qFuzzyCompare(settings.labelFontSizeScale(), ConfigDefaults::labelFontSizeScale()));

        // PhosphorZones::Zone geometry defaults
        QCOMPARE(settings.innerGap(), ConfigDefaults::innerGap());
        QCOMPARE(settings.outerGap(), ConfigDefaults::outerGap());
        QCOMPARE(settings.adjacentThreshold(), ConfigDefaults::adjacentThreshold());
        QCOMPARE(settings.pollIntervalMs(), ConfigDefaults::pollIntervalMs());

        // Behavior defaults
        QCOMPARE(settings.keepWindowsInZonesOnResolutionChange(),
                 ConfigDefaults::keepWindowsInZonesOnResolutionChange());
        QCOMPARE(settings.restoreOriginalSizeOnUnsnap(), ConfigDefaults::restoreOriginalSizeOnUnsnap());
        QCOMPARE(settings.excludeTransientWindows(), ConfigDefaults::excludeTransientWindows());

        // PhosphorZones::Zone selector defaults
        QCOMPARE(settings.zoneSelectorEnabled(), ConfigDefaults::zoneSelectorEnabled());
        QCOMPARE(settings.zoneSelectorTriggerDistance(), ConfigDefaults::triggerDistance());
        QCOMPARE(settings.zoneSelectorGridColumns(), ConfigDefaults::gridColumns());
        QCOMPARE(settings.zoneSelectorMaxRows(), ConfigDefaults::maxRows());

        // Autotile defaults
        QCOMPARE(settings.autotileEnabled(), ConfigDefaults::autotileEnabled());
        QVERIFY(qFuzzyCompare(settings.autotileSplitRatio(), ConfigDefaults::autotileSplitRatio()));
        QCOMPARE(settings.autotileMasterCount(), ConfigDefaults::autotileMasterCount());

        // Animation defaults
        QCOMPARE(settings.animationsEnabled(), ConfigDefaults::animationsEnabled());
        QCOMPARE(settings.animationDuration(), ConfigDefaults::animationDuration());

        // Shader defaults
        QCOMPARE(settings.enableShaderEffects(), ConfigDefaults::enableShaderEffects());
        QCOMPARE(settings.shaderFrameRate(), ConfigDefaults::shaderFrameRate());
    }

    // The earlier "empty excluded lists survive round-trip" test was
    // retired alongside excludedApplications / excludedWindowClasses
    // themselves: the v4 fold removed those QStringList settings from
    // Settings entirely. The Window Rules round-trip is covered by
    // test_windowrule_store; the empty-rule-set case is exercised there.
};

// NOT guiless: Settings::load → applySystemColorScheme reads
// QGuiApplication::palette(), which requires a QGuiApplication instance
// (crashes under QCoreApplication).
QTEST_MAIN(TestSettingsPerScreen)
#include "test_settings_perscreen.moc"

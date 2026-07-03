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
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleStore.h>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

// Build a context gap Rule scoped to one screen: a `ScreenId Equals <connector>`
// match carrying the inner/outer gap actions. Per-monitor gaps are config-backed
// now, so this stands in for any gap-action-bearing rule the gap-resync
// fingerprint must react to (testGapResyncOnlyOnGapRuleChange).
PhosphorRules::Rule makePerScreenGapRule(const QString& connector, int innerGap, int outerGap)
{
    using namespace PhosphorRules;
    const auto action = [](QLatin1StringView type, const QJsonValue& value) {
        RuleAction a;
        a.type = QString(type);
        a.params.insert(QString(ActionParam::Value), value);
        return a;
    };

    Rule rule;
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
        // Every declared per-screen autotile key, INCLUDING the inner/outer gap
        // dimensions (config-backed and unified snap+tile now — see
        // testPerScreenAutotile_gapKeysAreAccepted for the validation contract).
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
            {PerScreenAutotileKey::InnerGap, 10},
            {PerScreenAutotileKey::OuterGap, 12},
            {PerScreenAutotileKey::UsePerSideOuterGap, true},
            {PerScreenAutotileKey::OuterGapTop, 6},
            {PerScreenAutotileKey::OuterGapBottom, 7},
            {PerScreenAutotileKey::OuterGapLeft, 8},
            {PerScreenAutotileKey::OuterGapRight, 9},
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
     * The per-screen autotile map hosts three disjoint sub-domains — SmartGaps
     * (Tiling smart-gaps card), the inner/outer gap DIMENSIONS (Windows gaps
     * card, config-backed), and everything else (Algorithm card). Each card
     * reports and clears only its own keys, so resetting one never wipes another.
     */
    void testPerScreenAutotile_subdomainsAreIndependent()
    {
        IsolatedConfigGuard guard;

        Settings settings;

        const QString screen = QStringLiteral("test-screen-1");

        // One override from each sub-domain coexists in the shared per-screen map.
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileSmartGaps"), true);
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileInnerGap"), 15);
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileMasterCount"), 2);

        QVERIFY(settings.hasPerScreenAutotileGapsSettings(screen));
        QVERIFY(settings.hasPerScreenGapOverride(screen));
        QVERIFY(settings.hasPerScreenAutotileAlgorithmSettings(screen));

        // Spy from here so the three setter emits above don't count.
        QSignalSpy spy(&settings, &Settings::perScreenAutotileSettingsChanged);

        // Clearing the gap DIMENSIONS leaves SmartGaps and the algorithm intact.
        settings.clearPerScreenGapOverride(screen);
        QCOMPARE(spy.count(), 1);
        QVERIFY(!settings.hasPerScreenGapOverride(screen));
        QVERIFY(settings.hasPerScreenAutotileGapsSettings(screen));
        QVERIFY(settings.hasPerScreenAutotileAlgorithmSettings(screen));
        QVariantMap afterGapDimClear = settings.getPerScreenAutotileSettings(screen);
        QVERIFY2(!afterGapDimClear.contains(QStringLiteral("InnerGap")), "gap dimension must be cleared");
        QCOMPARE(afterGapDimClear.value(QStringLiteral("SmartGaps")).toBool(), true);
        QCOMPARE(afterGapDimClear.value(QStringLiteral("MasterCount")).toInt(), 2);

        // Clearing the SmartGaps sub-domain leaves the algorithm override intact.
        settings.clearPerScreenAutotileGapsSettings(screen);
        QCOMPARE(spy.count(), 2);
        QVERIFY(!settings.hasPerScreenAutotileGapsSettings(screen));
        QVERIFY(settings.hasPerScreenAutotileAlgorithmSettings(screen));
        QVERIFY2(!settings.getPerScreenAutotileSettings(screen).contains(QStringLiteral("SmartGaps")),
                 "SmartGaps must be cleared");

        // A no-op gap-dimension clear (none remain) changes nothing and does not emit.
        settings.clearPerScreenGapOverride(screen);
        QCOMPARE(spy.count(), 2);

        // Clearing the algorithm sub-domain removes the last key, dropping the entry.
        settings.clearPerScreenAutotileAlgorithmSettings(screen);
        QCOMPARE(spy.count(), 3);
        QVERIFY(!settings.hasPerScreenAutotileAlgorithmSettings(screen));
        QVERIFY(!settings.hasPerScreenAutotileSettings(screen));
    }

    /**
     * Per-screen inner/outer gap keys are config-backed now (unified — one value
     * per monitor drives both snap and tile): the validator ACCEPTS and clamps
     * them, they round-trip into the per-screen map, and the gap accessors surface
     * them. Out-of-range writes clamp; non-numeric writes are rejected.
     */
    void testPerScreenAutotile_gapKeysAreAccepted()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        const QString screen = QStringLiteral("test-screen-1");

        QSignalSpy spy(&settings, &Settings::perScreenAutotileSettingsChanged);
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileInnerGap"), 11);
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileOuterGap"), 13);
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileUsePerSideOuterGap"), true);
        QCOMPARE(spy.count(), 3);

        const QVariantMap overrides = settings.getPerScreenAutotileSettings(screen);
        QCOMPARE(overrides.value(QStringLiteral("InnerGap")).toInt(), 11);
        QCOMPARE(overrides.value(QStringLiteral("OuterGap")).toInt(), 13);
        QCOMPARE(overrides.value(QStringLiteral("UsePerSideOuterGap")).toBool(), true);

        // Out-of-range inner gap clamps to the shared gap range (not rejected).
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileInnerGap"), 100000);
        const int stored = settings.getPerScreenAutotileSettings(screen).value(QStringLiteral("InnerGap")).toInt();
        QVERIFY2(stored >= ConfigDefaults::innerGapMin() && stored <= ConfigDefaults::innerGapMax(),
                 "per-screen inner gap must clamp to the shared range");

        // Non-numeric payloads are rejected (no emit, value unchanged).
        const int before = spy.count();
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileOuterGap"), QStringLiteral("garbage"));
        QCOMPARE(spy.count(), before);
        QCOMPARE(settings.getPerScreenAutotileSettings(screen).value(QStringLiteral("OuterGap")).toInt(), 13);
    }

    // =========================================================================
    // Config-backed per-screen gaps (unified snap + tile)
    // =========================================================================

    /**
     * perScreenGapOverrides / getPerScreenSnappingSettings / getPerScreenAutotile-
     * Settings surface the config-backed per-monitor gap dimensions for a screen
     * that has them, and no gap keys for a screen that doesn't (consumer falls
     * back to global). Non-gap autotile keys coexist with the gap keys.
     */
    void testPerScreenGaps_configBacked()
    {
        IsolatedConfigGuard guard;
        namespace PSK = PhosphorEngine::PerScreenKeys;

        Settings settings;
        const QString screen = QStringLiteral("DP-cfg-1");
        const QString other = QStringLiteral("DP-cfg-2");

        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileInnerGap"), 13);
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileOuterGap"), 21);
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileMasterCount"), 2);

        const QVariantMap gapOverrides = settings.perScreenGapOverrides(screen);
        QCOMPARE(gapOverrides.value(QString(PSK::InnerGap)).toInt(), 13);
        QCOMPARE(gapOverrides.value(QString(PSK::OuterGap)).toInt(), 21);
        // perScreenGapOverrides is the gap subset ONLY — no algorithm keys.
        QVERIFY(!gapOverrides.contains(QStringLiteral("MasterCount")));

        const QVariantMap snapping = settings.getPerScreenSnappingSettings(screen);
        QCOMPARE(snapping.value(QString(PSK::InnerGap)).toInt(), 13);
        QCOMPARE(snapping.value(QString(PSK::OuterGap)).toInt(), 21);

        // The full autotile map carries both the gaps and the non-gap override.
        const QVariantMap autotile = settings.getPerScreenAutotileSettings(screen);
        QCOMPARE(autotile.value(QString(PSK::InnerGap)).toInt(), 13);
        QCOMPARE(autotile.value(QStringLiteral("MasterCount")).toInt(), 2);

        // A screen with no gap override gets no gap keys (falls through to global).
        QVERIFY(settings.perScreenGapOverrides(other).isEmpty());
        QVERIFY(settings.getPerScreenSnappingSettings(other).isEmpty());
    }

    /**
     * A config-backed per-monitor gap stored on the physical monitor must also
     * apply when queried with one of its virtual sub-screen ids ("<physical>/vs:N"),
     * via the virtual→physical fallback in perScreenGapOverrides.
     */
    void testPerScreenGaps_configBackedVirtualFallback()
    {
        IsolatedConfigGuard guard;
        namespace PSK = PhosphorEngine::PerScreenKeys;

        Settings settings;
        const QString physical = QStringLiteral("DP-virtfb");
        const QString virtualId = PhosphorIdentity::VirtualScreenId::make(physical, 0);

        settings.setPerScreenAutotileSetting(physical, QStringLiteral("AutotileInnerGap"), 7);
        settings.setPerScreenAutotileSetting(physical, QStringLiteral("AutotileOuterGap"), 9);

        const QVariantMap snapping = settings.getPerScreenSnappingSettings(virtualId);
        QCOMPARE(snapping.value(QString(PSK::InnerGap)).toInt(), 7);
        QCOMPARE(snapping.value(QString(PSK::OuterGap)).toInt(), 9);
    }

    /**
     * Regression: the rule-store gap re-sync (perScreenAutotile/Snapping +
     * settingsChanged) must fire ONLY when a gap action actually changes — never
     * on a non-gap rule write. Emitting it on every rulesChanged made a mode /
     * assignment toggle (also a rule write) fire settingsChanged, which drove the
     * daemon to re-resolve the default assignment and instantly revert the
     * toggle, breaking the snapping<->autotile switch.
     */
    void testGapResyncOnlyOnGapRuleChange()
    {
        using namespace PhosphorRules;
        IsolatedConfigGuard guard;

        auto store = std::make_unique<RuleStore>(ConfigDefaults::rulesFilePath());
        Settings settings(store.get(), nullptr);

        QSignalSpy settingsSpy(&settings, &Settings::settingsChanged);
        QSignalSpy autotileSpy(&settings, &Settings::perScreenAutotileSettingsChanged);
        QSignalSpy snappingSpy(&settings, &Settings::perScreenSnappingSettingsChanged);

        // A non-gap rule write (a border rule, standing in for a mode/assignment
        // toggle) must NOT trigger the gap re-sync.
        Rule borderRule;
        borderRule.id = QUuid::createUuidV5(ConfigDefaults::baselineBorderRuleId(), QByteArrayLiteral("nongap"));
        borderRule.name = QStringLiteral("Border");
        borderRule.enabled = true;
        borderRule.priority = 50;
        RuleAction widthAction;
        widthAction.type = QString(ActionType::SetBorderWidth);
        widthAction.params.insert(QString(ActionParam::Value), 4);
        borderRule.actions = {widthAction};
        QVERIFY(store->addRule(borderRule));
        QCOMPARE(settingsSpy.count(), 0);
        QCOMPARE(autotileSpy.count(), 0);
        QCOMPARE(snappingSpy.count(), 0);

        // A gap rule write DOES trigger the re-sync.
        QVERIFY(store->addRule(makePerScreenGapRule(QStringLiteral("DP-resync"), /*inner=*/11, /*outer=*/12)));
        QVERIFY(settingsSpy.count() >= 1);
        QVERIFY(autotileSpy.count() >= 1);
        QVERIFY(snappingSpy.count() >= 1);
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
    // Settings entirely. The Rules round-trip is covered by
    // test_rule_store; the empty-rule-set case is exercised there.
};

// NOT guiless: Settings::load → applySystemColorScheme reads
// QGuiApplication::palette(), which requires a QGuiApplication instance
// (crashes under QCoreApplication).
QTEST_MAIN(TestSettingsPerScreen)
#include "test_settings_perscreen.moc"

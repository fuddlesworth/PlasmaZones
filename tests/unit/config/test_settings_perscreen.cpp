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
 * 5. Per-screen snapping gaps sub-domain isolation
 * 6. Fresh config defaults
 */

#include <QTest>
#include <QSignalSpy>
#include <QVariantMap>

#include "../../../src/config/settings.h"
#include "../../../src/config/configdefaults.h"
#include "../../../src/core/constants.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

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
        // The setter normalizes long→short form, so the short-form spelling is
        // a distinct input path — it must be rejected identically.
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("HideTitleBars"), true);
        QCOMPARE(spy.count(), 0);
        const QVariantMap overrides = settings.getPerScreenAutotileSettings(screen);
        QVERIFY(!overrides.contains(QStringLiteral("HideTitleBars")));
        QVERIFY(!overrides.contains(QStringLiteral("AutotileHideTitleBars")));
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

        // A gaps override (InnerGap) and an algorithm override (MasterCount)
        // coexist in the one shared per-screen autotile map.
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileInnerGap"), 12);
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
        QVERIFY2(!afterGapsClear.contains(QStringLiteral("InnerGap")), "gaps key must be cleared");
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
     * The per-screen snapping Gaps card shares one map with the snapping
     * SnapAssist/ZoneSelector keys but must report and clear only its gap keys,
     * so resetting the Gaps card never wipes the map's other overrides.
     */
    void testPerScreenSnapping_gapsSubdomainIsolatesNonGapsKeys()
    {
        IsolatedConfigGuard guard;

        Settings settings;

        const QString screen = QStringLiteral("test-screen-1");

        // A gaps override (ZonePadding) and a non-gaps override
        // (SnapAssistEnabled) coexist in the one shared per-screen snapping map.
        settings.setPerScreenSnappingSetting(screen, QStringLiteral("ZonePadding"), 8);
        settings.setPerScreenSnappingSetting(screen, QStringLiteral("SnapAssistEnabled"), false);

        QVERIFY(settings.hasPerScreenSnappingGapsSettings(screen));
        QVERIFY(settings.hasPerScreenSnappingSettings(screen));

        QSignalSpy spy(&settings, &Settings::perScreenSnappingSettingsChanged);

        // Clearing the gaps sub-domain emits once and leaves the SnapAssist
        // override intact (the entry survives because a non-gaps key remains).
        settings.clearPerScreenSnappingGapsSettings(screen);
        QCOMPARE(spy.count(), 1);
        QVERIFY(!settings.hasPerScreenSnappingGapsSettings(screen));
        QVERIFY(settings.hasPerScreenSnappingSettings(screen));
        QVariantMap afterGapsClear = settings.getPerScreenSnappingSettings(screen);
        QVERIFY2(!afterGapsClear.contains(QStringLiteral("ZonePadding")), "gaps key must be cleared");
        QCOMPARE(afterGapsClear.value(QStringLiteral("SnapAssistEnabled")).toBool(), false);

        // A no-op gaps clear (no gaps remain) changes nothing and does not emit.
        settings.clearPerScreenSnappingGapsSettings(screen);
        QCOMPARE(spy.count(), 1);
        QVERIFY(settings.hasPerScreenSnappingSettings(screen));
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
        QCOMPARE(settings.zonePadding(), ConfigDefaults::zonePadding());
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

QTEST_MAIN(TestSettingsPerScreen)
#include "test_settings_perscreen.moc"

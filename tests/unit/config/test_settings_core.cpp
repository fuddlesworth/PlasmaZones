// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_core.cpp
 * @brief Unit tests for Settings: reset, save/load roundtrip, signal emission
 *
 * Split from test_settings.cpp. Tests cover:
 * 1. reset() correctness (P0 -- data-loss prevention)
 * 2. save/load round-trip fidelity (P0)
 * 3. Signal emission on load and setters (P1)
 * 4. LabelFontWeight default (regression guard)
 * 5. Legacy activation migration
 */

#include <QTest>
#include <QSignalSpy>
#include "config/configbackend_qsettings.h"

#include "../../../src/config/settings.h"
#include "../../../src/config/configdefaults.h"
#include "../../../src/core/constants.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestSettingsCore : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // =========================================================================
    // P0: crash/data-loss prevention
    // =========================================================================

    /**
     * reset() must delete the "Updates" config group so that the dismissed
     * update version is cleared. Regression: if Updates group survives reset,
     * the user can never see update notifications again.
     */
    void testReset_deletesUpdatesGroup()
    {
        IsolatedConfigGuard guard;

        // Write a value into the Updates group
        {
            auto backend = PlasmaZones::QSettingsConfigBackend::createDefault();
            auto updates = backend->group(QStringLiteral("Updates"));
            updates->writeString(QStringLiteral("DismissedUpdateVersion"), QStringLiteral("99.0.0"));
            updates.reset();
            backend->sync();
        }

        // Create Settings and reset
        Settings settings;
        settings.reset();

        // Verify the Updates group is gone
        auto backend = PlasmaZones::QSettingsConfigBackend::createDefault();
        QVERIFY2(!backend->groupList().contains(QStringLiteral("Updates")), "reset() must delete the Updates group");
    }

    /**
     * reset() must delete per-screen override groups (ZoneSelector:*, AutotileScreen:*,
     * SnappingScreen:*) to prevent stale per-screen configuration from surviving a reset.
     */
    void testReset_deletesPerScreenGroups()
    {
        IsolatedConfigGuard guard;

        // Write per-screen overrides
        {
            auto backend = PlasmaZones::QSettingsConfigBackend::createDefault();
            {
                auto g = backend->group(QStringLiteral("ZoneSelector:eDP-1"));
                g->writeInt(QStringLiteral("Position"), 3);
            }
            {
                auto g = backend->group(QStringLiteral("AutotileScreen:HDMI-A-1"));
                g->writeInt(QStringLiteral("AutotileMasterCount"), 2);
            }
            {
                auto g = backend->group(QStringLiteral("SnappingScreen:DP-1"));
                g->writeBool(QStringLiteral("SnapAssistEnabled"), true);
            }
            backend->sync();
        }

        Settings settings;
        settings.reset();

        auto backend = PlasmaZones::QSettingsConfigBackend::createDefault();
        const QStringList groups = backend->groupList();
        for (const QString& g : groups) {
            QVERIFY2(!g.startsWith(QLatin1String("ZoneSelector:")),
                     qPrintable(QStringLiteral("Per-screen ZoneSelector group survived reset: %1").arg(g)));
            QVERIFY2(!g.startsWith(QLatin1String("AutotileScreen:")),
                     qPrintable(QStringLiteral("Per-screen AutotileScreen group survived reset: %1").arg(g)));
            QVERIFY2(!g.startsWith(QLatin1String("SnappingScreen:")),
                     qPrintable(QStringLiteral("Per-screen SnappingScreen group survived reset: %1").arg(g)));
        }
    }

    /**
     * After save() then load(), every property must equal what was set.
     * This validates the full round-trip for a representative subset of settings
     * across all config groups.
     *
     * QSettingsConfigBackend::createDefault() reads from
     * $XDG_CONFIG_HOME/plasmazonesrc. The IsolatedConfigGuard redirects
     * XDG_CONFIG_HOME to a temp directory, so each test gets a fresh file.
     */
    void testSave_load_roundtrip_allGroups()
    {
        IsolatedConfigGuard guard;

        Settings settings;

        // Set non-default values across different groups
        settings.setZonePadding(15);
        settings.setOuterGap(20);
        settings.setBorderWidth(5);
        settings.setBorderRadius(25);
        settings.setActiveOpacity(0.8);
        settings.setInactiveOpacity(0.2);
        settings.setShowZoneNumbers(false);
        settings.setToggleActivation(true);
        settings.setZoneSelectorEnabled(false);
        settings.setZoneSelectorTriggerDistance(100);
        settings.setZoneSelectorGridColumns(3);
        settings.setAutotileSplitRatio(0.7);
        settings.setAutotileMasterCount(3);
        settings.setAutotileInnerGap(12);
        settings.setAnimationDuration(300);
        settings.setAnimationSequenceMode(0);
        settings.setLabelFontWeight(400);

        settings.save();

        // Verify the round-trip by reading from a fresh config backend
        // (re-reads from disk after save() flushed).
        auto backend = PlasmaZones::QSettingsConfigBackend::createDefault();

        {
            auto zones = backend->group(QStringLiteral("Zones"));
            QCOMPARE(zones->readInt(QStringLiteral("Padding"), 0), 15);
            QCOMPARE(zones->readInt(QStringLiteral("OuterGap"), 0), 20);
        }

        {
            auto appearance = backend->group(QStringLiteral("Appearance"));
            QCOMPARE(appearance->readInt(QStringLiteral("BorderWidth"), 0), 5);
            QCOMPARE(appearance->readInt(QStringLiteral("BorderRadius"), 0), 25);
            QVERIFY(qFuzzyCompare(appearance->readDouble(QStringLiteral("ActiveOpacity"), 0.0), 0.8));
            QVERIFY(qFuzzyCompare(appearance->readDouble(QStringLiteral("InactiveOpacity"), 0.0), 0.2));
            QCOMPARE(appearance->readInt(QStringLiteral("LabelFontWeight"), 0), 400);
        }

        {
            auto display = backend->group(QStringLiteral("Display"));
            QCOMPARE(display->readBool(QStringLiteral("ShowNumbers"), true), false);
        }

        {
            auto activation = backend->group(QStringLiteral("Activation"));
            QCOMPARE(activation->readBool(QStringLiteral("ToggleActivation"), false), true);
        }

        {
            auto zoneSelector = backend->group(QStringLiteral("ZoneSelector"));
            QCOMPARE(zoneSelector->readBool(QStringLiteral("Enabled"), true), false);
            QCOMPARE(zoneSelector->readInt(QStringLiteral("TriggerDistance"), 0), 100);
            QCOMPARE(zoneSelector->readInt(QStringLiteral("GridColumns"), 0), 3);
        }

        {
            auto autotiling = backend->group(QStringLiteral("Autotiling"));
            double readRatio = autotiling->readDouble(QStringLiteral("AutotileSplitRatio"), 0.0);
            QVERIFY2(qAbs(readRatio - 0.7) < 0.001,
                     qPrintable(QStringLiteral("AutotileSplitRatio: expected 0.7, got %1").arg(readRatio)));
            QCOMPARE(autotiling->readInt(QStringLiteral("AutotileMasterCount"), 0), 3);
            QCOMPARE(autotiling->readInt(QStringLiteral("AutotileInnerGap"), 0), 12);
        }

        {
            auto animations = backend->group(QStringLiteral("Animations"));
            QCOMPARE(animations->readInt(QStringLiteral("AnimationDuration"), 0), 300);
            QCOMPARE(animations->readInt(QStringLiteral("AnimationSequenceMode"), -1), 0);
        }
    }

    // =========================================================================
    // P1: functional correctness -- signal emission
    // =========================================================================

    /**
     * load() must emit settingsChanged() so that listeners can re-read all values.
     */
    void testLoad_emitsSettingsChanged()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        QSignalSpy spy(&settings, &Settings::settingsChanged);
        QVERIFY(spy.isValid());

        settings.load();

        QVERIFY2(spy.count() >= 1, "load() must emit settingsChanged() at least once");
    }

    /**
     * Setters must emit both the specific signal and settingsChanged() when the
     * value actually changes.
     */
    void testSetter_emitsSettingsChanged_onValueChange()
    {
        IsolatedConfigGuard guard;

        Settings settings;

        QSignalSpy generalSpy(&settings, &Settings::settingsChanged);
        QSignalSpy specificSpy(&settings, &Settings::zonePaddingChanged);
        QVERIFY(generalSpy.isValid());
        QVERIFY(specificSpy.isValid());

        int currentPadding = settings.zonePadding();
        int newPadding = (currentPadding == 15) ? 20 : 15;

        settings.setZonePadding(newPadding);

        QCOMPARE(specificSpy.count(), 1);
        QVERIFY(generalSpy.count() >= 1);
    }

    /**
     * Setters must NOT emit signals when the value is unchanged (guard check).
     */
    void testSetter_noSignal_onSameValue()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        settings.setZonePadding(15); // force a known value

        QSignalSpy generalSpy(&settings, &Settings::settingsChanged);
        QSignalSpy specificSpy(&settings, &Settings::zonePaddingChanged);

        settings.setZonePadding(15); // same value again

        QCOMPARE(specificSpy.count(), 0);
        QCOMPARE(generalSpy.count(), 0);
    }

    /**
     * LabelFontWeight default must be 700 (CSS Bold), not 75 (Qt5 QFont::Bold).
     * Regression guard for the CSS vs QFont weight confusion.
     */
    void testLabelFontWeight_default_is700_cssBold()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        QCOMPARE(settings.labelFontWeight(), 700);
    }

    /**
     * When config has no DragActivationTriggers JSON, load should use the
     * default trigger list (Alt modifier, no mouse button).
     */
    void testLoadActivation_missingTriggersUsesDefault()
    {
        IsolatedConfigGuard guard;

        // Write activation config without DragActivationTriggers
        {
            auto backend = PlasmaZones::QSettingsConfigBackend::createDefault();
            auto activation = backend->group(QStringLiteral("Activation"));
            activation->writeBool(QStringLiteral("SnappingEnabled"), true);
            // Deliberately do NOT write DragActivationTriggers
            activation.reset();
            backend->sync();
        }

        Settings settings;

        QVariantList triggers = settings.dragActivationTriggers();
        QVERIFY2(!triggers.isEmpty(), "Missing DragActivationTriggers must fall back to defaults");
        QVariantMap first = triggers.first().toMap();
        // Default: Alt modifier (3), no mouse button (0)
        QCOMPARE(first.value(QStringLiteral("modifier")).toInt(), 3);
        QCOMPARE(first.value(QStringLiteral("mouseButton")).toInt(), 0);
    }

    /**
     * When DragActivationTriggers contains corrupt/malformed JSON, load should
     * fall back to the default trigger list rather than crashing.
     */
    void testLoadActivation_corruptJsonUsesDefault()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::QSettingsConfigBackend::createDefault();
            auto activation = backend->group(QStringLiteral("Activation"));
            activation->writeString(QStringLiteral("DragActivationTriggers"), QStringLiteral("{not valid json!}"));
            activation.reset();
            backend->sync();
        }

        Settings settings;

        QVariantList triggers = settings.dragActivationTriggers();
        QVERIFY2(!triggers.isEmpty(), "Corrupt JSON must fall back to defaults");
        QVariantMap first = triggers.first().toMap();
        QCOMPARE(first.value(QStringLiteral("modifier")).toInt(), 3);
        QCOMPARE(first.value(QStringLiteral("mouseButton")).toInt(), 0);
    }

    /**
     * When ZoneSpanModifier is set but ZoneSpanTriggers JSON is absent,
     * the fallback trigger should use the actual ZoneSpanModifier value,
     * not the static default.
     */
    void testLoadActivation_zoneSpanTriggersUsesActualModifier()
    {
        IsolatedConfigGuard guard;

        {
            auto backend = PlasmaZones::QSettingsConfigBackend::createDefault();
            auto activation = backend->group(QStringLiteral("Activation"));
            activation->writeInt(QStringLiteral("ZoneSpanModifier"), 3); // Alt
            // Deliberately do NOT write ZoneSpanTriggers
            activation.reset();
            backend->sync();
        }

        Settings settings;

        QVariantList triggers = settings.zoneSpanTriggers();
        QVERIFY2(!triggers.isEmpty(), "Missing ZoneSpanTriggers must create trigger from ZoneSpanModifier value");
        QVariantMap first = triggers.first().toMap();
        QCOMPARE(first.value(QStringLiteral("modifier")).toInt(), 3);
    }
};

QTEST_MAIN(TestSettingsCore)
#include "test_settings_core.moc"

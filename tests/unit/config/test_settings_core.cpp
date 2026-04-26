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
#include "config/configbackends.h"

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
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto updates = backend->group(QStringLiteral("Updates"));
            updates->writeString(QStringLiteral("DismissedUpdateVersion"), QStringLiteral("99.0.0"));
            updates.reset();
            backend->sync();
        }

        // Create Settings and reset
        Settings settings;
        settings.reset();

        // Verify the Updates group is gone
        auto backend = PlasmaZones::createDefaultConfigBackend();
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
            auto backend = PlasmaZones::createDefaultConfigBackend();
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

        auto backend = PlasmaZones::createDefaultConfigBackend();
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
     * createDefaultConfigBackend() reads from
     * $XDG_CONFIG_HOME/plasmazones/config.json. The IsolatedConfigGuard
     * redirects XDG_CONFIG_HOME to a temp directory, so each test gets a fresh file.
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
        // (re-reads from disk after save() flushed). Uses v2 dot-path groups.
        auto backend = PlasmaZones::createDefaultConfigBackend();

        {
            auto gaps = backend->group(ConfigDefaults::snappingGapsGroup());
            QCOMPARE(gaps->readInt(ConfigDefaults::innerKey(), 0), 15);
            QCOMPARE(gaps->readInt(ConfigDefaults::outerKey(), 0), 20);
        }

        {
            auto border = backend->group(ConfigDefaults::snappingAppearanceBorderGroup());
            QCOMPARE(border->readInt(ConfigDefaults::widthKey(), 0), 5);
            QCOMPARE(border->readInt(ConfigDefaults::radiusKey(), 0), 25);
        }
        {
            auto opacity = backend->group(ConfigDefaults::snappingAppearanceOpacityGroup());
            QVERIFY(qFuzzyCompare(opacity->readDouble(ConfigDefaults::activeKey(), 0.0), 0.8));
            QVERIFY(qFuzzyCompare(opacity->readDouble(ConfigDefaults::inactiveKey(), 0.0), 0.2));
        }
        {
            auto labels = backend->group(ConfigDefaults::snappingAppearanceLabelsGroup());
            QCOMPARE(labels->readInt(ConfigDefaults::fontWeightKey(), 0), 400);
        }

        {
            auto effects = backend->group(ConfigDefaults::snappingEffectsGroup());
            QCOMPARE(effects->readBool(ConfigDefaults::showNumbersKey(), true), false);
        }

        {
            auto behavior = backend->group(ConfigDefaults::snappingBehaviorGroup());
            QCOMPARE(behavior->readBool(ConfigDefaults::toggleActivationKey(), false), true);
        }

        {
            auto zoneSelector = backend->group(ConfigDefaults::snappingZoneSelectorGroup());
            QCOMPARE(zoneSelector->readBool(ConfigDefaults::enabledKey(), true), false);
            QCOMPARE(zoneSelector->readInt(ConfigDefaults::triggerDistanceKey(), 0), 100);
            QCOMPARE(zoneSelector->readInt(ConfigDefaults::gridColumnsKey(), 0), 3);
        }

        {
            auto algo = backend->group(ConfigDefaults::tilingAlgorithmGroup());
            double readRatio = algo->readDouble(ConfigDefaults::splitRatioKey(), 0.0);
            QVERIFY2(qAbs(readRatio - 0.7) < 0.001,
                     qPrintable(QStringLiteral("SplitRatio: expected 0.7, got %1").arg(readRatio)));
            QCOMPARE(algo->readInt(ConfigDefaults::masterCountKey(), 0), 3);
        }
        {
            auto tilingGaps = backend->group(ConfigDefaults::tilingGapsGroup());
            QCOMPARE(tilingGaps->readInt(ConfigDefaults::innerKey(), 0), 12);
        }

        {
            auto animations = backend->group(ConfigDefaults::animationsGroup());
            QCOMPARE(animations->readInt(ConfigDefaults::durationKey(), 0), 300);
            QCOMPARE(animations->readInt(ConfigDefaults::sequenceModeKey(), -1), 0);
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
     * REGRESSION GUARD (PR #331 review).
     *
     * load() must emit per-property NOTIFY signals when the on-disk value
     * differs from the in-memory value at the time of the call. This is
     * the discard-changes UX flow: the user mutates a setting, the in-memory
     * Store now disagrees with disk, then calls load() to revert. QML
     * bindings rely on the specific NOTIFY signal — settingsChanged() alone
     * is not enough.
     *
     * The bug being guarded: pre-fix, load() reparseConfiguration()'d BEFORE
     * snapshotting properties. Store-backed getters read on demand from the
     * just-reloaded backend, so snapshot==reloaded value, the comparison
     * loop never detected a difference, and no NOTIFY signal fired —
     * leaving the QML binding stuck on the user's pre-discard value.
     *
     * Fix: snapshot must be taken BEFORE reparseConfiguration() so the
     * pre-discard in-memory value is captured.
     */
    void testLoad_emitsSpecificNotifyOnDiscardChanges()
    {
        IsolatedConfigGuard guard;

        // Seed disk with a known value via save().
        {
            Settings settings;
            settings.setBorderWidth(3);
            settings.save();
        }

        // Open Settings — getter now reads 3 from the freshly loaded backend.
        Settings settings;
        QCOMPARE(settings.borderWidth(), 3);

        // Mutate in-memory only — disk still says 3.
        settings.setBorderWidth(7);
        QCOMPARE(settings.borderWidth(), 7);

        // The discard-changes flow: load() reverts in-memory state to
        // disk and must emit borderWidthChanged() so QML rebinds.
        QSignalSpy specificSpy(&settings, &Settings::borderWidthChanged);
        QVERIFY(specificSpy.isValid());

        settings.load();

        QCOMPARE(settings.borderWidth(), 3);
        QCOMPARE(specificSpy.count(), 1);
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

        // Write v2 snapping behavior config without DragActivationTriggers
        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto activation = backend->group(ConfigDefaults::snappingBehaviorGroup());
            activation->writeBool(ConfigDefaults::enabledKey(), true);
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
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto behavior = backend->group(ConfigDefaults::snappingBehaviorGroup());
            behavior->writeString(ConfigDefaults::triggersKey(), QStringLiteral("{not valid json!}"));
            behavior.reset();
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
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto zoneSpan = backend->group(ConfigDefaults::snappingBehaviorZoneSpanGroup());
            zoneSpan->writeInt(ConfigDefaults::modifierKey(), 3); // Alt
            // Deliberately do NOT write Triggers
            zoneSpan.reset();
            backend->sync();
        }

        Settings settings;

        QVariantList triggers = settings.zoneSpanTriggers();
        QVERIFY2(!triggers.isEmpty(), "Missing ZoneSpanTriggers must create trigger from ZoneSpanModifier value");
        QVariantMap first = triggers.first().toMap();
        QCOMPARE(first.value(QStringLiteral("modifier")).toInt(), 3);
    }
    // =========================================================================
    // Stale key purging
    // =========================================================================

    /**
     * save() must remove stale keys that are not part of the current schema.
     * Simulates a refactor where a key was renamed: the old key should be
     * purged after save(), while all valid keys survive.
     */
    void testSave_purgesStaleKeys()
    {
        IsolatedConfigGuard guard;

        // Inject stale keys into several v2 groups
        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            {
                auto g = backend->group(ConfigDefaults::snappingBehaviorGroup());
                g->writeString(QStringLiteral("ObsoleteActivationKey"), QStringLiteral("stale"));
            }
            {
                auto g = backend->group(ConfigDefaults::snappingEffectsGroup());
                g->writeBool(QStringLiteral("OldDisplayToggle"), true);
            }
            {
                auto g = backend->group(ConfigDefaults::snappingAppearanceColorsGroup());
                g->writeInt(QStringLiteral("DeprecatedThemeIndex"), 42);
            }
            {
                auto g = backend->group(ConfigDefaults::tilingAlgorithmGroup());
                g->writeString(QStringLiteral("RemovedAutotileSetting"), QStringLiteral("gone"));
            }
            backend->sync();
        }

        // Load picks up the stale keys from disk (but ignores them in members)
        Settings settings;

        // Mutate one value to ensure save actually writes
        settings.setZonePadding(99);
        settings.save();

        // Re-read the file and verify stale keys are gone
        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(ConfigDefaults::snappingBehaviorGroup());
            QVERIFY2(!g->hasKey(QStringLiteral("ObsoleteActivationKey")),
                     "Stale key in Snapping.Behavior group must be purged by save()");
            // Valid key must survive
            QVERIFY2(g->hasKey(ConfigDefaults::toggleActivationKey()),
                     "Valid key ToggleActivation must survive save()");
        }
        {
            auto g = backend->group(ConfigDefaults::snappingEffectsGroup());
            QVERIFY2(!g->hasKey(QStringLiteral("OldDisplayToggle")),
                     "Stale key in Snapping.Effects group must be purged by save()");
            QVERIFY2(g->hasKey(ConfigDefaults::showNumbersKey()), "Valid key ShowNumbers must survive save()");
        }
        {
            auto g = backend->group(ConfigDefaults::snappingAppearanceColorsGroup());
            QVERIFY2(!g->hasKey(QStringLiteral("DeprecatedThemeIndex")),
                     "Stale key in Snapping.Appearance.Colors group must be purged by save()");
        }
        {
            auto g = backend->group(ConfigDefaults::tilingGroup());
            QVERIFY2(!g->hasKey(QStringLiteral("RemovedAutotileSetting")),
                     "Stale key in Tiling group must be purged by save()");
            QVERIFY2(g->hasKey(ConfigDefaults::enabledKey()), "Valid key Enabled must survive save()");
        }
    }

    /**
     * save() must delete per-screen override groups that it doesn't rewrite,
     * which removes any stale keys they contain. Groups for screens the
     * Settings object doesn't know about are deleted entirely.
     */
    void testSave_purgesStaleKeysInPerScreenGroups()
    {
        IsolatedConfigGuard guard;

        // Inject a per-screen group with a valid key and a stale key
        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            {
                auto g = backend->group(QStringLiteral("ZoneSelector:eDP-1"));
                g->writeInt(ConfigDefaults::positionKey(), 2);
                g->writeString(QStringLiteral("ObsoletePerScreenKey"), QStringLiteral("stale"));
            }
            backend->sync();
        }

        Settings settings;
        settings.save();

        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("ZoneSelector:eDP-1"));
            QVERIFY2(!g->hasKey(QStringLiteral("ObsoletePerScreenKey")),
                     "Stale key in per-screen group must be purged by save()");
        }
    }

    /**
     * save() must NOT purge groups it doesn't manage (e.g., TilingQuickLayoutSlots,
     * Updates) — those are written independently and must survive a settings save.
     */
    void testSave_preservesUnmanagedGroups()
    {
        IsolatedConfigGuard guard;

        // Write to unmanaged groups
        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            {
                auto g = backend->group(QStringLiteral("TilingQuickLayoutSlots"));
                g->writeString(QStringLiteral("1"), QStringLiteral("some-layout-id"));
            }
            {
                auto g = backend->group(QStringLiteral("Updates"));
                g->writeString(QStringLiteral("DismissedUpdateVersion"), QStringLiteral("2.0.0"));
            }
            backend->sync();
        }

        Settings settings;
        settings.save();

        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("TilingQuickLayoutSlots"));
            QCOMPARE(g->readString(QStringLiteral("1")), QStringLiteral("some-layout-id"));
        }
        {
            auto g = backend->group(QStringLiteral("Updates"));
            QCOMPARE(g->readString(QStringLiteral("DismissedUpdateVersion")), QStringLiteral("2.0.0"));
        }
    }

    /**
     * save() must purge unknown root-level groups that are not part of the
     * known schema (neither managed nor explicitly unmanaged).  This catches
     * stale groups left behind by removed features, botched migrations, or
     * hand-editing.
     */
    void testSave_purgesUnknownRootLevelGroups()
    {
        IsolatedConfigGuard guard;

        // Inject unknown root-level groups
        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            {
                auto g = backend->group(QStringLiteral("ObsoleteFeature"));
                g->writeString(QStringLiteral("Key"), QStringLiteral("value"));
            }
            {
                auto g = backend->group(QStringLiteral("OldGroupFromV0"));
                g->writeBool(QStringLiteral("Flag"), true);
            }
            // Inject valid unmanaged groups to prove they survive
            {
                auto g = backend->group(QStringLiteral("TilingQuickLayoutSlots"));
                g->writeString(QStringLiteral("1"), QStringLiteral("layout-id"));
            }
            {
                auto g = backend->group(QStringLiteral("Updates"));
                g->writeString(QStringLiteral("LastCheck"), QStringLiteral("2026-04-07"));
            }
            backend->sync();
        }

        Settings settings;
        settings.save();

        auto backend = PlasmaZones::createDefaultConfigBackend();
        const QStringList groups = backend->groupList();

        // Unknown groups must be gone
        bool hasObsolete = false;
        bool hasOldGroup = false;
        for (const QString& g : groups) {
            if (g == QStringLiteral("ObsoleteFeature"))
                hasObsolete = true;
            if (g == QStringLiteral("OldGroupFromV0"))
                hasOldGroup = true;
        }
        QVERIFY2(!hasObsolete, "Unknown root-level group 'ObsoleteFeature' must be purged by save()");
        QVERIFY2(!hasOldGroup, "Unknown root-level group 'OldGroupFromV0' must be purged by save()");

        // Unmanaged groups must survive
        {
            auto g = backend->group(QStringLiteral("TilingQuickLayoutSlots"));
            QCOMPARE(g->readString(QStringLiteral("1")), QStringLiteral("layout-id"));
        }
        {
            auto g = backend->group(QStringLiteral("Updates"));
            QCOMPARE(g->readString(QStringLiteral("LastCheck")), QStringLiteral("2026-04-07"));
        }
    }

    /**
     * save() must purge unknown dot-path groups (e.g. "OldFeature.SubGroup")
     * by deleting the entire top-level parent.
     */
    void testSave_purgesUnknownDotPathGroups()
    {
        IsolatedConfigGuard guard;

        // Inject an unknown nested group
        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            {
                auto g = backend->group(QStringLiteral("RemovedFeature.SubGroup"));
                g->writeString(QStringLiteral("Key"), QStringLiteral("leftover"));
            }
            {
                auto g = backend->group(QStringLiteral("RemovedFeature.SubGroup.Deeper"));
                g->writeBool(QStringLiteral("Flag"), true);
            }
            backend->sync();
        }

        Settings settings;
        settings.save();

        auto backend = PlasmaZones::createDefaultConfigBackend();
        const QStringList groups = backend->groupList();

        for (const QString& g : groups) {
            QVERIFY2(!g.startsWith(QStringLiteral("RemovedFeature")),
                     qPrintable(QStringLiteral("Dot-path group '%1' must be purged by save()").arg(g)));
        }
    }

    /**
     * save() round-trip must preserve all valid settings values even after
     * stale key purging (regression guard — purging must not corrupt data).
     */
    void testSave_purgeDoesNotCorruptValues()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        settings.setZonePadding(42);
        settings.setBorderWidth(7);
        settings.setActiveOpacity(0.65);
        settings.setShowZoneNumbers(false);
        settings.setAutotileEnabled(true);
        settings.setAnimationDuration(500);
        settings.save();

        // Reload from disk
        Settings reloaded;
        QCOMPARE(reloaded.zonePadding(), 42);
        QCOMPARE(reloaded.borderWidth(), 7);
        QCOMPARE(reloaded.activeOpacity(), 0.65);
        QCOMPARE(reloaded.showZoneNumbers(), false);
        QCOMPARE(reloaded.autotileEnabled(), true);
        QCOMPARE(reloaded.animationDuration(), 500);
    }

    /**
     * Global "Auto-assign for all layouts" master toggle (#370): defaults to
     * false to preserve per-layout-only behavior on upgrade, the setter emits
     * the specific NOTIFY signal once per real change, and the value
     * round-trips through save/reload via the WindowHandling group.
     */
    void testAutoAssignAllLayouts_defaultSetterRoundtrip()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        QCOMPARE(settings.autoAssignAllLayouts(), false);

        QSignalSpy specificSpy(&settings, &Settings::autoAssignAllLayoutsChanged);
        QSignalSpy generalSpy(&settings, &Settings::settingsChanged);
        QVERIFY(specificSpy.isValid());
        QVERIFY(generalSpy.isValid());

        settings.setAutoAssignAllLayouts(true);
        QCOMPARE(settings.autoAssignAllLayouts(), true);
        QCOMPARE(specificSpy.count(), 1);
        QVERIFY(generalSpy.count() >= 1);

        // Idempotent: same value must not re-emit
        settings.setAutoAssignAllLayouts(true);
        QCOMPARE(specificSpy.count(), 1);

        settings.save();

        Settings reloaded;
        QCOMPARE(reloaded.autoAssignAllLayouts(), true);
    }
};

QTEST_MAIN(TestSettingsCore)
#include "test_settings_core.moc"

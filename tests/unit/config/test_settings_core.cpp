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
 * 5. Activation setting load + default fallback
 *
 * Companion test files:
 *   - test_settings_animation_profile.cpp — Profile JSON-blob storage,
 *     per-field signals, aggregate-setter merge semantics
 *   - test_settings_shader_tree.cpp       — ShaderProfileTree persistence,
 *     prune-on-write/read, autoAssignAllLayouts master toggle
 */

#include <QSet>
#include <QTest>
#include <QColor>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
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
     * Snapping focus-behavior settings default off and return to the default on
     * reset(). Both gate runtime focus changes (the daemon focus-new-windows emit
     * and the effect focus-follows-mouse), so a flipped default would silently
     * change focus behavior for every user.
     */
    void testSnappingFocusSettings_defaultsAndReset()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        QCOMPARE(settings.snappingFocusNewWindows(), ConfigDefaults::snappingFocusNewWindows());
        QCOMPARE(settings.snappingFocusFollowsMouse(), ConfigDefaults::snappingFocusFollowsMouse());
        QCOMPARE(settings.snappingFocusNewWindows(), false);
        QCOMPARE(settings.snappingFocusFollowsMouse(), false);

        settings.setSnappingFocusNewWindows(true);
        settings.setSnappingFocusFollowsMouse(true);
        QCOMPARE(settings.snappingFocusNewWindows(), true);
        QCOMPARE(settings.snappingFocusFollowsMouse(), true);

        settings.reset();
        QCOMPARE(settings.snappingFocusNewWindows(), ConfigDefaults::snappingFocusNewWindows());
        QCOMPARE(settings.snappingFocusFollowsMouse(), ConfigDefaults::snappingFocusFollowsMouse());
    }

    /**
     * The decoration window-filtering knobs (Decorations.WindowFiltering group)
     * must return to their defaults on reset(). Regression: the group has to be
     * listed in managedGroupNames() or reset() leaves the user's values on disk,
     * and the schema-claimed group is never otherwise purged, so a factory reset
     * would silently fail to restore the three Window Appearance filter knobs.
     */
    void testDecorationWindowFiltering_defaultsAndReset()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        QCOMPARE(settings.decorationExcludeTransientWindows(), ConfigDefaults::decorationExcludeTransientWindows());
        QCOMPARE(settings.decorationMinimumWindowWidth(), ConfigDefaults::decorationMinimumWindowWidth());
        QCOMPARE(settings.decorationMinimumWindowHeight(), ConfigDefaults::decorationMinimumWindowHeight());

        settings.setDecorationExcludeTransientWindows(false);
        settings.setDecorationMinimumWindowWidth(300);
        settings.setDecorationMinimumWindowHeight(200);
        QCOMPARE(settings.decorationExcludeTransientWindows(), false);
        QCOMPARE(settings.decorationMinimumWindowWidth(), 300);
        QCOMPARE(settings.decorationMinimumWindowHeight(), 200);

        settings.reset();
        QCOMPARE(settings.decorationExcludeTransientWindows(), ConfigDefaults::decorationExcludeTransientWindows());
        QCOMPARE(settings.decorationMinimumWindowWidth(), ConfigDefaults::decorationMinimumWindowWidth());
        QCOMPARE(settings.decorationMinimumWindowHeight(), ConfigDefaults::decorationMinimumWindowHeight());
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

        // Set non-default values across different groups. Inner/outer gaps are
        // config-backed (the Gaps group); their save/reload round-trip is covered
        // by testConfigBackedGap_roundTripsAndEmits, so they're omitted here to
        // avoid duplicating that coverage.
        settings.setBorderWidth(5);
        settings.setBorderRadius(25);
        settings.setActiveOpacity(0.8);
        settings.setInactiveOpacity(0.2);
        settings.setShowZoneNumbers(false);
        settings.setToggleActivation(true);
        settings.setSnappingFocusNewWindows(true);
        settings.setSnappingFocusFollowsMouse(true);
        settings.setZoneSpanToggleMode(true);
        settings.setZoneSelectorEnabled(false);
        settings.setZoneSelectorTriggerDistance(100);
        settings.setZoneSelectorGridColumns(3);
        settings.setAutotileSplitRatio(0.7);
        settings.setAutotileMasterCount(3);
        settings.setAnimationDuration(300);
        settings.setAnimationSequenceMode(0);
        settings.setLabelFontWeight(400);

        settings.save();

        // Verify the round-trip by reading from a fresh config backend
        // (re-reads from disk after save() flushed). Uses v2 dot-path groups.
        auto backend = PlasmaZones::createDefaultConfigBackend();

        {
            auto border = backend->group(ConfigDefaults::snappingZonesBorderGroup());
            QCOMPARE(border->readInt(ConfigDefaults::widthKey(), 0), 5);
            QCOMPARE(border->readInt(ConfigDefaults::radiusKey(), 0), 25);
        }
        {
            auto opacity = backend->group(ConfigDefaults::snappingZonesOpacityGroup());
            QVERIFY(qFuzzyCompare(opacity->readDouble(ConfigDefaults::activeKey(), 0.0), 0.8));
            QVERIFY(qFuzzyCompare(opacity->readDouble(ConfigDefaults::inactiveKey(), 0.0), 0.2));
        }
        {
            auto labels = backend->group(ConfigDefaults::snappingZonesLabelsGroup());
            QCOMPARE(labels->readInt(ConfigDefaults::fontWeightKey(), 0), 400);
        }

        {
            auto effects = backend->group(ConfigDefaults::snappingEffectsGroup());
            QCOMPARE(effects->readBool(ConfigDefaults::showNumbersKey(), true), false);
        }

        {
            auto behavior = backend->group(ConfigDefaults::snappingBehaviorGroup());
            QCOMPARE(behavior->readBool(ConfigDefaults::toggleActivationKey(), false), true);
            QCOMPARE(behavior->readBool(ConfigDefaults::focusNewWindowsKey(), false), true);
            QCOMPARE(behavior->readBool(ConfigDefaults::focusFollowsMouseKey(), false), true);
        }

        {
            auto zoneSpan = backend->group(ConfigDefaults::snappingBehaviorZoneSpanGroup());
            QCOMPARE(zoneSpan->readBool(ConfigDefaults::toggleActivationKey(), false), true);
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
            // Phase 4 sub-commit 6: animation settings persist as a
            // single Profile JSON blob under `Animations/Profile`
            // (decision S). The per-field setters compose into that
            // blob — verify the on-disk shape carries both values.
            auto animations = backend->group(ConfigDefaults::animationsGroup());
            const QString profileJson = animations->readString(ConfigDefaults::animationProfileKey(), QString());
            QVERIFY2(!profileJson.isEmpty(), "Animations/Profile blob missing from persisted config");
            const QJsonDocument doc = QJsonDocument::fromJson(profileJson.toUtf8());
            QVERIFY(doc.isObject());
            const QJsonObject obj = doc.object();
            QCOMPARE(obj.value(QLatin1String("duration")).toInt(), 300);
            QCOMPARE(obj.value(QLatin1String("sequenceMode")).toInt(), 0);
        }
    }

    // =========================================================================
    // P1: functional correctness -- signal emission
    // =========================================================================

    /**
     * load() must emit settingsChanged() exactly once when reloading actually
     * changes values, so listeners re-read them. Exercised via the discard
     * flow: an unsaved in-memory edit is reverted by load()'s reparse, which
     * must announce the change through the single aggregate emission.
     */
    void testLoad_emitsSettingsChanged()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        const int committed = settings.adjacentThreshold();
        settings.setAdjacentThreshold(committed == 15 ? 20 : 15);

        QSignalSpy spy(&settings, &Settings::settingsChanged);
        QVERIFY(spy.isValid());

        settings.load();

        QCOMPARE(settings.adjacentThreshold(), committed);
        QCOMPARE(spy.count(), 1);
    }

    /**
     * A reload that leaves the effective (palette-derived) values unchanged
     * must stay silent. Pins the system-colors regression where load()'s
     * palette re-derive routed through the public color setters, firing each
     * color NOTIFY twice and settingsChanged several times per themed reload
     * even when no value changed.
     *
     * The on-disk highlight color is deliberately made stale before load():
     * with useSystemColors on, the mid-load derive silently restores the
     * palette color, so no signal may fire. Without the load-suppression
     * guard the derive would route the palette value through the public
     * setter (disk value != palette value, so the same-value early-return
     * cannot mask the regression) and emit highlightColorChanged +
     * settingsChanged mid-load.
     */
    void testLoad_noSignal_whenUnchanged()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        QVERIFY(settings.useSystemColors()); // default: themed reload path
        settings.save(); // commit the constructor-derived state to disk

        const QColor derivedHighlight = settings.highlightColor();

        // Hand-write a stale highlight color to disk so load()'s reparse sees
        // a value that differs from the palette-derived one.
        const QColor staleHighlight(1, 2, 3);
        QVERIFY(staleHighlight != derivedHighlight);
        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto g = backend->group(ConfigDefaults::snappingZonesColorsGroup());
            g->writeColor(ConfigDefaults::highlightKey(), staleHighlight);
            backend->sync();
        }

        QSignalSpy spy(&settings, &Settings::settingsChanged);
        QSignalSpy highlightSpy(&settings, &Settings::highlightColorChanged);
        QSignalSpy fontColorSpy(&settings, &Settings::labelFontColorChanged);
        QVERIFY(spy.isValid());

        settings.load();

        // The derive restored the palette color over the stale disk value...
        QCOMPARE(settings.highlightColor(), derivedHighlight);
        // ...without any signal traffic.
        QCOMPARE(spy.count(), 0);
        QCOMPARE(highlightSpy.count(), 0);
        QCOMPARE(fontColorSpy.count(), 0);
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
        QSignalSpy specificSpy(&settings, &Settings::adjacentThresholdChanged);
        QVERIFY(generalSpy.isValid());
        QVERIFY(specificSpy.isValid());

        int current = settings.adjacentThreshold();
        int next = (current == 15) ? 20 : 15;

        settings.setAdjacentThreshold(next);

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
        settings.setAdjacentThreshold(15); // force a known value

        QSignalSpy generalSpy(&settings, &Settings::settingsChanged);
        QSignalSpy specificSpy(&settings, &Settings::adjacentThresholdChanged);

        settings.setAdjacentThreshold(15); // same value again

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
                auto g = backend->group(ConfigDefaults::snappingZonesColorsGroup());
                g->writeInt(QStringLiteral("DeprecatedThemeIndex"), 42);
            }
            {
                auto g = backend->group(ConfigDefaults::tilingAlgorithmGroup());
                g->writeString(QStringLiteral("RemovedAutotileSetting"), QStringLiteral("gone"));
            }
            backend->sync();
        }

        // Precondition: every stale key really is on disk, in the group it was
        // written to. Without this the "it is gone after save()" assertions
        // below cannot tell a working purge from a key that was never there —
        // which is exactly how the Tiling.Algorithm case passed for so long
        // while reading back from the wrong group.
        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            QVERIFY(backend->group(ConfigDefaults::snappingBehaviorGroup())
                        ->hasKey(QStringLiteral("ObsoleteActivationKey")));
            QVERIFY(backend->group(ConfigDefaults::snappingEffectsGroup())->hasKey(QStringLiteral("OldDisplayToggle")));
            QVERIFY(backend->group(ConfigDefaults::snappingZonesColorsGroup())
                        ->hasKey(QStringLiteral("DeprecatedThemeIndex")));
            QVERIFY(backend->group(ConfigDefaults::tilingAlgorithmGroup())
                        ->hasKey(QStringLiteral("RemovedAutotileSetting")));
        }

        // Load picks up the stale keys from disk (but ignores them in members)
        Settings settings;

        // Mutate one value to ensure save actually writes
        settings.setAdjacentThreshold(99);
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
            auto g = backend->group(ConfigDefaults::snappingZonesColorsGroup());
            QVERIFY2(!g->hasKey(QStringLiteral("DeprecatedThemeIndex")),
                     "Stale key in Snapping.Zones.Colors group must be purged by save()");
        }
        {
            // Read back from the group the key was WRITTEN to. Tiling.Algorithm
            // and Tiling are distinct nested groups, so asking Tiling whether it
            // holds a key only ever planted in Tiling.Algorithm is trivially
            // true and left the purge unverified.
            auto g = backend->group(ConfigDefaults::tilingAlgorithmGroup());
            QVERIFY2(!g->hasKey(QStringLiteral("RemovedAutotileSetting")),
                     "Stale key in Tiling.Algorithm group must be purged by save()");
        }
        {
            auto g = backend->group(ConfigDefaults::tilingGroup());
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

        // Precondition, mirroring testSave_purgesStaleKeys: both keys really are
        // on disk in that group before save() runs. Without it the absence
        // assertion below cannot tell a working purge from a key that was never
        // written.
        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto g = backend->group(QStringLiteral("ZoneSelector:eDP-1"));
            QVERIFY(g->hasKey(ConfigDefaults::positionKey()));
            QVERIFY(g->hasKey(QStringLiteral("ObsoletePerScreenKey")));
        }

        Settings settings;
        settings.save();

        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("ZoneSelector:eDP-1"));
            QVERIFY2(!g->hasKey(QStringLiteral("ObsoletePerScreenKey")),
                     "Stale key in per-screen group must be purged by save()");
            // Positive control for the mechanism: save() deletes every
            // prefix-matching group and rewrites the ones it loaded, so the
            // stale key vanishing only means "purged" if the group's valid key
            // came back with it. Absence alone reads the same whether the key
            // was dropped or the whole group was deleted, and the delete arm is
            // the one that fires for a group Settings never loaded.
            QVERIFY2(g->hasKey(ConfigDefaults::positionKey()),
                     "Valid per-screen key must survive save() — the group was rewritten, not just deleted");
            QCOMPARE(g->readInt(ConfigDefaults::positionKey(), -1), 2);
        }
    }

    /**
     * The legacy "Updates" group is retired: nothing writes or reads it since
     * the dismissed-update-version moved to the settings app's QSettings, so
     * save() sweeps any stale husk of it like every other unknown group
     * (it used to be carved out of the purge as an "unmanaged" group).
     */
    void testSave_purgesRetiredUpdatesGroup()
    {
        IsolatedConfigGuard guard;

        // Simulate a config written by an older build that still carried it
        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            {
                auto g = backend->group(QStringLiteral("Updates"));
                g->writeString(QStringLiteral("DismissedUpdateVersion"), QStringLiteral("2.0.0"));
            }
            backend->sync();
        }

        Settings settings;
        settings.save();

        auto backend = PlasmaZones::createDefaultConfigBackend();
        QVERIFY2(!backend->groupList().contains(QStringLiteral("Updates")),
                 "Retired group 'Updates' must be purged by save()");
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
            // The retired TilingQuickLayoutSlots group is no longer known or
            // preserved (quick slots moved to the daemon's quicklayouts.json),
            // so save() must purge any leftover.
            {
                auto g = backend->group(QStringLiteral("TilingQuickLayoutSlots"));
                g->writeString(QStringLiteral("1"), QStringLiteral("layout-id"));
            }
            // The retired Updates group is covered by
            // testSave_purgesRetiredUpdatesGroup above; no root-level group
            // outside the schema survives save() anymore (only the v4
            // migration stash KEYS are carved out — see purgeStaleKeys).
            backend->sync();
        }

        Settings settings;
        settings.save();

        auto backend = PlasmaZones::createDefaultConfigBackend();
        const QStringList groups = backend->groupList();

        // Unknown groups must be gone
        bool hasObsolete = false;
        bool hasOldGroup = false;
        bool hasRetiredTilingSlots = false;
        for (const QString& g : groups) {
            if (g == QStringLiteral("ObsoleteFeature"))
                hasObsolete = true;
            if (g == QStringLiteral("OldGroupFromV0"))
                hasOldGroup = true;
            if (g == QStringLiteral("TilingQuickLayoutSlots"))
                hasRetiredTilingSlots = true;
        }
        QVERIFY2(!hasObsolete, "Unknown root-level group 'ObsoleteFeature' must be purged by save()");
        QVERIFY2(!hasOldGroup, "Unknown root-level group 'OldGroupFromV0' must be purged by save()");
        QVERIFY2(!hasRetiredTilingSlots, "Retired group 'TilingQuickLayoutSlots' must be purged by save()");
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
        settings.setBorderWidth(7);
        settings.setActiveOpacity(0.65);
        settings.setShowZoneNumbers(false);
        settings.setAutotileEnabled(true);
        settings.setAnimationDuration(500);
        settings.save();

        // Reload from disk
        Settings reloaded;
        QCOMPARE(reloaded.borderWidth(), 7);
        QCOMPARE(reloaded.activeOpacity(), 0.65);
        QCOMPARE(reloaded.showZoneNumbers(), false);
        QCOMPARE(reloaded.autotileEnabled(), true);
        QCOMPARE(reloaded.animationDuration(), 500);
    }

    /**
     * The shared inner/outer gap global default is config-backed: the getter reads
     * the "Gaps" config group (compile default on a fresh config), the setter
     * writes it and re-emits the per-property NOTIFY + settingsChanged exactly
     * once per real change, and the value survives a save/reload.
     */
    /// The profiles staging seam: applyConfigOverlayStaged writes the blob's
    /// values into memory, fires the per-property NOTIFY + settingsChanged,
    /// and does NOT move the baseline — the staged key must read as modified
    /// so the Save footer lights.
    void testApplyConfigOverlayStaged_stagesWithoutCommitting()
    {
        IsolatedConfigGuard guard;
        // load() captures the modified-tracking baseline itself.
        Settings settings;
        settings.load();

        QJsonObject blob = settings.exportConfigToJson();
        QJsonObject windows = blob.value(QStringLiteral("Windows")).toObject();
        const int newWidth = windows.value(QStringLiteral("Width")).toInt() + 3;
        windows.insert(QStringLiteral("Width"), newWidth);
        blob.insert(QStringLiteral("Windows"), windows);

        QSignalSpy changedSpy(&settings, &Settings::settingsChanged);
        QVERIFY(settings.applyConfigOverlayStaged(blob));
        QCOMPARE(settings.windowBorderWidth(), newWidth);
        QCOMPARE(changedSpy.count(), 1);
        // Staged, not committed: the key diverges from the baseline.
        QVERIFY(settings.isKeyModified(QStringLiteral("Windows"), QStringLiteral("Width")));
    }

    /// A blob stamped with a foreign schema version is refused whole: nothing
    /// staged, nothing signalled, and the caller learns about it.
    void testApplyConfigOverlayStaged_refusesForeignVersion()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        settings.load();
        const int before = settings.windowBorderWidth();

        QJsonObject blob = settings.exportConfigToJson();
        QJsonObject windows = blob.value(QStringLiteral("Windows")).toObject();
        windows.insert(QStringLiteral("Width"), before + 3);
        blob.insert(QStringLiteral("Windows"), windows);
        blob.insert(QStringLiteral("_version"), 1); // ancient

        QSignalSpy changedSpy(&settings, &Settings::settingsChanged);
        QVERIFY(!settings.applyConfigOverlayStaged(blob));
        QCOMPARE(settings.windowBorderWidth(), before);
        QCOMPARE(changedSpy.count(), 0);
    }

    /// defaultConfigJson delegates to the store's defaults snapshot, so it is
    /// field-for-field comparable with exportConfigToJson — the invariant the
    /// profiles delta engine diffs across.
    void testDefaultConfigJson_matchesExportShape()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        settings.load();

        const QJsonObject defaults = settings.defaultConfigJson();
        const QJsonObject exported = settings.exportConfigToJson();
        QCOMPARE(defaults.keys(), exported.keys());
        QCOMPARE(defaults.value(QStringLiteral("_version")), exported.value(QStringLiteral("_version")));
        // The palette-derived keys are the one legitimate divergence: with
        // UseSystem on (the default), load() runs applySystemColorScheme and
        // writes the live palette's highlight/inactive/border/font colours
        // into the store, so a fresh export carries THOSE, not the schema
        // defaults. Everything else must be value-identical — any other
        // difference is one serializer coercing what the other does not,
        // the exact drift the delegation to Store::defaultsToJson prevents.
        const QSet<QString> paletteDerived{
            QStringLiteral("Snapping.Zones.Colors/Highlight"),
            QStringLiteral("Snapping.Zones.Colors/Inactive"),
            QStringLiteral("Snapping.Zones.Colors/Border"),
            QStringLiteral("Snapping.Zones.Labels/FontColor"),
        };
        for (const QString& group : exported.keys()) {
            if (group == QStringLiteral("_version")) {
                continue;
            }
            const QJsonObject defaultGroup = defaults.value(group).toObject();
            const QJsonObject exportedGroup = exported.value(group).toObject();
            QCOMPARE(defaultGroup.keys(), exportedGroup.keys());
            for (const QString& key : exportedGroup.keys()) {
                if (paletteDerived.contains(group + QLatin1Char('/') + key)) {
                    continue;
                }
                QVERIFY2(
                    defaultGroup.value(key) == exportedGroup.value(key),
                    qPrintable(QStringLiteral("%1/%2: default %3 != fresh export %4")
                                   .arg(group, key,
                                        QString::fromUtf8(QJsonDocument(QJsonObject{{key, defaultGroup.value(key)}})
                                                              .toJson(QJsonDocument::Compact)),
                                        QString::fromUtf8(QJsonDocument(QJsonObject{{key, exportedGroup.value(key)}})
                                                              .toJson(QJsonDocument::Compact)))));
            }
        }
    }

    void testConfigBackedGap_roundTripsAndEmits()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        // Fresh config → the compile default.
        QCOMPARE(settings.innerGap(), ConfigDefaults::innerGap());

        QSignalSpy specificSpy(&settings, &Settings::innerGapChanged);
        QSignalSpy generalSpy(&settings, &Settings::settingsChanged);
        QVERIFY(specificSpy.isValid());
        QVERIFY(generalSpy.isValid());

        settings.setInnerGap(24);
        QCOMPARE(settings.innerGap(), 24);
        QCOMPARE(specificSpy.count(), 1);
        QVERIFY(generalSpy.count() >= 1);

        // A no-op write of the same value does not re-emit.
        settings.setInnerGap(24);
        QCOMPARE(specificSpy.count(), 1);

        // The value persists across a save/reload.
        settings.save();
        Settings reloaded;
        QCOMPARE(reloaded.innerGap(), 24);
    }
};

QTEST_MAIN(TestSettingsCore)
#include "test_settings_core.moc"

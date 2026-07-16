// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_pagereset.cpp
 * @brief Unit tests for the per-page Discard/Reset primitives on Settings.
 *
 * Covers the mutation engine the SettingsController per-page kebab drives:
 *   - captureBaseline() (via load()/save()) + isKeyModified() value tracking
 *   - discardKeys() — revert listed keys to the committed baseline
 *   - resetKeys()   — set listed keys to their schema default
 *   - NOTIFY re-emission so QML bindings refresh, and no-op when unchanged
 *   - key isolation: touching one key never disturbs a sibling
 *
 * Split from test_settings_core.cpp to stay under the 1000-line guideline.
 */

#include <QTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include "../../../src/config/settings.h"
#include "../../../src/config/configdefaults.h"
#include "../../../src/config/perscreenresolver.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestSettingsPageReset : public QObject
{
    Q_OBJECT

    // Two scalar keys in the same group, both with wide integer ranges
    // (border width 0-10, radius 0-50) so the test's chosen values never clamp.
    static Settings::ConfigKey widthKey()
    {
        return {ConfigDefaults::snappingZonesBorderGroup(), ConfigDefaults::widthKey()};
    }
    static Settings::ConfigKey radiusKey()
    {
        return {ConfigDefaults::snappingZonesBorderGroup(), ConfigDefaults::radiusKey()};
    }

    /// Read one int straight out of a config document, bypassing Settings.
    /// Group names are dot-paths ("Snapping.Zones.Border") that JsonBackend
    /// stores as nested objects, so walk the segments. Returns -1 when absent,
    /// which no key under test uses as a legitimate value.
    static int readIntFromJson(const QString& filePath, const QString& group, const QString& key)
    {
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly)) {
            return -1;
        }
        QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        const QStringList segments = group.split(QLatin1Char('.'));
        for (const QString& segment : segments) {
            obj = obj.value(segment).toObject();
        }
        return obj.value(key).toInt(-1);
    }

private Q_SLOTS:

    // isKeyModified() reports the difference from the last committed baseline,
    // which load() (here, construction) and save() refresh.
    void testIsKeyModified_tracksBaseline()
    {
        IsolatedConfigGuard guard;
        Settings s;
        const auto key = widthKey();

        // Freshly constructed: store equals the on-disk baseline.
        QVERIFY(!s.isKeyModified(key.first, key.second));

        const int def = s.borderWidth();
        const int edited = (def == 3) ? 4 : 3;
        s.setBorderWidth(edited);
        QVERIFY(s.isKeyModified(key.first, key.second));

        // Saving re-baselines: the edited value is now the committed value.
        s.save();
        QVERIFY(!s.isKeyModified(key.first, key.second));
    }

    // exportTo() writes what the user can currently see, without committing it.
    // The old export flushed via save() first, which persisted pending edits to
    // the live config and re-baselined, so a later Discard had nothing to
    // revert to. All three properties are pinned here: the export carries the
    // pending value, the live config keeps the committed one, and the baseline
    // does not move. A fourth property covers the non-Store staging path:
    // exportTo stages per-screen overrides into the live backend root to
    // serialize them, and must restore the root afterwards — otherwise the
    // staged groups leak to disk through the backend's flush-on-destruction.
    void testExportTo_writesPendingValueWithoutCommitting()
    {
        IsolatedConfigGuard guard;
        const auto key = widthKey();
        const QString dest = guard.configPath() + QStringLiteral("/exported.json");
        const QString screenName = QStringLiteral("test-screen");
        // Settings writes the group as "ZoneSelector:<screen>", but on disk the
        // PerScreenPathResolver nests it at PerScreen/ZoneSelector/<screen>.
        // readIntFromJson bypasses Settings, so it must walk the resolved path.
        const QString perScreenGroup = PerScreenPathResolver::perScreenKey() + QLatin1Char('.')
            + PerScreenPathResolver::prefixToCategory(QStringLiteral("ZoneSelector")) + QLatin1Char('.') + screenName;
        const QString perScreenKey = QLatin1String(ZoneSelectorConfigKey::MaxRows);

        {
            Settings s;
            const int def = s.borderWidth();
            const int saved = (def == 3) ? 4 : 3;
            // `saved` is always 3 or 4, so 7 differs from both `saved` and the
            // default; the export/live-config/baseline assertions below can only
            // distinguish the three values if all three are distinct.
            const int pending = 7;
            s.setBorderWidth(saved);
            s.save(); // baseline and live config = saved

            s.setBorderWidth(pending); // an edit the user has NOT saved
            // A staged per-screen override: it only reaches the backend root
            // through the save/export helpers, never through the setter.
            s.setPerScreenZoneSelectorSetting(screenName, perScreenKey, 2);
            QVERIFY(s.isKeyModified(key.first, key.second));

            QVERIFY(s.exportTo(dest));

            // The baseline did not move, so the edit is still pending and a later
            // Discard still has the committed value to revert to.
            QVERIFY(s.isKeyModified(key.first, key.second));
            s.discardKeys({key});
            QCOMPARE(s.borderWidth(), saved);

            // The live config on disk was not touched: it still holds `saved`.
            QCOMPARE(readIntFromJson(ConfigDefaults::configFilePath(), key.first, key.second), saved);

            // The export carries the value the user could see when they
            // exported, including the staged per-screen override.
            QCOMPARE(readIntFromJson(dest, key.first, key.second), pending);
            QCOMPARE(readIntFromJson(dest, perScreenGroup, perScreenKey), 2);
        }

        // Settings destruction flushes any dirty backend state to disk. The
        // export restored the live root, so the per-screen group it staged
        // must not have leaked into the live config.
        QCOMPARE(readIntFromJson(ConfigDefaults::configFilePath(), perScreenGroup, perScreenKey), -1);
    }

    // discardKeys() reverts the key to the committed baseline, fires the
    // property NOTIFY exactly once, and clears the modified flag.
    void testDiscardKeys_revertsToBaselineAndEmits()
    {
        IsolatedConfigGuard guard;
        Settings s;
        const auto key = widthKey();

        const int def = s.borderWidth();
        const int saved = (def == 3) ? 4 : 3;
        const int edited = (def == 7) ? 6 : 7;
        s.setBorderWidth(saved);
        s.save(); // baseline = saved

        s.setBorderWidth(edited);
        QVERIFY(s.isKeyModified(key.first, key.second));

        QSignalSpy spy(&s, &Settings::borderWidthChanged);
        s.discardKeys({key});

        QCOMPARE(s.borderWidth(), saved);
        QCOMPARE(spy.count(), 1);
        QVERIFY(!s.isKeyModified(key.first, key.second));
    }

    // resetKeys() sets the key to its schema default, NOT the saved baseline;
    // when the default differs from the baseline the key reads as modified
    // (the reset itself is a staged change awaiting Save/Discard).
    void testResetKeys_setsSchemaDefault()
    {
        IsolatedConfigGuard guard;
        Settings s;
        const auto key = widthKey();

        const int def = s.borderWidth();
        const int saved = (def == 3) ? 4 : 3; // != def, in [0,10]
        const int edited = (def == 7) ? 6 : 7; // != def and != saved
        s.setBorderWidth(saved);
        s.save(); // baseline = saved

        s.setBorderWidth(edited);
        QSignalSpy spy(&s, &Settings::borderWidthChanged);
        s.resetKeys({key});

        QCOMPARE(s.borderWidth(), def); // schema default, not the baseline
        QCOMPARE(spy.count(), 1);
        QVERIFY(s.isKeyModified(key.first, key.second)); // def != saved baseline
    }

    // discardKeys()/resetKeys() touch only the listed keys — a sibling key's
    // unsaved edit in the same group survives.
    void testDiscardKeys_leavesOtherKeysUntouched()
    {
        IsolatedConfigGuard guard;
        Settings s;
        const auto width = widthKey();
        const auto radius = radiusKey();

        const int w0 = s.borderWidth();
        const int r0 = s.borderRadius();
        s.save(); // baseline = defaults

        const int wEdit = (w0 == 3) ? 4 : 3;
        const int rEdit = (r0 == 20) ? 21 : 20;
        s.setBorderWidth(wEdit);
        s.setBorderRadius(rEdit);

        s.discardKeys({width}); // revert width only

        QCOMPARE(s.borderWidth(), w0); // reverted
        QCOMPARE(s.borderRadius(), rEdit); // untouched
        QVERIFY(!s.isKeyModified(width.first, width.second));
        QVERIFY(s.isKeyModified(radius.first, radius.second));
    }

    // Reverting a key already at the baseline is a no-op: no NOTIFY, no change.
    void testDiscardKeys_noopWhenAtBaseline()
    {
        IsolatedConfigGuard guard;
        Settings s;
        const auto key = widthKey();
        s.save(); // baseline = current value, nothing dirty

        QSignalSpy spy(&s, &Settings::borderWidthChanged);
        s.discardKeys({key});

        QCOMPARE(spy.count(), 0);
        QVERIFY(!s.isKeyModified(key.first, key.second));
    }

    // resetKeys() touches only the listed keys — a sibling edit survives.
    void testResetKeys_leavesOtherKeysUntouched()
    {
        IsolatedConfigGuard guard;
        Settings s;
        const auto width = widthKey();
        const auto radius = radiusKey();

        const int w0 = s.borderWidth();
        const int r0 = s.borderRadius();
        const int wEdit = (w0 == 3) ? 4 : 3;
        const int rEdit = (r0 == 20) ? 21 : 20;
        s.setBorderWidth(wEdit);
        s.setBorderRadius(rEdit);
        s.save(); // baseline = the edited values

        s.resetKeys({width}); // reset width to its schema default only

        QCOMPARE(s.borderWidth(), w0); // schema default
        QCOMPARE(s.borderRadius(), rEdit); // untouched
        QVERIFY(s.isKeyModified(width.first, width.second)); // default != saved (wEdit)
        QVERIFY(!s.isKeyModified(radius.first, radius.second));
    }

    // resetKeys() on a key already at its schema default emits no NOTIFY.
    void testResetKeys_noopWhenAtDefault()
    {
        IsolatedConfigGuard guard;
        Settings s;
        const auto key = widthKey();
        // Fresh Settings: borderWidth is already the schema default.
        const int def = s.borderWidth();
        QSignalSpy spy(&s, &Settings::borderWidthChanged);
        s.resetKeys({key});

        QCOMPARE(spy.count(), 0);
        QCOMPARE(s.borderWidth(), def); // unchanged
    }

    // An empty key list is a no-op for both primitives: no settingsChanged, no
    // property NOTIFY, nothing modified. Guards the "reset a page that owns no
    // keys" path.
    void testEmptyList_isNoop()
    {
        IsolatedConfigGuard guard;
        Settings s;
        s.save(); // baseline = defaults

        QSignalSpy settingsSpy(&s, &Settings::settingsChanged);
        QSignalSpy widthSpy(&s, &Settings::borderWidthChanged);
        s.discardKeys({});
        s.resetKeys({});

        QCOMPARE(settingsSpy.count(), 0);
        QCOMPARE(widthSpy.count(), 0);
    }

    // A key absent from the schema (and thus the captured baseline) is silently
    // skipped by discardKeys — no crash, no spurious write clobbering a live
    // sibling, no NOTIFY. resetKeys likewise leaves the store untouched (reset()
    // is a no-op on an undeclared key). The synthetic key is intentionally not a
    // ConfigDefaults accessor: it names a group/key the schema does not declare.
    void testUnknownKey_isIgnored()
    {
        IsolatedConfigGuard guard;
        Settings s;
        const auto width = widthKey();
        const Settings::ConfigKey bogus{QStringLiteral("NoSuchGroup"), QStringLiteral("NoSuchKey")};

        const int w0 = s.borderWidth();
        const int wEdit = (w0 == 3) ? 4 : 3;
        s.setBorderWidth(wEdit);
        s.save(); // baseline = wEdit

        QSignalSpy widthSpy(&s, &Settings::borderWidthChanged);
        s.discardKeys({bogus});
        s.resetKeys({bogus});

        // The unknown key never reports modified, and the real sibling is intact
        // (value and modified-flag both undisturbed by the bogus operations).
        QVERIFY(!s.isKeyModified(bogus.first, bogus.second));
        QCOMPARE(s.borderWidth(), wEdit);
        QVERIFY(!s.isKeyModified(width.first, width.second));
        QCOMPARE(widthSpy.count(), 0);
    }
};

QTEST_MAIN(TestSettingsPageReset)
#include "test_settings_pagereset.moc"

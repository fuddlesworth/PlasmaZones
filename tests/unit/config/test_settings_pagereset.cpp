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
 * Split from test_settings_core.cpp to stay under the <800-line guideline.
 */

#include <QTest>
#include <QSignalSpy>

#include "../../../src/config/settings.h"
#include "../../../src/config/configdefaults.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestSettingsPageReset : public QObject
{
    Q_OBJECT

    // Two scalar config keys in the same group (both integer, range 1-10) so the
    // test's chosen values never clamp. NB the zone-overlay border width/radius are
    // rule-backed now, so this uses the still-config global zone-selector max-rows
    // and grid-columns keys as the representative config-key pair.
    static Settings::ConfigKey widthKey()
    {
        return {ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::maxRowsKey()};
    }
    static Settings::ConfigKey radiusKey()
    {
        return {ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::gridColumnsKey()};
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

        const int def = s.zoneSelectorMaxRows();
        const int edited = (def == 3) ? 4 : 3;
        s.setZoneSelectorMaxRows(edited);
        QVERIFY(s.isKeyModified(key.first, key.second));

        // Saving re-baselines: the edited value is now the committed value.
        s.save();
        QVERIFY(!s.isKeyModified(key.first, key.second));
    }

    // discardKeys() reverts the key to the committed baseline, fires the
    // property NOTIFY exactly once, and clears the modified flag.
    void testDiscardKeys_revertsToBaselineAndEmits()
    {
        IsolatedConfigGuard guard;
        Settings s;
        const auto key = widthKey();

        const int def = s.zoneSelectorMaxRows();
        const int saved = (def == 3) ? 4 : 3;
        const int edited = (def == 7) ? 6 : 7;
        s.setZoneSelectorMaxRows(saved);
        s.save(); // baseline = saved

        s.setZoneSelectorMaxRows(edited);
        QVERIFY(s.isKeyModified(key.first, key.second));

        QSignalSpy spy(&s, &Settings::zoneSelectorMaxRowsChanged);
        s.discardKeys({key});

        QCOMPARE(s.zoneSelectorMaxRows(), saved);
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

        const int def = s.zoneSelectorMaxRows();
        const int saved = (def == 3) ? 4 : 3; // != def, in [0,10]
        const int edited = (def == 7) ? 6 : 7; // != def and != saved
        s.setZoneSelectorMaxRows(saved);
        s.save(); // baseline = saved

        s.setZoneSelectorMaxRows(edited);
        QSignalSpy spy(&s, &Settings::zoneSelectorMaxRowsChanged);
        s.resetKeys({key});

        QCOMPARE(s.zoneSelectorMaxRows(), def); // schema default, not the baseline
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

        const int w0 = s.zoneSelectorMaxRows();
        const int r0 = s.zoneSelectorGridColumns();
        s.save(); // baseline = defaults

        const int wEdit = (w0 == 3) ? 4 : 3;
        const int rEdit = (r0 == 5) ? 6 : 5;
        s.setZoneSelectorMaxRows(wEdit);
        s.setZoneSelectorGridColumns(rEdit);

        s.discardKeys({width}); // revert width only

        QCOMPARE(s.zoneSelectorMaxRows(), w0); // reverted
        QCOMPARE(s.zoneSelectorGridColumns(), rEdit); // untouched
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

        QSignalSpy spy(&s, &Settings::zoneSelectorMaxRowsChanged);
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

        const int w0 = s.zoneSelectorMaxRows();
        const int r0 = s.zoneSelectorGridColumns();
        const int wEdit = (w0 == 3) ? 4 : 3;
        const int rEdit = (r0 == 5) ? 6 : 5;
        s.setZoneSelectorMaxRows(wEdit);
        s.setZoneSelectorGridColumns(rEdit);
        s.save(); // baseline = the edited values

        s.resetKeys({width}); // reset width to its schema default only

        QCOMPARE(s.zoneSelectorMaxRows(), w0); // schema default
        QCOMPARE(s.zoneSelectorGridColumns(), rEdit); // untouched
        QVERIFY(s.isKeyModified(width.first, width.second)); // default != saved (wEdit)
        QVERIFY(!s.isKeyModified(radius.first, radius.second));
    }

    // resetKeys() on a key already at its schema default emits no NOTIFY.
    void testResetKeys_noopWhenAtDefault()
    {
        IsolatedConfigGuard guard;
        Settings s;
        const auto key = widthKey();
        // Fresh Settings: the key is already at its schema default.
        const int def = s.zoneSelectorMaxRows();
        QSignalSpy spy(&s, &Settings::zoneSelectorMaxRowsChanged);
        s.resetKeys({key});

        QCOMPARE(spy.count(), 0);
        QCOMPARE(s.zoneSelectorMaxRows(), def); // unchanged
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
        QSignalSpy widthSpy(&s, &Settings::zoneSelectorMaxRowsChanged);
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

        const int w0 = s.zoneSelectorMaxRows();
        const int wEdit = (w0 == 3) ? 4 : 3;
        s.setZoneSelectorMaxRows(wEdit);
        s.save(); // baseline = wEdit

        QSignalSpy widthSpy(&s, &Settings::zoneSelectorMaxRowsChanged);
        s.discardKeys({bogus});
        s.resetKeys({bogus});

        // The unknown key never reports modified, and the real sibling is intact
        // (value and modified-flag both undisturbed by the bogus operations).
        QVERIFY(!s.isKeyModified(bogus.first, bogus.second));
        QCOMPARE(s.zoneSelectorMaxRows(), wEdit);
        QVERIFY(!s.isKeyModified(width.first, width.second));
        QCOMPARE(widthSpy.count(), 0);
    }
};

QTEST_MAIN(TestSettingsPageReset)
#include "test_settings_pagereset.moc"

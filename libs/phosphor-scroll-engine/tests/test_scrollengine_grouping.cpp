// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Tab grouping on the open path (engine_grouping.cpp): the same-app default
// and the openTabGroup rule key, their precedence against the consume rule,
// the stash, the order seed, migration and unfloat, the focus-new-windows
// rewind of a background host, and the hosts the scan must not offer. Split
// out of test_scrollengine_behaviour.cpp at the file-size ceiling; the same
// two-screen provider engine and the same settings stub, read live.
//
// Everything asserted here is an OBSERVABLE (which column a window landed
// in, a column's display and tile count, the strip's active window, the tab
// on show, the tab-strip payload), never the grouping resolver's own return
// value.

#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollStrip.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include "scrollstriptestutils.h"
#include "scrollstubsettings.h"
#include "scrollstubtracking.h"

#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorScrollEngine;

using ScrollTestUtils::makeProviderEngine;
using ScrollTestUtils::StubScrollSettings;
using ScrollTestUtils::StubWindowTracking;

namespace {

const QString kS1 = QStringLiteral("S1");
const QString kS2 = QStringLiteral("S2");

} // namespace

class TestScrollEngineGrouping : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Proves the vertical arm really is transposed, so a lost ENVIRONMENT
    /// property cannot leave it silently re-running the horizontal suite.
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    void sameAppOpenJoinsItsColumnAsATab();
    void sameAppGroupingYieldsToRulesAndUnstableIds();
    void openTabbedFalseOptsOutOfAppGrouping();
    void groupedJoinUnderFocusNewOffKeepsTheHostsShownTab();
    void groupingSkipsMigrationsUnfloatsAndOutrankedArms();
    void groupingSkipsDraggedAndMinimizedHosts();

private:
    /// columnOfWindow that WARNS on the -1 miss so the log names the window.
    /// A static helper cannot fail the case itself, so every caller must
    /// capture the result and QVERIFY(idx >= 0) before feeding it to .at()
    /// or comparing two results for equality (-1 == -1 passes vacuously).
    static int columnOfOrFail(ScrollState* state, const QString& windowId)
    {
        const int idx = state->strip().columnOfWindow(windowId);
        if (idx < 0) {
            qWarning("%s is not a strip tile", qPrintable(windowId));
        }
        return idx;
    }

    /// An engine on S1 and S2 with @p settings installed and its cached
    /// globals refreshed. @p tracker is wired only where the path under test
    /// needs one.
    static ScrollEngine* makeEngine(QObject* parent, StubScrollSettings* settings,
                                    StubWindowTracking* tracker = nullptr)
    {
        ScrollEngine* engine = makeProviderEngine(parent, {kS1, kS2}, {}, {}, tracker);
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();
        return engine;
    }

    /// The strip's active window on @p screenId, or empty when the screen has
    /// no state — which fails every comparison here rather than asserting.
    static QString activeWindowOn(ScrollEngine* engine, const QString& screenId)
    {
        auto* state = static_cast<ScrollState*>(engine->stateForScreen(screenId));
        return state ? state->strip().activeWindowId() : QString();
    }
};

void TestScrollEngineGrouping::sameAppOpenJoinsItsColumnAsATab()
{
    // Global ON. A second window of an app already on the strip joins that
    // app's column as a tab, turning a Normal column tabbed on the way; a
    // window of a different app still takes a column of its own. When two
    // columns hold the app, the ACTIVE one wins, so the arrival lands where
    // the user is working.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->groupSameAppAsTabs = true;
    ScrollEngine* engine = makeEngine(&owner, settings);

    QSignalSpy strips(engine, &ScrollEngine::tabStripsChanged);
    engine->windowOpened(QStringLiteral("firefox|1"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("kate|1"), kS1, 0, 0);
    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    QCOMPARE(state->strip().columnCount(), 2);
    QCOMPARE(state->strip().columns().at(0).display, ColumnDisplay::Normal);
    // No tabbed column yet, so no tab strip has been announced.
    QVERIFY(strips.isEmpty());

    // firefox|1 sits in column 0; kate is active (pinned, because the leg
    // below only discriminates active-first from scan-first when the active
    // column is NOT the firefox one). The firefox arrival must find its
    // sibling's column rather than the active one.
    QCOMPARE(activeWindowOn(engine, kS1), QStringLiteral("kate|1"));
    engine->windowOpened(QStringLiteral("firefox|2"), kS1, 0, 0);
    QCOMPARE(state->strip().columnCount(), 2);
    QCOMPARE(columnOfOrFail(state, QStringLiteral("firefox|2")), 0);
    const Column& firefoxCol = state->strip().columns().at(0);
    QCOMPARE(firefoxCol.display, ColumnDisplay::Tabbed);
    QCOMPARE(firefoxCol.tiles.size(), 2);
    // The join turned the column tabbed BEFORE the tile joined, so the tab on
    // show at that moment (firefox|1) owns the extent, not the arrival.
    QCOMPARE(firefoxCol.heightOwnerId, QStringLiteral("firefox|1"));
    // The arrival is the tab on show and the strip's focus.
    QCOMPARE(activeWindowOn(engine, kS1), QStringLiteral("firefox|2"));
    // A tabbed column resolves exactly one visible tile, so the strip still
    // shows two rects.
    QCOMPARE(engine->visibleTileRects(kS1).size(), 2);
    // Side effect: the Normal-to-Tabbed flip reached the tab-strip payload,
    // and the payload names the freshly tabbed column.
    QVERIFY2(!strips.isEmpty(), "a column turned tabbed by a grouped join must announce its indicator");
    const auto stripCount = [](const QSignalSpy& spy, int index) {
        return QJsonDocument::fromJson(spy.at(index).at(1).toString().toUtf8()).array().size();
    };
    QVERIFY(stripCount(strips, strips.count() - 1) > 0);

    // A MIXED host: hand kate's column a firefox tab through the strip verb
    // (a disengaged display override leaves it a Normal stack of kate|1 and
    // firefox|3, and the insert itself makes that column active). The next
    // firefox open joins the ACTIVE column, converting the mixed stack to
    // tabs with kate|1 now a hidden tab of it; scan-first would have landed
    // firefox|4 in column 0 instead.
    QVERIFY(
        state->strip().insertWindowIntoColumnAt(1, 1, QStringLiteral("firefox|3"), ScrollTestUtils::engineParams()));
    QCOMPARE(columnOfOrFail(state, QStringLiteral("firefox|3")), 1);
    QCOMPARE(state->strip().columns().at(1).display, ColumnDisplay::Normal);
    QCOMPARE(state->strip().activeColumnIndex(), 1);
    engine->windowOpened(QStringLiteral("firefox|4"), kS1, 0, 0);
    QCOMPARE(state->strip().columnCount(), 2);
    QCOMPARE(columnOfOrFail(state, QStringLiteral("firefox|4")), 1);
    QCOMPARE(state->strip().columns().at(1).display, ColumnDisplay::Tabbed);
    QCOMPARE(columnOfOrFail(state, QStringLiteral("kate|1")), 1);
    QCOMPARE(state->strip().columns().at(1).tiles.size(), 3);

    // Global OFF: the same shape opens a column of its own, and both
    // existing tabbed columns are left exactly as they were. The stub field
    // is flipped WITHOUT refreshConfigFromSettings on purpose: the engine
    // reads this toggle live on every open, and a refresh here would keep
    // the leg green while silently un-pinning that liveness.
    settings->groupSameAppAsTabs = false;
    engine->windowOpened(QStringLiteral("firefox|5"), kS1, 0, 0);
    QCOMPARE(state->strip().columnCount(), 3);
    const int ownCol = columnOfOrFail(state, QStringLiteral("firefox|5"));
    QVERIFY(ownCol >= 0);
    QCOMPARE(state->strip().columns().at(ownCol).tiles.size(), 1);
    QCOMPARE(state->strip().columns().at(0).display, ColumnDisplay::Tabbed);
    QCOMPARE(state->strip().columns().at(0).tiles.size(), 2);
    QCOMPARE(state->strip().columns().at(1).display, ColumnDisplay::Tabbed);
    QCOMPARE(state->strip().columns().at(1).tiles.size(), 3);
    // And back ON, still without a refresh: the next open groups again.
    settings->groupSameAppAsTabs = true;
    engine->windowOpened(QStringLiteral("firefox|6"), kS1, 0, 0);
    QCOMPARE(state->strip().columnCount(), 3);
    QVERIFY(columnOfOrFail(state, QStringLiteral("firefox|6")) >= 0);
}

void TestScrollEngineGrouping::sameAppGroupingYieldsToRulesAndUnstableIds()
{
    // The grouping is a config-wide DEFAULT, so a per-window rule outranks
    // it: an openColumnPlacement=consume rule joins the ACTIVE column even
    // when a same-app column exists elsewhere. Floated windows are not
    // columns to join. Two legs here are GUARDS against an over-eager
    // implementation rather than pins of the feature: the consume leg
    // exercises the pre-existing consume arm, and the bare-id leg pins only
    // that whole-id appIds never collide (a bare id is its own appId through
    // this engine's currentAppIdFor, so "bare" and "bare2" cannot match each
    // other with or without the stable-id gate). Both pass with the feature
    // removed, by design.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->groupSameAppAsTabs = true;
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->windowOpened(QStringLiteral("firefox|1"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("kate|1"), kS1, 0, 0);
    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    QCOMPARE(activeWindowOn(engine, kS1), QStringLiteral("kate|1"));

    engine->setOpenParamsResolver([](const QString& windowId, const QString&) {
        ScrollOpenParams params;
        if (windowId == QStringLiteral("firefox|ruled")) {
            params.consume = true;
        }
        return params;
    });
    engine->windowOpened(QStringLiteral("firefox|ruled"), kS1, 0, 0);
    QCOMPARE(state->strip().columnCount(), 2);
    // Consumed into kate's column (the active one), not firefox's.
    const int kateCol = columnOfOrFail(state, QStringLiteral("kate|1"));
    QVERIFY(kateCol >= 0);
    QCOMPARE(columnOfOrFail(state, QStringLiteral("firefox|ruled")), kateCol);
    engine->setOpenParamsResolver({});

    // Bare ids (no separator): each is its own appId, so two of them never
    // group and each opens a column of its own.
    engine->windowOpened(QStringLiteral("bare"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("bare2"), kS1, 0, 0);
    QCOMPARE(state->strip().columnCount(), 4);

    // A floated same-app window is not a column: the next firefox open joins
    // the firefox COLUMN, never the float.
    engine->setWindowFloat(QStringLiteral("firefox|ruled"), true, kS1);
    QVERIFY(state->isFloating(QStringLiteral("firefox|ruled")));
    engine->windowOpened(QStringLiteral("firefox|2"), kS1, 0, 0);
    const int firefoxCol = columnOfOrFail(state, QStringLiteral("firefox|1"));
    QVERIFY(firefoxCol >= 0);
    QCOMPARE(columnOfOrFail(state, QStringLiteral("firefox|2")), firefoxCol);

    // A NAMED tab group (openTabGroup rule) keys on the name, not the app:
    // windows of different apps whose rules resolve to one name share a
    // column, a window with a name never falls back to app grouping (a kate
    // window named "work" opens beside kate|1's column, not into it, even
    // with app grouping ON and no "work" column to join yet), and the rule
    // outranks an OFF global.
    engine->setOpenParamsResolver([](const QString& windowId, const QString&) {
        ScrollOpenParams params;
        if (PhosphorIdentity::WindowId::extractInstanceId(windowId).startsWith(QLatin1String("work"))) {
            params.tabGroup = QStringLiteral("work");
        }
        return params;
    });
    settings->groupSameAppAsTabs = true;
    const int before = state->strip().columnCount();
    engine->windowOpened(QStringLiteral("kate|work1"), kS1, 0, 0);
    // No "work" column yet: its own column, NOT kate|1's (a fallback into
    // app grouping would have joined kate|1 and left the count unchanged).
    // Looked up fresh: the new column was inserted right of the active one,
    // so the index captured earlier no longer names kate|1's column.
    QCOMPARE(state->strip().columnCount(), before + 1);
    QVERIFY(columnOfOrFail(state, QStringLiteral("kate|work1")) != columnOfOrFail(state, QStringLiteral("kate|1")));
    settings->groupSameAppAsTabs = false;
    engine->windowOpened(QStringLiteral("konsole|work2"), kS1, 0, 0);
    QCOMPARE(state->strip().columnCount(), before + 1);
    const int workCol = columnOfOrFail(state, QStringLiteral("kate|work1"));
    QVERIFY(workCol >= 0);
    QCOMPARE(columnOfOrFail(state, QStringLiteral("konsole|work2")), workCol);
    QCOMPARE(state->strip().columns().at(workCol).display, ColumnDisplay::Tabbed);
    // Named beats app: even with app grouping back ON, the firefox window
    // named "work" joins the work column rather than firefox|1's.
    settings->groupSameAppAsTabs = true;
    engine->windowOpened(QStringLiteral("firefox|work3"), kS1, 0, 0);
    QCOMPARE(columnOfOrFail(state, QStringLiteral("firefox|work3")), workCol);
    // And an unnamed same-app window still groups by app, not into "work".
    engine->windowOpened(QStringLiteral("kate|2"), kS1, 0, 0);
    const int kate2Col = columnOfOrFail(state, QStringLiteral("kate|2"));
    QVERIFY(kate2Col >= 0);
    QCOMPARE(kate2Col, columnOfOrFail(state, QStringLiteral("kate|1")));
    QVERIFY(kate2Col != workCol);
    // A padded name is the same group: the engine trims before comparing.
    engine->setOpenParamsResolver([](const QString& windowId, const QString&) {
        ScrollOpenParams params;
        if (windowId == QStringLiteral("konsole|padded")) {
            params.tabGroup = QStringLiteral("  work ");
        } else if (PhosphorIdentity::WindowId::extractInstanceId(windowId).startsWith(QLatin1String("work"))) {
            params.tabGroup = QStringLiteral("work");
        }
        return params;
    });
    engine->windowOpened(QStringLiteral("konsole|padded"), kS1, 0, 0);
    QCOMPARE(columnOfOrFail(state, QStringLiteral("konsole|padded")), workCol);
}

void TestScrollEngineGrouping::openTabbedFalseOptsOutOfAppGrouping()
{
    // The per-window rule outranks the global default (the settings card
    // promises exactly that): a window whose rules say openTabbed=false is
    // opting out of tabs, so the same-app default must not force it into a
    // tabbed column. A NAMED group is different: the rule itself asks for a
    // tab, so it still joins.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->groupSameAppAsTabs = true;
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->windowOpened(QStringLiteral("firefox|1"), kS1, 0, 0);
    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    engine->setOpenParamsResolver([](const QString& windowId, const QString&) {
        ScrollOpenParams params;
        if (windowId == QStringLiteral("firefox|solo")) {
            params.tabbed = false;
        } else if (windowId == QStringLiteral("firefox|named")) {
            params.tabbed = false;
            params.tabGroup = QStringLiteral("work");
        } else if (windowId == QStringLiteral("kate|named")) {
            params.tabGroup = QStringLiteral("work");
        }
        return params;
    });
    engine->windowOpened(QStringLiteral("firefox|solo"), kS1, 0, 0);
    QCOMPARE(state->strip().columnCount(), 2);
    const int soloCol = columnOfOrFail(state, QStringLiteral("firefox|solo"));
    QVERIFY(soloCol >= 0);
    QCOMPARE(state->strip().columns().at(soloCol).tiles.size(), 1);
    QCOMPARE(state->strip().columns().at(soloCol).display, ColumnDisplay::Normal);
    // Column 0 (firefox|1) was left untouched: still a lone Normal column.
    QCOMPARE(state->strip().columns().at(0).tiles.size(), 1);
    QCOMPARE(state->strip().columns().at(0).display, ColumnDisplay::Normal);

    // Control: the same window WITHOUT the opt-out does group, so the leg
    // above pins the rule and not an inert setting.
    engine->windowOpened(QStringLiteral("firefox|2"), kS1, 0, 0);
    QCOMPARE(state->strip().columnCount(), 2);
    QVERIFY(columnOfOrFail(state, QStringLiteral("firefox|2")) >= 0);

    // Named group with openTabbed=false: the name wins and the join happens.
    engine->windowOpened(QStringLiteral("kate|named"), kS1, 0, 0);
    const int workCol = columnOfOrFail(state, QStringLiteral("kate|named"));
    QVERIFY(workCol >= 0);
    engine->windowOpened(QStringLiteral("firefox|named"), kS1, 0, 0);
    QCOMPARE(columnOfOrFail(state, QStringLiteral("firefox|named")), workCol);
    QCOMPARE(state->strip().columns().at(workCol).display, ColumnDisplay::Tabbed);
}

void TestScrollEngineGrouping::groupedJoinUnderFocusNewOffKeepsTheHostsShownTab()
{
    // Focus-new-windows OFF and a grouped arrival into a BACKGROUND column:
    // the strip's active column must stay where the user is working AND
    // the host column must keep showing the tab it was showing. Without the
    // second half the firefox column behind the user would flip to the new
    // firefox window, the very disturbance the OFF setting exists to
    // prevent.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->groupSameAppAsTabs = true;
    settings->focusNewWindows = true;
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->windowOpened(QStringLiteral("firefox|1"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("firefox|2"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("kate|1"), kS1, 0, 0);
    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    QCOMPARE(state->strip().columnCount(), 2);
    const int firefoxCol = columnOfOrFail(state, QStringLiteral("firefox|1"));
    QVERIFY(firefoxCol >= 0);
    QCOMPARE(state->strip().columns().at(firefoxCol).display, ColumnDisplay::Tabbed);
    // Show firefox|1 in the tabbed column, then go back to kate.
    engine->windowFocused(QStringLiteral("firefox|1"), kS1);
    engine->windowFocused(QStringLiteral("kate|1"), kS1);
    QCOMPARE(activeWindowOn(engine, kS1), QStringLiteral("kate|1"));
    const Column& hostBefore = state->strip().columns().at(firefoxCol);
    QCOMPARE(hostBefore.tiles.at(hostBefore.activeTileIdx).windowId, QStringLiteral("firefox|1"));

    settings->focusNewWindows = false;
    engine->windowOpened(QStringLiteral("firefox|3"), kS1, 0, 0);
    QCOMPARE(columnOfOrFail(state, QStringLiteral("firefox|3")), firefoxCol);
    // The user's column is still the active one.
    QCOMPARE(activeWindowOn(engine, kS1), QStringLiteral("kate|1"));
    // And the host still shows the tab it showed before the arrival.
    const Column& hostAfter = state->strip().columns().at(firefoxCol);
    QCOMPARE(hostAfter.tiles.size(), 3);
    QCOMPARE(hostAfter.tiles.at(hostAfter.activeTileIdx).windowId, QStringLiteral("firefox|1"));

    // Control: with focus-new-windows ON the arrival IS the shown tab and
    // the focus, so the rewind above is what the assertions pinned.
    settings->focusNewWindows = true;
    engine->windowOpened(QStringLiteral("firefox|4"), kS1, 0, 0);
    QCOMPARE(activeWindowOn(engine, kS1), QStringLiteral("firefox|4"));
}

void TestScrollEngineGrouping::groupingSkipsMigrationsUnfloatsAndOutrankedArms()
{
    // Four paths that reach the open path with a same-app column present
    // and must NOT group: a migration (a move, not an open), an unfloat
    // (never an open), a mode-round-trip stash restore and a
    // mode-transition order seed (both outrank a grouping verdict).
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->groupSameAppAsTabs = true;
    ScrollEngine* engine = makeEngine(&owner, settings);

    // MIGRATION: firefox|m opens on S1, S2 already holds a firefox column;
    // moving it to S2 must give it a column of its own there.
    engine->windowOpened(QStringLiteral("firefox|1"), kS2, 0, 0);
    engine->windowOpened(QStringLiteral("firefox|m"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("firefox|m"), kS2, 0, 0); // same id, new screen
    auto* s2 = static_cast<ScrollState*>(engine->stateForScreen(kS2));
    QVERIFY(s2);
    QCOMPARE(s2->strip().columnCount(), 2);
    QVERIFY(columnOfOrFail(s2, QStringLiteral("firefox|m")) != columnOfOrFail(s2, QStringLiteral("firefox|1")));
    QCOMPARE(s2->strip().columns().at(0).display, ColumnDisplay::Normal);
    QCOMPARE(s2->strip().columns().at(1).display, ColumnDisplay::Normal);
    // Control on the same screen: a genuinely fresh open does group.
    engine->windowOpened(QStringLiteral("firefox|2"), kS2, 0, 0);
    QCOMPARE(s2->strip().columnCount(), 2);

    // UNFLOAT: a window floated AT OPEN by rule, re-tiled by the user, opens
    // its own column even beside a same-app column.
    engine->setFloatPredicate([](const QString& windowId, const QString&) {
        return windowId == QStringLiteral("firefox|floater");
    });
    engine->windowOpened(QStringLiteral("firefox|floater"), kS2, 0, 0);
    QVERIFY(s2->isFloating(QStringLiteral("firefox|floater")));
    engine->setFloatPredicate({});
    engine->windowFocused(QStringLiteral("firefox|floater"), kS2);
    engine->moveFocusedToTiling(kS2);
    QVERIFY(s2->strip().containsWindow(QStringLiteral("firefox|floater")));
    QCOMPARE(s2->strip().columnCount(), 3);
    const int floaterCol = columnOfOrFail(s2, QStringLiteral("firefox|floater"));
    QVERIFY(floaterCol >= 0);
    QCOMPARE(s2->strip().columns().at(floaterCol).tiles.size(), 1);

    // ORDER SEED: S1 holds firefox|m's old slot no more (it migrated), so
    // build a fresh S1 strip from a seed that puts firefox|b FIRST, then
    // open firefox|a and firefox|b in the other order. The seed outranks
    // grouping, so firefox|b takes column 0 of its own rather than joining
    // firefox|a as a tab.
    engine->setInitialWindowOrder(kS1, {QStringLiteral("firefox|b"), QStringLiteral("firefox|a")});
    engine->windowOpened(QStringLiteral("firefox|a"), kS1, 0, 0);
    auto* s1 = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(s1);
    engine->windowOpened(QStringLiteral("firefox|b"), kS1, 0, 0);
    QCOMPARE(s1->strip().columnCount(), 2);
    QCOMPARE(columnOfOrFail(s1, QStringLiteral("firefox|b")), 0);
    QCOMPARE(columnOfOrFail(s1, QStringLiteral("firefox|a")), 1);
    QCOMPARE(s1->strip().columns().at(0).display, ColumnDisplay::Normal);
    QCOMPARE(s1->strip().columns().at(1).display, ColumnDisplay::Normal);

    // STASH: a mode round trip stashes S1's two-column shape; on the way
    // back each firefox window returns to ITS stashed column rather than
    // being grouped into whichever firefox column arrived first.
    engine->setActiveScreens({kS2});
    QVERIFY(!engine->isWindowTracked(QStringLiteral("firefox|a")));
    engine->setActiveScreens({kS1, kS2});
    engine->windowOpened(QStringLiteral("firefox|b"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("firefox|a"), kS1, 0, 0);
    s1 = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(s1);
    QCOMPARE(s1->strip().columnCount(), 2);
    QVERIFY(columnOfOrFail(s1, QStringLiteral("firefox|a")) != columnOfOrFail(s1, QStringLiteral("firefox|b")));
    QCOMPARE(s1->strip().columns().at(0).display, ColumnDisplay::Normal);
    QCOMPARE(s1->strip().columns().at(1).display, ColumnDisplay::Normal);
}

void TestScrollEngineGrouping::groupingSkipsDraggedAndMinimizedHosts()
{
    // Two hosts the scan must not offer: a column holding the window under a
    // plain interactive move (its rect is frozen; the join would make it a
    // hidden tab under the arrival), and a column whose only same-app tile
    // is minimized (applyColumnDisplay would name that tile the tabbed
    // extent owner and relayout could never resolve it).
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->groupSameAppAsTabs = true;
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->windowOpened(QStringLiteral("firefox|1"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("kate|1"), kS1, 0, 0);
    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    QCOMPARE(state->strip().columnCount(), 2);

    // Drag: firefox|1 is under a plain move. The arrival opens its own column.
    engine->setInteractiveDragWindow(QStringLiteral("firefox|1"));
    engine->windowOpened(QStringLiteral("firefox|2"), kS1, 0, 0);
    QCOMPARE(state->strip().columnCount(), 3);
    const int draggedCol = columnOfOrFail(state, QStringLiteral("firefox|1"));
    QVERIFY(draggedCol >= 0);
    QCOMPARE(state->strip().columns().at(draggedCol).display, ColumnDisplay::Normal);
    engine->setInteractiveDragWindow(QString());
    // Control: with the drag over, the next open has two firefox columns to
    // choose from and joins one.
    engine->windowOpened(QStringLiteral("firefox|3"), kS1, 0, 0);
    QCOMPARE(state->strip().columnCount(), 3);

    // Minimized: a strip whose only konsole tile is minimized is not a host.
    engine->windowOpened(QStringLiteral("konsole|1"), kS1, 0, 0);
    QVERIFY(state->strip().setWindowMinimized(QStringLiteral("konsole|1"), true, ScrollTestUtils::engineParams()));
    const int before = state->strip().columnCount();
    engine->windowOpened(QStringLiteral("konsole|2"), kS1, 0, 0);
    QCOMPARE(state->strip().columnCount(), before + 1);
    const int k2 = columnOfOrFail(state, QStringLiteral("konsole|2"));
    QVERIFY(k2 >= 0);
    QCOMPARE(state->strip().columns().at(k2).tiles.size(), 1);
    QVERIFY(k2 != columnOfOrFail(state, QStringLiteral("konsole|1")));
}

QTEST_GUILESS_MAIN(TestScrollEngineGrouping)
#include "test_scrollengine_grouping.moc"

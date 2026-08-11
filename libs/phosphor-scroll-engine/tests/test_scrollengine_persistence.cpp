// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Strip-structure persistence: the mode-round-trip stash's focus/anchor
// carry, and the serialize/restore blob a login restore rides — including
// the appId claim that survives cross-session window-uuid drift. Split from
// test_scrollengine_smoke.cpp (file-size ceiling); same headless setup.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include "scrollstriptestutils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>
#include <QtTest>

using namespace PhosphorScrollEngine;

using ScrollTestUtils::makeProviderEngine;

class TestScrollEnginePersistence : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void modeRoundTripRestoresFocusAndAnchor();
    void presetIntentRoundTripsExactly();
    void stashedShapeOutranksTheOpenHeightRule();
    void legacyPresetIndexBlobResolvesAgainstEffectiveList();
    void outOfRangePresetFractionIsClampedAtTheBoundary();
    void serializedStripRestoreSurvivesIdDrift();
    void arrivalBurstRestoreAppliesOnce();
    void pruneSweepsStashedTilesForClosedWindows();
    void pruneSpareStashStagedFromPersistence();
    void unclaimedStashTilesExpireAfterThreeSessions();
    void coTenantClaimDoesNotRenewSiblingLease();
    void serializeKeepsAnUnclaimedStashTileBesideALiveStrip();
    void windowedFullscreenSurvivesSerializeRestore();
    void windowedFullscreenTogglesEmitAndFloatClears();
    void windowedFullscreenHiddenTabStillEmitsFlag();
    void windowedFullscreenMinimizeDropsModeKeeps();
    void windowedFullscreenFuzzyClaimDoesNotTransfer();
    void restoreDropsMalformedKeysAndBoundsAnchor();
    void restoreStagesADuplicateWindowIdOnlyOnce();
    void restoreFocusFallsBackToASurvivingTile();
    void restoreCapsTilesPerColumn();
    void backgroundContextClearAndReapplySkipRelayout();

private:
    /// The state for a screen, or nullptr. QVERIFY'd at every call site: a
    /// regression that drops the state would otherwise segfault the binary
    /// and take the remaining tests' results with it.
    static ScrollState* stateFor(ScrollEngine* engine, const QString& screenId)
    {
        return static_cast<ScrollState*>(engine->stateForScreen(screenId));
    }

    /// The column index @p windowId holds on S1, asserted to be a REAL slot
    /// first: columnIndexForWindow answers -1 for an untracked window, and a
    /// bare `!=` between two -1s is a comparison that passes for the wrong
    /// reason on exactly the regression these tests exist to catch.
    static int columnOf(ScrollEngine* engine, const QString& windowId)
    {
        const int col = engine->columnIndexForWindow(QStringLiteral("S1"), windowId);
        [&]() {
            QVERIFY2(col >= 0, qPrintable(QStringLiteral("%1 holds no column").arg(windowId)));
        }();
        return col;
    }
};

void TestScrollEnginePersistence::modeRoundTripRestoresFocusAndAnchor()
{
    // The structural stash alone rebuilt stacks and widths but was
    // focus-blind: the first arrival won the focus on the empty strip, so
    // every mode round trip re-anchored the strip on an arbitrary window.
    // The stash now carries the focused window and the view anchor.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1")); // b joins a's stack
    engine->toggleColumnTabbed(QStringLiteral("S1"));
    engine->windowFocused(QStringLiteral("app|c"), QStringLiteral("S1"));
    engine->centerColumn(QStringLiteral("S1"));

    ScrollState* before = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(before);
    QCOMPARE(before->strip().activeWindowId(), QStringLiteral("app|c"));
    const int anchorBefore = before->strip().viewAnchor();
    // The anchor has to be NON-default for the carry assertion at the tail to
    // mean anything: 0 is what a strip that never carried an anchor comes back
    // with, so centering above must genuinely have moved it.
    QVERIFY2(anchorBefore != 0, "the centered anchor must differ from a fresh strip's, or the carry is unfalsifiable");

    // Mode reassignment of the SAME context: teardown stashes the strip.
    engine->setActiveScreens({});
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|c")));

    // Cycle back; the FOCUSED window arrives last so its focus/anchor
    // restore lands on the fully rebuilt strip (exact-anchor assertion).
    engine->setActiveScreens({QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);

    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|a")), 0);
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|b")), 0);
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|c")), 1);
    ScrollState* after = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(after);
    QCOMPARE(after->strip().columns().at(0).display, ColumnDisplay::Tabbed);
    QCOMPARE(after->strip().activeWindowId(), QStringLiteral("app|c"));
    QCOMPARE(after->strip().viewAnchor(), anchorBefore);
}

void TestScrollEnginePersistence::presetIntentRoundTripsExactly()
{
    // Width/height blob coverage (previously unpinned by any test): a
    // value-anchored Preset intent survives serialize → restore → claim
    // BYTE-EXACT, and a Fixed height rides beside it untouched.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|u1"), QStringLiteral("S1"), 0, 0);
    ScrollState* live = stateFor(engine1, QStringLiteral("S1"));
    QVERIFY(live);
    QVERIFY(live->strip().setActiveColumnWidth(ColumnWidth::makePreset(0.42)));
    QVERIFY(live->strip().setActiveWindowHeight(WindowHeight::makeFixed(333)));

    const QJsonObject blob = engine1->serializeStripState();
    QVERIFY(!blob.isEmpty());

    ScrollEngine* engine2 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine2->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine2->restoreStripState(blob);
    engine2->windowOpened(QStringLiteral("app|n1"), QStringLiteral("S1"), 0, 0);

    ScrollState* state = stateFor(engine2, QStringLiteral("S1"));
    QVERIFY(state);
    QCOMPARE(state->strip().columns().size(), 1);
    const Column& col = state->strip().columns().first();
    QCOMPARE(col.width.kind, ColumnWidth::Preset);
    QCOMPARE(col.width.presetFraction, 0.42);
    QCOMPARE(col.tiles.first().height.kind, WindowHeight::Fixed);
    QCOMPARE(col.tiles.first().height.fixedPx, 333);
}

void TestScrollEnginePersistence::stashedShapeOutranksTheOpenHeightRule()
{
    // Precedence between the stash and the per-window open rules, on BOTH
    // axes. A stash restore rebuilds the shape the user left the strip in,
    // and the width verdicts are structurally dropped on that arm (the
    // restore inserts with the stashed width, never the resolved one), so
    // the height must not be the single axis that overrides it — that
    // combination returns a window at its old width and its rule height,
    // a shape the user never had.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|u1"), QStringLiteral("S1"), 0, 0);
    ScrollState* live = stateFor(engine1, QStringLiteral("S1"));
    QVERIFY(live);
    QVERIFY(live->strip().setActiveColumnWidth(ColumnWidth::makeProportion(0.42)));
    QVERIFY(live->strip().setActiveWindowHeight(WindowHeight::makeFixed(333)));
    const QJsonObject blob = engine1->serializeStripState();
    QVERIFY(!blob.isEmpty());

    ScrollEngine* engine2 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine2->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine2->restoreStripState(blob);
    // Rules that would reshape BOTH axes if they reached the restored tile.
    engine2->setOpenParamsResolver([](const QString&, const QString&) {
        ScrollOpenParams params;
        params.widthFraction = 0.9;
        params.heightFraction = 0.9;
        return params;
    });
    engine2->windowOpened(QStringLiteral("app|n1"), QStringLiteral("S1"), 0, 0);

    ScrollState* state = stateFor(engine2, QStringLiteral("S1"));
    QVERIFY(state);
    QCOMPARE(state->strip().columns().size(), 1);
    const Column& claimed = state->strip().columns().first();
    QCOMPARE(claimed.width.kind, ColumnWidth::Proportion);
    QCOMPARE(claimed.width.proportion, 0.42);
    QCOMPARE(claimed.tiles.first().height.kind, WindowHeight::Fixed);
    QCOMPARE(claimed.tiles.first().height.fixedPx, 333);

    // Control: with every stashed tile consumed, the SAME rules do land on
    // the next arrival — so the assertions above pin the stash's precedence
    // and not an inert resolver.
    engine2->windowOpened(QStringLiteral("app|n2"), QStringLiteral("S1"), 0, 0);
    const int freshCol = columnOf(engine2, QStringLiteral("app|n2"));
    const Column& fresh = state->strip().columns().at(freshCol);
    QCOMPARE(fresh.width.kind, ColumnWidth::Proportion);
    QCOMPARE(fresh.width.proportion, 0.9);
    QCOMPARE(fresh.tiles.first().height.kind, WindowHeight::Fixed);
    QCOMPARE(fresh.tiles.first().height.fixedPx, qRound(0.9 * ScrollTestUtils::kScreenHeight));
}

void TestScrollEnginePersistence::legacyPresetIndexBlobResolvesAgainstEffectiveList()
{
    // A pre-value-anchor blob carries "presetIdx" and no "presetFraction":
    // the claim-site fixup resolves the index against the restoring screen's
    // EFFECTIVE preset list. Build the legacy shape by hand (no code writes
    // it anymore) inside an otherwise-modern blob.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|u1"), QStringLiteral("S1"), 0, 0);
    QJsonObject blob = engine1->serializeStripState();
    QVERIFY(!blob.isEmpty());

    // Rewrite the one column's width to the legacy index form (index 2 of
    // the default 1/3, 1/2, 2/3 vocabulary).
    const QString key = blob.keys().first();
    QJsonObject strip = blob.value(key).toObject();
    QJsonArray columns = strip.value(QLatin1String("columns")).toArray();
    QJsonObject colObj = columns.first().toObject();
    QJsonObject widthObj;
    widthObj.insert(QLatin1String("kind"), static_cast<int>(ColumnWidth::Preset));
    widthObj.insert(QLatin1String("presetIdx"), 2);
    colObj.insert(QLatin1String("width"), widthObj);
    columns.replace(0, colObj);
    strip.insert(QLatin1String("columns"), columns);
    blob.insert(key, strip);

    ScrollEngine* engine2 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine2->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    // A template vocabulary that is nothing like the settings one, installed
    // BEFORE the restore: the fixup must resolve index 2 against THIS list.
    // Asserting the literal rather than re-reading the engine's own effective
    // list is the point — the old form compared the implementation against
    // itself and passed for any vocabulary the fixup happened to pick.
    // applyPerScreenConfig stores the map synchronously (only the retile it
    // schedules is queued), so no event drain is needed for the restore below
    // to see it.
    QVariantMap templ;
    templ.insert(ScrollPerScreenKeys::presetColumnWidths(), QVariantList{0.2, 0.4, 0.9});
    engine2->applyPerScreenConfig(QStringLiteral("S1"), templ);
    engine2->restoreStripState(blob);
    engine2->windowOpened(QStringLiteral("app|n1"), QStringLiteral("S1"), 0, 0);

    ScrollState* state = stateFor(engine2, QStringLiteral("S1"));
    QVERIFY(state);
    QCOMPARE(state->strip().columns().size(), 1);
    const ColumnWidth width = state->strip().columns().first().width;
    QCOMPARE(width.kind, ColumnWidth::Preset);
    QCOMPARE(width.presetFraction, 0.9);
}

void TestScrollEnginePersistence::outOfRangePresetFractionIsClampedAtTheBoundary()
{
    // Persisted config is user-writable: a hand-edited fraction outside
    // [0.05, 1.0] is bounded at the read, like every other numeric in the
    // blob.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|u1"), QStringLiteral("S1"), 0, 0);
    QJsonObject blob = engine1->serializeStripState();

    const QString key = blob.keys().first();
    QJsonObject strip = blob.value(key).toObject();
    QJsonArray columns = strip.value(QLatin1String("columns")).toArray();
    QJsonObject colObj = columns.first().toObject();
    QJsonObject widthObj;
    widthObj.insert(QLatin1String("kind"), static_cast<int>(ColumnWidth::Preset));
    widthObj.insert(QLatin1String("presetFraction"), 47.0);
    colObj.insert(QLatin1String("width"), widthObj);
    columns.replace(0, colObj);
    strip.insert(QLatin1String("columns"), columns);
    blob.insert(key, strip);

    ScrollEngine* engine2 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine2->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine2->restoreStripState(blob);
    engine2->windowOpened(QStringLiteral("app|n1"), QStringLiteral("S1"), 0, 0);

    ScrollState* state = stateFor(engine2, QStringLiteral("S1"));
    QVERIFY(state);
    const ColumnWidth width = state->strip().columns().first().width;
    QCOMPARE(width.kind, ColumnWidth::Preset);
    QCOMPARE(width.presetFraction, 1.0);
}

void TestScrollEnginePersistence::serializedStripRestoreSurvivesIdDrift()
{
    // Login restore: serialize a live strip, feed it to a FRESH engine, and
    // re-announce the windows with DIFFERENT uuids (same appId prefix) —
    // the drift a real restart produces. The tabbed two-window column, the
    // focus, and the structure must rebuild via the appId claim.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|u1"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("app|u2"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("other|u3"), QStringLiteral("S1"), 0, 0);
    engine1->windowFocused(QStringLiteral("app|u1"), QStringLiteral("S1"));
    engine1->consumeWindowIntoColumn(QStringLiteral("S1")); // u2 joins u1's stack
    engine1->toggleColumnTabbed(QStringLiteral("S1"));
    // Show the SECOND tab before focusing away: the shown tab is per-column
    // persisted state (StashedColumn::activeWindowId) and must survive the
    // round trip independently of the strip-level focus.
    engine1->windowFocused(QStringLiteral("app|u2"), QStringLiteral("S1"));
    engine1->windowFocused(QStringLiteral("other|u3"), QStringLiteral("S1"));
    const QJsonObject blob = engine1->serializeStripState();
    QVERIFY(!blob.isEmpty());

    ScrollEngine* engine2 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine2->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine2->restoreStripState(blob);
    // New-session uuids; the stashed tiles are claimed one-to-one in
    // arrival order per app: n1 claims u1's slot, n2 claims u2's.
    engine2->windowOpened(QStringLiteral("app|n1"), QStringLiteral("S1"), 0, 0);
    engine2->windowOpened(QStringLiteral("app|n2"), QStringLiteral("S1"), 0, 0);
    engine2->windowOpened(QStringLiteral("other|n3"), QStringLiteral("S1"), 0, 0);

    QCOMPARE(engine2->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|n1")), 0);
    QCOMPARE(engine2->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|n2")), 0);
    QCOMPARE(engine2->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("other|n3")), 1);
    ScrollState* state = stateFor(engine2, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY(!state->strip().columns().isEmpty());
    QCOMPARE(state->strip().columns().at(0).display, ColumnDisplay::Tabbed);
    QCOMPARE(state->strip().columns().at(0).tiles.size(), 2);
    // The shown TAB followed its claimed successor too: u2 was the visible
    // tab at serialize time, so n2 must be the visible tab after restore.
    const Column& tabbed = state->strip().columns().at(0);
    // Bounds-checked before the indexed read: activeTileIdx is model state,
    // and a regression that leaves it out of range would abort the whole
    // binary here instead of failing one assertion.
    QVERIFY(tabbed.activeTileIdx >= 0 && tabbed.activeTileIdx < tabbed.tiles.size());
    QCOMPARE(tabbed.tiles.at(tabbed.activeTileIdx).windowId, QStringLiteral("app|n2"));
    // The stashed focus (other|u3) followed its claimed successor.
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("other|n3"));
}

void TestScrollEnginePersistence::arrivalBurstRestoreAppliesOnce()
{
    // Daemon-restart re-announce: the adaptor brackets the whole open batch
    // with begin/endArrivalBurst, and the engine must resolve the restored
    // strip in ONE geometry batch — the per-arrival applies otherwise march
    // every already-placed window through partial-strip intermediates the
    // user sees as a restore-time retile even when nothing changed.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|u1"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("app|u2"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("other|u3"), QStringLiteral("S1"), 0, 0);
    const QRect r1 = engine1->lastManagedRect(QStringLiteral("app|u1"));
    const QRect r2 = engine1->lastManagedRect(QStringLiteral("app|u2"));
    const QRect r3 = engine1->lastManagedRect(QStringLiteral("other|u3"));
    QVERIFY(r1.isValid() && r2.isValid() && r3.isValid());
    const QJsonObject blob = engine1->serializeStripState();
    QVERIFY(!blob.isEmpty());

    // Fresh engine, same window ids (same-session daemon reload — KWin kept
    // the windows, only the daemon restarted).
    ScrollEngine* engine2 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine2->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine2->restoreStripState(blob);
    QSignalSpy tiledSpy(engine2, &ScrollEngine::windowsTiled);
    engine2->beginArrivalBurst();
    engine2->windowOpened(QStringLiteral("app|u1"), QStringLiteral("S1"), 0, 0);
    engine2->windowOpened(QStringLiteral("app|u2"), QStringLiteral("S1"), 0, 0);
    engine2->windowOpened(QStringLiteral("other|u3"), QStringLiteral("S1"), 0, 0);
    // Model state is fully live during the burst; only geometry is deferred.
    QCOMPARE(tiledSpy.count(), 0);
    engine2->endArrivalBurst();
    QCOMPARE(tiledSpy.count(), 1);

    // The one batch resolves the FINAL restored layout: identical rects to
    // the pre-restart strip, so the compositor's same-rect skip makes the
    // whole restore invisible.
    QCOMPARE(engine2->lastManagedRect(QStringLiteral("app|u1")), r1);
    QCOMPARE(engine2->lastManagedRect(QStringLiteral("app|u2")), r2);
    QCOMPARE(engine2->lastManagedRect(QStringLiteral("other|u3")), r3);
}

void TestScrollEnginePersistence::pruneSweepsStashedTilesForClosedWindows()
{
    // A stash is keyed by CONTEXT, and the existing sweeps only fire when the
    // context itself dies (desktop, activity, output). A window that closes
    // while the screen is in ANOTHER mode leaves its tile in a stash whose
    // context is still perfectly live, so nothing ever reached it: the entry
    // could never satisfy the all-consumed drop condition, survived into
    // serializeStripState, and its appId claim could later hand an unrelated
    // same-app window the dead tile's slot.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);

    // Leave Scrolling: the teardown stashes all three tiles.
    engine->setActiveScreens({});
    // Substring probes over the serialized blob, and safe here ONLY because
    // these ids are mutually non-prefixing: "app|a", "app|b" and "app|c" are
    // whole, distinct tokens. Add an id that is a prefix of another and these
    // assertions start reading each other's tiles — pick disjoint names or
    // parse the JSON.
    const QByteArray stashed = QJsonDocument(engine->serializeStripState()).toJson();
    QVERIFY2(stashed.contains("app|b"), "precondition: the stash holds all three tiles");

    // One window closes while the screen is in another mode. The context is
    // untouched, so the context-keyed sweeps cannot reach this stash and only
    // pruneStaleWindows can. The return counts TRACKED windows dropped, and
    // there are none — the mode exit already handed all three back — so 0 is
    // the right answer and the stash sweep is deliberately not reflected in
    // it. Asserted rather than discarded so the day the return does start
    // counting stash tiles, this test says so.
    QCOMPARE(engine->pruneStaleWindows({QStringLiteral("app|a"), QStringLiteral("app|c")}), 0);

    // Asserted while the screen is STILL out of Scrolling, deliberately: once
    // the context goes live again its strip wins the serialize key and would
    // mask a surviving ghost in the stash underneath.
    const QByteArray swept = QJsonDocument(engine->serializeStripState()).toJson();
    QVERIFY2(!swept.contains("app|b"), "the closed window must not survive in the stash");
    QVERIFY2(swept.contains("app|a") && swept.contains("app|c"), "the surviving tiles must be left alone by the sweep");

    // And the pruned stash still restores the survivors on the way back.
    engine->setActiveScreens({QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    QVERIFY(engine->isWindowTracked(QStringLiteral("app|a")));
    QVERIFY(engine->isWindowTracked(QStringLiteral("app|c")));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|b")));
}

void TestScrollEnginePersistence::pruneSpareStashStagedFromPersistence()
{
    // The aliveness sweep above and the cross-session restore pull in exactly
    // opposite directions, and the daemon runs them back to back at every
    // login: init stages the persisted blob into the stash, then the effect's
    // bringup fires pruneStaleWindows with the live window set.
    //
    // NO staged id can be in that set. The whole premise of the appId claim is
    // that the instance half of a window id is regenerated each launch, which
    // is why the restore matches on the app prefix. So a sweep that reads
    // "absent from the alive set" as "closed" erases the entire snapshot on
    // the first prune, and the tabbed columns, widths, focus and view anchor
    // are gone. Only pruneSweepsStashedTilesForClosedWindows exercised the
    // sweep, and it uses in-session ids, so it cannot see this at all.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|u1"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("app|u2"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("other|u3"), QStringLiteral("S1"), 0, 0);
    engine1->windowFocused(QStringLiteral("app|u1"), QStringLiteral("S1"));
    engine1->consumeWindowIntoColumn(QStringLiteral("S1"));
    engine1->toggleColumnTabbed(QStringLiteral("S1"));
    const QJsonObject blob = engine1->serializeStripState();
    QVERIFY(!blob.isEmpty());

    ScrollEngine* engine2 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine2->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine2->restoreStripState(blob);

    // The login-order prune, with THIS session's ids. None of them matches a
    // staged tile, which is the normal and expected case.
    QCOMPARE(
        engine2->pruneStaleWindows({QStringLiteral("app|n1"), QStringLiteral("app|n2"), QStringLiteral("other|n3")}),
        0);

    engine2->windowOpened(QStringLiteral("app|n1"), QStringLiteral("S1"), 0, 0);
    engine2->windowOpened(QStringLiteral("app|n2"), QStringLiteral("S1"), 0, 0);
    engine2->windowOpened(QStringLiteral("other|n3"), QStringLiteral("S1"), 0, 0);

    ScrollState* state = stateFor(engine2, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY2(state->strip().columns().size() == 2,
             "the persisted structure must survive the bringup prune, not collapse to one column per window");
    QVERIFY2(state->strip().columns().at(0).display == ColumnDisplay::Tabbed,
             "the stashed tabbed column must still rebuild after the prune");
    QCOMPARE(state->strip().columns().at(0).tiles.size(), 2);
    QCOMPARE(engine2->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("other|n3")), 1);

    // The exemption is PER TILE, and a claim on one tile must not expose its
    // co-tenant's. Both stashed tiles name last session's ids; claiming the
    // first anchors THAT tile in this session, but the second still names an
    // id no alive set can ever contain, so a prune in between must leave it
    // alone. An entry-wide lift on the first claim let this very sequence
    // erase the second slot — the case the per-tile unclaimedSessions lease
    // exists to age out over three logins instead of destroying on sight.
    //
    // Asserted BEHAVIOURALLY, on the strip, with the screen still active.
    // Reading serializeStripState here would prove nothing: taking the screen
    // out of scrolling to expose the stash runs stashStripStructure, which
    // REPLACES the entry wholesale from the live strip.
    ScrollEngine* engine3 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine3->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine3->restoreStripState(blob);
    // app|m1 claims app|u1's tile in the stashed tabbed column.
    engine3->windowOpened(QStringLiteral("app|m1"), QStringLiteral("S1"), 0, 0);
    // A prune lands between the two arrivals — the daemon really does run one
    // at bringup. No TRACKED window dies (app|m1 is alive), so the count is 0
    // and everything this asserts is about the stash.
    QCOMPARE(engine3->pruneStaleWindows({QStringLiteral("app|m1")}), 0);
    // app|u2's slot survived, so the second arrival still stacks into the
    // rebuilt column. With the exemption lifted entry-wide by the first
    // claim, the prune above erased that slot and this lands in its own
    // column instead.
    engine3->windowOpened(QStringLiteral("app|n2"), QStringLiteral("S1"), 0, 0);
    const int m1Col = columnOf(engine3, QStringLiteral("app|m1"));
    const int n2Col = columnOf(engine3, QStringLiteral("app|n2"));
    QVERIFY2(m1Col == n2Col,
             "a claim on one staged tile must not expose its unclaimed co-tenant's slot to the aliveness sweep");

    // Control for the arm above: the identical sequence WITHOUT the
    // intervening prune reaches the same stacking (the prune must be
    // invisible), and then a THIRD same-app arrival lands in its OWN column.
    // The third arrival is what gives the control discriminating power: both
    // staged tiles are consumed by now, so a claim path that stacked same-app
    // arrivals unconditionally would pass the equality yet fail here — the
    // stacking really is the staged slots and nothing else.
    ScrollEngine* engine4 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine4->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine4->restoreStripState(blob);
    engine4->windowOpened(QStringLiteral("app|m1"), QStringLiteral("S1"), 0, 0);
    engine4->windowOpened(QStringLiteral("app|n2"), QStringLiteral("S1"), 0, 0);
    QCOMPARE(engine4->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|n2")),
             engine4->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|m1")));
    engine4->windowOpened(QStringLiteral("app|p3"), QStringLiteral("S1"), 0, 0);
    QVERIFY2(engine4->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|p3"))
                 != engine4->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|m1")),
             "with every staged tile consumed, a same-app arrival must open plainly, not stack");
}

void TestScrollEnginePersistence::unclaimedStashTilesExpireAfterThreeSessions()
{
    // The per-tile unclaimedSessions lease: a staged tile ages by one at each
    // serialize it sits through unclaimed, and restoreStripState drops it at
    // kMaxUnclaimedSessions (3). Without the drop, the prune exemption makes
    // the tile immortal — pruneStaleWindows fires exactly once per session,
    // at bringup, while the staged entry is still sweep-exempt — and a
    // long-dead slot eventually captures an unrelated same-app window.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|u1"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("app|u2"), QStringLiteral("S1"), 0, 0);
    engine1->windowFocused(QStringLiteral("app|u1"), QStringLiteral("S1"));
    engine1->consumeWindowIntoColumn(QStringLiteral("S1")); // u2 joins u1's stack
    engine1->toggleColumnTabbed(QStringLiteral("S1"));
    QJsonObject blob = engine1->serializeStripState();
    QVERIFY(!blob.isEmpty());

    // Three sessions restore the blob and shut down without ANY claim. Each
    // pass ages the tiles by one: 1, 2, 3.
    QJsonObject penultimate;
    for (int session = 0; session < 3; ++session) {
        penultimate = blob;
        ScrollEngine* e = makeProviderEngine(&owner, {QStringLiteral("S1")});
        e->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
        e->restoreStripState(blob);
        blob = e->serializeStripState();
        // Re-asserted every pass: an empty blob would carry the loop to the
        // end just as well, and the expiry assertion at the tail would then
        // pass because there was nothing left to claim rather than because
        // the lease ran out.
        QVERIFY2(!blob.isEmpty(), qPrintable(QStringLiteral("session %1 wrote an empty snapshot").arg(session)));
    }

    // Control first, with the SECOND-to-last blob (ages 2): the lease is not
    // yet expired, so the claim still fires and the pair rebuilds tabbed in
    // one column. This is what makes the expiry assertion below non-vacuous.
    ScrollEngine* control = makeProviderEngine(&owner, {QStringLiteral("S1")});
    control->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    control->restoreStripState(penultimate);
    control->windowOpened(QStringLiteral("app|c1"), QStringLiteral("S1"), 0, 0);
    control->windowOpened(QStringLiteral("app|c2"), QStringLiteral("S1"), 0, 0);
    QCOMPARE(columnOf(control, QStringLiteral("app|c1")), columnOf(control, QStringLiteral("app|c2")));

    // The last blob carries age 3: restore drops both tiles, so the arrivals
    // find no staged slots and open plainly, one column each.
    ScrollEngine* engine5 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine5->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine5->restoreStripState(blob);
    engine5->windowOpened(QStringLiteral("app|x1"), QStringLiteral("S1"), 0, 0);
    engine5->windowOpened(QStringLiteral("app|x2"), QStringLiteral("S1"), 0, 0);
    const int x1Col = columnOf(engine5, QStringLiteral("app|x1"));
    const int x2Col = columnOf(engine5, QStringLiteral("app|x2"));
    QVERIFY2(x1Col != x2Col, "an expired stash tile must not capture a later same-app arrival");
}

void TestScrollEnginePersistence::coTenantClaimDoesNotRenewSiblingLease()
{
    // The lease is PER TILE, not per entry. A same-app co-tenant that comes
    // back every session claims ITS tile (fresh lease) but must not renew the
    // dead sibling's: with an entry-level counter the claim reset the whole
    // entry's age and the sibling slot lived forever. The claiming window is
    // closed before each serialize so the stash entry — not a live strip —
    // is what the shutdown snapshot writes for the key (the claimed-then-
    // closed tile legitimately writes a fresh lease: its app demonstrably
    // comes back).
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|u1"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("app|u2"), QStringLiteral("S1"), 0, 0);
    engine1->windowFocused(QStringLiteral("app|u1"), QStringLiteral("S1"));
    engine1->consumeWindowIntoColumn(QStringLiteral("S1")); // u2 joins u1's stack
    engine1->toggleColumnTabbed(QStringLiteral("S1"));
    QJsonObject blob = engine1->serializeStripState();
    QVERIFY(!blob.isEmpty());

    // Three sessions each claim ONE tile (the first slot, by appId match, in
    // arrival order) and close it again before shutdown. The claimed lineage
    // re-leases every time; the sibling ages 1, 2, 3 untouched.
    for (int session = 0; session < 3; ++session) {
        ScrollEngine* e = makeProviderEngine(&owner, {QStringLiteral("S1")});
        e->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
        e->restoreStripState(blob);
        const QString claimant = QStringLiteral("app|m%1").arg(session);
        e->windowOpened(claimant, QStringLiteral("S1"), 0, 0);
        // Positive control, every pass: the arrival really did CLAIM a
        // stashed slot rather than open a plain column. The stashed column is
        // Tabbed and the plain open path is Normal, so the display is the
        // discriminator — and without it the whole test would still pass if
        // the restore had staged nothing at all.
        ScrollState* claimed = stateFor(e, QStringLiteral("S1"));
        QVERIFY(claimed);
        QVERIFY2(!claimed->strip().columns().isEmpty(),
                 qPrintable(QStringLiteral("session %1 claimant built no column").arg(session)));
        QVERIFY2(
            claimed->strip().columns().at(0).display == ColumnDisplay::Tabbed,
            qPrintable(QStringLiteral("session %1 claimant did not land in the stashed tabbed column").arg(session)));
        e->windowClosed(claimant);
        blob = e->serializeStripState();
        QVERIFY2(!blob.isEmpty(), qPrintable(QStringLiteral("session %1 wrote an empty snapshot").arg(session)));
    }

    // The sibling's lease expired on the final restore; only the claimed
    // lineage's slot survives. The first arrival claims it, the second finds
    // no staged slot left and opens its own column. With an entry-level
    // counter both would claim and stack tabbed into one column.
    ScrollEngine* engine5 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine5->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine5->restoreStripState(blob);
    engine5->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine5->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    const int aCol = columnOf(engine5, QStringLiteral("app|a"));
    const int bCol = columnOf(engine5, QStringLiteral("app|b"));
    QVERIFY2(aCol != bCol, "a returning co-tenant's claim must not renew the dead sibling tile's lease");
}

void TestScrollEnginePersistence::serializeKeepsAnUnclaimedStashTileBesideALiveStrip()
{
    // A key can hold a live strip AND an unconsumed stash at the same time,
    // and serializeStripState used to write them to the same QJsonObject key
    // in two passes, so the second insert REPLACED the first instead of
    // merging. The window still waiting in the stash vanished from the save.
    //
    // Three windows are persisted. Two of them come back, which claims two of
    // the three staged tiles and builds a live strip, but leaves the entry
    // unconsumed and still carrying the third. Saving there must not lose it.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|u1"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("app|u2"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("other|u3"), QStringLiteral("S1"), 0, 0);
    const QJsonObject blob = engine1->serializeStripState();
    QVERIFY(!blob.isEmpty());

    ScrollEngine* engine2 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine2->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine2->restoreStripState(blob);
    // Only the two "app" windows return. "other" stays away, so its tile is
    // still staged when the save runs.
    engine2->windowOpened(QStringLiteral("app|n1"), QStringLiteral("S1"), 0, 0);
    engine2->windowOpened(QStringLiteral("app|n2"), QStringLiteral("S1"), 0, 0);

    ScrollState* live = stateFor(engine2, QStringLiteral("S1"));
    QVERIFY(live);
    // Both halves genuinely present, or the assertion below could pass for
    // the wrong reason: no live strip at this key means no collision to merge.
    QCOMPARE(live->windowCount(), 2);

    const QByteArray saved = QJsonDocument(engine2->serializeStripState()).toJson();
    QVERIFY2(saved.contains("app|n1"), "the live strip's own windows must be written");
    QVERIFY2(saved.contains("other|u3"),
             "the tile still waiting in the stash must survive a save taken while a live strip shares its key");
}

void TestScrollEnginePersistence::windowedFullscreenSurvivesSerializeRestore()
{
    // The flag is strip-owned state the compositor mirrors, so a DAEMON
    // RESTART must hand it back (unlike minimized, which the effect
    // re-reports live). Restart means the windows keep their uuids and the
    // re-announce claims exact-id — that is the only claim that carries the
    // flag. The uuid-drift (login) shape deliberately does NOT transfer it;
    // windowedFullscreenFuzzyClaimDoesNotTransfer pins that half.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|u1"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("other|u2"), QStringLiteral("S1"), 0, 0);
    engine1->windowFocused(QStringLiteral("app|u1"), QStringLiteral("S1"));
    engine1->toggleWindowedFullscreen(QStringLiteral("S1"));
    ScrollState* before = stateFor(engine1, QStringLiteral("S1"));
    QVERIFY(before);
    QVERIFY(before->strip().isWindowedFullscreen(QStringLiteral("app|u1")));
    const QJsonObject blob = engine1->serializeStripState();
    QVERIFY(!blob.isEmpty());

    ScrollEngine* engine2 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine2->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine2->restoreStripState(blob);
    engine2->windowOpened(QStringLiteral("app|u1"), QStringLiteral("S1"), 0, 0);
    engine2->windowOpened(QStringLiteral("other|u2"), QStringLiteral("S1"), 0, 0);
    ScrollState* after = stateFor(engine2, QStringLiteral("S1"));
    QVERIFY(after);
    QVERIFY(after->strip().isWindowedFullscreen(QStringLiteral("app|u1")));
    QVERIFY(!after->strip().isWindowedFullscreen(QStringLiteral("other|u2")));
}

void TestScrollEnginePersistence::windowedFullscreenTogglesEmitAndFloatClears()
{
    // The flag never moves a rect, so it needs its own leg of applyLayout's
    // emit-on-change gate: deleting the m_lastAppliedWindowedFs compare
    // leaves the batch unsent on an otherwise motionless strip and fails
    // the first spy count below. The float half pins the exclusivity
    // contract: a float takes the tile out of the strip, and the flag and
    // its restore round trip die with it.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));

    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    engine->toggleWindowedFullscreen(QStringLiteral("S1"));
    QCOMPARE(tiled.count(), 1);
    const QJsonArray batch = QJsonDocument::fromJson(tiled.takeFirst().at(0).toString().toUtf8()).array();
    bool sawFlag = false;
    for (const QJsonValue& v : batch) {
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("windowId")).toString() == QLatin1String("app|a")) {
            sawFlag = o.value(QLatin1String("windowedFullscreen")).toBool(false);
        }
    }
    QVERIFY2(sawFlag, "the toggled window's batch entry must carry the flag");

    // Toggle off re-emits through the same gate leg.
    engine->toggleWindowedFullscreen(QStringLiteral("S1"));
    QCOMPARE(tiled.count(), 1);
    tiled.clear();

    // Float clears: on and out, then back in without the flag.
    engine->toggleWindowedFullscreen(QStringLiteral("S1"));
    engine->setWindowFloat(QStringLiteral("app|a"), true, QStringLiteral("S1"));
    engine->setWindowFloat(QStringLiteral("app|a"), false, QStringLiteral("S1"));
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY(state->strip().containsWindow(QStringLiteral("app|a")));
    QVERIFY(!state->strip().isWindowedFullscreen(QStringLiteral("app|a")));
    // And the reconciliation entry point drops a set flag engine-wide.
    // Re-focus first: the unfloat round trip above may have handed the
    // strip focus elsewhere, and the toggle acts on the ACTIVE window.
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->toggleWindowedFullscreen(QStringLiteral("S1"));
    QVERIFY(state->strip().isWindowedFullscreen(QStringLiteral("app|a")));
    engine->clearWindowedFullscreen(QStringLiteral("app|a"));
    QVERIFY(!state->strip().isWindowedFullscreen(QStringLiteral("app|a")));
}

// The boundary-crossing carry (niri keeps windowed fullscreen across
// move-column-to-monitor) is pinned in test_scrollengine_zonenumbers.cpp's
// crossing test, which owns the cross-surface resolver fixture the verb
// needs.

void TestScrollEnginePersistence::windowedFullscreenHiddenTabStillEmitsFlag()
{
    // The flag rides the tile UNGATED by presentation: a hidden tab keeps
    // its client's fullscreen state, so its batch entry still carries the
    // key (this test pins the hidden-tab arm; the parked-column case shares
    // the same ungated emit in engine_apply but is not separately pinned
    // here). The first design suppressed the flag off-canvas and every tab
    // switch or scroll past a flagged column cycled the client's fullscreen
    // presentation, seen live as resize and decoration flicker. The
    // effect's layer demotion and geometry-bail exemption keep an
    // off-canvas fullscreen client's GEOMETRY and PAINT inert, so
    // presentation gating buys nothing there (focus is a separate concern
    // the demotion does not cover — it is stacking-only).
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1")); // b joins a's column
    engine->toggleColumnTabbed(QStringLiteral("S1"));
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->toggleWindowedFullscreen(QStringLiteral("S1"));

    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    // Show the OTHER tab: a becomes the hidden tab of an on-screen tabbed
    // column, and its entry must STILL carry the flag.
    engine->windowFocused(QStringLiteral("app|b"), QStringLiteral("S1"));
    QVERIFY(tiled.count() >= 1);
    bool sawA = false;
    bool aFlagged = false;
    const QJsonArray batch = QJsonDocument::fromJson(tiled.last().at(0).toString().toUtf8()).array();
    for (const QJsonValue& v : batch) {
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("windowId")).toString() == QLatin1String("app|a")) {
            sawA = true;
            aFlagged = o.value(QLatin1String("windowedFullscreen")).toBool(false);
        }
    }
    QVERIFY2(sawA, "the hidden tab must still be in the batch, or the compare is vacuous");
    QVERIFY(aFlagged);
    // The model keeps the flag through the round trip.
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY(state->strip().isWindowedFullscreen(QStringLiteral("app|a")));
}

void TestScrollEnginePersistence::windowedFullscreenMinimizeDropsModeKeeps()
{
    // The design decision pinned: minimize (which rides the float machinery)
    // DROPS windowed fullscreen, while a mode round trip (the stash) KEEPS
    // it. Whichever side regresses, exactly one of these arms fails.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->toggleWindowedFullscreen(QStringLiteral("S1"));

    // Minimize arm: the effect reports minimize as a float toggle.
    engine->setWindowFloat(QStringLiteral("app|a"), true, QStringLiteral("S1"));
    engine->setWindowFloat(QStringLiteral("app|a"), false, QStringLiteral("S1"));
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY(!state->strip().isWindowedFullscreen(QStringLiteral("app|a")));

    // Mode arm: flag again, cycle the screen out of scrolling and back.
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->toggleWindowedFullscreen(QStringLiteral("S1"));
    engine->setActiveScreens({});
    engine->setActiveScreens({QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    ScrollState* after = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(after);
    QVERIFY(after->strip().isWindowedFullscreen(QStringLiteral("app|a")));
}

void TestScrollEnginePersistence::windowedFullscreenFuzzyClaimDoesNotTransfer()
{
    // The cross-session appId claim hands a NEW same-app window a dead
    // sibling's slot, width and height — but NOT its fullscreen
    // presentation. Exact-id restores (same uuid) keep the flag; the fuzzy
    // claim must not.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|u1"), QStringLiteral("S1"), 0, 0);
    engine1->windowFocused(QStringLiteral("app|u1"), QStringLiteral("S1"));
    engine1->toggleWindowedFullscreen(QStringLiteral("S1"));
    const QJsonObject blob = engine1->serializeStripState();

    ScrollEngine* engine2 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine2->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine2->restoreStripState(blob);
    // Different uuid, same appId: the fuzzy claim fires, the slot transfers,
    // the flag does not.
    engine2->windowOpened(QStringLiteral("app|n1"), QStringLiteral("S1"), 0, 0);
    QCOMPARE(engine2->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|n1")), 0);
    ScrollState* state = stateFor(engine2, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY(!state->strip().isWindowedFullscreen(QStringLiteral("app|n1")));
}

void TestScrollEnginePersistence::restoreDropsMalformedKeysAndBoundsAnchor()
{
    // keyFromString's boundary rejections (no '|', one '|', non-numeric or
    // negative desktop, empty screen) plus the viewAnchor sanity bound —
    // previously unpinned. Built from a real blob so the payload under each
    // hostile key is well-formed, isolating the KEY parse.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine1->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine1->consumeWindowIntoColumn(QStringLiteral("S1")); // b stacks on a
    const QJsonObject blob = engine1->serializeStripState();
    QVERIFY(!blob.isEmpty());
    const QString goodKey = QStringLiteral("S1|1|");
    QVERIFY2(blob.contains(goodKey), "fixture assumption: the live key spells S1|1|<empty activity>");
    QJsonObject payload = blob.value(goodKey).toObject();

    // Hostile anchor rides a well-formed key so the bound is observable.
    payload.insert(QLatin1String("viewAnchor"), 2147483647.0);
    QJsonObject hostile;
    // QJsonObject iterates in sorted key order, so the malformed keys are
    // named to sort BEFORE the good key: the restore walks (and rejects)
    // every one of them before the good key stages, instead of the good key
    // staging first and the rejects never being order-exercised. The
    // empty-screen key is the exception — "|" sorts after "S" — so its
    // rejection is order-unpinned; it still must not stage.
    hostile.insert(QStringLiteral("Anokey"), payload); // no separator
    hostile.insert(QStringLiteral("A1|x|"), payload); // non-numeric desktop
    hostile.insert(QStringLiteral("A2|-2|"), payload); // negative desktop
    hostile.insert(QStringLiteral("|1|"), payload); // empty screen id
    hostile.insert(goodKey, payload); // the one legal key

    ScrollEngine* engine2 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine2->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine2->restoreStripState(hostile);
    engine2->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine2->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    // Only the legal key staged: the stack claim proves the good entry
    // survived beside the four rejects (a rejected GOOD key would fail
    // this; an accepted BAD key has no observable strip to disagree with,
    // which is why the anchor bound below carries the other half).
    QCOMPARE(engine2->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|b")),
             engine2->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|a")));
    ScrollState* state = stateFor(engine2, QStringLiteral("S1"));
    QVERIFY(state);
    // INT_MAX was clamped at the boundary to the documented sanity range.
    QVERIFY(state->strip().viewAnchor() <= 1000000);
}

void TestScrollEnginePersistence::restoreStagesADuplicateWindowIdOnlyOnce()
{
    // The cross-key claimedWindowIds dedup: two persisted keys both listing
    // the same window must stage it exactly once, or whichever context
    // announces second splices the window into a second strip.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|d"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("app|e"), QStringLiteral("S1"), 0, 0);
    engine1->windowFocused(QStringLiteral("app|d"), QStringLiteral("S1"));
    engine1->consumeWindowIntoColumn(QStringLiteral("S1")); // e stacks on d
    const QJsonObject blob = engine1->serializeStripState();
    const QString key1 = QStringLiteral("S1|1|");
    QVERIFY(blob.contains(key1));
    QJsonObject dup = blob;
    dup.insert(QStringLiteral("S1|2|"), blob.value(key1)); // same windows, second key

    ScrollEngine* engine2 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine2->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine2->restoreStripState(dup);
    // Claim on desktop 1 (iteration order between the two keys is
    // QJsonObject's, so assert the INVARIANT rather than which key won: the
    // window is claimed by exactly one context, and the other stages
    // nothing for it).
    engine2->windowOpened(QStringLiteral("app|d"), QStringLiteral("S1"), 0, 0);
    engine2->windowOpened(QStringLiteral("app|e"), QStringLiteral("S1"), 0, 0);
    const int col1d = engine2->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|d"));
    QVERIFY(col1d >= 0);
    // Switch context; a second claim of the SAME ids must not resurrect the
    // duplicate staging (the windows are live in desktop 1's strip, so a
    // re-open here is the splice the dedup exists to prevent).
    engine2->setCurrentDesktopForScreen(QStringLiteral("S1"), 2);
    engine2->windowOpened(QStringLiteral("app|d"), QStringLiteral("S1"), 0, 0);
    ScrollState* d2 = stateFor(engine2, QStringLiteral("S1"));
    QVERIFY(d2);
    // Desktop 2's strip holds d as a FRESH single tile (default placement),
    // not the staged two-window stack: e must not be pulled in beside it.
    QVERIFY(!d2->strip().containsWindow(QStringLiteral("app|e")));
}

void TestScrollEnginePersistence::restoreFocusFallsBackToASurvivingTile()
{
    // A dangling focusedWindow (its tile dropped or never present) must not
    // strand the restore: focus falls back to a surviving tile.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|f"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("app|g"), QStringLiteral("S1"), 0, 0);
    QJsonObject blob = engine1->serializeStripState();
    const QString key = QStringLiteral("S1|1|");
    QVERIFY(blob.contains(key));
    QJsonObject payload = blob.value(key).toObject();
    payload.insert(QLatin1String("focusedWindow"), QStringLiteral("app|gone"));
    blob.insert(key, payload);

    ScrollEngine* engine2 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine2->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine2->restoreStripState(blob);
    engine2->windowOpened(QStringLiteral("app|f"), QStringLiteral("S1"), 0, 0);
    engine2->windowOpened(QStringLiteral("app|g"), QStringLiteral("S1"), 0, 0);
    ScrollState* state = stateFor(engine2, QStringLiteral("S1"));
    QVERIFY(state);
    const QString active = state->strip().activeWindowId();
    QVERIFY2(active == QStringLiteral("app|f") || active == QStringLiteral("app|g"),
             "focus must land on a real tile, never the dangling id");
}

void TestScrollEnginePersistence::restoreCapsTilesPerColumn()
{
    // The count caps (enginelimits.h): a corrupt blob column with more tiles
    // than kMaxRestoredTilesPerColumn stages only the cap; the surplus is
    // dropped (logged, never staged) instead of walked on every later open.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|h"), QStringLiteral("S1"), 0, 0);
    QJsonObject blob = engine1->serializeStripState();
    const QString key = QStringLiteral("S1|1|");
    QVERIFY(blob.contains(key));
    QJsonObject payload = blob.value(key).toObject();
    QJsonArray columns = payload.value(QLatin1String("columns")).toArray();
    QVERIFY(!columns.isEmpty());
    QJsonObject col = columns.at(0).toObject();
    QJsonArray tiles = col.value(QLatin1String("tiles")).toArray();
    QVERIFY(!tiles.isEmpty());
    const QJsonObject seedTile = tiles.at(0).toObject();
    QJsonArray fatTiles;
    for (int i = 0; i < 40; ++i) { // cap is 32
        QJsonObject t = seedTile;
        // DISTINCT appIds: a shared app prefix would let an un-staged tile
        // past the cap fuzzy-claim a staged sibling's dead slot, which is
        // exactly the noise this pin must not measure.
        t.insert(QLatin1String("windowId"), QStringLiteral("a%1|w%1").arg(i));
        fatTiles.append(t);
    }
    col.insert(QLatin1String("tiles"), fatTiles);
    columns = QJsonArray();
    columns.append(col);
    payload.insert(QLatin1String("columns"), columns);
    blob.insert(key, payload);

    ScrollEngine* engine2 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine2->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine2->restoreStripState(blob);
    // Tiles under the cap claim their staged slot (they stack into the one
    // column); a tile past the cap was never staged, so it opens as a fresh
    // default-placement column instead.
    engine2->windowOpened(QStringLiteral("a0|w0"), QStringLiteral("S1"), 0, 0);
    engine2->windowOpened(QStringLiteral("a31|w31"), QStringLiteral("S1"), 0, 0);
    engine2->windowOpened(QStringLiteral("a35|w35"), QStringLiteral("S1"), 0, 0);
    const int colW0 = engine2->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("a0|w0"));
    const int colW31 = engine2->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("a31|w31"));
    const int colW35 = engine2->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("a35|w35"));
    QCOMPARE(colW31, colW0);
    QVERIFY2(colW35 != colW0, "a tile past the cap must not have been staged into the capped column");
}

void TestScrollEnginePersistence::backgroundContextClearAndReapplySkipRelayout()
{
    // The two wire verbs' background-context branches (the live path for a
    // client leaving fullscreen on a non-current desktop): the model write
    // lands and placementChanged fires, but NO windowsTiled batch is
    // emitted for a strip that is not the screen's current context.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->windowOpened(QStringLiteral("app|bg"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(QStringLiteral("app|bg"), QStringLiteral("S1"));
    engine->toggleWindowedFullscreen(QStringLiteral("S1"));
    ScrollState* d1 = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(d1);
    QVERIFY(d1->strip().isWindowedFullscreen(QStringLiteral("app|bg")));

    // Switch the screen to desktop 2: app|bg's strip is now background.
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 2);
    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    QSignalSpy placement(engine, &ScrollEngine::placementChanged);

    engine->clearWindowedFullscreen(QStringLiteral("app|bg"));
    QVERIFY2(!d1->strip().isWindowedFullscreen(QStringLiteral("app|bg")),
             "the model flag must drop on the window's OWN context state");
    QCOMPARE(tiled.count(), 0);
    QVERIFY(placement.count() >= 1);

    engine->reapplyWindowGeometry(QStringLiteral("app|bg"));
    QCOMPARE(tiled.count(), 0);

    // Switching back retiles the mutated strip and the next batch carries
    // the cleared flag. QTRY: the context-switch retile is queued
    // (scheduleRetileForScreen), so the spy needs the event loop.
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    QTRY_VERIFY(tiled.count() >= 1);
    bool sawBg = false;
    bool bgFlagged = true;
    const QJsonArray batch = QJsonDocument::fromJson(tiled.last().at(0).toString().toUtf8()).array();
    for (const QJsonValue& v : batch) {
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("windowId")).toString() == QLatin1String("app|bg")) {
            sawBg = true;
            bgFlagged = o.value(QLatin1String("windowedFullscreen")).toBool(false);
        }
    }
    QVERIFY2(sawBg, "the window must be in the switch-back batch, or the flag compare is vacuous");
    QVERIFY(!bgFlagged);
}

QTEST_GUILESS_MAIN(TestScrollEnginePersistence)
#include "test_scrollengine_persistence.moc"

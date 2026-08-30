// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Column maximize at the ENGINE layer: what the published maximizedToEdges
// flag means (declared per-column state, driven by toggleMaximizeToEdges —
// toggleMaximizeColumn is a pure width verb with no wire representation),
// which column the window-scoped verbs act on, and what survives the state
// transitions that discard the strip's single pre-maximize slot.
//
// Its own file rather than more of test_scrollengine_smoke, whose sanctioned
// size exception says in as many words that a new concern takes a sibling. The
// strip-level toggle arms live in test_scrollstrip_sizing; what this file owns
// is the ENGINE's half — the wire flag, the verb's targeting, and the
// compositions no single-step test covers.
//
// The blob and float arms of the same concern live here too: the persisted
// per-column key (round trip and the absent-key fallback), the cross-session
// fuzzy appId claim's transfer, and what the FloatRestore slot carries out and
// back. They belong with the concern rather than in test_scrollengine_persistence,
// which owns the stash mechanics and is already past the file-size ceiling.
//
// The flag is deliberately published for tiles the user cannot see (parked
// columns, hidden tabs). Suppressing it there cycled the client's maximize bit
// on every scroll past a maximized column, so the "invisible tile" cases below
// are pinning intended behaviour, not an accident.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include "scrollstriptestutils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorScrollEngine;

namespace Ax = ScrollTestUtils::Ax;

using ScrollTestUtils::makeProviderEngine;

namespace {

/// The engine's ScrollState for a screen. Local rather than shared, matching
/// the sibling suites: it is two lines and a downcast, and every call site
/// QVERIFYs the result, because a regression that drops the state would
/// otherwise segfault the binary and take the remaining slots' results with it.
ScrollState* stateFor(ScrollEngine* engine, const QString& screenId)
{
    return static_cast<ScrollState*>(engine->stateForScreen(screenId));
}

/// The stored width intent of the column holding @p windowId on S1.
///
/// Both guards, for stateFor's reason: a dropped state or a window the strip no
/// longer holds must fail the comparison rather than index .at(-1). The
/// sentinel is deliberately unreachable — a default-constructed ColumnWidth is
/// Proportion(0.5), a value a REAL column can hold, so a genuine lookup miss
/// would compare equal and pass.
ColumnWidth widthOf(ScrollEngine* engine, const QString& windowId)
{
    const ScrollState* st = stateFor(engine, QStringLiteral("S1"));
    if (!st) {
        return ColumnWidth::makeFixed(-1);
    }
    const int idx = st->strip().columnOfWindow(windowId);
    return idx < 0 ? ColumnWidth::makeFixed(-1) : st->strip().columns().at(idx).width;
}

/// Whether the column holding @p windowId on S1 carries the declared
/// maximize-to-edges flag. False for a window the strip does not hold, which is
/// never the passing answer at any call site below.
bool columnFlagged(ScrollEngine* engine, const QString& windowId)
{
    const ScrollState* st = stateFor(engine, QStringLiteral("S1"));
    if (!st) {
        return false;
    }
    const int idx = st->strip().columnOfWindow(windowId);
    return idx >= 0 && st->strip().columns().at(idx).maximizedToEdges;
}

/// Every windowId carrying maximizedToEdges in the last emitted batch.
QSet<QString> maximizedInBatch(const QSignalSpy& spy)
{
    QSet<QString> out;
    if (spy.isEmpty()) {
        return out;
    }
    const QJsonArray arr = QJsonDocument::fromJson(spy.last().at(0).toString().toUtf8()).array();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("maximizedToEdges")).toBool(false)) {
            out.insert(o.value(QLatin1String("windowId")).toString());
        }
    }
    return out;
}

/// Every windowId present in the last emitted batch at all.
QSet<QString> windowsInBatch(const QSignalSpy& spy)
{
    QSet<QString> out;
    if (spy.isEmpty()) {
        return out;
    }
    const QJsonArray arr = QJsonDocument::fromJson(spy.last().at(0).toString().toUtf8()).array();
    for (const QJsonValue& v : arr) {
        out.insert(v.toObject().value(QLatin1String("windowId")).toString());
    }
    return out;
}

} // namespace

class TestScrollEngineMaximize : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Proves the vertical arm really is transposed, so a lost ENVIRONMENT
    /// property cannot leave it silently re-running the horizontal suite.
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    void flagRidesTilesTheUserCannotSee();
    void twoColumnsCanBeMaximizedAtOnce();
    void namedVerbTogglesBackOnASecondPress();
    void maximizeSurvivesAModeRoundTripWithoutItsRestoreSlot();
    void expellingFromAMaximizedColumnDoesNotMaximizeTheExpelledTile();
    void maximizedToEdgesRoundTripsThroughTheBlob();
    void maximizedToEdgesTransfersOnAFuzzyAppIdClaim();
    void floatingASoleTileCarriesTheFlagBothWays();
    void floatingOneTileOfTwoLeavesTheFlagWithTheColumn();
};

// The maximize-to-edges flag lives on the COLUMN and must not travel with an
// expelled tile: the expelled window arrives in a fresh column (default
// constructed, so unflagged) while the source keeps its flag. Under the old
// MEASURED flag the expel's width copy produced two columns both reporting
// maximized; the declared flag cannot be copied by accident, and this slot
// pins that it is not.
//
// Asserted on the published FLAG rather than on any stored state, because the
// flag is what the effect mirrors onto KWin's maximize bit.
void TestScrollEngineMaximize::expellingFromAMaximizedColumnDoesNotMaximizeTheExpelledTile()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);

    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    // Focus the FIRST column: consume pulls the next column's window in, so
    // running it on the last column would consume nothing and leave two
    // separate columns, which is not the shape this slot is about.
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    // One column holding both tiles, then maximized.
    engine->consumeWindowIntoColumn(QStringLiteral("S1"));
    engine->toggleMaximizeToEdges(QStringLiteral("S1"));
    QCoreApplication::processEvents();
    QCOMPARE(maximizedInBatch(tiled), (QSet<QString>{QStringLiteral("app|a"), QStringLiteral("app|b")}));

    // Expel the focused tile into its own column.
    engine->expelWindowFromColumn(QStringLiteral("S1"));
    QCoreApplication::processEvents();

    // Both windows are still tiled and still reported...
    QCOMPARE(windowsInBatch(tiled), (QSet<QString>{QStringLiteral("app|a"), QStringLiteral("app|b")}));
    // ...but exactly one column is maximized now, not both. Which tile stayed
    // behind is the expel verb's business and not this slot's; the defect was
    // the COUNT, and a size assertion catches it without pinning that choice.
    QCOMPARE(maximizedInBatch(tiled).size(), 1);
}

void TestScrollEngineMaximize::flagRidesTilesTheUserCannotSee()
{
    // The flag is published per TILE but decided per COLUMN, and it is
    // deliberately ungated by presentation: a parked column's tiles and a
    // tabbed column's hidden tabs carry it just like a visible one.
    //
    // Adding a `&& !parked` or `&& !hidden` term to the emission is the
    // mutation this pins. It survives every other maximize test, because their
    // fixtures leave the maximized column on screen and untabbed — and what it
    // breaks is the property the emission's own comment calls load-bearing:
    // scrolling past a maximized column must not cycle its clients' maximize
    // bit off and on, which on a real compositor is a KWin maximize call per
    // scroll step.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    for (const char* id : {"app|a", "app|b", "app|c", "app|d"}) {
        engine->windowOpened(QString::fromLatin1(id), QStringLiteral("S1"), 0, 0);
    }
    QCoreApplication::processEvents();

    // Maximize the FIRST column, then scroll away so it parks off-viewport.
    engine->focusColumnFirst(QStringLiteral("S1"));
    QCoreApplication::processEvents();
    engine->toggleMaximizeToEdges(QStringLiteral("S1"));
    QCoreApplication::processEvents();

    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    engine->focusColumnLast(QStringLiteral("S1"));
    QCoreApplication::processEvents();
    QVERIFY2(!tiled.isEmpty(), "focusing the far end must emit a batch");

    // The maximized column is now off-viewport. It must still be named, and
    // still carry the flag.
    QVERIFY2(windowsInBatch(tiled).contains(QStringLiteral("app|a")),
             "a parked column's tile must still appear in the batch");
    QVERIFY2(maximizedInBatch(tiled).contains(QStringLiteral("app|a")),
             "a parked maximized column must still publish maximizedToEdges");

    // Now the tabbed half. Fold a neighbour into the first column and tab it,
    // so one of the two tabs is hidden.
    engine->focusColumnFirst(QStringLiteral("S1"));
    QCoreApplication::processEvents();
    engine->consumeWindowIntoColumn(QStringLiteral("S1"));
    QCoreApplication::processEvents();
    engine->toggleColumnTabbed(QStringLiteral("S1"));
    QCoreApplication::processEvents();

    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    const int idx = state->strip().activeColumnIndex();
    QVERIFY(idx >= 0);
    QStringList tabIds;
    for (const auto& tile : state->strip().columns().at(idx).tiles) {
        tabIds << tile.windowId;
    }
    QVERIFY2(tabIds.size() > 1, "the fold must have produced a multi-tile column, or the tabbed leg proves nothing");

    // Drive the toggle until the column READS maximized, rather than assuming
    // which way the earlier press left it: the fold and the tab both relayout,
    // and a press on an already-full column un-maximizes it.
    QSignalSpy tabbed(engine, &ScrollEngine::windowsTiled);
    engine->toggleMaximizeToEdges(QStringLiteral("S1"));
    QCoreApplication::processEvents();
    if (!maximizedInBatch(tabbed).contains(tabIds.first())) {
        tabbed.clear();
        engine->toggleMaximizeToEdges(QStringLiteral("S1"));
        QCoreApplication::processEvents();
    }
    QVERIFY2(!tabbed.isEmpty(), "the maximize must emit a batch");
    const QSet<QString> flagged = maximizedInBatch(tabbed);
    QVERIFY2(flagged.contains(tabIds.first()), "the tabbed column must read as maximized, or this leg proves nothing");
    // EVERY tile of the column, shown or hidden.
    for (const QString& wid : std::as_const(tabIds)) {
        QVERIFY2(flagged.contains(wid),
                 qPrintable(QStringLiteral("hidden tab %1 must still publish maximizedToEdges").arg(wid)));
    }
}

void TestScrollEngineMaximize::twoColumnsCanBeMaximizedAtOnce()
{
    // The strip keeps ONE pre-maximize slot, so maximizing a second column
    // discards the first's stored width. That is a real, reachable state and
    // the behaviour on the way back out is what this pins: the first column
    // un-maximizes to the DEFAULT width rather than to the width it had, and
    // says so honestly rather than silently restoring something else.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    QCoreApplication::processEvents();

    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY2(state->strip().columnOfWindow(QStringLiteral("app|a"))
                 != state->strip().columnOfWindow(QStringLiteral("app|b")),
             "the two windows must sit in different columns");

    // A DISTINCTIVE width, deliberately not the context default. The
    // no-usable-slot fallback restores the DEFAULT, so a column that started at
    // the default comes back at an identical value and the assertion below
    // would pass whether or not the slot was discarded — the same trap
    // test_scrollstrip_ops documents beside its own Fixed(377).
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    QCoreApplication::processEvents();
    engine->setColumnWidth(ColumnWidth::makeFixed(377), QStringLiteral("S1"));
    QCoreApplication::processEvents();
    const ColumnWidth aOriginal = widthOf(engine, QStringLiteral("app|a"));
    QCOMPARE(aOriginal, ColumnWidth::makeFixed(377));

    engine->toggleMaximizeColumn(QStringLiteral("S1"), QStringLiteral("app|a"));
    QCoreApplication::processEvents();
    engine->toggleMaximizeColumn(QStringLiteral("S1"), QStringLiteral("app|b"));
    QCoreApplication::processEvents();

    // BOTH columns are full width at the same time. The headline claim, and
    // asserted on the stored WIDTH rather than on a batch: these are the width
    // verb's presses, which have no wire flag of their own, and only one of two
    // full-width columns fits the viewport so the other's emitted rect is a
    // parked one. The maximize arm writes Proportion(1.0) verbatim, so an
    // implementation that un-maximized a on b's behalf would leave a at the
    // no-slot fallback width instead and fail here.
    QCOMPARE(widthOf(engine, QStringLiteral("app|a")), ColumnWidth::makeProportion(1.0));
    QCOMPARE(widthOf(engine, QStringLiteral("app|b")), ColumnWidth::makeProportion(1.0));

    // a's stored width is gone with the single slot, so its un-maximize takes
    // the no-slot fallback rather than restoring aOriginal.
    engine->toggleMaximizeColumn(QStringLiteral("S1"), QStringLiteral("app|a"));
    QCoreApplication::processEvents();
    const ColumnWidth aAfter = widthOf(engine, QStringLiteral("app|a"));
    QVERIFY2(aAfter != ColumnWidth::makeFixed(-1), "a must still be in the strip");
    QVERIFY2(aAfter != aOriginal,
             "the second maximize discarded the single slot, so a must NOT come back at its original width");
    // And b is untouched by a's un-maximize, the mirror of the claim above.
    QCOMPARE(widthOf(engine, QStringLiteral("app|b")), ColumnWidth::makeProportion(1.0));
}

void TestScrollEngineMaximize::namedVerbTogglesBackOnASecondPress()
{
    // The window-scoped entry point is driven twice on the SAME window. The
    // compositor does exactly this — it dispatches the verb on every
    // intercepted maximize, so the user's un-maximize click is a second call
    // through this path, not through the active-column spelling.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    QCoreApplication::processEvents();

    // Focus a, act on b, so the two answers differ throughout.
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    QCoreApplication::processEvents();
    const ColumnWidth aBefore = widthOf(engine, QStringLiteral("app|a"));
    const ColumnWidth bBefore = widthOf(engine, QStringLiteral("app|b"));

    engine->toggleMaximizeColumn(QStringLiteral("S1"), QStringLiteral("app|b"));
    QCoreApplication::processEvents();
    QVERIFY2(widthOf(engine, QStringLiteral("app|b")) != bBefore, "the first press must maximize b's column");

    engine->toggleMaximizeColumn(QStringLiteral("S1"), QStringLiteral("app|b"));
    QCoreApplication::processEvents();
    QCOMPARE(widthOf(engine, QStringLiteral("app|b")), bBefore);
    QCOMPARE(widthOf(engine, QStringLiteral("app|a")), aBefore);
}

void TestScrollEngineMaximize::maximizeSurvivesAModeRoundTripWithoutItsRestoreSlot()
{
    // A composition no step-level test covers, and the end state is
    // lossy-but-consistent rather than wrong — which is exactly why nothing
    // caught it: the column's WIDTH is stashed and comes back, the strip's
    // single pre-maximize slot is not stashed and does not.
    //
    // So after a mode round trip the column is still full width and, since
    // the maximize-to-edges flag rides the stash, still publishes
    // maximizedToEdges (the effect re-asserts the KWin bit from that, which
    // is the only thing that restores it), while the next un-maximize press
    // falls to the default-width arm instead of the user's old width. Both
    // halves are asserted, because it is the PAIR that is the contract. The
    // wire flag is driven by toggleMaximizeToEdges beside the width verb —
    // the width verb alone publishes nothing.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    QCoreApplication::processEvents();

    engine->toggleMaximizeColumn(QStringLiteral("S1"), QStringLiteral("app|a"));
    QCoreApplication::processEvents();
    engine->toggleMaximizeToEdges(QStringLiteral("S1"), QStringLiteral("app|a"));
    QCoreApplication::processEvents();
    const ColumnWidth maximized = widthOf(engine, QStringLiteral("app|a"));

    // Mode reassignment of the same context — the engine-side proxy for a
    // placement-mode flip, which is arbitrated in the daemon and has no seam
    // in this library. The re-adoption arrivals are spied, because THEY are
    // the batches that carry a rect change: a later retile re-resolves the
    // identical rects and identical flag membership, so the emit-on-change
    // gate legitimately suppresses it and a spy installed afterwards would
    // read empty.
    engine->setActiveScreens({});
    QCoreApplication::processEvents();
    engine->setActiveScreens({QStringLiteral("S1")});
    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    QCoreApplication::processEvents();

    QCOMPARE(widthOf(engine, QStringLiteral("app|a")), maximized);

    // The load-bearing half, asserted on the STRIP so it is unconditional: the
    // rebuilt column carries the declared flag, which is the only thing the
    // effect re-asserts the KWin maximize bit from.
    ScrollState* readopted = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(readopted);
    const int aCol = readopted->strip().columnOfWindow(QStringLiteral("app|a"));
    QVERIFY2(aCol >= 0, "the re-adopted window must be back on the strip");
    QVERIFY2(readopted->strip().columns().at(aCol).maximizedToEdges,
             "the re-adopted column must come back maximized to edges");
    // And the wire half, from the arrival batches themselves.
    QVERIFY2(!tiled.isEmpty(), "the re-adoption arrivals must emit a batch");
    QVERIFY2(maximizedInBatch(tiled).contains(QStringLiteral("app|a")),
             "the re-adopted column must re-publish maximizedToEdges, since nothing else restores the KWin bit");

    // The slot did NOT survive, so this press takes the no-usable-slot arm.
    engine->toggleMaximizeColumn(QStringLiteral("S1"), QStringLiteral("app|a"));
    QCoreApplication::processEvents();
    QVERIFY2(widthOf(engine, QStringLiteral("app|a")) != maximized,
             "the press must leave the column narrower than full");
}

void TestScrollEngineMaximize::maximizedToEdgesRoundTripsThroughTheBlob()
{
    // The persisted half of the maximize-to-edges flag: serializeStripState
    // writes the per-column key only when set, restoreStripState reads it back
    // with an absent-key false fallback, and the claim path re-asserts it on
    // the rebuilt column. Deleting either the write or the read leaves the
    // in-memory stash tests green, so this one drives the blob itself.
    //
    // Lives here rather than in test_scrollengine_persistence, which owns the
    // stash and blob mechanics but is already past the file-size ceiling: the
    // maximize concern is this file's, and the blob is one more surface of it.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|m1"), QStringLiteral("S1"), 0, 0);
    ScrollState* live = stateFor(engine1, QStringLiteral("S1"));
    QVERIFY(live);
    // A DISTINCTIVE width, so the legacy arm below can say something about the
    // rest of the column rather than only about the flag. Written BEFORE the
    // toggle: a width write is one of the verbs that clears the flag.
    QVERIFY(live->strip().setActiveColumnWidth(ColumnWidth::makeFixed(377)));
    engine1->toggleMaximizeToEdges(QStringLiteral("S1"));
    QVERIFY(live->strip().columns().first().maximizedToEdges);

    const QJsonObject blob = engine1->serializeStripState();
    QVERIFY(!blob.isEmpty());

    ScrollEngine* engine2 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine2->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine2->restoreStripState(blob);
    engine2->windowOpened(QStringLiteral("app|m1"), QStringLiteral("S1"), 0, 0);
    ScrollState* restored = stateFor(engine2, QStringLiteral("S1"));
    QVERIFY(restored);
    QCOMPARE(restored->strip().columns().size(), 1);
    QVERIFY(restored->strip().columns().first().maximizedToEdges);

    // A blob from before the key existed reads false, and the rest of the
    // column still restores — asserted on the stored WIDTH, so "still
    // restores" is a claim the slot actually checks rather than prose beside a
    // column count.
    const QString key = QStringLiteral("S1|1|");
    QVERIFY(blob.contains(key));
    QJsonObject legacy = blob;
    QJsonObject payload = legacy.value(key).toObject();
    QJsonArray columns = payload.value(QLatin1String("columns")).toArray();
    QVERIFY(!columns.isEmpty());
    QJsonObject colObj = columns.first().toObject();
    QVERIFY(colObj.contains(QLatin1String("maximizedToEdges")));
    colObj.remove(QLatin1String("maximizedToEdges"));
    columns.replace(0, colObj);
    payload.insert(QLatin1String("columns"), columns);
    legacy.insert(key, payload);

    ScrollEngine* engine3 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine3->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine3->restoreStripState(legacy);
    engine3->windowOpened(QStringLiteral("app|m1"), QStringLiteral("S1"), 0, 0);
    ScrollState* attached = stateFor(engine3, QStringLiteral("S1"));
    QVERIFY(attached);
    QCOMPARE(attached->strip().columns().size(), 1);
    QVERIFY(!attached->strip().columns().first().maximizedToEdges);
    QCOMPARE(attached->strip().columns().first().width, ColumnWidth::makeFixed(377));
}

void TestScrollEngineMaximize::maximizedToEdgesTransfersOnAFuzzyAppIdClaim()
{
    // The cross-session appId claim hands a NEW same-app window a dead
    // sibling's slot, and the maximize-to-edges flag goes WITH it — the
    // opposite call from windowedFullscreen, whose claim deliberately stops at
    // the slot (windowedFullscreenFuzzyClaimDoesNotTransfer pins that side).
    //
    // The difference is what the two flags describe. Windowed fullscreen is a
    // presentation pushed onto the client, so handing it to a window that never
    // asked for it is a surprise. Maximize-to-edges says how the COLUMN
    // presents, on the same terms as the width and display the claim already
    // transfers, and the restore applies it from the arrival that CREATES the
    // column (engine_core's creation-only arm), which for a claimed single-tile
    // column is this arrival.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|u1"), QStringLiteral("S1"), 0, 0);
    engine1->windowFocused(QStringLiteral("app|u1"), QStringLiteral("S1"));
    engine1->toggleMaximizeToEdges(QStringLiteral("S1"));
    ScrollState* live = stateFor(engine1, QStringLiteral("S1"));
    QVERIFY(live);
    QVERIFY(live->strip().columns().first().maximizedToEdges);
    const QJsonObject blob = engine1->serializeStripState();
    QVERIFY(!blob.isEmpty());

    ScrollEngine* engine2 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine2->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine2->restoreStripState(blob);
    // Different uuid, same appId: the fuzzy claim fires.
    engine2->windowOpened(QStringLiteral("app|n1"), QStringLiteral("S1"), 0, 0);
    QCOMPARE(engine2->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|n1")), 0);
    QVERIFY2(columnFlagged(engine2, QStringLiteral("app|n1")),
             "a fuzzy appId claim must bring the column's maximize-to-edges state with the slot");
}

void TestScrollEngineMaximize::floatingASoleTileCarriesTheFlagBothWays()
{
    // The float/minimize round trip of a maximized column's ONLY window. The
    // flag is declared column state and nothing re-derives it, so the
    // FloatRestore slot carries it out and back; without that carry a minimize
    // and restore silently un-maximizes the window.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    // A sibling column, so the float does not empty the strip and the restore
    // has to find its way back into a populated one.
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    QCoreApplication::processEvents();
    engine->toggleMaximizeToEdges(QStringLiteral("S1"), QStringLiteral("app|a"));
    QCoreApplication::processEvents();
    QVERIFY(columnFlagged(engine, QStringLiteral("app|a")));

    engine->setWindowFloat(QStringLiteral("app|a"), true, QStringLiteral("S1"));
    QCoreApplication::processEvents();
    QVERIFY2(engine->isWindowFloatingInScroll(QStringLiteral("app|a")), "the window must actually have floated");
    ScrollState* floated = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(floated);
    QCOMPARE(floated->strip().columnOfWindow(QStringLiteral("app|a")), -1);
    // The sibling never carried the flag and must not acquire it.
    QVERIFY(!columnFlagged(engine, QStringLiteral("app|b")));

    engine->setWindowFloat(QStringLiteral("app|a"), false, QStringLiteral("S1"));
    QCoreApplication::processEvents();
    QVERIFY2(!engine->isWindowFloatingInScroll(QStringLiteral("app|a")), "the window must have come back to the strip");
    QVERIFY2(columnFlagged(engine, QStringLiteral("app|a")), "the rebuilt column must come back maximized to edges");
    QVERIFY2(!columnFlagged(engine, QStringLiteral("app|b")), "the returning tile must not flag the sibling column");
}

void TestScrollEngineMaximize::floatingOneTileOfTwoLeavesTheFlagWithTheColumn()
{
    // The other half of the lone-tile rule. A tile floating out of a SHARED
    // column captures no flag, because the column survives and keeps its own,
    // and re-asserting it from a returning sibling would re-maximize a column
    // the user un-maximized in the meantime — which is exactly what this slot
    // drives.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    // Focus the FIRST column: consume pulls the NEXT column's window in, so
    // running it on the last column would consume nothing.
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1"));
    QCoreApplication::processEvents();
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QCOMPARE(state->strip().columnCount(), 1);
    QCOMPARE(state->strip().columns().first().tiles.size(), 2);

    engine->toggleMaximizeToEdges(QStringLiteral("S1"), QStringLiteral("app|a"));
    QCoreApplication::processEvents();
    QVERIFY(columnFlagged(engine, QStringLiteral("app|a")));

    engine->setWindowFloat(QStringLiteral("app|b"), true, QStringLiteral("S1"));
    QCoreApplication::processEvents();
    QVERIFY2(engine->isWindowFloatingInScroll(QStringLiteral("app|b")), "the tile must actually have floated");
    // The column survived the float with its own flag, untouched.
    QVERIFY2(columnFlagged(engine, QStringLiteral("app|a")),
             "a column that survives one tile floating out keeps its own flag");

    // The user un-maximizes while b is away. A returning tile that re-asserted
    // a flag it should never have captured would undo this.
    engine->toggleMaximizeToEdges(QStringLiteral("S1"), QStringLiteral("app|a"));
    QCoreApplication::processEvents();
    QVERIFY(!columnFlagged(engine, QStringLiteral("app|a")));

    engine->setWindowFloat(QStringLiteral("app|b"), false, QStringLiteral("S1"));
    QCoreApplication::processEvents();
    ScrollState* back = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(back);
    const int aCol = back->strip().columnOfWindow(QStringLiteral("app|a"));
    const int bCol = back->strip().columnOfWindow(QStringLiteral("app|b"));
    QVERIFY2(aCol >= 0 && bCol >= 0, "both windows must be back on the strip");
    // Rejoined the stack it left, so the flag assertion below is about that
    // column rather than about a fresh one that trivially has no flag.
    QCOMPARE(bCol, aCol);
    QVERIFY2(!columnFlagged(engine, QStringLiteral("app|b")),
             "a tile that floated out of a SHARED column must not re-maximize it on the way back");
}

QTEST_GUILESS_MAIN(TestScrollEngineMaximize)
#include "test_scrollengine_maximize.moc"

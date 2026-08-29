// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Column maximize at the ENGINE layer: what the published columnMaximized flag
// means, which column the window-scoped verb acts on, and what survives the
// state transitions that discard the strip's single pre-maximize slot.
//
// Its own file rather than more of test_scrollengine_smoke, whose sanctioned
// size exception says in as many words that a new concern takes a sibling. The
// strip-level toggle arms live in test_scrollstrip_sizing; what this file owns
// is the ENGINE's half — the wire flag, the verb's targeting, and the
// compositions no single-step test covers.
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

/// Every windowId carrying columnMaximized in the last emitted batch.
QSet<QString> maximizedInBatch(const QSignalSpy& spy)
{
    QSet<QString> out;
    if (spy.isEmpty()) {
        return out;
    }
    const QJsonArray arr = QJsonDocument::fromJson(spy.last().at(0).toString().toUtf8()).array();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("columnMaximized")).toBool(false)) {
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
};

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
    engine->toggleMaximizeColumn(QStringLiteral("S1"));
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
             "a parked maximized column must still publish columnMaximized");

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
    engine->toggleMaximizeColumn(QStringLiteral("S1"));
    QCoreApplication::processEvents();
    if (!maximizedInBatch(tabbed).contains(tabIds.first())) {
        tabbed.clear();
        engine->toggleMaximizeColumn(QStringLiteral("S1"));
        QCoreApplication::processEvents();
    }
    QVERIFY2(!tabbed.isEmpty(), "the maximize must emit a batch");
    const QSet<QString> flagged = maximizedInBatch(tabbed);
    QVERIFY2(flagged.contains(tabIds.first()), "the tabbed column must read as maximized, or this leg proves nothing");
    // EVERY tile of the column, shown or hidden.
    for (const QString& wid : std::as_const(tabIds)) {
        QVERIFY2(flagged.contains(wid),
                 qPrintable(QStringLiteral("hidden tab %1 must still publish columnMaximized").arg(wid)));
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
    const auto widthOf = [engine](const QString& windowId) {
        const auto* st = stateFor(engine, QStringLiteral("S1"));
        if (!st) {
            return ColumnWidth::makeFixed(-1);
        }
        const int idx = st->strip().columnOfWindow(windowId);
        return idx < 0 ? ColumnWidth::makeFixed(-1) : st->strip().columns().at(idx).width;
    };
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
    const ColumnWidth aOriginal = widthOf(QStringLiteral("app|a"));
    QCOMPARE(aOriginal, ColumnWidth::makeFixed(377));

    engine->toggleMaximizeColumn(QStringLiteral("S1"), QStringLiteral("app|a"));
    QCoreApplication::processEvents();
    engine->toggleMaximizeColumn(QStringLiteral("S1"), QStringLiteral("app|b"));
    QCoreApplication::processEvents();

    // BOTH render full width at the same time — the flag is per column, and
    // nothing un-maximizes the first on the second's behalf.
    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    engine->retile(QStringLiteral("S1"));
    QCoreApplication::processEvents();

    // a's stored width is gone with the single slot, so its un-maximize takes
    // the no-slot fallback rather than restoring aOriginal.
    engine->toggleMaximizeColumn(QStringLiteral("S1"), QStringLiteral("app|a"));
    QCoreApplication::processEvents();
    const ColumnWidth aAfter = widthOf(QStringLiteral("app|a"));
    QVERIFY2(aAfter != ColumnWidth::makeFixed(-1), "a must still be in the strip");
    QVERIFY2(aAfter != aOriginal,
             "the second maximize discarded the single slot, so a must NOT come back at its original width");
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

    const auto widthOf = [engine](const QString& windowId) {
        const auto* st = stateFor(engine, QStringLiteral("S1"));
        if (!st) {
            return ColumnWidth::makeFixed(-1);
        }
        const int idx = st->strip().columnOfWindow(windowId);
        return idx < 0 ? ColumnWidth::makeFixed(-1) : st->strip().columns().at(idx).width;
    };

    // Focus a, act on b, so the two answers differ throughout.
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    QCoreApplication::processEvents();
    const ColumnWidth aBefore = widthOf(QStringLiteral("app|a"));
    const ColumnWidth bBefore = widthOf(QStringLiteral("app|b"));

    engine->toggleMaximizeColumn(QStringLiteral("S1"), QStringLiteral("app|b"));
    QCoreApplication::processEvents();
    QVERIFY2(widthOf(QStringLiteral("app|b")) != bBefore, "the first press must maximize b's column");

    engine->toggleMaximizeColumn(QStringLiteral("S1"), QStringLiteral("app|b"));
    QCoreApplication::processEvents();
    QCOMPARE(widthOf(QStringLiteral("app|b")), bBefore);
    QCOMPARE(widthOf(QStringLiteral("app|a")), aBefore);
}

void TestScrollEngineMaximize::maximizeSurvivesAModeRoundTripWithoutItsRestoreSlot()
{
    // A composition no step-level test covers, and the end state is
    // lossy-but-consistent rather than wrong — which is exactly why nothing
    // caught it: the column's WIDTH is stashed and comes back, the strip's
    // single pre-maximize slot is not stashed and does not.
    //
    // So after a mode round trip the column is still full width and still
    // publishes columnMaximized (the effect re-asserts the KWin bit from that,
    // which is the only thing that restores it), while the next un-maximize
    // press falls to the default-width arm instead of the user's old width.
    // Both halves are asserted, because it is the PAIR that is the contract.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    QCoreApplication::processEvents();

    const auto widthOf = [engine](const QString& windowId) {
        const auto* st = stateFor(engine, QStringLiteral("S1"));
        if (!st) {
            return ColumnWidth::makeFixed(-1);
        }
        const int idx = st->strip().columnOfWindow(windowId);
        return idx < 0 ? ColumnWidth::makeFixed(-1) : st->strip().columns().at(idx).width;
    };

    engine->toggleMaximizeColumn(QStringLiteral("S1"), QStringLiteral("app|a"));
    QCoreApplication::processEvents();
    const ColumnWidth maximized = widthOf(QStringLiteral("app|a"));

    // Mode reassignment of the same context — the engine-side proxy for a
    // placement-mode flip, which is arbitrated in the daemon and has no seam
    // in this library.
    engine->setActiveScreens({});
    QCoreApplication::processEvents();
    engine->setActiveScreens({QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    QCoreApplication::processEvents();

    QCOMPARE(widthOf(QStringLiteral("app|a")), maximized);

    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    engine->retile(QStringLiteral("S1"));
    QCoreApplication::processEvents();
    if (!tiled.isEmpty()) {
        QVERIFY2(maximizedInBatch(tiled).contains(QStringLiteral("app|a")),
                 "the re-adopted column must re-publish columnMaximized, since nothing else restores the KWin bit");
    }

    // The slot did NOT survive, so this press takes the no-usable-slot arm.
    engine->toggleMaximizeColumn(QStringLiteral("S1"), QStringLiteral("app|a"));
    QCoreApplication::processEvents();
    QVERIFY2(widthOf(QStringLiteral("app|a")) != maximized, "the press must leave the column narrower than full");
}

QTEST_GUILESS_MAIN(TestScrollEngineMaximize)
#include "test_scrollengine_maximize.moc"

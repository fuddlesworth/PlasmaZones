// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCoreApplication>
#include <QTest>

#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include "helpers/AutotileFakes.h"

using namespace PhosphorScrollEngine;
using PhosphorEngine::WindowPlacement;
using PlasmaZones::TestHelpers::FakeStickyWindowTracking;

/**
 * @brief Close/reopen restore through the unified placement store, on the
 *        SCROLL engine.
 *
 * A reopened window carries a fresh KWin uuid, so its record matches by the
 * appId FIFO — never uuid-exact. The engine's open-time restore resolves that
 * through WindowPlacementStore::takeForReopen (the shared reopen pattern
 * autotile's insert path uses). These tests pin the regression that motivated
 * the hoist: the scroll engine consulted only peekExact, so a window floated
 * in scrolling mode, closed, and reopened re-tiled into the strip instead of
 * staying floated.
 */
class TestScrollReopenRestore : public QObject
{
    Q_OBJECT

private:
    static constexpr auto Screen = "S1";

    /// A headless tracker-wired engine on the geometry-provider seam,
    /// active on Screen. The activation echo mirrors the lib suite's
    /// makeProviderEngine (which cannot be reused here: it hardwires a
    /// null tracker, and this suite exists to test the tracker seam).
    static ScrollEngine* makeEngine(QObject* parent, FakeStickyWindowTracking* tracker)
    {
        auto* engine = new ScrollEngine(tracker, nullptr, parent);
        const auto geometry = [](const QString&) {
            return QRect(0, 0, 1200, 800);
        };
        engine->setScreenGeometryProviders(geometry, geometry);
        QObject::connect(engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested, engine,
                         [engine](const QString& windowId) {
                             engine->windowFocused(windowId, engine->screenForTrackedWindow(windowId));
                         });
        engine->setActiveScreens({QLatin1String(Screen)});
        engine->setCurrentDesktopForScreen(QLatin1String(Screen), 1);
        return engine;
    }

    static ScrollState* stateFor(ScrollEngine* engine, const QString& screenId)
    {
        return static_cast<ScrollState*>(engine->stateForScreen(screenId));
    }

    /// The daemon's close capture, condensed: snapshot the engine's slot,
    /// graft the free-geometry rect the shared layer would carry, record.
    static void captureClose(ScrollEngine* engine, FakeStickyWindowTracking* tracker, const QString& windowId,
                             const QRect& freeGeo = QRect())
    {
        auto record = engine->capturePlacement(windowId);
        QVERIFY(record.has_value());
        if (freeGeo.isValid()) {
            record->freeGeometryByScreen.insert(QLatin1String(Screen), freeGeo);
        }
        QVERIFY(tracker->placementStore().record(*record));
        engine->windowClosed(windowId);
    }

private Q_SLOTS:

    void reopenWithFreshUuidStaysFloating()
    {
        QObject owner;
        FakeStickyWindowTracking tracker;
        ScrollEngine* engine = makeEngine(&owner, &tracker);
        const QString screen = QLatin1String(Screen);

        engine->windowOpened(QStringLiteral("editor|e1"), screen, 0, 0);
        engine->windowOpened(QStringLiteral("term|t1"), screen, 0, 0);
        engine->setWindowFloat(QStringLiteral("term|t1"), true, screen);

        ScrollState* state = stateFor(engine, screen);
        QVERIFY(state);
        QVERIFY(state->isFloating(QStringLiteral("term|t1")));

        captureClose(engine, &tracker, QStringLiteral("term|t1"), QRect(40, 40, 500, 300));
        QVERIFY(!state->containsWindow(QStringLiteral("term|t1")));

        // Reopen: same app, FRESH uuid. The floating record must be consumed
        // via the appId FIFO and the window must arrive floating, not tiled.
        engine->windowOpened(QStringLiteral("term|t2"), screen, 0, 0);
        QVERIFY(state->isFloating(QStringLiteral("term|t2")));
        QVERIFY(!state->strip().containsWindow(QStringLiteral("term|t2")));
    }

    void reopenWithSameUuidStaysFloating()
    {
        // The daemon-restart shape (uuid stable) the old peekExact-only read
        // covered — pinned so the reopen fix cannot regress it.
        QObject owner;
        FakeStickyWindowTracking tracker;
        ScrollEngine* engine = makeEngine(&owner, &tracker);
        const QString screen = QLatin1String(Screen);

        engine->windowOpened(QStringLiteral("term|t1"), screen, 0, 0);
        engine->setWindowFloat(QStringLiteral("term|t1"), true, screen);
        captureClose(engine, &tracker, QStringLiteral("term|t1"), QRect(40, 40, 500, 300));

        engine->windowOpened(QStringLiteral("term|t1"), screen, 0, 0);
        ScrollState* state = stateFor(engine, screen);
        QVERIFY(state);
        QVERIFY(state->isFloating(QStringLiteral("term|t1")));
    }

    void reopenWithFreshUuidRestoresColumn()
    {
        QObject owner;
        FakeStickyWindowTracking tracker;
        ScrollEngine* engine = makeEngine(&owner, &tracker);
        const QString screen = QLatin1String(Screen);

        engine->windowOpened(QStringLiteral("one|a"), screen, 0, 0);
        engine->windowOpened(QStringLiteral("two|b"), screen, 0, 0);
        engine->windowOpened(QStringLiteral("three|c"), screen, 0, 0);
        const int closedColumn = engine->columnIndexForWindow(screen, QStringLiteral("two|b"));
        QVERIFY(closedColumn >= 0);

        captureClose(engine, &tracker, QStringLiteral("two|b"));

        engine->windowOpened(QStringLiteral("two|b2"), screen, 0, 0);
        QCOMPARE(engine->columnIndexForWindow(screen, QStringLiteral("two|b2")), closedColumn);
    }

    void geometrylessFloatingResidueNotConsumedByFreshSibling()
    {
        // A floating record with NO float-back rect is meaningful only for
        // the SAME instance; consumed by a FIFO sibling it would float a
        // fresh window at its spawn rect for no visible reason. Autotile's
        // accept rule, mirrored on scroll.
        QObject owner;
        FakeStickyWindowTracking tracker;
        ScrollEngine* engine = makeEngine(&owner, &tracker);
        const QString screen = QLatin1String(Screen);

        engine->windowOpened(QStringLiteral("term|t1"), screen, 0, 0);
        engine->setWindowFloat(QStringLiteral("term|t1"), true, screen);
        captureClose(engine, &tracker, QStringLiteral("term|t1")); // no free rect

        engine->windowOpened(QStringLiteral("term|t2"), screen, 0, 0);
        ScrollState* state = stateFor(engine, screen);
        QVERIFY(state);
        QVERIFY(!state->isFloating(QStringLiteral("term|t2")));
        QVERIFY(state->strip().containsWindow(QStringLiteral("term|t2")));
    }
};

QTEST_MAIN(TestScrollReopenRestore)
#include "test_scroll_reopen_restore.moc"

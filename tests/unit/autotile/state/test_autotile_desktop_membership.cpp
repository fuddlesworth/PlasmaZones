// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_autotile_desktop_membership.cpp
 *
 * The tiling engine's per-desktop membership arm: a window present on
 * several desktops (sticky, or a span such as {1,2}) holds a tile in each
 * desktop's state, and the paths that take a window OUT of the engine have
 * to take it out of every one of those states, not the one that happened to
 * be resolved. The scroll twin lives in test_scrollengine_membership.cpp.
 */

#include <QCoreApplication>
#include <QObject>
#include <QTest>

#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/TilingState.h>

#include "helpers/AutotileFakes.h"
#include "helpers/AutotileTestHelpers.h"

using namespace PlasmaZones;
using PhosphorTileEngine::AutotileEngine;

namespace {
const QString kScreen = QStringLiteral("DP-1");
const QString kInstance = QStringLiteral("11111111-2222-3333-4444-555555555555");
const QString kWindow = QStringLiteral("app|11111111-2222-3333-4444-555555555555");
const QString kOther = QStringLiteral("app|66666666-7777-8888-9999-000000000000");

PhosphorEngine::DesktopSpan sticky()
{
    PhosphorEngine::DesktopSpan span;
    span.known = true;
    span.sticky = true;
    return span;
}

PhosphorEngine::DesktopSpan on(QSet<int> desktops)
{
    PhosphorEngine::DesktopSpan span;
    span.known = true;
    span.desktops = std::move(desktops);
    return span;
}

/// A query answering @p windowSpan for kWindow and desktop 1 for everything
/// else, so the other windows never move.
PhosphorEngine::DesktopSpanQuery spanOf(const PhosphorEngine::DesktopSpan& windowSpan)
{
    return [windowSpan](const QString& windowId) {
        return windowId == kWindow ? windowSpan : on({1});
    };
}

struct Fixture
{
    PhosphorEngine::WindowRegistry registry;
    TestHelpers::FakeStickyWindowTracking tracker;
    AutotileEngine engine{nullptr, &tracker, nullptr, TestHelpers::testRegistry()};

    Fixture()
    {
        engine.setWindowRegistry(&registry);
        registry.canonicalizeWindowId(kWindow);
        engine.setAutotileScreens({kScreen});
    }

    /// The state for @p desktop, resolved by switching the screen to it.
    PhosphorTiles::TilingState* stateOn(int desktop)
    {
        engine.setCurrentDesktopForScreen(kScreen, desktop);
        return engine.tilingStateForScreen(kScreen);
    }

    void open(int desktop, const QStringList& windowIds)
    {
        engine.setCurrentDesktopForScreen(kScreen, desktop);
        for (const QString& windowId : windowIds) {
            engine.windowOpened(windowId, kScreen);
        }
        QCoreApplication::processEvents();
    }

    /// Adopt kWindow into @p desktop under @p span through the screen-wide
    /// pass the daemon runs after a switch.
    void switchAndReconcile(int desktop, const PhosphorEngine::DesktopSpan& span)
    {
        engine.setCurrentDesktopForScreen(kScreen, desktop);
        engine.setAutotileScreens({kScreen});
        engine.reconcileDesktopMemberships(kScreen, spanOf(span));
        QCoreApplication::processEvents();
    }
};
} // namespace

class TestAutotileDesktopMembership : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void stickyWindowGetsATileOnEveryDesktopItIsSwitchedTo()
    {
        Fixture f;
        f.open(1, {kOther, kWindow});
        QVERIFY(f.stateOn(1)->containsWindow(kWindow));

        f.switchAndReconcile(2, sticky());
        QVERIFY2(f.stateOn(2)->containsWindow(kWindow), "the desktop just entered adopts the sticky window");
        QVERIFY2(!f.stateOn(2)->containsWindow(kOther), "a desktop-1 window is not dragged along");
        QVERIFY2(f.stateOn(1)->containsWindow(kWindow), "adoption adds a context, it does not move the window");
        // The primary membership follows the desktop in view.
        f.engine.setCurrentDesktopForScreen(kScreen, 2);
        QCOMPARE(f.engine.heldKeyForWindow(kWindow)->desktop, 2);
        f.engine.setCurrentDesktopForScreen(kScreen, 1);
        QCOMPARE(f.engine.heldKeyForWindow(kWindow)->desktop, 1);
    }

    // The path the author left unexercised: a close arrives while one of the
    // window's desktops is in view, and every OTHER desktop's state has to
    // give the window up too, or those layouts keep reserving a slot for a
    // closed window that nothing can ever reap.
    void closingAMultiDesktopWindowClearsEveryDesktop()
    {
        Fixture f;
        f.open(1, {kOther, kWindow});
        f.switchAndReconcile(2, sticky());
        QVERIFY(f.stateOn(2)->containsWindow(kWindow));

        f.engine.setCurrentDesktopForScreen(kScreen, 2);
        f.engine.windowClosed(kWindow);
        QCoreApplication::processEvents();

        QVERIFY(!f.stateOn(2)->containsWindow(kWindow));
        QVERIFY2(!f.stateOn(1)->containsWindow(kWindow), "the background desktop's state must drop the window too");
        QVERIFY(!f.engine.isWindowTracked(kWindow));
        QVERIFY(!f.engine.heldKeyForWindow(kWindow).has_value());
        QVERIFY2(f.stateOn(1)->containsWindow(kOther), "an unrelated window on desktop 1 is untouched");
    }

    // A window that leaves one engine for another (the cross-mode handoff)
    // leaves every desktop of it.
    void handoffReleaseClearsEveryDesktop()
    {
        Fixture f;
        f.open(1, {kWindow});
        f.switchAndReconcile(2, sticky());
        QVERIFY(f.stateOn(2)->containsWindow(kWindow));

        f.engine.handoffRelease(kWindow);
        QVERIFY(!f.stateOn(2)->containsWindow(kWindow));
        QVERIFY(!f.stateOn(1)->containsWindow(kWindow));
        QVERIFY(!f.engine.isWindowTracked(kWindow));
    }

    // The per-window form the daemon drives from a metadata change: a span
    // that shrinks releases only the desktop the window left, and keeps the
    // one it still covers.
    void spanShrinkReleasesOnlyTheDesktopLeft()
    {
        Fixture f;
        f.open(1, {kWindow});
        f.switchAndReconcile(2, on({1, 2}));
        QVERIFY(f.stateOn(2)->containsWindow(kWindow));

        // Still on desktop 2, the window is un-spanned back to desktop 2 only.
        f.engine.setCurrentDesktopForScreen(kScreen, 2);
        const auto result = f.engine.reconcileWindowMemberships(kWindow, spanOf(on({2})));
        QCoreApplication::processEvents();
        QCOMPARE(result.released.size(), 1);
        QCOMPARE(result.released.first().second.desktop, 1);
        QVERIFY(result.adopted.isEmpty());
        QVERIFY(!f.stateOn(1)->containsWindow(kWindow));
        QVERIFY(f.stateOn(2)->containsWindow(kWindow));
        QVERIFY(f.engine.isWindowTracked(kWindow));
    }

    // A window whose desktop the registry has not stamped yet is neither
    // sticky nor anywhere: reading it as "every desktop" adopted such windows
    // into every desktop the user visited.
    void unknownSpanAdoptsNothingAndReleasesNothing()
    {
        Fixture f;
        f.open(1, {kWindow});
        f.switchAndReconcile(2, PhosphorEngine::DesktopSpan{});
        QVERIFY(!f.stateOn(2)->containsWindow(kWindow));
        QVERIFY(f.stateOn(1)->containsWindow(kWindow));
    }

    // A minimized window is hidden; a tile granted to it would hold a layout
    // slot the user cannot see. Its unminimize re-announces it, which adopts
    // it then.
    void minimizedWindowIsNotAdopted()
    {
        Fixture f;
        f.open(1, {kWindow});
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.title = QStringLiteral("t");
        meta.virtualDesktop = 0;
        meta.isSticky = true;
        meta.isMinimized = true;
        f.registry.upsert(kInstance, meta);

        f.switchAndReconcile(2, sticky());
        QVERIFY2(!f.stateOn(2)->containsWindow(kWindow), "a minimized window must not take a tile");
        QVERIFY(f.stateOn(1)->containsWindow(kWindow));
    }

    // A window the user floated on the desktop it came from is floating on
    // the desktop it is adopted into, not given a tile there while the
    // daemon's per-window mirror still says "floating".
    void floatingSourceIsAdoptedAsFloating()
    {
        Fixture f;
        f.open(1, {kOther, kWindow});
        f.engine.setWindowFloat(kWindow, true, kScreen);
        QVERIFY(f.stateOn(1)->isFloating(kWindow));

        f.switchAndReconcile(2, sticky());
        QVERIFY(f.stateOn(2)->containsWindow(kWindow));
        QVERIFY2(f.stateOn(2)->isFloating(kWindow), "the float state crosses with the window");
    }

    // The float a window carries into a new desktop is read from EVERY
    // context it holds, not from the primary: with no membership yet in the
    // desktop being entered, the primary falls back to the first-adopted
    // desktop, which is not the one the user floated it on.
    void adoptionReadsTheFloatFromAnyHeldContext()
    {
        Fixture f;
        f.open(1, {kOther, kWindow});
        f.switchAndReconcile(2, sticky());
        QVERIFY(!f.stateOn(2)->isFloating(kWindow));
        f.engine.setCurrentDesktopForScreen(kScreen, 2);
        f.engine.setWindowFloat(kWindow, true, kScreen);
        QVERIFY(f.stateOn(2)->isFloating(kWindow));
        QVERIFY(!f.stateOn(1)->isFloating(kWindow));

        f.switchAndReconcile(3, sticky());
        QVERIFY(f.stateOn(3)->containsWindow(kWindow));
        QVERIFY2(f.stateOn(3)->isFloating(kWindow), "the float the user last saw is the one that crosses");
    }

    // The minimize-suspension shape: floated on the desktop it was on, the
    // user switches desktops while it is hidden (a minimized window is not
    // adopted), and the unminimize's unfloat arrives with the screen showing
    // a desktop the window holds no place in. The suspension is lifted in
    // every context that holds it and the desktop in view adopts the window
    // as a tile, instead of the primary reading "not floating" and leaving
    // it unmanaged where the user sees it.
    void unfloatOnAnUnheldDesktopLiftsTheSuspensionAndAdopts()
    {
        Fixture f;
        f.open(1, {kOther, kWindow});
        f.engine.setWindowFloat(kWindow, true, kScreen);
        QVERIFY(f.stateOn(1)->isFloating(kWindow));

        f.engine.setCurrentDesktopForScreen(kScreen, 2);
        f.engine.setAutotileScreens({kScreen});
        QVERIFY(!f.engine.tilingStateForScreen(kScreen)->containsWindow(kWindow));
        f.engine.setWindowFloat(kWindow, false, kScreen);
        QCoreApplication::processEvents();
        QVERIFY2(f.stateOn(2)->containsWindow(kWindow), "the desktop in view adopts the window");
        QVERIFY(!f.stateOn(2)->isFloating(kWindow));
        QVERIFY2(!f.stateOn(1)->isFloating(kWindow), "the suspension float is lifted where it was set");
        QVERIFY(f.stateOn(1)->containsWindow(kWindow));
    }

    // A membership under the screen's sticky pin is not evidence the window
    // left anything: the pin keys every desktop's state by the pinned desktop,
    // and the engine's own unpin migration moves it.
    void pinnedKeyIsNeverReleased()
    {
        Fixture f;
        f.tracker.stickyWindows.insert(kWindow);
        f.open(1, {kWindow});
        f.engine.updateStickyScreenPins(
            [](const QString&) {
                return true;
            },
            PhosphorEngine::StickyPinPhase::Acquire);
        QCOMPARE(f.engine.stickyPinnedDesktopForScreen(kScreen), 1);

        // The screen shows desktop 3 (the pin keeps the key at 1) and the
        // window is reported on desktop 3 only.
        f.engine.setCurrentDesktopForScreen(kScreen, 3);
        f.engine.reconcileWindowMemberships(kWindow, spanOf(on({3})));
        QCoreApplication::processEvents();
        QVERIFY2(f.engine.tilingStateForScreen(kScreen)->containsWindow(kWindow),
                 "the pinned state keeps the window; the unpin migration owns the move");
    }

    // A pinned state that has been emptied has nothing to keep the pin for,
    // and left standing the pin would key every later open under a desktop
    // the user is not on.
    void emptiedPinnedStateReleasesThePin()
    {
        Fixture f;
        f.tracker.stickyWindows.insert(kWindow);
        f.open(1, {kWindow});
        f.engine.updateStickyScreenPins(
            [](const QString&) {
                return true;
            },
            PhosphorEngine::StickyPinPhase::Acquire);
        QCOMPARE(f.engine.stickyPinnedDesktopForScreen(kScreen), 1);

        f.engine.windowClosed(kWindow);
        f.engine.setCurrentDesktopForScreen(kScreen, 2);
        f.engine.updateStickyScreenPins(
            [](const QString&) {
                return true;
            },
            PhosphorEngine::StickyPinPhase::Release);
        QCOMPARE(f.engine.stickyPinnedDesktopForScreen(kScreen), 0);
    }
};

QTEST_GUILESS_MAIN(TestAutotileDesktopMembership)
#include "test_autotile_desktop_membership.moc"

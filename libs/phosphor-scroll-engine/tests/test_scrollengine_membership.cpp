// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The scroll engine's per-desktop membership arm beyond the bookkeeping the
// stripcontext suite pins: what the compositor is told when a window is
// adopted or released, what happens to a window's OTHER strips when it
// closes, that one screen's pass never touches another screen's windows, and
// that a floated window crosses as floating.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include "scrollstriptestutils.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorScrollEngine;
using ScrollTestUtils::defaultScreenRect;
using ScrollTestUtils::GeometryFn;
using ScrollTestUtils::makeProviderEngine;

namespace {
const QString kS1 = QStringLiteral("S1");
const QString kS2 = QStringLiteral("S2");
const QString kSticky = QStringLiteral("app|sticky");
const QString kD1 = QStringLiteral("app|d1");

PhosphorEngine::DesktopSpanQuery stickyOnly(const QString& stickyId)
{
    return [stickyId](const QString& windowId) {
        PhosphorEngine::DesktopSpan span;
        span.known = true;
        span.sticky = (windowId == stickyId);
        if (!span.sticky) {
            span.desktops = {1};
        }
        return span;
    };
}

bool holdsPlaceOn(ScrollEngine* engine, const QString& screenId, int desktop, const QString& windowId)
{
    engine->setCurrentDesktopForScreen(screenId, desktop);
    const auto held = engine->heldKeyForWindow(windowId);
    return held && held->screenId == screenId && held->desktop == desktop;
}

bool batchNames(const QSignalSpy& spy, const QString& windowId)
{
    for (const auto& emission : spy) {
        const QJsonArray batch = QJsonDocument::fromJson(emission.at(0).toString().toUtf8()).array();
        for (const QJsonValue& v : batch) {
            if (v.toObject().value(QLatin1String("windowId")).toString() == windowId) {
                return true;
            }
        }
    }
    return false;
}
} // namespace

class TestScrollEngineMembership : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    // Membership alone is not placement: the strip the user is looking at
    // gained a column, and the compositor has to be told with a batch naming
    // the adopted window. The emit-on-change gate cannot notice by itself,
    // because the window's rect memory belongs to another desktop's strip.
    void adoptionEmitsABatchNamingTheWindow()
    {
        QObject owner;
        const GeometryFn geometry = [](const QString&) {
            return defaultScreenRect();
        };
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1}, geometry, geometry);
        engine->setCurrentDesktopForScreen(kS1, 1);
        engine->windowOpened(kD1, kS1, 0, 0);
        engine->windowOpened(kSticky, kS1, 0, 0);
        QCoreApplication::processEvents();

        engine->setCurrentDesktopForScreen(kS1, 2);
        engine->setActiveScreens({kS1});
        QCoreApplication::processEvents();
        QSignalSpy tiledSpy(engine, &ScrollEngine::windowsTiled);
        const auto result = engine->reconcileDesktopMemberships(kS1, stickyOnly(kSticky));
        QCoreApplication::processEvents();

        QCOMPARE(result.adopted.size(), 1);
        QCOMPARE(result.adopted.first().first, kSticky);
        QVERIFY2(batchNames(tiledSpy, kSticky), "the adopted column must reach the compositor");
    }

    // A window present on several desktops has a column in each; a close
    // arriving while one desktop is in view must take it out of the others
    // too, or those strips keep a tile nothing reaps, the layout keeps a gap
    // and the save persists a closed window as live structure.
    void closingAMultiDesktopWindowClearsEveryStrip()
    {
        QObject owner;
        const GeometryFn geometry = [](const QString&) {
            return defaultScreenRect();
        };
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1}, geometry, geometry);
        engine->setCurrentDesktopForScreen(kS1, 1);
        engine->windowOpened(kD1, kS1, 0, 0);
        engine->windowOpened(kSticky, kS1, 0, 0);
        QCoreApplication::processEvents();
        engine->setCurrentDesktopForScreen(kS1, 2);
        engine->reconcileDesktopMemberships(kS1, stickyOnly(kSticky));
        QCoreApplication::processEvents();
        QVERIFY(holdsPlaceOn(engine, kS1, 2, kSticky));
        QVERIFY(holdsPlaceOn(engine, kS1, 1, kSticky));

        engine->setCurrentDesktopForScreen(kS1, 2);
        engine->windowClosed(kSticky);
        QCoreApplication::processEvents();

        QVERIFY(!holdsPlaceOn(engine, kS1, 2, kSticky));
        QVERIFY2(!holdsPlaceOn(engine, kS1, 1, kSticky), "the background strip must drop the closed window");
        QVERIFY(!engine->isWindowTracked(kSticky));
        engine->setCurrentDesktopForScreen(kS1, 1);
        QVERIFY2(!engine->managedWindowOrder(kS1).contains(kSticky), "the desktop-1 strip must not list it");
        QVERIFY(engine->managedWindowOrder(kS1).contains(kD1));
    }

    // Two outputs showing different desktops: S2's pass reads only S2's
    // memberships, so a window on S1 is neither adopted into S2 nor released
    // from S1 by it.
    void aScreensPassNeverTouchesAnotherScreensWindows()
    {
        QObject owner;
        const GeometryFn geometry = [](const QString&) {
            return defaultScreenRect();
        };
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1, kS2}, geometry, geometry);
        engine->setCurrentDesktopForScreen(kS1, 1);
        engine->setCurrentDesktopForScreen(kS2, 2);
        engine->windowOpened(kSticky, kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|s2"), kS2, 0, 0);
        QCoreApplication::processEvents();
        // The S2 window lives on desktop 2 (the desktop S2 is showing).
        const PhosphorEngine::DesktopSpanQuery spanOf = [](const QString& windowId) {
            PhosphorEngine::DesktopSpan span;
            span.known = true;
            span.sticky = (windowId == kSticky);
            if (!span.sticky) {
                span.desktops = {windowId == QStringLiteral("app|s2") ? 2 : 1};
            }
            return span;
        };

        const auto result = engine->reconcileDesktopMemberships(kS2, spanOf);
        QCoreApplication::processEvents();
        QVERIFY(result.isEmpty());
        QVERIFY(holdsPlaceOn(engine, kS1, 1, kSticky));
        engine->setCurrentDesktopForScreen(kS2, 2);
        QVERIFY(!engine->managedWindowOrder(kS2).contains(kSticky));

        // S1's own pass onto desktop 3 adopts it there and only there.
        engine->setCurrentDesktopForScreen(kS1, 3);
        engine->reconcileDesktopMemberships(kS1, spanOf);
        QCoreApplication::processEvents();
        QVERIFY(holdsPlaceOn(engine, kS1, 3, kSticky));
        engine->setCurrentDesktopForScreen(kS2, 2);
        QVERIFY(!engine->managedWindowOrder(kS2).contains(kSticky));
    }

    // A window the user floated on the desktop it came from is floating on
    // the desktop it is adopted into, not tiled there with the daemon's
    // per-window float mirror still saying "floating".
    void floatingSourceIsAdoptedAsFloating()
    {
        QObject owner;
        const GeometryFn geometry = [](const QString&) {
            return defaultScreenRect();
        };
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1}, geometry, geometry);
        engine->setCurrentDesktopForScreen(kS1, 1);
        engine->windowOpened(kD1, kS1, 0, 0);
        engine->windowOpened(kSticky, kS1, 0, 0);
        QCoreApplication::processEvents();
        engine->setWindowFloat(kSticky, true, kS1);
        QCoreApplication::processEvents();
        QVERIFY(engine->isWindowFloatingInScroll(kSticky));

        engine->setCurrentDesktopForScreen(kS1, 2);
        engine->reconcileDesktopMemberships(kS1, stickyOnly(kSticky));
        QCoreApplication::processEvents();
        engine->setCurrentDesktopForScreen(kS1, 2);
        QVERIFY2(engine->isWindowFloatingInScroll(kSticky), "floating on desktop 1 means floating on desktop 2");
        QVERIFY2(!engine->managedWindowOrder(kS1).contains(kSticky), "and never a column there");
        QVERIFY(holdsPlaceOn(engine, kS1, 2, kSticky));
    }

    // A release from a desktop that is NOT on screen mutates persisted strip
    // structure; placementChanged is the sole producer of the strip's dirty
    // mark, so it has to fire for the background screen too.
    void backgroundReleaseMarksTheStripDirty()
    {
        QObject owner;
        const GeometryFn geometry = [](const QString&) {
            return defaultScreenRect();
        };
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1}, geometry, geometry);
        engine->setCurrentDesktopForScreen(kS1, 1);
        engine->windowOpened(kSticky, kS1, 0, 0);
        QCoreApplication::processEvents();
        engine->setCurrentDesktopForScreen(kS1, 2);
        engine->reconcileDesktopMemberships(kS1, stickyOnly(kSticky));
        QCoreApplication::processEvents();
        QVERIFY(holdsPlaceOn(engine, kS1, 2, kSticky));

        // Still showing desktop 2, the window is reported on desktop 2 only:
        // desktop 1's column (in the background) goes.
        engine->setCurrentDesktopForScreen(kS1, 2);
        QSignalSpy placementSpy(engine, &ScrollEngine::placementChanged);
        const PhosphorEngine::DesktopSpanQuery onlyTwo = [](const QString&) {
            PhosphorEngine::DesktopSpan span;
            span.known = true;
            span.desktops = {2};
            return span;
        };
        const auto result = engine->reconcileWindowMemberships(kSticky, onlyTwo);
        QCOMPARE(result.released.size(), 1);
        QCOMPARE(result.released.first().second.desktop, 1);
        QVERIFY2(placementSpy.count() > 0, "a background release must mark the strip dirty");
        QVERIFY(!holdsPlaceOn(engine, kS1, 1, kSticky));
        QVERIFY(holdsPlaceOn(engine, kS1, 2, kSticky));
    }
};

QTEST_GUILESS_MAIN(TestScrollEngineMembership)
#include "test_scrollengine_membership.moc"

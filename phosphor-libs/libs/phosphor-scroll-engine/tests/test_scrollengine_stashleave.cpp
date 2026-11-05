// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// A window that left the context a mode-round-trip stash remembers gives up
// its column there (F648, F696, F714). The strip on S1 leaves scrolling, which
// stashes its structure; the window then moves to another monitor, desktop or
// activity, and the strip's return must not pull it back into its old column
// at its old width. A window still on the stashed context keeps its column.
// Headless, on the geometry-provider seam like the sibling engine suites.

#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorScrollEngine/ScrollEngine.h>

#include "scrollstriptestutils.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

using namespace PhosphorScrollEngine;

using ScrollTestUtils::makeProviderEngine;
namespace Ax = ScrollTestUtils::Ax;

namespace {

const QString kS1 = QStringLiteral("S1");
const QString kS2 = QStringLiteral("S2");
const QString kA = QStringLiteral("app|a");
const QString kB = QStringLiteral("app|b");
const QString kC = QStringLiteral("app|c");
/// The default column on the suite's 1200 px main extent.
constexpr int kDefaultMain = 600;
/// One adjustColumnWidth(10.0) on a default column: 600 + 10% of 1200.
constexpr int kWidenedMain = 720;

PhosphorEngine::DesktopSpanQuery spanOn(const QSet<int>& desktops, bool sticky = false)
{
    return [desktops, sticky](const QString&) {
        PhosphorEngine::DesktopSpan span;
        span.known = true;
        span.sticky = sticky;
        span.desktops = desktops;
        return span;
    };
}

/// The window ids the saved blob stashes under (S1, @p desktop).
QStringList savedIds(const ScrollEngine* engine, int desktop)
{
    QStringList ids;
    const QString key = kS1 + QLatin1Char('|') + QString::number(desktop) + QLatin1Char('|');
    const QJsonObject entry = engine->serializeStripState().value(key).toObject();
    for (const QJsonValue& column : entry.value(QLatin1String("columns")).toArray()) {
        for (const QJsonValue& tile : column.toObject().value(QLatin1String("tiles")).toArray()) {
            ids.append(tile.toObject().value(QLatin1String("windowId")).toString());
        }
    }
    return ids;
}

} // namespace

class TestScrollEngineStashLeave : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    // b moves to another monitor while S1 is in another mode. The release
    // the daemon sends off the kept screen gives up its column on S1.
    void leftScreenGivesUpItsColumn()
    {
        QObject owner;
        ScrollEngine* engine = stripWithWidenedB(&owner, {kS1});
        engine->setActiveScreens({});

        engine->releaseWindowOffScreen(kB, kS2);

        reopenS1(engine);
        QCOMPARE(Ax::mainLen(engine->lastManagedRect(kB)), kDefaultMain);
        QCOMPARE(Ax::mainLen(engine->lastManagedRect(kA)), kDefaultMain);
    }

    // b opens on another scrolling monitor and c is handed to it: scroll is
    // the arriving engine there, and no release reaches it, so the arrival
    // gives up the column on S1 itself.
    void arrivalOnAnotherScrollScreen()
    {
        QObject owner;
        ScrollEngine* engine = stripWithWidenedB(&owner, {kS1, kS2});
        engine->setCurrentDesktopForScreen(kS2, 1);
        engine->windowFocused(kC, kS1);
        engine->adjustColumnWidth(10.0, kS1);
        QCOMPARE(Ax::mainLen(engine->lastManagedRect(kC)), kWidenedMain);
        engine->setActiveScreens({kS2});

        engine->windowOpened(kB, kS2, 0, 0);
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = kC;
        ctx.fromEngineId = QStringLiteral("snap");
        ctx.toScreenId = kS2;
        engine->handoffReceive(ctx);
        // Both come back to S1 while it is still in the other mode.
        engine->handoffRelease(kB);
        engine->handoffRelease(kC);

        engine->setActiveScreens({kS1, kS2});
        reopenS1(engine);
        QCOMPARE(Ax::mainLen(engine->lastManagedRect(kB)), kDefaultMain);
        QCOMPARE(Ax::mainLen(engine->lastManagedRect(kC)), kDefaultMain);
    }

    // b moves to desktop 2 while S1 is in another mode, and back. The
    // reconcile for its new desktop set reaches the stash although no strip
    // tracks b any more (F696).
    void leftDesktopGivesUpItsColumn()
    {
        QObject owner;
        ScrollEngine* engine = stripWithWidenedB(&owner, {kS1});
        engine->setActiveScreens({});

        engine->reconcileWindowMemberships(kB, spanOn({2}));
        engine->reconcileWindowMemberships(kB, spanOn({1}));

        reopenS1(engine);
        QCOMPARE(Ax::mainLen(engine->lastManagedRect(kB)), kDefaultMain);
    }

    // a is still open, on a monitor in another mode. A new window of the same
    // app does not take its stashed column (F648).
    void fuzzyClaimSkipsALiveWindow()
    {
        QObject owner;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        auto* registry = new PhosphorEngine::WindowRegistry(&owner);
        engine->setWindowRegistry(registry);
        engine->setCurrentDesktopForScreen(kS1, 1);
        engine->windowOpened(QStringLiteral("other|x"), kS1, 0, 0);
        engine->windowOpened(kA, kS1, 0, 0);
        engine->windowFocused(kA, kS1);
        engine->adjustColumnWidth(10.0, kS1);
        QCOMPARE(Ax::mainLen(engine->lastManagedRect(kA)), kWidenedMain);
        engine->setActiveScreens({});
        PhosphorEngine::WindowMetadata live;
        live.appId = QStringLiteral("app");
        registry->upsert(QStringLiteral("a"), live);

        engine->setActiveScreens({kS1});
        engine->windowOpened(QStringLiteral("other|x"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|n"), kS1, 0, 0);

        QCOMPARE(Ax::mainLen(engine->lastManagedRect(QStringLiteral("app|n"))), kDefaultMain);
    }

    // F714: a sticky window has left no desktop, so it keeps its column.
    void stickyKeepsItsColumn()
    {
        QObject owner;
        ScrollEngine* engine = stripWithWidenedB(&owner, {kS1});
        engine->setActiveScreens({});

        engine->reconcileWindowMemberships(kB, spanOn({}, /*sticky=*/true));

        reopenS1(engine);
        QCOMPARE(Ax::mainLen(engine->lastManagedRect(kB)), kWidenedMain);
    }

    // F714: b has a stashed column on desktops 1 and 2, widened on both, and
    // is then on desktop 2 only. Desktop 2 keeps its column; desktop 1, which
    // it left, gives its column up.
    void multiDesktopKeepsTheDesktopItIsOn()
    {
        QObject owner;
        ScrollEngine* engine = stripWithWidenedB(&owner, {kS1});
        engine->setActiveScreens({});
        engine->setCurrentDesktopForScreen(kS1, 2);
        engine->setActiveScreens({kS1});
        engine->windowOpened(kB, kS1, 0, 0);
        engine->windowFocused(kB, kS1);
        engine->adjustColumnWidth(10.0, kS1);
        QCOMPARE(Ax::mainLen(engine->lastManagedRect(kB)), kWidenedMain);
        engine->setActiveScreens({});

        engine->reconcileWindowMemberships(kB, spanOn({2}));

        engine->setActiveScreens({kS1});
        engine->windowOpened(kB, kS1, 0, 0);
        QCOMPARE(Ax::mainLen(engine->lastManagedRect(kB)), kWidenedMain);
        engine->setActiveScreens({});
        engine->setCurrentDesktopForScreen(kS1, 1);
        reopenS1(engine);
        QCOMPARE(Ax::mainLen(engine->lastManagedRect(kB)), kDefaultMain);
    }

    // A column given up is not saved, so a restart does not bring it back.
    void givenUpColumnIsNotPersisted()
    {
        QObject owner;
        ScrollEngine* engine = stripWithWidenedB(&owner, {kS1});
        engine->setActiveScreens({});
        QCOMPARE(savedIds(engine, 1), (QStringList{kA, kB, kC}));

        engine->releaseWindowOffScreen(kB, kS2);

        QCOMPARE(savedIds(engine, 1), (QStringList{kA, kC}));
    }

    // A daemon restart keeps window ids, so the staged strip names live
    // windows. b was moved to S2 while the daemon was down: the frame the
    // registry holds for it lies there, and the bring-up prune gives up its
    // column on S1. a is still on S1 and keeps its.
    void restartMoveGivesUpTheStagedColumn()
    {
        QObject owner;
        ScrollEngine* seed = stripWithWidenedB(&owner, {kS1});
        const QJsonObject blob = seed->serializeStripState();
        const QRect s1 = Ax::defaultScreenRect();
        const QRect s2 = s1.translated(s1.width(), 0);
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1}, [s1, s2](const QString& screenId) {
            return screenId == kS2 ? s2 : s1;
        });
        engine->setAllScreenGeometriesProvider([s1, s2]() {
            return QList<QRect>{s1, s2};
        });
        auto* registry = new PhosphorEngine::WindowRegistry(&owner);
        engine->setWindowRegistry(registry);
        registry->upsert(QStringLiteral("a"), frameAt(s1.center()));
        registry->upsert(QStringLiteral("b"), frameAt(s2.center()));
        engine->restoreStripState(blob);
        QCOMPARE(savedIds(engine, 1), (QStringList{kA, kB, kC}));

        engine->pruneStaleWindows({kA, kB, kC});

        QCOMPARE(savedIds(engine, 1), (QStringList{kA, kC}));
    }

private:
    /// a, b and c on desktop 1 of S1, b's column widened.
    static ScrollEngine* stripWithWidenedB(QObject* owner, const QSet<QString>& screens)
    {
        ScrollEngine* engine = makeProviderEngine(owner, screens);
        engine->setCurrentDesktopForScreen(kS1, 1);
        for (const QString& windowId : {kA, kB, kC}) {
            engine->windowOpened(windowId, kS1, 0, 0);
        }
        engine->windowFocused(kB, kS1);
        engine->adjustColumnWidth(10.0, kS1);
        return engine;
    }

    /// S1 back in scrolling, its three windows announced again.
    static void reopenS1(ScrollEngine* engine)
    {
        if (!engine->isActiveOnScreen(kS1)) {
            engine->setActiveScreens({kS1});
        }
        for (const QString& windowId : {kA, kB, kC}) {
            engine->windowOpened(windowId, kS1, 0, 0);
        }
    }

    /// Metadata whose frame is centred on @p centre.
    static PhosphorEngine::WindowMetadata frameAt(const QPoint& centre)
    {
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.width = 200;
        meta.height = 100;
        meta.positionX = centre.x() - 100;
        meta.positionY = centre.y() - 50;
        return meta;
    }
};

QTEST_GUILESS_MAIN(TestScrollEngineStashLeave)
#include "test_scrollengine_stashleave.moc"

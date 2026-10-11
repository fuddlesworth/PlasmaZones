// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The scroll engine's desktop renumber (F763, F787): every strip, stash and
// per-context override keyed by a desktop NUMBER follows a change of KWin's
// desktop list through the one old-to-new position map, a removal, an insert
// in the middle or a reorder alike. One window per desktop on S1, so the
// strip a desktop shows names the context it came from.

#include <PhosphorEngine/DesktopRemap.h>
#include <PhosphorScrollEngine/ScrollEngine.h>

#include "scrollstriptestutils.h"

#include <QtTest>

using namespace PhosphorScrollEngine;
using PhosphorEngine::DesktopRemap;

using ScrollTestUtils::makeProviderEngine;
namespace Ax = ScrollTestUtils::Ax;

namespace {

const QString kS1 = QStringLiteral("S1");

QStringList ids(std::initializer_list<const char*> names)
{
    QStringList out;
    for (const char* name : names) {
        out.append(QString::fromLatin1(name));
    }
    return out;
}

} // namespace

class TestScrollEngineDesktopRemap : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    // A desktop inserted at position 2 renumbers 2 and 3 up one.
    void insertMidListMovesTheStripsAbove()
    {
        QObject owner;
        ScrollEngine* engine = oneWindowPerDesktop(&owner, 3);
        engine->setCurrentDesktopForScreen(kS1, 1);

        engine->remapDesktops(DesktopRemap::fromIds(ids({"d1", "d2", "d3"}), ids({"d1", "new", "d2", "d3"})));

        QCOMPARE(windowsOn(engine, 1), QStringList{QStringLiteral("app|w1")});
        QVERIFY(windowsOn(engine, 2).isEmpty());
        QCOMPARE(windowsOn(engine, 3), QStringList{QStringLiteral("app|w2")});
        QCOMPARE(windowsOn(engine, 4), QStringList{QStringLiteral("app|w3")});
    }

    // Desktops 2 and 3 swap. Moved straight to its new number, each strip
    // would land on the other before it moved.
    void reorderSwapExchangesTwoStrips()
    {
        QObject owner;
        ScrollEngine* engine = oneWindowPerDesktop(&owner, 3);
        engine->setCurrentDesktopForScreen(kS1, 1);

        engine->remapDesktops(DesktopRemap::fromIds(ids({"d1", "d2", "d3"}), ids({"d1", "d3", "d2"})));

        QCOMPARE(windowsOn(engine, 1), QStringList{QStringLiteral("app|w1")});
        QCOMPARE(windowsOn(engine, 2), QStringList{QStringLiteral("app|w3")});
        QCOMPARE(windowsOn(engine, 3), QStringList{QStringLiteral("app|w2")});
        for (const char* window : {"app|w1", "app|w2", "app|w3"}) {
            QVERIFY(engine->isWindowTracked(QString::fromLatin1(window)));
        }
    }

    // Desktop 2 of 4 removed while S1 shows desktop 4: the per-removal pair
    // the daemon drives today and the map give the same result, the removed
    // strip pruned, the ones above shifted down, the screen on the desktop it
    // showed.
    void removalMatchesTheShift()
    {
        for (const bool viaMap : {false, true}) {
            QObject owner;
            ScrollEngine* engine = oneWindowPerDesktop(&owner, 4);
            QCOMPARE(engine->managedWindowOrder(kS1), QStringList{QStringLiteral("app|w4")});
            if (viaMap) {
                engine->remapDesktops(DesktopRemap::removal(2, 4));
            } else {
                engine->pruneStatesForDesktop(2);
                engine->renumberDesktopsAfterRemoval(2);
            }
            QCOMPARE(engine->managedWindowOrder(kS1), QStringList{QStringLiteral("app|w4")});
            QVERIFY(!engine->isWindowTracked(QStringLiteral("app|w2")));
            QCOMPARE(windowsOn(engine, 1), QStringList{QStringLiteral("app|w1")});
            QCOMPARE(windowsOn(engine, 2), QStringList{QStringLiteral("app|w3")});
            QCOMPARE(windowsOn(engine, 3), QStringList{QStringLiteral("app|w4")});
        }
    }

    // A strip stashed by a mode change and the context's overrides have no
    // live state, and still follow their desktop's new number.
    void stashAndOverridesFollowTheirContext()
    {
        QObject owner;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setCurrentDesktopForScreen(kS1, 2);
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
        engine->windowFocused(QStringLiteral("app|b"), kS1);
        engine->adjustColumnWidth(10.0, kS1);
        QCOMPARE(Ax::mainLen(engine->lastManagedRect(QStringLiteral("app|b"))), 720);
        const QVariantMap overrides{{QStringLiteral("probe"), 2}};
        engine->applyPerScreenConfig(kS1, overrides);
        engine->setActiveScreens({});
        engine->setCurrentDesktopForScreen(kS1, 1);

        engine->remapDesktops(DesktopRemap::fromIds(ids({"d1", "d2"}), ids({"new", "d1", "d2"})));

        engine->setCurrentDesktopForScreen(kS1, 2);
        QVERIFY(engine->perScreenOverrides(kS1).isEmpty());
        engine->setCurrentDesktopForScreen(kS1, 3);
        QCOMPARE(engine->perScreenOverrides(kS1), overrides);
        engine->setActiveScreens({kS1});
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
        QCOMPARE(Ax::mainLen(engine->lastManagedRect(QStringLiteral("app|b"))), 720);
    }

private:
    /// app|wN alone on desktop N of S1, for N in 1..@p desktops; S1 left on the last.
    static ScrollEngine* oneWindowPerDesktop(QObject* owner, int desktops)
    {
        ScrollEngine* engine = makeProviderEngine(owner, {kS1});
        for (int desktop = 1; desktop <= desktops; ++desktop) {
            engine->setCurrentDesktopForScreen(kS1, desktop);
            engine->windowOpened(QStringLiteral("app|w%1").arg(desktop), kS1, 0, 0);
        }
        return engine;
    }

    /// The windows of the strip S1 shows on @p desktop.
    static QStringList windowsOn(ScrollEngine* engine, int desktop)
    {
        engine->setCurrentDesktopForScreen(kS1, desktop);
        return engine->managedWindowOrder(kS1);
    }
};

QTEST_GUILESS_MAIN(TestScrollEngineDesktopRemap)
#include "test_scrollengine_desktopremap.moc"

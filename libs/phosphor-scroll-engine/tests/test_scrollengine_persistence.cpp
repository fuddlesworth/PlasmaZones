// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Strip-structure persistence: the mode-round-trip stash's focus/anchor
// carry, and the serialize/restore blob a login restore rides — including
// the appId claim that survives cross-session window-uuid drift. Split from
// test_scrollengine_smoke.cpp (file-size ceiling); same headless setup.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include <QJsonObject>
#include <QtTest>

using namespace PhosphorScrollEngine;

class TestScrollEnginePersistence : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void modeRoundTripRestoresFocusAndAnchor();
    void serializedStripRestoreSurvivesIdDrift();
    void pruneSweepsStashedTilesForClosedWindows();

private:
    /// Smoke-suite twin: geometry providers wired so the apply path
    /// resolves real rects against a 1200x800 work area.
    static ScrollEngine* makeProviderEngine(QObject* parent, const QSet<QString>& screens)
    {
        auto* engine = new ScrollEngine(nullptr, nullptr, parent);
        const auto geometry = [](const QString&) {
            return QRect(0, 0, 1200, 800);
        };
        engine->setScreenGeometryProviders(geometry, geometry);
        engine->setActiveScreens(screens);
        return engine;
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

    auto* before = static_cast<ScrollState*>(engine->stateForScreen(QStringLiteral("S1")));
    QVERIFY(before);
    QCOMPARE(before->strip().activeWindowId(), QStringLiteral("app|c"));
    const int anchorBefore = before->strip().viewAnchor();

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
    auto* after = static_cast<ScrollState*>(engine->stateForScreen(QStringLiteral("S1")));
    QVERIFY(after);
    QCOMPARE(after->strip().columns().at(0).display, ColumnDisplay::Tabbed);
    QCOMPARE(after->strip().activeWindowId(), QStringLiteral("app|c"));
    QCOMPARE(after->strip().viewAnchor(), anchorBefore);
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
    auto* state = static_cast<ScrollState*>(engine2->stateForScreen(QStringLiteral("S1")));
    QVERIFY(state);
    QCOMPARE(state->strip().columns().at(0).display, ColumnDisplay::Tabbed);
    QCOMPARE(state->strip().columns().at(0).tiles.size(), 2);
    // The stashed focus (other|u3) followed its claimed successor.
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("other|n3"));
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
    const QByteArray stashed = QJsonDocument(engine->serializeStripState()).toJson();
    QVERIFY2(stashed.contains("app|b"), "precondition: the stash holds all three tiles");

    // One window closes while the screen is in another mode. The context is
    // untouched, so the context-keyed sweeps cannot reach this stash and only
    // pruneStaleWindows can.
    engine->pruneStaleWindows({QStringLiteral("app|a"), QStringLiteral("app|c")});

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

QTEST_GUILESS_MAIN(TestScrollEnginePersistence)
#include "test_scrollengine_persistence.moc"

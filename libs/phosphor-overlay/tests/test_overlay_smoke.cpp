// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Phase 2 foundation smoke test. Pins the public type and accessor
// shape so subsequent commits within Phase 2 (method moves from
// OverlayService) can rely on the public surface without re-deriving
// it. Real behavioral coverage of create / destroy / rekey / sync
// lands as those methods migrate; this test focuses on the state
// container and failure-flag bookkeeping.

#include <PhosphorOverlay/PhosphorOverlay.h>

#include <QObject>
#include <QString>
#include <QtTest/QtTest>

class TestOverlaySmoke : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void shellHostConstructsAndDestructs();
    void shellHostIsQObject();
    void stateForMaterializesOnFirstAccess();
    void stateForConstReturnsNullForUnknownScreen();
    void removeStateClearsEntry();
    void screenIdsReflectsLiveEntries();
    void failureFlagToggles();
};

void TestOverlaySmoke::shellHostConstructsAndDestructs()
{
    PhosphorOverlay::ShellHost host;
    Q_UNUSED(host);
}

void TestOverlaySmoke::shellHostIsQObject()
{
    QObject parent;
    auto* host = new PhosphorOverlay::ShellHost(&parent);
    QCOMPARE(host->parent(), &parent);
}

void TestOverlaySmoke::stateForMaterializesOnFirstAccess()
{
    PhosphorOverlay::ShellHost host;
    QVERIFY(!host.hasState(QStringLiteral("DP-1")));

    auto& state = host.stateFor(QStringLiteral("DP-1"));
    state.shellSurface = nullptr;
    QVERIFY(host.hasState(QStringLiteral("DP-1")));
}

void TestOverlaySmoke::stateForConstReturnsNullForUnknownScreen()
{
    const PhosphorOverlay::ShellHost host;
    QCOMPARE(host.stateFor(QStringLiteral("never-seen")), static_cast<const PhosphorOverlay::ShellState*>(nullptr));
}

void TestOverlaySmoke::removeStateClearsEntry()
{
    PhosphorOverlay::ShellHost host;
    host.stateFor(QStringLiteral("DP-1"));
    QVERIFY(host.hasState(QStringLiteral("DP-1")));
    host.removeState(QStringLiteral("DP-1"));
    QVERIFY(!host.hasState(QStringLiteral("DP-1")));
}

void TestOverlaySmoke::screenIdsReflectsLiveEntries()
{
    PhosphorOverlay::ShellHost host;
    host.stateFor(QStringLiteral("DP-1"));
    host.stateFor(QStringLiteral("HDMI-A-1"));
    const auto ids = host.screenIds();
    QCOMPARE(ids.size(), 2);
    QVERIFY(ids.contains(QStringLiteral("DP-1")));
    QVERIFY(ids.contains(QStringLiteral("HDMI-A-1")));
}

void TestOverlaySmoke::failureFlagToggles()
{
    PhosphorOverlay::ShellHost host;
    QVERIFY(!host.hasFailure(QStringLiteral("DP-1")));
    host.markFailure(QStringLiteral("DP-1"));
    QVERIFY(host.hasFailure(QStringLiteral("DP-1")));
    host.clearFailure(QStringLiteral("DP-1"));
    QVERIFY(!host.hasFailure(QStringLiteral("DP-1")));
}

QTEST_MAIN(TestOverlaySmoke)
#include "test_overlay_smoke.moc"

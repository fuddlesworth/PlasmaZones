// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Phase 2 foundation smoke test. Pins the public type and accessor
// shape so subsequent commits within Phase 2 (method moves from
// OverlayService) can rely on the public surface without re-deriving
// it. Real behavioral coverage of create / destroy / rekey / sync
// lands as those methods migrate; this test focuses on the state
// container and failure-flag bookkeeping.

#include <PhosphorOverlay/PhosphorOverlay.h>

#include <PhosphorLayer/Role.h>
#include <PhosphorShellPatterns/Patterns.h>

#include <QObject>
#include <QString>
#include <QStringLiteral>
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
    void rekeyReturnsFalseForSameKey();
    void rekeyReturnsFalseWhenDonorAbsent();
    void rekeyReturnsFalseWhenDonorHasNoLiveShell();
    void makePerInstanceRoleAppendsScreenAndGenerationToScope();
    void registerConfigForRoleIsNoOpWithoutAnimator();
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

void TestOverlaySmoke::rekeyReturnsFalseForSameKey()
{
    PhosphorOverlay::ShellHost host;
    QCOMPARE(host.rekey(QStringLiteral("DP-1"), QStringLiteral("DP-1")), false);
}

void TestOverlaySmoke::rekeyReturnsFalseWhenDonorAbsent()
{
    PhosphorOverlay::ShellHost host;
    QCOMPARE(host.rekey(QStringLiteral("never-seen"), QStringLiteral("HDMI-A-1")), false);
}

void TestOverlaySmoke::rekeyReturnsFalseWhenDonorHasNoLiveShell()
{
    PhosphorOverlay::ShellHost host;
    // stateFor materializes a zeroed entry (shellSurface == nullptr).
    // Rekey requires a live shell on the donor and must reject this case
    // rather than silently moving an empty entry.
    host.stateFor(QStringLiteral("DP-1"));
    QCOMPARE(host.rekey(QStringLiteral("DP-1"), QStringLiteral("HDMI-A-1")), false);
}

void TestOverlaySmoke::makePerInstanceRoleAppendsScreenAndGenerationToScope()
{
    const auto base = PhosphorShellPatterns::Hud().withScopePrefix(QStringLiteral("phosphor-overlay-test"));
    const auto perInstance = PhosphorOverlay::makePerInstanceRole(base, QStringLiteral("DP-1"), 7);
    QCOMPARE(perInstance.scopePrefix, QStringLiteral("phosphor-overlay-test-DP-1-7"));
    // The longest-prefix lookup the SurfaceAnimator does on per-instance
    // roles only resolves when the per-instance prefix starts with the
    // base prefix — pin that invariant.
    QVERIFY(perInstance.scopePrefix.startsWith(base.scopePrefix));
}

void TestOverlaySmoke::registerConfigForRoleIsNoOpWithoutAnimator()
{
    PhosphorOverlay::ShellHost host;
    // No setSurfaceAnimator call — the lib silently no-ops rather than
    // dereferencing a null animator pointer. Consumers that call this
    // without injection get nothing rather than a crash.
    const auto role = PhosphorShellPatterns::Hud().withScopePrefix(QStringLiteral("phosphor-overlay-test"));
    host.registerConfigForRole(role, {});
}

QTEST_MAIN(TestOverlaySmoke)
#include "test_overlay_smoke.moc"

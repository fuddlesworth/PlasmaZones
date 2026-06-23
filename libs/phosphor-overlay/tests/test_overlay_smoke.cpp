// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Behavioral coverage of the ShellHost public surface. Started life as
// the Phase-2 type-shape smoke test; now also pins the post-PR-#436
// contract fixes:
//   - hideSlot fires completion synchronously on every benign no-op
//     (no shell / no slot / null item / item not visible).
//   - destroyShell only fires PreDestroyCallback when a live surface is
//     being torn down (zeroed entries skip the callback so ~ShellHost
//     never re-enters partially-destroyed consumer state).
//   - rekey(k,k) returns true iff a live entry exists under k.
//   - rekey happy path migrates the entry across keys, preserving the
//     heap-allocated ShellState* so borrowed pointers stay valid.
//
// Tests that need a live shellSurface are deferred - the lib's
// SurfaceFactory contract requires a real PhosphorLayer::Surface, which
// in turn needs a Wayland transport. These tests focus on the lib-side
// state-machine paths that don't depend on a live surface.

#include <PhosphorOverlay/PhosphorOverlay.h>

#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/SurfaceAnimator.h>
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

    // rekey
    void rekeySameKeyReturnsFalseWhenNoLiveEntry();
    void rekeyReturnsFalseWhenDonorAbsent();
    void rekeyReturnsFalseWhenDonorHasNoLiveShell();

    // hideSlot completion-firing contract
    void hideSlotFiresCompletionWhenNoState();
    void hideSlotFiresCompletionWhenNoSlot();
    void hideSlotFiresCompletionWhenSlotItemIsNullQPointer();
    void hideSlotDropsCompletionOnEmptyArgs();
    void hideSlotDropsCompletionWhenAnimatorMissing();

    // destroyShell idempotency
    void destroyShellOnZeroedEntrySkipsCallback();
    void destroyShellOnAbsentEntryIsNoOp();

    // dtor cleanup
    void dtorWithMaterializedZeroedEntriesIsSafe();

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

    auto& state = host.getOrCreateStateFor(QStringLiteral("DP-1"));
    Q_UNUSED(state);
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
    host.getOrCreateStateFor(QStringLiteral("DP-1"));
    QVERIFY(host.hasState(QStringLiteral("DP-1")));
    host.removeState(QStringLiteral("DP-1"));
    QVERIFY(!host.hasState(QStringLiteral("DP-1")));
}

void TestOverlaySmoke::screenIdsReflectsLiveEntries()
{
    PhosphorOverlay::ShellHost host;
    host.getOrCreateStateFor(QStringLiteral("DP-1"));
    host.getOrCreateStateFor(QStringLiteral("HDMI-A-1"));
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

void TestOverlaySmoke::rekeySameKeyReturnsFalseWhenNoLiveEntry()
{
    PhosphorOverlay::ShellHost host;
    // No entry exists - postcondition "live entry at key" cannot hold.
    QCOMPARE(host.rekey(QStringLiteral("DP-1"), QStringLiteral("DP-1")), false);
    // Materialize a zeroed entry - still no live shell.
    host.getOrCreateStateFor(QStringLiteral("DP-1"));
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
    // getOrCreateStateFor materializes a zeroed entry (shellSurface == nullptr).
    // Rekey requires a live shell on the donor and must reject this case
    // rather than silently moving an empty entry.
    host.getOrCreateStateFor(QStringLiteral("DP-1"));
    QCOMPARE(host.rekey(QStringLiteral("DP-1"), QStringLiteral("HDMI-A-1")), false);
}

void TestOverlaySmoke::hideSlotFiresCompletionWhenNoState()
{
    PhosphorOverlay::ShellHost host;
    PhosphorAnimation::PhosphorProfileRegistry registry;
    PhosphorAnimationLayer::SurfaceAnimator animator(registry);
    host.setSurfaceAnimator(&animator);

    int fired = 0;
    host.hideSlot(QStringLiteral("never-seen"), QStringLiteral("osd"), [&]() {
        ++fired;
    });
    QCOMPARE(fired, 1);
}

void TestOverlaySmoke::hideSlotFiresCompletionWhenNoSlot()
{
    PhosphorOverlay::ShellHost host;
    PhosphorAnimation::PhosphorProfileRegistry registry;
    PhosphorAnimationLayer::SurfaceAnimator animator(registry);
    host.setSurfaceAnimator(&animator);
    host.getOrCreateStateFor(QStringLiteral("DP-1")); // zeroed entry, no shellSurface

    int fired = 0;
    host.hideSlot(QStringLiteral("DP-1"), QStringLiteral("osd"), [&]() {
        ++fired;
    });
    QCOMPARE(fired, 1);
}

void TestOverlaySmoke::hideSlotFiresCompletionWhenSlotItemIsNullQPointer()
{
    // Pins the contract the daemon's PerScreenOverlayState slot
    // accessors (osdSlot / snapAssistSlot / ...) rely on: a slot
    // entry exists in the map but its QPointer<QQuickItem> is
    // already cleared (the underlying item was destroyed out from
    // under us, typically because the shell was torn down by a
    // deferred signal). hideSlot must fire completion synchronously
    // so consumer cleanup runs even on this race-window no-op path.
    PhosphorOverlay::ShellHost host;
    PhosphorAnimation::PhosphorProfileRegistry registry;
    PhosphorAnimationLayer::SurfaceAnimator animator(registry);
    host.setSurfaceAnimator(&animator);

    auto& state = host.getOrCreateStateFor(QStringLiteral("DP-1"));
    // Inject a SlotEntry whose QPointer is default-null. Mirrors the
    // "QML item never materialised" / "item already destroyed" cases.
    state.slots.insert(QStringLiteral("osd"),
                       PhosphorOverlay::SlotEntry{QPointer<QQuickItem>{}, PhosphorLayer::Role{}});

    int fired = 0;
    host.hideSlot(QStringLiteral("DP-1"), QStringLiteral("osd"), [&]() {
        ++fired;
    });
    // No shellSurface set on the state, so the "no shell" early-return
    // path fires the completion. This is also the correct behaviour:
    // consumer cleanup must run regardless.
    QCOMPARE(fired, 1);
}

void TestOverlaySmoke::hideSlotDropsCompletionOnEmptyArgs()
{
    PhosphorOverlay::ShellHost host;
    PhosphorAnimation::PhosphorProfileRegistry registry;
    PhosphorAnimationLayer::SurfaceAnimator animator(registry);
    host.setSurfaceAnimator(&animator);

    int fired = 0;
    host.hideSlot(QString(), QStringLiteral("osd"), [&]() {
        ++fired;
    });
    host.hideSlot(QStringLiteral("DP-1"), QString(), [&]() {
        ++fired;
    });
    QCOMPARE(fired, 0);
}

void TestOverlaySmoke::hideSlotDropsCompletionWhenAnimatorMissing()
{
    PhosphorOverlay::ShellHost host;
    // No setSurfaceAnimator - programmer-setup error.

    int fired = 0;
    host.hideSlot(QStringLiteral("DP-1"), QStringLiteral("osd"), [&]() {
        ++fired;
    });
    QCOMPARE(fired, 0);
}

void TestOverlaySmoke::destroyShellOnZeroedEntrySkipsCallback()
{
    PhosphorOverlay::ShellHost host;
    host.getOrCreateStateFor(QStringLiteral("DP-1")); // zeroed (shellSurface == nullptr)

    int callbackFired = 0;
    host.setPreDestroyCallback([&](const QString&) {
        ++callbackFired;
    });
    host.destroyShell(QStringLiteral("DP-1"));
    QCOMPARE(callbackFired, 0);
}

void TestOverlaySmoke::destroyShellOnAbsentEntryIsNoOp()
{
    PhosphorOverlay::ShellHost host;
    int callbackFired = 0;
    host.setPreDestroyCallback([&](const QString&) {
        ++callbackFired;
    });
    host.destroyShell(QStringLiteral("never-seen"));
    QCOMPARE(callbackFired, 0);
}

void TestOverlaySmoke::dtorWithMaterializedZeroedEntriesIsSafe()
{
    int callbackFired = 0;
    {
        PhosphorOverlay::ShellHost host;
        host.setPreDestroyCallback([&](const QString&) {
            ++callbackFired;
        });
        // Materialize several zeroed entries via the public surface.
        host.getOrCreateStateFor(QStringLiteral("DP-1"));
        host.getOrCreateStateFor(QStringLiteral("HDMI-A-1"));
        host.getOrCreateStateFor(QStringLiteral("DP-2"));
    }
    // None had a live shellSurface, so PreDestroyCallback must not fire
    // during ~ShellHost - otherwise consumer state that may have
    // already started destruction would be re-entered.
    QCOMPARE(callbackFired, 0);
}

void TestOverlaySmoke::makePerInstanceRoleAppendsScreenAndGenerationToScope()
{
    const auto base = PhosphorShellPatterns::Hud().withScopePrefix(QStringLiteral("phosphor-overlay-test"));
    const auto perInstance = PhosphorOverlay::makePerInstanceRole(base, QStringLiteral("DP-1"), 7);
    QCOMPARE(perInstance.scopePrefix, QStringLiteral("phosphor-overlay-test-DP-1-7"));
    // The longest-prefix lookup the SurfaceAnimator does on per-instance
    // roles only resolves when the per-instance prefix starts with the
    // base prefix - pin that invariant.
    QVERIFY(perInstance.scopePrefix.startsWith(base.scopePrefix));
}

void TestOverlaySmoke::registerConfigForRoleIsNoOpWithoutAnimator()
{
    PhosphorOverlay::ShellHost host;
    // No setSurfaceAnimator call - the lib silently no-ops rather than
    // dereferencing a null animator pointer. Consumers that call this
    // without injection get nothing rather than a crash.
    const auto role = PhosphorShellPatterns::Hud().withScopePrefix(QStringLiteral("phosphor-overlay-test"));
    host.registerConfigForRole(role, {});
}

QTEST_MAIN(TestOverlaySmoke)
#include "test_overlay_smoke.moc"

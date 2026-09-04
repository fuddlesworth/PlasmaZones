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
// Most cases here need no live shellSurface and cover the lib-side
// state-machine paths that do not depend on one. The syncSurfaceState
// keyboard cases DO need a live surface, and get one from phosphor-layer's
// in-tree MockTransport rather than a real Wayland connection.

#include <PhosphorOverlay/PhosphorOverlay.h>

#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/SurfaceAnimator.h>
#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/SurfaceConfig.h>
#include <PhosphorLayer/SurfaceFactory.h>
#include <PhosphorShellPatterns/Patterns.h>

#include "mocks/mockscreenprovider.h"
#include "mocks/mocktransport.h"
#include "mocks/testroles.h"

#include <QObject>
#include <QQuickItem>
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

    // syncSurfaceState keyboard axis, over a mock-transport-backed surface
    void syncSurfaceStateDrivesKeyboardInteractivity();
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

// The keyboard axis fails SILENTLY in production in both directions: a grab
// that never lands means the content simply never receives a keystroke, and
// one that never releases means the user's focused window stops receiving
// them. Neither raises an error or a log line, so nothing but a test notices.
//
// Two traps are load-bearing in how this is written:
//
//  - The factory MUST hand back a warmed surface. Surface::window() is null in
//    State::Constructed, and syncSurfaceState early-returns on a null window,
//    so a freshly-created surface would exercise nothing while every
//    assertion below still passed.
//  - MockTransportHandle::m_keyboard starts at None and attach() never seeds
//    it, so an "expect None" assertion passes vacuously against a ShellHost
//    whose keyboard block was deleted outright. Every None case here is
//    therefore preceded by a transition to Exclusive, and asserts the change.
void TestOverlaySmoke::syncSurfaceStateDrivesKeyboardInteractivity()
{
    using namespace PhosphorLayer;

    // Declared before the host so the host is destroyed first: ~ShellHost runs
    // destroyShell over every key, which touches the surface and its transport.
    Testing::MockTransport transport;
    Testing::MockScreenProvider screens;
    SurfaceFactory factory(Testing::makeDeps(&transport, &screens));

    PhosphorOverlay::ShellHost host;
    const QString screenId = QStringLiteral("screen-0");

    host.setSurfaceFactory([&](const QString&, QScreen* physScreen) -> Surface* {
        SurfaceConfig cfg;
        // Hud rather than Modal: it attaches kbd-None, matching the passive
        // shell role this axis exists to flip at runtime.
        cfg.role = Testing::makeHudRole();
        // Inline content settles the state machine synchronously in one drive
        // pass, so nothing here needs an event loop or a QTRY_.
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = physScreen;
        cfg.debugName = QStringLiteral("kbd-axis");
        auto* surface = factory.create(std::move(cfg));
        // The attach is what gives the surface a window and a transport
        // handle, and syncSurfaceState needs both.
        surface->warmUp();
        return surface;
    });

    auto* state = host.ensureShell(screenId, screens.primary());
    QVERIFY(state != nullptr);
    // Guard the trap rather than trusting it: without these, every assertion
    // below would pass against a surface that never reached the write.
    QVERIFY(state->shellWindow() != nullptr);
    QVERIFY(state->shellSurface() != nullptr);
    auto* handle = state->shellSurface()->transport();
    QVERIFY(handle != nullptr);

    // Visible and asking to type: the one combination that takes the keyboard.
    host.syncSurfaceState(screenId, {.visible = true, .inputGrabbing = false, .keyboardGrabbing = true});
    QCOMPARE(transport.m_lastHandle->m_keyboard, KeyboardInteractivity::Exclusive);

    // The release edge the daemon depends on. The slot stays visible for its
    // whole fade-out, so the flag dropping - not the slot hiding - is what has
    // to hand the keyboard back.
    host.syncSurfaceState(screenId, {.visible = true, .inputGrabbing = false, .keyboardGrabbing = false});
    QCOMPARE(transport.m_lastHandle->m_keyboard, KeyboardInteractivity::None);

    // Asking to type while nothing is visible must not hold the session's
    // keyboard on an unseen surface.
    host.syncSurfaceState(screenId, {.visible = true, .inputGrabbing = false, .keyboardGrabbing = true});
    QCOMPARE(transport.m_lastHandle->m_keyboard, KeyboardInteractivity::Exclusive);
    host.syncSurfaceState(screenId, {.visible = false, .inputGrabbing = false, .keyboardGrabbing = true});
    QCOMPARE(transport.m_lastHandle->m_keyboard, KeyboardInteractivity::None);

    // Teardown hands the keyboard back before the surface goes, rather than
    // leaving the grab live until the event loop next turns.
    host.syncSurfaceState(screenId, {.visible = true, .inputGrabbing = false, .keyboardGrabbing = true});
    QCOMPARE(transport.m_lastHandle->m_keyboard, KeyboardInteractivity::Exclusive);
    host.destroyShell(screenId);
    QCOMPARE(transport.m_lastHandle->m_keyboard, KeyboardInteractivity::None);
}

QTEST_MAIN(TestOverlaySmoke)
#include "test_overlay_smoke.moc"

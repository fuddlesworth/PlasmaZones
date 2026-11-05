// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_snap_state_class_mutation.cpp
 * @brief Regression tests for issue #628: the daemon's snap stores must not
 *        orphan a window's state when the KWin effect restarts after the
 *        window's WM_CLASS mutated during downtime (Electron/CEF apps rename
 *        their class after mapping), so the restarted effect re-derives a
 *        DIFFERENT `appId|instanceId` composite for the same window.
 *
 * The fix canonicalizes every windowId-keyed SnapState accessor through the
 * shared WindowRegistry (instanceId → first-seen composite), mirroring what the
 * AutotileEngine already does for tiling state (test_autotile_engine_class_mutation).
 * Seeding is centralized in the daemon's WindowTrackingAdaptor::setWindowMetadata;
 * here we seed the registry directly to stand in for that one call.
 *
 * These tests exercise SnapState directly — no KWin effect, no D-Bus — with a
 * real WindowRegistry wired in, which is exactly the production configuration.
 */

#include <QTest>

#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorSnapEngine/SnapState.h>

using PhosphorEngine::WindowRegistry;
using PhosphorSnapEngine::SnapState;

namespace {

QString makeComposite(const QString& appId, const QString& instanceId)
{
    return appId + QLatin1Char('|') + instanceId;
}

// A stable instance id (KWin internalId) — the part that never changes when the
// app mutates its class.
const QString kInstanceId = QStringLiteral("cef1ba31-3316-4f05-84f5-ef627674b504");
const QString kFirstSeen = QStringLiteral("google-chrome|") + kInstanceId;
const QString kAfterRename = QStringLiteral("google-chrome-beta|") + kInstanceId;
const QString kScreen = QStringLiteral("DP-1");
const QString kZone = QStringLiteral("zone-uuid-1");

} // namespace

class TestSnapStateClassMutation : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ────────────────────────────────────────────────────────────────────
    // The bug case: state written under the first-seen composite must resolve
    // when queried/mutated under the post-restart, mutated-class composite.
    // ────────────────────────────────────────────────────────────────────
    void snapStateResolvesAcrossClassMutation()
    {
        WindowRegistry registry;
        SnapState state{QString()};
        state.setWindowRegistry(&registry);

        // The daemon seeds the canonical mapping once per window at open
        // (setWindowMetadata). First composite wins.
        registry.canonicalizeWindowId(kFirstSeen);

        // Snap the window under the first-seen composite.
        state.assignWindowToZones(kFirstSeen, {kZone}, kScreen, /*virtualDesktop=*/1);
        QVERIFY(state.isWindowSnapped(kFirstSeen));

        // Effect restarts → it re-derives a mutated-class composite for the SAME
        // window (same instance id). Every windowId-keyed read must resolve it
        // back to the first-seen entry — no orphaning.
        QVERIFY2(state.isWindowSnapped(kAfterRename), "snapped state must resolve across the class mutation");
        QVERIFY(state.containsWindow(kAfterRename));
        QCOMPARE(state.zoneForWindow(kAfterRename), kZone);
        QCOMPARE(state.zonesForWindow(kAfterRename), QStringList{kZone});
        QCOMPARE(state.placementIdForWindow(kAfterRename), kZone);
        QCOMPARE(state.screenForWindow(kAfterRename), kScreen);
        QCOMPARE(state.desktopForWindow(kAfterRename), 1);
        QVERIFY(!state.isFloating(kAfterRename));

        // A WRITE under the mutated composite must land on the SAME entry, not a
        // phantom second window.
        state.setFloating(kAfterRename, true);
        QVERIFY2(state.isFloating(kFirstSeen),
                 "float written under the mutated composite must flip the first-seen entry");
        QVERIFY(state.isFloating(kAfterRename));

        state.markAsAutoSnapped(kAfterRename);
        QVERIFY(state.isAutoSnapped(kFirstSeen));

        // Still exactly one managed window — no duplicate under the new composite.
        QCOMPARE(state.windowCount(), 1);

        // Closing under the mutated composite removes the first-seen entry.
        state.windowClosed(kAfterRename);
        QVERIFY(!state.isWindowSnapped(kFirstSeen));
        QVERIFY(!state.isFloating(kFirstSeen));
        QCOMPARE(state.windowCount(), 0);
    }

    // Pre-float (unfloat memory) must also resolve cross-composite.
    void preFloatResolvesAcrossClassMutation()
    {
        WindowRegistry registry;
        SnapState state{QString()};
        state.setWindowRegistry(&registry);
        registry.canonicalizeWindowId(kFirstSeen);

        state.assignWindowToZones(kFirstSeen, {kZone}, kScreen, /*virtualDesktop=*/1);
        // Float-from-snap records the pre-float zone+screen, keyed canonically.
        state.unsnapForFloat(kAfterRename);

        QCOMPARE(state.preFloatZone(kAfterRename), kZone);
        QCOMPARE(state.preFloatZone(kFirstSeen), kZone);
        QCOMPARE(state.preFloatZones(kAfterRename), QStringList{kZone});
        QCOMPARE(state.preFloatScreen(kAfterRename), kScreen);
    }

    // pruneStaleAssignments must NOT prune a window that is still alive under a
    // mutated-class composite (the alive set arrives in raw form).
    void pruneKeepsWindowAliveUnderMutatedComposite()
    {
        WindowRegistry registry;
        SnapState state{QString()};
        state.setWindowRegistry(&registry);
        registry.canonicalizeWindowId(kFirstSeen);

        state.assignWindowToZones(kFirstSeen, {kZone}, kScreen, /*virtualDesktop=*/1);

        // The window is reported alive under the MUTATED composite. It must
        // canonicalize to the first-seen key and survive the prune.
        const int pruned = state.pruneStaleAssignments({kAfterRename});
        QCOMPARE(pruned, 0);
        QVERIFY(state.isWindowSnapped(kFirstSeen));

        // A genuinely-gone window (different instance) IS pruned.
        const int pruned2 =
            state.pruneStaleAssignments({makeComposite(QStringLiteral("x"), QStringLiteral("other-uuid"))});
        QCOMPARE(pruned2, 1);
        QVERIFY(!state.isWindowSnapped(kFirstSeen));
    }

    // Without a registry (unit-test / pre-engine-wiring path) the state keys on
    // the raw id verbatim — preserving the pre-#628 behaviour so existing tests
    // and the unwired fallback are unaffected.
    void withoutRegistry_keysVerbatim()
    {
        SnapState state{QString()};
        state.assignWindowToZones(kFirstSeen, {kZone}, kScreen, /*virtualDesktop=*/1);

        QVERIFY(state.isWindowSnapped(kFirstSeen));
        // No canonicalization without a registry, so a different composite is a
        // different (unknown) window.
        QVERIFY(!state.isWindowSnapped(kAfterRename));
        QVERIFY(state.zoneForWindow(kAfterRename).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSnapStateClassMutation)
#include "test_snap_state_class_mutation.moc"

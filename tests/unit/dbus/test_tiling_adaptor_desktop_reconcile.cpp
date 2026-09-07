// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_tiling_adaptor_desktop_reconcile.cpp
 *
 * Pins the daemon-side desktop-membership reconcile (#1076 and its family):
 * when the compositor reports a window's virtual-desktop set changed, the
 * TilingAdaptor releases the window from any engine state keyed by a
 * desktop the window no longer belongs to, whatever desktop the screen is
 * showing at the time. The effect used to own that decision from proxies of
 * the desktop in view, and every (moved window, desktop in view, mode per
 * desktop) combination it had not foreseen left a slot behind.
 *
 * Driven end to end through a real WindowRegistry, so the subscription
 * setWindowRegistry makes is under test too, against a real AutotileEngine
 * and a real ScrollEngine with headless geometry.
 */

#include <QCoreApplication>
#include <QObject>
#include <QTest>

#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/TilingState.h>

#include "dbus/tilingadaptor/tilingadaptor.h"
#include "helpers/AutotileTestHelpers.h"

#include <optional>

using namespace PlasmaZones;

namespace {
const QString kScreen = QStringLiteral("DP-1");
const QString kScreen2 = QStringLiteral("HDMI-1");
const QString kInstance = QStringLiteral("11111111-2222-3333-4444-555555555555");
const QString kWindow = QStringLiteral("app|11111111-2222-3333-4444-555555555555");
const QString kStays = QStringLiteral("app|66666666-7777-8888-9999-000000000000");

PhosphorEngine::WindowMetadata onDesktop(int desktop, const QList<int>& span = {}, const QString& activity = QString())
{
    PhosphorEngine::WindowMetadata meta;
    meta.appId = QStringLiteral("app");
    meta.title = QStringLiteral("t");
    meta.virtualDesktop = desktop;
    meta.virtualDesktops = span;
    meta.activity = activity;
    return meta;
}

// The wiring every case needs: a real registry, a real engine and an adaptor
// subscribed to the registry.
//
// MEMBER ORDER IS THE DESTRUCTION CONTRACT and is not free to rearrange. The
// adaptor is declared last so it is destroyed FIRST, while the parent it is
// Qt-parented to and the registry its subscription names are both still alive.
struct AutotileFixture
{
    PhosphorEngine::WindowRegistry registry;
    PhosphorTileEngine::AutotileEngine engine{nullptr, nullptr, nullptr, TestHelpers::testRegistry()};
    QObject adaptorParent;
    TilingAdaptor adaptor{nullptr, &adaptorParent};

    AutotileFixture()
    {
        engine.setWindowRegistry(&registry);
        adaptor.setLifecycleEngines({&engine});
        adaptor.setWindowRegistry(&registry);
    }

    /// Open @p windowId into @p desktop's state and hand back that state.
    /// The registry is seeded with the SAME desktop unless the caller seeded
    /// it already, so the first upsert is an insert (which emits
    /// windowAppeared, not metadataChanged) and drives no reconcile.
    PhosphorTiles::TilingState* openOn(int desktop, const QStringList& windowIds, bool seedRegistry = true)
    {
        if (seedRegistry) {
            registry.canonicalizeWindowId(kWindow);
            registry.upsert(kInstance, onDesktop(desktop));
        }
        engine.setCurrentDesktopForScreen(kScreen, desktop);
        engine.setAutotileScreens({kScreen});
        for (const QString& windowId : windowIds) {
            engine.windowOpened(windowId, kScreen);
        }
        QCoreApplication::processEvents();
        return engine.tilingStateForScreen(kScreen);
    }
};

struct ScrollFixture
{
    PhosphorEngine::WindowRegistry registry;
    PhosphorScrollEngine::ScrollEngine engine{nullptr, nullptr};
    QObject adaptorParent;
    TilingAdaptor adaptor{nullptr, &adaptorParent};

    ScrollFixture()
    {
        engine.setWindowRegistry(&registry);
        // Headless geometry: layoutParamsForScreen refuses to resolve without
        // it, so the strip would never lay out.
        engine.setScreenGeometryProviders(
            [](const QString&) {
                return QRect(0, 40, 1200, 760);
            },
            [](const QString&) {
                return QRect(0, 0, 1200, 800);
            });
        adaptor.setLifecycleEngines({&engine});
        adaptor.setWindowRegistry(&registry);
    }
};
} // namespace

class TestTilingAdaptorDesktopReconcile : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // The reported bug: the user is looking at desktop 3, where the screen
    // has nothing assigned, and drags a window there from tiled desktop 1.
    void autotile_windowMovedToUnassignedDesktop_isReleasedFromSourceState()
    {
        AutotileFixture f;
        PhosphorTiles::TilingState* d1 = f.openOn(1, {kWindow, kStays});
        QVERIFY(d1 != nullptr);
        QCOMPARE(d1->windowCount(), 2);

        // Desktop 3 in view, unassigned: the screen leaves the managed set.
        f.engine.setCurrentDesktopForScreen(kScreen, 3);
        f.engine.setAutotileScreens({});
        QVERIFY(!f.engine.isAutotileScreen(kScreen));

        // KWin reports the move; the effect's metadata push lands in the registry.
        f.registry.upsert(kInstance, onDesktop(3));
        QCoreApplication::processEvents();

        QVERIFY(!f.engine.isWindowTracked(kWindow));
        QVERIFY(!d1->containsWindow(kWindow));
        QVERIFY(d1->containsWindow(kStays));
        QCOMPARE(d1->windowCount(), 1);
    }

    // The mirror move onto ANOTHER tiled desktop releases the source state
    // too; adoption on the destination is the effect's, and is not run here.
    void autotile_windowMovedBetweenTiledDesktops_isReleasedFromSourceState()
    {
        AutotileFixture f;
        PhosphorTiles::TilingState* d1 = f.openOn(1, {kWindow});
        QVERIFY(d1 != nullptr);
        QVERIFY(d1->containsWindow(kWindow));

        f.engine.setCurrentDesktopForScreen(kScreen, 2);
        f.engine.setAutotileScreens({kScreen});
        f.registry.upsert(kInstance, onDesktop(2));
        QCoreApplication::processEvents();

        QVERIFY(!d1->containsWindow(kWindow));
        QVERIFY(!f.engine.isWindowTracked(kWindow));
    }

    // A desktop set that merely GREW to include the holding desktop keeps the
    // slot, and so does a window reported on no desktop at all.
    void autotile_spanGrowthAndEmptySet_keepTheSlot()
    {
        AutotileFixture f;
        PhosphorTiles::TilingState* d1 = f.openOn(1, {kWindow});
        QVERIFY(d1 != nullptr);
        QVERIFY(d1->containsWindow(kWindow));

        f.registry.upsert(kInstance, onDesktop(1, {1, 3}));
        QCoreApplication::processEvents();
        QVERIFY(d1->containsWindow(kWindow));

        // An EMPTY reported set — a sticky window, or one whose desktop the
        // compositor did not report. The keep comes from the empty-set early
        // return, not from any sticky-aware branch: the reconcile cannot tell
        // the two apart and treats both as "nothing to check".
        f.registry.upsert(kInstance, onDesktop(0));
        QCoreApplication::processEvents();
        QVERIFY(d1->containsWindow(kWindow));
    }

    // A title tick that leaves the desktop fields alone must not reconcile at
    // all. Seeded so the registry and the engine DISAGREE about the desktop, so
    // the set-equality guard is the only thing standing between the retitle and
    // a release and deleting it fails this case instead of passing silently.
    // (The cheap sameDesktopFields check ahead of it is an optimisation, not a
    // second gate: equal fields imply equal sets, so it cannot be pinned
    // separately.)
    void autotile_titleTickWithUnchangedDesktopSet_isNotAMove()
    {
        AutotileFixture f;
        // First insert emits windowAppeared, not metadataChanged, so this
        // divergent seed drives no reconcile of its own.
        f.registry.canonicalizeWindowId(kWindow);
        f.registry.upsert(kInstance, onDesktop(2));

        PhosphorTiles::TilingState* d1 = f.openOn(1, {kWindow}, /*seedRegistry=*/false);
        QVERIFY(d1 != nullptr);
        QVERIFY(d1->containsWindow(kWindow));

        PhosphorEngine::WindowMetadata retitled = onDesktop(2);
        retitled.title = QStringLiteral("t2");
        f.registry.upsert(kInstance, retitled);
        QCoreApplication::processEvents();

        QVERIFY(d1->containsWindow(kWindow));
        QVERIFY(f.engine.isWindowTracked(kWindow));
    }

    // The multi-desktop span is what the reconcile reads when it disagrees
    // with the scalar field, in BOTH directions.
    void autotile_spanDisagreesWithScalarDesktop_followsTheSpan()
    {
        AutotileFixture f;
        PhosphorTiles::TilingState* d1 = f.openOn(1, {kWindow});
        QVERIFY(d1 != nullptr);
        QVERIFY(d1->containsWindow(kWindow));

        // KEEP. The scalar says 5, the span says {1,5}. Reading the scalar
        // alone would compute {5} and release a window that never left.
        f.registry.upsert(kInstance, onDesktop(5, {1, 5}));
        QCoreApplication::processEvents();
        QVERIFY(d1->containsWindow(kWindow));

        // RELEASE. The scalar still says 1, the span says {2,3}. Reading the
        // scalar alone would compute {1} and keep a window that did leave.
        f.registry.upsert(kInstance, onDesktop(1, {2, 3}));
        QCoreApplication::processEvents();
        QVERIFY(!d1->containsWindow(kWindow));
        QVERIFY(!f.engine.isWindowTracked(kWindow));
    }

    // A span that SHRANK is only a move when it drops the holding desktop —
    // the case a "did the set get smaller" reading gets backwards.
    void autotile_spanShrink_releasesOnlyWhenItDropsTheHeldDesktop()
    {
        AutotileFixture f;
        PhosphorTiles::TilingState* d1 = f.openOn(1, {kWindow});
        QVERIFY(d1 != nullptr);

        f.registry.upsert(kInstance, onDesktop(1, {1, 2, 3}));
        QCoreApplication::processEvents();
        QVERIFY(d1->containsWindow(kWindow));

        // Shrinks, still contains 1: keep.
        f.registry.upsert(kInstance, onDesktop(1, {1, 2}));
        QCoreApplication::processEvents();
        QVERIFY(d1->containsWindow(kWindow));

        // Shrinks and drops 1: release.
        f.registry.upsert(kInstance, onDesktop(2, {2, 3}));
        QCoreApplication::processEvents();
        QVERIFY(!d1->containsWindow(kWindow));
    }

    // A second report after a release is a clean no-op, and above all must not
    // take anything else down with it.
    void autotile_repeatReportAfterRelease_isIdempotent()
    {
        AutotileFixture f;
        PhosphorTiles::TilingState* d1 = f.openOn(1, {kWindow, kStays});
        QVERIFY(d1 != nullptr);
        QCOMPARE(d1->windowCount(), 2);

        f.registry.upsert(kInstance, onDesktop(3));
        QCoreApplication::processEvents();
        QVERIFY(!d1->containsWindow(kWindow));
        QCOMPARE(d1->windowCount(), 1);

        // Nothing holds the window now, so no engine answers and no release
        // runs. Characterisation rather than regression: with one engine in
        // the fixture there is no production line whose removal makes the
        // second report do something. The wrong-engine dispatch it would
        // otherwise be probing is pinned properly by the two-engine case.
        f.registry.upsert(kInstance, onDesktop(4));
        QCoreApplication::processEvents();
        QVERIFY(!f.engine.isWindowTracked(kWindow));
        QVERIFY(d1->containsWindow(kStays));
        QCOMPARE(d1->windowCount(), 1);
    }

    // A detached registry no longer drives the reconcile (the shutdown contract).
    void autotile_detachedRegistry_doesNothing()
    {
        AutotileFixture f;
        f.adaptor.setWindowRegistry(nullptr);

        PhosphorTiles::TilingState* d1 = f.openOn(1, {kWindow});
        QVERIFY(d1 != nullptr);

        f.registry.upsert(kInstance, onDesktop(3));
        QCoreApplication::processEvents();
        QVERIFY(d1->containsWindow(kWindow));
    }

    // Re-wiring to a second registry drops the first subscription rather than
    // stacking a second one on top of it.
    void autotile_rewiredRegistry_onlyTheCurrentOneDrives()
    {
        AutotileFixture f;
        PhosphorTiles::TilingState* d1 = f.openOn(1, {kWindow});
        QVERIFY(d1 != nullptr);
        QVERIFY(d1->containsWindow(kWindow));

        PhosphorEngine::WindowRegistry second;
        // Each registry canonicalizes independently, and canonicalizeForLookup
        // returns its argument on a miss — without this the reconcile would ask
        // about the bare instance id, every engine would answer nullopt, and
        // the case would pass for the wrong reason.
        second.canonicalizeWindowId(kWindow);
        QCOMPARE(second.canonicalizeForLookup(kInstance), kWindow);
        second.upsert(kInstance, onDesktop(1));
        f.adaptor.setWindowRegistry(&second);

        // The OLD registry is dead.
        f.registry.upsert(kInstance, onDesktop(3));
        QCoreApplication::processEvents();
        QVERIFY(d1->containsWindow(kWindow));

        // The NEW one drives.
        second.upsert(kInstance, onDesktop(3));
        QCoreApplication::processEvents();
        QVERIFY(!d1->containsWindow(kWindow));
    }

    // The release goes to the engine that ANSWERED the membership lookup, not
    // to whichever engine an id-keyed predicate happens to reach first.
    //
    // Autotile is registered first and holds the window on kScreen2 under the
    // DESTINATION desktop, so its key matches and the loop passes over it.
    // Scroll holds the same window on kScreen under desktop 1, which is the
    // stale one. Resolving the release by window id would find autotile (its
    // isWindowTracked answers true for any context) and close the wrong slot.
    void twoEngines_releaseGoesToTheEngineThatAnswered()
    {
        PhosphorEngine::WindowRegistry registry;
        PhosphorTileEngine::AutotileEngine autotile(nullptr, nullptr, nullptr, TestHelpers::testRegistry());
        autotile.setWindowRegistry(&registry);
        PhosphorScrollEngine::ScrollEngine scroll(nullptr, nullptr);
        scroll.setWindowRegistry(&registry);
        scroll.setScreenGeometryProviders(
            [](const QString&) {
                return QRect(0, 40, 1200, 760);
            },
            [](const QString&) {
                return QRect(0, 0, 1200, 800);
            });
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        adaptor.setLifecycleEngines({&autotile, &scroll});
        adaptor.setWindowRegistry(&registry);

        registry.canonicalizeWindowId(kWindow);
        registry.upsert(kInstance, onDesktop(1));

        scroll.setCurrentDesktopForScreen(kScreen, 1);
        scroll.setActiveScreens({kScreen});
        scroll.windowOpened(kWindow, kScreen);
        QCoreApplication::processEvents();
        QVERIFY(scroll.heldKeyForWindow(kWindow).has_value());
        QCOMPARE(scroll.heldKeyForWindow(kWindow)->desktop, 1);

        // The screen has to be in the managed set and on the right desktop
        // BEFORE the open, or the adopt is refused outright.
        autotile.setCurrentDesktopForScreen(kScreen2, 3);
        autotile.setAutotileScreens({kScreen2});
        autotile.windowOpened(kWindow, kScreen2);
        QCoreApplication::processEvents();
        PhosphorTiles::TilingState* a3 = autotile.tilingStateForScreen(kScreen2);
        QVERIFY(a3 != nullptr);
        QVERIFY(a3->containsWindow(kWindow));

        registry.upsert(kInstance, onDesktop(3));
        QCoreApplication::processEvents();

        QVERIFY(!scroll.heldKeyForWindow(kWindow).has_value());
        QVERIFY(!scroll.isWindowTracked(kWindow));
        // Autotile's key matched the new set, so it keeps the window.
        QVERIFY(a3->containsWindow(kWindow));
    }

    // A screen pinned to a desktop keys its states by the PIN, so a held key
    // naming the pin is not evidence the window went anywhere.
    void autotile_screenPinnedToDesktop_isLeftAlone()
    {
        AutotileFixture f;
        PhosphorTiles::TilingState* d2 = f.openOn(2, {kWindow});
        QVERIFY(d2 != nullptr);
        QVERIFY(d2->containsWindow(kWindow));

        // Pin the screen. The predicate is the caller's, so the fixture can
        // pin without a window-tracking service.
        f.engine.updateStickyScreenPins([](const QString&) {
            return true;
        });
        QCOMPARE(f.engine.stickyPinnedDesktopForScreen(kScreen), 2);

        // The compositor now reports the window on desktop 5. Its held key
        // still says 2 because that is the pin, so the reconcile must leave it
        // to the engine's own unpin migration.
        f.registry.upsert(kInstance, onDesktop(5));
        QCoreApplication::processEvents();
        QVERIFY(d2->containsWindow(kWindow));
        QVERIFY(f.engine.isWindowTracked(kWindow));
    }

    // Same invariant for the scrolling engine: a column left on desktop 1
    // when its window moved to an unassigned desktop 3 is released.
    void scrolling_windowMovedToUnassignedDesktop_isReleasedFromSourceStrip()
    {
        ScrollFixture f;
        f.registry.canonicalizeWindowId(kWindow);
        f.registry.upsert(kInstance, onDesktop(1));
        f.engine.setCurrentDesktopForScreen(kScreen, 1);
        f.engine.setActiveScreens({kScreen});
        f.engine.windowOpened(kWindow, kScreen);
        f.engine.windowOpened(kStays, kScreen);
        QCoreApplication::processEvents();
        QVERIFY(f.engine.isWindowTracked(kWindow));
        const std::optional<PhosphorEngine::PlacementStateKey> held = f.engine.heldKeyForWindow(kWindow);
        QVERIFY(held.has_value());
        QCOMPARE(held->desktop, 1);

        f.engine.setCurrentDesktopForScreen(kScreen, 3);
        f.engine.setActiveScreens({});
        QVERIFY(f.engine.isWindowTracked(kWindow));

        f.registry.upsert(kInstance, onDesktop(3));
        QCoreApplication::processEvents();

        QVERIFY(!f.engine.isWindowTracked(kWindow));
        QVERIFY(f.engine.isWindowTracked(kStays));
        QVERIFY(!f.engine.heldKeyForWindow(kWindow).has_value());
    }

    // An ACTIVITY move strands a slot exactly as a desktop move does: the key
    // carries both, and nothing else releases on the activity axis.
    void autotile_windowMovedToAnotherActivity_isReleasedFromSourceState()
    {
        const QString kActivityA = QStringLiteral("aaaaaaaa-1111-2222-3333-444444444444");
        const QString kActivityB = QStringLiteral("bbbbbbbb-5555-6666-7777-888888888888");

        AutotileFixture f;
        f.registry.canonicalizeWindowId(kWindow);
        f.registry.upsert(kInstance, onDesktop(1, {}, kActivityA));
        f.engine.setCurrentActivity(kActivityA);
        PhosphorTiles::TilingState* a = f.openOn(1, {kWindow, kStays}, /*seedRegistry=*/false);
        QVERIFY(a != nullptr);
        QCOMPARE(a->windowCount(), 2);
        QVERIFY(f.engine.heldKeyForWindow(kWindow).has_value());
        QCOMPARE(f.engine.heldKeyForWindow(kWindow)->activity, kActivityA);

        // Same desktop throughout — only the activity moves, so a
        // desktop-only reconcile sees nothing to do.
        f.registry.upsert(kInstance, onDesktop(1, {}, kActivityB));
        QCoreApplication::processEvents();

        QVERIFY(!a->containsWindow(kWindow));
        QVERIFY(!f.engine.isWindowTracked(kWindow));
        QVERIFY(a->containsWindow(kStays));
    }

    // An EMPTY reported activity is "all activities, or unknown", never "it
    // left". This pins the LENIENCY specifically — the !activity.isEmpty()
    // guard in leftTheContext — not the activity axis itself, which is pinned
    // by autotile_windowMovedToAnotherActivity_isReleasedFromSourceState and by
    // the last assertion of the direct-call case. Removing the axis entirely
    // leaves this passing, because the window never changes desktop.
    void autotile_unknownActivity_keepsTheSlot()
    {
        const QString kActivityA = QStringLiteral("aaaaaaaa-1111-2222-3333-444444444444");

        AutotileFixture f;
        f.registry.canonicalizeWindowId(kWindow);
        f.registry.upsert(kInstance, onDesktop(1, {}, kActivityA));
        f.engine.setCurrentActivity(kActivityA);
        PhosphorTiles::TilingState* a = f.openOn(1, {kWindow}, /*seedRegistry=*/false);
        QVERIFY(a != nullptr);
        QVERIFY(a->containsWindow(kWindow));

        // The window reports no activity at all. That is "on all of them, or
        // the compositor did not say", never "it left".
        f.registry.upsert(kInstance, onDesktop(1, {}, QString()));
        QCoreApplication::processEvents();
        QVERIFY(a->containsWindow(kWindow));
    }

    // The public contract, driven directly rather than through the registry —
    // which is what the method is public for.
    void reconcileWindowMembership_directCall_honoursBothAxes()
    {
        const QString kActivityA = QStringLiteral("aaaaaaaa-1111-2222-3333-444444444444");
        AutotileFixture f;
        f.registry.canonicalizeWindowId(kWindow);
        f.registry.upsert(kInstance, onDesktop(1, {}, kActivityA));
        f.engine.setCurrentActivity(kActivityA);
        PhosphorTiles::TilingState* a = f.openOn(1, {kWindow}, /*seedRegistry=*/false);
        QVERIFY(a != nullptr);
        QVERIFY(a->containsWindow(kWindow));

        // Same desktop, same activity: nothing moved.
        f.adaptor.reconcileWindowMembership(kWindow, {1}, kActivityA);
        QVERIFY(a->containsWindow(kWindow));

        // Both axes unknown. This is a no-crash / redundancy check rather than
        // a gate: leftTheContext already answers false for empty on either
        // side, so the early return that also covers it cannot be pinned
        // separately. Kept because the direct contract says an all-unknown call
        // releases nothing, and that should be readable here.
        f.adaptor.reconcileWindowMembership(kWindow, {}, QString());
        QVERIFY(a->containsWindow(kWindow));

        // Desktop still matches, activity does not.
        f.adaptor.reconcileWindowMembership(kWindow, {1}, QStringLiteral("other-activity"));
        QVERIFY(!a->containsWindow(kWindow));
    }

    // Snapping keeps per-context stores like the tiling engines, but zone
    // occupancy is resolved across EVERY store rather than the one in view. So
    // a window left listed in the zone it was snapped into on a desktop it has
    // since left stays a live navigation target, and picking it drags the user
    // to whichever desktop the window is really on now.
    //
    // Snap is reconciled through the membership list, not the lifecycle
    // pipeline: it takes no part in window dispatch or the replay cache.
    void snapping_windowMovedOffItsDesktop_stopsOccupyingTheZone()
    {
        const QString kZone = QStringLiteral("{11111111-1111-1111-1111-111111111111}");
        PhosphorEngine::WindowRegistry registry;
        PhosphorSnapEngine::SnapEngine snap(nullptr, nullptr, nullptr, nullptr);
        snap.setWindowRegistry(&registry);
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        // Deliberately NOT a lifecycle engine — that list drives dispatch.
        adaptor.setLifecycleEngines({});
        adaptor.setMembershipEngines({&snap});
        adaptor.setWindowRegistry(&registry);

        registry.canonicalizeWindowId(kWindow);
        registry.upsert(kInstance, onDesktop(1));

        snap.setCurrentDesktopForScreen(kScreen, 1);
        PhosphorSnapEngine::SnapState* d1 = snap.stateForWindowOnScreen(kWindow, kScreen);
        QVERIFY(d1 != nullptr);
        d1->assignWindowToZone(kWindow, kZone, kScreen, 1);
        QVERIFY(d1->windowsInZone(kZone).contains(kWindow));
        QVERIFY(snap.heldKeyForWindow(kWindow).has_value());
        QCOMPARE(snap.heldKeyForWindow(kWindow)->desktop, 1);

        // The window moves to desktop 3. Desktop 1's store must let it go.
        registry.upsert(kInstance, onDesktop(3));
        QCoreApplication::processEvents();

        QVERIFY2(!d1->windowsInZone(kZone).contains(kWindow),
                 "a window that left the desktop must stop occupying the zone it was snapped into there");
        QVERIFY(!snap.heldKeyForWindow(kWindow).has_value());
    }

    // heldKeyForWindow is MEMBERSHIP-grade, and a drag-insert preview is the
    // one state where that differs from the reverse-map key: the preview keeps
    // the key while the window leaves the strip. A reconcile arriving mid-drag
    // therefore finds no holder and releases nothing, which is the safe
    // direction for a window the user is still moving.
    void scrolling_windowDetachedForDragPreview_isNotHeld()
    {
        ScrollFixture f;
        f.registry.canonicalizeWindowId(kWindow);
        f.registry.upsert(kInstance, onDesktop(1));
        f.engine.setCurrentDesktopForScreen(kScreen, 1);
        f.engine.setActiveScreens({kScreen});
        f.engine.windowOpened(kWindow, kScreen);
        f.engine.windowOpened(kStays, kScreen);
        QCoreApplication::processEvents();
        QVERIFY(f.engine.heldKeyForWindow(kWindow).has_value());

        QVERIFY(f.engine.beginDragInsertPreview(kWindow, kScreen));
        // Tracked, but no state holds it.
        QVERIFY(f.engine.isWindowTracked(kWindow));
        QVERIFY(!f.engine.heldKeyForWindow(kWindow).has_value());

        // A desktop report landing mid-drag must not release it.
        f.registry.upsert(kInstance, onDesktop(3));
        QCoreApplication::processEvents();
        QVERIFY(f.engine.isWindowTracked(kWindow));

        f.engine.cancelDragInsertPreview();
        QCoreApplication::processEvents();
        QVERIFY(f.engine.heldKeyForWindow(kWindow).has_value());
    }
};

QTEST_GUILESS_MAIN(TestTilingAdaptorDesktopReconcile)
#include "test_tiling_adaptor_desktop_reconcile.moc"

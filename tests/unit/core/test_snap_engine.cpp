// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>
#include <QLoggingCategory>

#include <memory>

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorWindowRule/MatchExpression.h>
#include <PhosphorWindowRule/MatchTypes.h>
#include <PhosphorWindowRule/RuleAction.h>
#include <PhosphorWindowRule/WindowRule.h>
#include <PhosphorWindowRule/WindowRuleSet.h>
#include "config/configbackends.h"
#include "core/interfaces.h"

using namespace PlasmaZones;
using namespace PhosphorSnapEngine;

// =========================================================================
// Minimal stubs for WTS constructor assertions
// =========================================================================

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/LayoutRegistryTestHelpers.h"
#include "../helpers/StubSettings.h"
#include "../helpers/StubZoneDetector.h"

using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class StubZoneDetectorSnap : public PhosphorZones::IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorSnap(QObject* parent = nullptr)
        : PhosphorZones::IZoneDetector(parent)
    {
    }
    PhosphorZones::Layout* layout() const override
    {
        return nullptr;
    }
    void setLayout(PhosphorZones::Layout*) override
    {
    }
    PhosphorZones::ZoneDetectionResult detectZone(const QPointF&) const override
    {
        return {};
    }
    PhosphorZones::ZoneDetectionResult detectMultiZone(const QPointF&) const override
    {
        return {};
    }
    PhosphorZones::Zone* zoneAtPoint(const QPointF&) const override
    {
        return nullptr;
    }
    PhosphorZones::Zone* nearestZone(const QPointF&) const override
    {
        return nullptr;
    }
    QVector<PhosphorZones::Zone*> expandPaintedZonesToRect(const QVector<PhosphorZones::Zone*>&) const override
    {
        return {};
    }
    void highlightZone(PhosphorZones::Zone*) override
    {
    }
    void highlightZones(const QVector<PhosphorZones::Zone*>&) override
    {
    }
    void clearHighlights() override
    {
    }
};

/**
 * @brief Unit tests for SnapEngine: screen routing, lifecycle, float state,
 *        signal emission, and persistence delegation.
 */
class TestSnapEngine : public QObject
{
    Q_OBJECT

private:
    // Isolates XDG_DATA_HOME / XDG_CONFIG_HOME under a temp dir so the
    // LayoutRegistry's default "plasmazones/layouts" subdir resolves into
    // the temp dir instead of ~/.local/share/plasmazones/layouts/.
    // Without this every test run leaks a "TestLayout-<uuid>.json" into
    // the user's real layouts dir — by April 2026 the directory had
    // accumulated >100 stale TestLayouts from prior CI / dev test runs,
    // showing up duplicated in the layout picker overlay.
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetectorSnap* m_zoneDetector = nullptr;
    PhosphorPlacement::WindowTrackingService* m_wts = nullptr;
    PhosphorSnapEngine::SnapState* m_snapState = nullptr;

private Q_SLOTS:

    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new StubZoneDetectorSnap(nullptr);
        m_wts = new PhosphorPlacement::WindowTrackingService(m_layoutManager, m_zoneDetector, nullptr, nullptr);
        m_snapState = new PhosphorSnapEngine::SnapState(QString(), nullptr);
        m_wts->setSnapState(m_snapState);
    }

    void cleanup()
    {
        m_wts->setSnapState(nullptr);
        delete m_snapState;
        delete m_wts;
        delete m_zoneDetector;
        delete m_settings;
        delete m_layoutManager;
        m_guard.reset();
    }

    // =========================================================================
    // isActiveOnScreen tests
    // =========================================================================

    void testIsActiveOnScreen_noAutotileEngine_returnsTrue()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);

        // No autotile engine set — SnapEngine owns all screens
        QVERIFY(engine.isActiveOnScreen(QStringLiteral("DP-1")));
        QVERIFY(engine.isActiveOnScreen(QStringLiteral("HDMI-1")));
        QVERIFY(engine.isActiveOnScreen(QString()));
    }

    // =========================================================================
    // windowFocused tests
    // =========================================================================

    void testWindowFocused_updatesLastActiveScreen()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);

        QCOMPARE(engine.lastActiveScreenId(), QString());

        engine.windowFocused(QStringLiteral("app|uuid1"), QStringLiteral("DP-2"));

        QCOMPARE(engine.lastActiveScreenId(), QStringLiteral("DP-2"));
    }

    // =========================================================================
    // windowClosed tests
    // =========================================================================

    void testWindowClosed_doesNotCrash()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);

        engine.windowClosed(QStringLiteral("app|uuid1"));
        engine.windowClosed(QString());
    }

    // =========================================================================
    // PhosphorPlacement::WindowTrackingService::clearFloatingForSnap tests
    //
    // (The former SnapEngine::clearFloatingStateForSnap wrapper was removed —
    // all snap-commit paths now go through PhosphorPlacement::WindowTrackingService::commitSnap
    // which handles floating-state clearing internally via clearFloatingForSnap.)
    // =========================================================================

    void testClearFloatingForSnap_returnsTrue_whenFloating()
    {
        const QString windowId = QStringLiteral("app|uuid-float");
        m_wts->setWindowFloating(windowId, true);
        QVERIFY(m_wts->isWindowFloating(windowId));

        bool result = m_wts->clearFloatingForSnap(windowId);
        QVERIFY(result);
        QVERIFY(!m_wts->isWindowFloating(windowId));
    }

    void testClearFloatingForSnap_returnsFalse_whenNotFloating()
    {
        const QString windowId = QStringLiteral("app|uuid-nofloat");
        QVERIFY(!m_wts->isWindowFloating(windowId));

        bool result = m_wts->clearFloatingForSnap(windowId);
        QVERIFY(!result);
    }

    // =========================================================================
    // toggleWindowFloat signal tests
    // =========================================================================

    void testToggleWindowFloat_snappedWindow_emitsFloatingTrue()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        m_wts->setSnapState(engine.snapState());
        const QString windowId = QStringLiteral("app|uuid-snap");
        const QString screenName = QStringLiteral("DP-1");

        m_wts->assignWindowToZone(windowId, QStringLiteral("zone-1"), screenName, 0);
        QVERIFY(engine.snapState()->isWindowSnapped(windowId));
        QVERIFY(!engine.snapState()->isFloating(windowId));

        QSignalSpy floatSpy(&engine, &SnapEngine::windowFloatingChanged);
        QSignalSpy feedbackSpy(&engine, &SnapEngine::navigationFeedback);

        engine.toggleWindowFloat(windowId, screenName);

        QCOMPARE(floatSpy.count(), 1);
        QCOMPARE(floatSpy.at(0).at(0).toString(), windowId);
        QCOMPARE(floatSpy.at(0).at(1).toBool(), true);
        QCOMPARE(floatSpy.at(0).at(2).toString(), screenName);

        QCOMPARE(feedbackSpy.count(), 1);
        QCOMPARE(feedbackSpy.at(0).at(0).toBool(), true);
        QCOMPARE(feedbackSpy.at(0).at(2).toString(), QStringLiteral("floated"));
    }

    void testToggleWindowFloat_notSnappedNotFloating_noSignal()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        m_wts->setSnapState(engine.snapState());
        const QString windowId = QStringLiteral("app|uuid-untracked");
        const QString screenName = QStringLiteral("DP-1");

        QSignalSpy floatSpy(&engine, &SnapEngine::windowFloatingChanged);
        QSignalSpy feedbackSpy(&engine, &SnapEngine::navigationFeedback);

        engine.toggleWindowFloat(windowId, screenName);

        QCOMPARE(floatSpy.count(), 0);
        QCOMPARE(feedbackSpy.count(), 0);
    }

    void testSetWindowFloat_true_emitsFloatingChanged()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        m_wts->setSnapState(engine.snapState());
        const QString windowId = QStringLiteral("app|uuid-setfloat");

        m_wts->assignWindowToZone(windowId, QStringLiteral("zone-1"), QStringLiteral("DP-1"), 0);

        QSignalSpy floatSpy(&engine, &SnapEngine::windowFloatingChanged);
        engine.setWindowFloat(windowId, true);

        QCOMPARE(floatSpy.count(), 1);
        QCOMPARE(floatSpy.at(0).at(0).toString(), windowId);
        QCOMPARE(floatSpy.at(0).at(1).toBool(), true);
    }

    void testSetWindowFloat_false_noPreFloatZone_keepsFloating()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        m_wts->setSnapState(engine.snapState());
        const QString windowId = QStringLiteral("app|uuid-unfloat-fail");

        m_wts->setWindowFloating(windowId, true);

        QSignalSpy floatSpy(&engine, &SnapEngine::windowFloatingChanged);
        engine.setWindowFloat(windowId, false);

        // No pre-float zone → unfloat fails → window stays floating, no signal
        QCOMPARE(floatSpy.count(), 0);
        QVERIFY(m_wts->isWindowFloating(windowId));
    }

    // =========================================================================
    // Dual-store sync tests — verify WTS and SnapState agree after mutations
    // =========================================================================

    void testDualStoreSync_assignWindowToZone()
    {
        const QString windowId = QStringLiteral("app|uuid-sync");
        const QString zoneId = QStringLiteral("zone-1");
        const QString screen = QStringLiteral("DP-1");

        m_wts->assignWindowToZone(windowId, zoneId, screen, 1);
        QVERIFY(m_wts->isWindowSnapped(windowId));
        QVERIFY(m_snapState->isWindowSnapped(windowId));
        QCOMPARE(m_snapState->zoneForWindow(windowId), zoneId);
        QCOMPARE(m_snapState->screenForWindow(windowId), screen);
        QCOMPARE(m_snapState->desktopForWindow(windowId), 1);
    }

    void testDualStoreSync_floatViaWts()
    {
        const QString windowId = QStringLiteral("app|uuid-float-sync");

        m_wts->setWindowFloating(windowId, true);
        QVERIFY(m_wts->isWindowFloating(windowId));
        QVERIFY(m_snapState->isFloating(windowId));

        m_wts->setWindowFloating(windowId, false);
        QVERIFY(!m_wts->isWindowFloating(windowId));
        QVERIFY(!m_snapState->isFloating(windowId));
    }

    void testDualStoreSync_unsnapForFloat()
    {
        const QString windowId = QStringLiteral("app|uuid-unsnap-sync");
        const QString screen = QStringLiteral("DP-1");

        m_wts->assignWindowToZone(windowId, QStringLiteral("zone-2"), screen, 0);
        QVERIFY(m_wts->isWindowSnapped(windowId));
        QVERIFY(m_snapState->isWindowSnapped(windowId));

        m_wts->unsnapForFloat(windowId);
        QVERIFY(!m_wts->isWindowSnapped(windowId));
        QVERIFY(!m_snapState->isWindowSnapped(windowId));
        QCOMPARE(m_snapState->preFloatZone(windowId), QStringLiteral("zone-2"));
    }

    void testDualStoreSync_clearPreFloatZone()
    {
        const QString windowId = QStringLiteral("app|uuid-prefloat-sync");
        const QString screen = QStringLiteral("DP-1");

        m_wts->assignWindowToZone(windowId, QStringLiteral("zone-3"), screen, 0);
        m_wts->unsnapForFloat(windowId);
        QVERIFY(!m_snapState->preFloatZone(windowId).isEmpty());

        m_wts->clearPreFloatZone(windowId);
        QVERIFY(m_snapState->preFloatZone(windowId).isEmpty());
    }

    // testEngineBaseUnmanagedGeometry removed: the per-engine unmanaged-geometry store
    // was collapsed into the unified WindowPlacementStore (shared freeGeometryByScreen).

    void testDualStoreSync_windowClosed()
    {
        const QString windowId = QStringLiteral("app|uuid-closed-sync");
        const QString screen = QStringLiteral("DP-1");

        m_wts->assignWindowToZone(windowId, QStringLiteral("zone-1"), screen, 1);
        QVERIFY(m_snapState->isWindowSnapped(windowId));

        m_wts->windowClosed(windowId);
        QVERIFY(!m_snapState->isWindowSnapped(windowId));
        QVERIFY(!m_snapState->isFloating(windowId));
        QCOMPARE(m_snapState->screenForWindow(windowId), QString());
    }

    void testDualStoreSync_updateLastUsedZone()
    {
        m_wts->updateLastUsedZone(QStringLiteral("zone-5"), QStringLiteral("DP-2"), QStringLiteral("konsole"), 2);
        QCOMPARE(m_snapState->lastUsedZoneId(), QStringLiteral("zone-5"));
        QCOMPARE(m_snapState->lastUsedScreenId(), QStringLiteral("DP-2"));
        QCOMPARE(m_snapState->lastUsedZoneClass(), QStringLiteral("konsole"));
        QCOMPARE(m_snapState->lastUsedDesktop(), 2);
    }

    void testDualStoreSync_markAndClearAutoSnapped()
    {
        const QString windowId = QStringLiteral("app|uuid-auto-sync");

        m_wts->markAsAutoSnapped(windowId);
        QVERIFY(m_snapState->isAutoSnapped(windowId));

        m_wts->clearAutoSnapped(windowId);
        QVERIFY(!m_snapState->isAutoSnapped(windowId));
    }

    void testDualStoreSync_unassignClearsLastUsedZone()
    {
        const QString windowId = QStringLiteral("app|uuid-lastused-unassign");
        const QString zoneId = QStringLiteral("zone-7");
        const QString screen = QStringLiteral("DP-1");

        m_wts->assignWindowToZone(windowId, zoneId, screen, 1);
        m_wts->updateLastUsedZone(zoneId, screen, QStringLiteral("dolphin"), 1);
        QCOMPARE(m_snapState->lastUsedZoneId(), zoneId);

        m_wts->unassignWindow(windowId);
        QCOMPARE(m_snapState->lastUsedZoneId(), QString());
        QCOMPARE(m_snapState->lastUsedScreenId(), QString());
        QCOMPARE(m_snapState->lastUsedDesktop(), 0);
    }

    void testDualStoreSync_unsnapForFloatClearsLastUsedZone()
    {
        const QString windowId = QStringLiteral("app|uuid-lastused-float");
        const QString zoneId = QStringLiteral("zone-8");
        const QString screen = QStringLiteral("DP-2");

        m_wts->assignWindowToZone(windowId, zoneId, screen, 2);
        m_wts->updateLastUsedZone(zoneId, screen, QStringLiteral("konsole"), 2);
        QCOMPARE(m_snapState->lastUsedZoneId(), zoneId);

        m_wts->unsnapForFloat(windowId);
        QCOMPARE(m_snapState->lastUsedZoneId(), QString());
        QCOMPARE(m_snapState->lastUsedDesktop(), 0);
    }

    void testDualStoreSync_recordSnapIntent()
    {
        const QString windowId = QStringLiteral("dolphin|uuid-intent");

        m_wts->recordSnapIntent(windowId, true);
        QVERIFY(m_snapState->userSnappedClasses().contains(QStringLiteral("dolphin")));
    }

    void testPruneStaleAssignments_coversPreFloatMaps()
    {
        const QString windowId = QStringLiteral("app|uuid-prefloat-prune");
        const QString screen = QStringLiteral("DP-1");

        m_snapState->assignWindowToZone(windowId, QStringLiteral("zone-1"), screen, 0);
        m_snapState->unsnapForFloat(windowId);
        QVERIFY(!m_snapState->preFloatZone(windowId).isEmpty());

        QSet<QString> alive;
        int pruned = m_snapState->pruneStaleAssignments(alive);
        QVERIFY(pruned > 0);
        QVERIFY(m_snapState->preFloatZone(windowId).isEmpty());
    }

    // =========================================================================
    // saveState / loadState persistence delegation tests
    // =========================================================================

    void testSaveState_callsDelegateWhenSet()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        bool saveCalled = false;
        bool loadCalled = false;
        engine.setPersistenceDelegate(
            [&saveCalled]() {
                saveCalled = true;
            },
            [&loadCalled]() {
                loadCalled = true;
            });

        engine.saveState();
        QVERIFY(saveCalled);
        QVERIFY(!loadCalled);
    }

    void testLoadState_callsDelegateWhenSet()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        bool saveCalled = false;
        bool loadCalled = false;
        engine.setPersistenceDelegate(
            [&saveCalled]() {
                saveCalled = true;
            },
            [&loadCalled]() {
                loadCalled = true;
            });

        engine.loadState();
        QVERIFY(!saveCalled);
        QVERIFY(loadCalled);
    }

    void testSaveState_noopWithoutDelegate()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.saveState();
        engine.loadState();
    }

    void testSaveLoadState_bothDelegatesCalled()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        int saveCount = 0;
        int loadCount = 0;
        engine.setPersistenceDelegate(
            [&saveCount]() {
                saveCount++;
            },
            [&loadCount]() {
                loadCount++;
            });

        engine.saveState();
        engine.saveState();
        engine.loadState();

        QCOMPARE(saveCount, 2);
        QCOMPARE(loadCount, 1);
    }

    // =========================================================================
    // Float-state screen preservation (PR #366 regression pin)
    //
    // unsnapForFloat now preserves the window's screen assignment so that the
    // daemon can still answer "what screen does this floating window live on"
    // — that's what makes the float-toggle router send the unfloat shortcut
    // back to this engine. Erasing the screen would route to whatever the
    // focus cache pointed at, which is exactly the bug the PR fixes.
    // =========================================================================

    void testUnsnapForFloat_preservesScreenAssignment()
    {
        const QString windowId = QStringLiteral("app|uuid-screen-preserve");
        const QString screen = QStringLiteral("DP-3");

        m_snapState->assignWindowToZone(windowId, QStringLiteral("zone-1"), screen, 4);
        QCOMPARE(m_snapState->screenAssignments().value(windowId), screen);

        m_snapState->unsnapForFloat(windowId);

        // Zone gone, but screen + desktop preserved.
        QVERIFY(!m_snapState->isWindowSnapped(windowId));
        QCOMPARE(m_snapState->screenAssignments().value(windowId), screen);
        QCOMPARE(m_snapState->desktopForWindow(windowId), 4);
    }

    void testUnassignWindow_clearsScreenAssignment()
    {
        // Sister test confirming the non-float path still clears the screen
        // — the DRY refactor of clearZoneAssignment must keep the two paths
        // distinct.
        const QString windowId = QStringLiteral("app|uuid-screen-clear");
        const QString screen = QStringLiteral("DP-3");

        m_snapState->assignWindowToZone(windowId, QStringLiteral("zone-1"), screen, 4);
        m_snapState->unassignWindow(windowId);

        QVERIFY(!m_snapState->isWindowSnapped(windowId));
        QVERIFY(m_snapState->screenAssignments().value(windowId).isEmpty());
        QCOMPARE(m_snapState->desktopForWindow(windowId), 0);
    }

    // =========================================================================
    // setFloatingOnScreen — used by SnapEngine::handoffReceive when adopting
    // a floating window from another engine. Must populate the screen and
    // desktop assignments so subsequent screenForTrackedWindow lookups
    // resolve, and so isWindowTracked returns true.
    // =========================================================================

    void testSetFloatingOnScreen_populatesScreenDesktopAndFloating()
    {
        const QString windowId = QStringLiteral("app|uuid-handoff-recv");
        const QString screen = QStringLiteral("HDMI-1");

        m_snapState->setFloatingOnScreen(windowId, screen, 2);

        QVERIFY(m_snapState->isFloating(windowId));
        QCOMPARE(m_snapState->screenAssignments().value(windowId), screen);
        QCOMPARE(m_snapState->desktopForWindow(windowId), 2);
        QVERIFY(!m_snapState->isWindowSnapped(windowId));
    }

    void testSetFloatingOnScreen_emptyArgsIsNoop()
    {
        m_snapState->setFloatingOnScreen(QString(), QStringLiteral("DP-1"), 0);
        m_snapState->setFloatingOnScreen(QStringLiteral("app|uuid"), QString(), 0);
        QVERIFY(m_snapState->isEmpty());
    }

    // =========================================================================
    // SnapEngine::handoffRelease — drops snap-private tracking only. Must
    // NOT route through WindowTrackingService (which fires shared signals
    // that the autotile engine listens to). Pre-float capture is preserved.
    // =========================================================================

    void testHandoffRelease_clearsZoneAssignment()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        m_wts->setSnapState(engine.snapState());

        const QString windowId = QStringLiteral("app|uuid-release-zone");
        const QString screen = QStringLiteral("DP-2");
        engine.snapState()->assignWindowToZone(windowId, QStringLiteral("zone-3"), screen, 1);
        QVERIFY(engine.snapState()->isWindowSnapped(windowId));

        engine.handoffRelease(windowId);

        QVERIFY(!engine.snapState()->isWindowSnapped(windowId));
        QVERIFY(engine.snapState()->screenAssignments().value(windowId).isEmpty());
        m_wts->setSnapState(nullptr);
    }

    void testHandoffRelease_clearsFloatingTracking()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        m_wts->setSnapState(engine.snapState());

        const QString windowId = QStringLiteral("app|uuid-release-float");
        const QString screen = QStringLiteral("DP-2");
        engine.snapState()->setFloatingOnScreen(windowId, screen, 0);
        QVERIFY(engine.snapState()->isFloating(windowId));

        engine.handoffRelease(windowId);

        QVERIFY(!engine.snapState()->isFloating(windowId));
        m_wts->setSnapState(nullptr);
    }

    void testHandoffRelease_doesNotEmitWtsZoneSignal()
    {
        // Routing release through WTS would fire windowZoneChanged, which
        // the autotile engine listens to — and would interpret as "drop
        // this window from your state mid-handoff" (the bug commit
        // 56146efc fixed). Guard the regression by spying for the WTS
        // signal during a handoffRelease.
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        m_wts->setSnapState(engine.snapState());

        const QString windowId = QStringLiteral("app|uuid-release-no-signal");
        engine.snapState()->assignWindowToZone(windowId, QStringLiteral("zone-1"), QStringLiteral("DP-1"), 0);

        QSignalSpy zoneSpy(m_wts, &PhosphorPlacement::WindowTrackingService::windowZoneChanged);

        engine.handoffRelease(windowId);

        QCOMPARE(zoneSpy.count(), 0);
        m_wts->setSnapState(nullptr);
    }

    void testHandoffRelease_preservesPreFloatCapture()
    {
        // The receiving engine may consult HandoffContext for size restoration
        // on a future return handoff — pre-float capture must survive.
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        m_wts->setSnapState(engine.snapState());

        const QString windowId = QStringLiteral("app|uuid-release-prefloat");
        const QString screen = QStringLiteral("DP-2");
        engine.snapState()->assignWindowToZone(windowId, QStringLiteral("zone-2"), screen, 0);
        engine.snapState()->unsnapForFloat(windowId); // populates pre-float capture
        QVERIFY(!engine.snapState()->preFloatZone(windowId).isEmpty());

        engine.handoffRelease(windowId);

        QVERIFY(!engine.snapState()->preFloatZone(windowId).isEmpty());
        m_wts->setSnapState(nullptr);
    }

    // =========================================================================
    // SnapEngine::handoffReceive — floating arrival populates screen so
    // subsequent screenForTrackedWindow / lastActiveScreenName resolve.
    // =========================================================================

    void testHandoffReceive_floatingArrivalPopulatesScreen()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        m_wts->setSnapState(engine.snapState());

        const QString windowId = QStringLiteral("app|uuid-recv-float");
        const QString screen = QStringLiteral("DP-1");

        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = screen;
        ctx.fromEngineId = PhosphorEngine::WindowPlacement::autotileEngineId();
        ctx.wasFloating = true;
        engine.handoffReceive(ctx);

        QCOMPARE(engine.screenForTrackedWindow(windowId), screen);
        QVERIFY(engine.isWindowTracked(windowId));
        QVERIFY(engine.snapState()->isFloating(windowId));
        m_wts->setSnapState(nullptr);
    }

    void testHandoffReceive_emptyArgsIsNoop()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        m_wts->setSnapState(engine.snapState());

        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = QString();
        ctx.toScreenId = QStringLiteral("DP-1");
        engine.handoffReceive(ctx);

        ctx.windowId = QStringLiteral("app|uuid");
        ctx.toScreenId = QString();
        engine.handoffReceive(ctx);

        QVERIFY(engine.snapState()->isEmpty());
        m_wts->setSnapState(nullptr);
    }

    // =========================================================================
    // screenForTrackedWindow / isWindowTracked — used by the daemon's
    // shortcut router (lastActiveScreenName) to resolve the right engine.
    // =========================================================================

    void testScreenForTrackedWindow_returnsEmptyWhenUntracked()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        m_wts->setSnapState(engine.snapState());
        QCOMPARE(engine.screenForTrackedWindow(QStringLiteral("app|uuid-not-tracked")), QString());
        QVERIFY(!engine.isWindowTracked(QStringLiteral("app|uuid-not-tracked")));
        m_wts->setSnapState(nullptr);
    }

    void testScreenForTrackedWindow_followsSnapAssignment()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        m_wts->setSnapState(engine.snapState());

        const QString windowId = QStringLiteral("app|uuid-tracked-snap");
        const QString screen = QStringLiteral("DP-2");
        engine.snapState()->assignWindowToZone(windowId, QStringLiteral("zone-1"), screen, 0);

        QCOMPARE(engine.screenForTrackedWindow(windowId), screen);
        QVERIFY(engine.isWindowTracked(windowId));
        m_wts->setSnapState(nullptr);
    }

    void testScreenForTrackedWindow_followsFloatedSnap()
    {
        // After unsnapForFloat the screen is preserved — so the snap engine
        // must still claim ownership of the window for routing purposes.
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        m_wts->setSnapState(engine.snapState());

        const QString windowId = QStringLiteral("app|uuid-tracked-floated");
        const QString screen = QStringLiteral("DP-2");
        engine.snapState()->assignWindowToZone(windowId, QStringLiteral("zone-1"), screen, 0);
        engine.snapState()->unsnapForFloat(windowId);

        QCOMPARE(engine.screenForTrackedWindow(windowId), screen);
        QVERIFY(engine.isWindowTracked(windowId));
        m_wts->setSnapState(nullptr);
    }

    // =========================================================================
    // calculateSnapToEmptyZone — auto-assign gate (#370)
    //
    // Effective gate is `layout->autoAssign() OR settings->autoAssignAllLayouts()`.
    // The four-quadrant truth table verifies that toggling EITHER input flips
    // the gate, and only the FALSE/FALSE quadrant short-circuits to noSnap with
    // the "autoAssign=false" debug log. The other three quadrants pass the
    // gate; geometry resolution then fails in this guiless fixture (no
    // ScreenManager), so a different debug log fires. We assert via log
    // capture rather than result.shouldSnap because the geometry side of
    // calculateSnapToEmptyZone needs a wired QScreen — out of scope for
    // exercising the gate itself.
    // =========================================================================

private:
    static QStringList& gateLogSink()
    {
        static QStringList sink;
        return sink;
    }
    static void gateLogHandler(QtMsgType, const QMessageLogContext&, const QString& msg)
    {
        gateLogSink().append(msg);
    }

    /// Run calculateSnapToEmptyZone with the given gate inputs and capture
    /// debug logs from the snap-engine category. Returns the captured lines.
    QStringList runGate(SnapEngine& engine, PhosphorZones::Layout* layout, bool perLayoutAuto, bool globalAuto,
                        const QString& screenId)
    {
        layout->setAutoAssign(perLayoutAuto);
        m_settings->setAutoAssignAllLayouts(globalAuto);

        QLoggingCategory::setFilterRules(QStringLiteral("org.phosphor.snap-engine.debug=true"));
        gateLogSink().clear();
        QtMessageHandler prev = qInstallMessageHandler(&TestSnapEngine::gateLogHandler);

        // Result is intentionally ignored — geometry resolution depends on a
        // wired ScreenManager, which a guiless fixture doesn't provide. The
        // log line tells us which branch executed, which is what the gate
        // contract is actually about.
        (void)engine.calculateSnapToEmptyZone(QStringLiteral("app|uuid-gate"), screenId, /*isSticky*/ false);

        qInstallMessageHandler(prev);
        QLoggingCategory::setFilterRules(QString());
        return gateLogSink();
    }

    /// Run resolveWindowRestore while capturing snap-engine debug logs, so a
    /// test can assert WHICH branch produced the result — the disabled-context
    /// gate logs a distinctive line. Mirrors runGate()'s capture pattern.
    QStringList captureResolveLogs(SnapEngine& engine, const QString& windowId, const QString& screenId,
                                   PhosphorEngine::SnapResult* outResult)
    {
        QLoggingCategory::setFilterRules(QStringLiteral("org.phosphor.snap-engine.debug=true"));
        gateLogSink().clear();
        QtMessageHandler prev = qInstallMessageHandler(&TestSnapEngine::gateLogHandler);

        const PhosphorEngine::SnapResult result = engine.resolveWindowRestore(windowId, screenId, /*sticky*/ false);
        if (outResult) {
            *outResult = result;
        }

        qInstallMessageHandler(prev);
        QLoggingCategory::setFilterRules(QString());
        return gateLogSink();
    }

private Q_SLOTS:

    void testCalculateSnapToEmptyZone_gate_globalOff_perLayoutOff_blocks()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);

        const QStringList lines =
            runGate(engine, layout, /*perLayoutAuto*/ false, /*globalAuto*/ false, QStringLiteral("DP-1"));

        QVERIFY2(lines.join(QLatin1Char('\n')).contains(QStringLiteral("autoAssign=false")),
                 "gate must short-circuit with the autoAssign=false debug log when both inputs are false");
        m_wts->setSnapState(nullptr);
    }

    void testCalculateSnapToEmptyZone_gate_globalOff_perLayoutOn_passes()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);

        const QStringList lines =
            runGate(engine, layout, /*perLayoutAuto*/ true, /*globalAuto*/ false, QStringLiteral("DP-1"));

        const QString joined = lines.join(QLatin1Char('\n'));
        QVERIFY2(!joined.contains(QStringLiteral("autoAssign=false")),
                 "per-layout flag alone must pass the gate (no autoAssign=false log)");
        m_wts->setSnapState(nullptr);
    }

    void testCalculateSnapToEmptyZone_gate_globalOn_perLayoutOff_passes()
    {
        // Force-on override (#370): when the global toggle is on, the gate
        // must pass even with the per-layout flag off.
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);

        const QStringList lines =
            runGate(engine, layout, /*perLayoutAuto*/ false, /*globalAuto*/ true, QStringLiteral("DP-1"));

        const QString joined = lines.join(QLatin1Char('\n'));
        QVERIFY2(!joined.contains(QStringLiteral("autoAssign=false")),
                 "global toggle alone must pass the gate (no autoAssign=false log)");
        m_wts->setSnapState(nullptr);
    }

    void testCalculateSnapToEmptyZone_gate_globalOn_perLayoutOn_passes()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);

        const QStringList lines =
            runGate(engine, layout, /*perLayoutAuto*/ true, /*globalAuto*/ true, QStringLiteral("DP-1"));

        const QString joined = lines.join(QLatin1Char('\n'));
        QVERIFY2(!joined.contains(QStringLiteral("autoAssign=false")),
                 "both inputs true must pass the gate (no autoAssign=false log)");
        m_wts->setSnapState(nullptr);
    }

    // =========================================================================
    // focus-new-windows — commitSnapImpl emits activateWindowRequested only for
    // AutoRestored commits when ISnapSettings::focusNewWindows() is on. Manual
    // (UserInitiated) commits never request focus, regardless of the setting.
    // =========================================================================

    void testFocusNewWindows_autoRestored_emitsWhenEnabled()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        m_settings->setSnappingFocusNewWindows(true);

        QSignalSpy spy(&engine, &SnapEngine::activateWindowRequested);
        engine.commitSnap(QStringLiteral("win-focus-1"), QStringLiteral("zone-1"), QStringLiteral("DP-1"),
                          PhosphorEngine::SnapIntent::AutoRestored);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), QStringLiteral("win-focus-1"));
        m_wts->setSnapState(nullptr);
    }

    void testFocusNewWindows_autoRestored_silentWhenDisabled()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        m_settings->setSnappingFocusNewWindows(false);

        QSignalSpy spy(&engine, &SnapEngine::activateWindowRequested);
        engine.commitSnap(QStringLiteral("win-focus-2"), QStringLiteral("zone-1"), QStringLiteral("DP-1"),
                          PhosphorEngine::SnapIntent::AutoRestored);

        QCOMPARE(spy.count(), 0);
        m_wts->setSnapState(nullptr);
    }

    void testFocusNewWindows_userInitiated_neverEmits()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        // Even with the setting on, a user-initiated snap (drag, keyboard) must
        // not steal focus — the window is already where the user put it.
        m_settings->setSnappingFocusNewWindows(true);

        QSignalSpy spy(&engine, &SnapEngine::activateWindowRequested);
        engine.commitSnap(QStringLiteral("win-focus-3"), QStringLiteral("zone-1"), QStringLiteral("DP-1"),
                          PhosphorEngine::SnapIntent::UserInitiated);

        QCOMPARE(spy.count(), 0);
        m_wts->setSnapState(nullptr);
    }

    void testFocusNewWindows_multiZone_autoRestored_emitsOnce()
    {
        // A multi-zone auto-restore (zone span) routes through the same
        // commitSnapImpl chokepoint, so it must request focus exactly once.
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        m_settings->setSnappingFocusNewWindows(true);

        QSignalSpy spy(&engine, &SnapEngine::activateWindowRequested);
        engine.commitMultiZoneSnap(QStringLiteral("win-focus-4"), {QStringLiteral("zone-1"), QStringLiteral("zone-2")},
                                   QStringLiteral("DP-1"), PhosphorEngine::SnapIntent::AutoRestored);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), QStringLiteral("win-focus-4"));
        m_wts->setSnapState(nullptr);
    }

    // =========================================================================
    // resolveWindowRestore — disabled-context gate (ShouldRestorePredicate,
    // discussion #461 item 7)
    //
    // The daemon injects a predicate that returns false for a screen the user
    // disabled snapping on. resolveWindowRestore must then refuse the restore —
    // and it must be that GATE that refuses, which the log capture asserts via
    // the distinctive debug line. With no predicate the engine behaves as if
    // every context is active (the historical default the rest of this suite
    // relies on).
    // =========================================================================

    void testResolveWindowRestore_disabledContextGate_rejectsDisabledScreen()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        // Predicate marks DP-OFF as a disabled context, every other screen active.
        engine.setShouldRestorePredicate([](const QString& screenId) {
            return screenId != QStringLiteral("DP-OFF");
        });

        PhosphorEngine::SnapResult result;
        const QStringList lines =
            captureResolveLogs(engine, QStringLiteral("app|uuid-gate-off"), QStringLiteral("DP-OFF"), &result);

        QVERIFY2(!result.shouldSnap, "a restore onto a disabled context must be refused");
        QVERIFY2(lines.join(QLatin1Char('\n')).contains(QStringLiteral("disabled-context gate rejected restore")),
                 "the disabled-context gate must be the branch that refused the restore");
        m_wts->setSnapState(nullptr);
    }

    void testResolveWindowRestore_disabledContextGate_allowsEnabledScreen()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        engine.setShouldRestorePredicate([](const QString& screenId) {
            return screenId != QStringLiteral("DP-OFF");
        });

        // DP-1 is active — the gate must not be the branch that fires. The
        // restore still resolves to noSnap in this guiless fixture (no app
        // rules / pending session entries / ScreenManager); this asserts only
        // that the GATE did not reject it.
        PhosphorEngine::SnapResult result;
        const QStringList lines =
            captureResolveLogs(engine, QStringLiteral("app|uuid-gate-on"), QStringLiteral("DP-1"), &result);

        QVERIFY2(!result.shouldSnap,
                 "guiless fixture has no layout/app-rule/session entry — restore resolves to noSnap");
        // Match the exact top-level gate line, not the broader "disabled-context
        // gate" substring (the appRule/session re-checks log a different
        // phrasing) — so the assertion fails only if the caller-screen gate
        // actually rejected an enabled context.
        QVERIFY2(!lines.join(QLatin1Char('\n')).contains(QStringLiteral("disabled-context gate rejected restore")),
                 "an enabled context must pass the disabled-context gate");
        m_wts->setSnapState(nullptr);
    }

    void testResolveWindowRestore_noPredicate_gateInactive()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        // No predicate injected — the engine must behave as if every context
        // is active (the historical default the rest of the suite relies on).
        PhosphorEngine::SnapResult result;
        const QStringList lines =
            captureResolveLogs(engine, QStringLiteral("app|uuid-no-pred"), QStringLiteral("DP-1"), &result);

        QVERIFY2(!result.shouldSnap,
                 "guiless fixture has no layout/app-rule/session entry — restore resolves to noSnap");
        QVERIFY2(!lines.join(QLatin1Char('\n')).contains(QStringLiteral("disabled-context gate rejected restore")),
                 "with no predicate the disabled-context gate must never fire");
        m_wts->setSnapState(nullptr);
    }

    // =========================================================================
    // resolveWindowRestore — cross-screen ownership gate (multi-monitor login)
    //
    // A window snapped on a SNAP monitor can be reopened by KWin's session
    // restore on a DIFFERENT monitor that happens to be in autotile mode. The
    // opening-screen ownership gate must NOT blindly defer such a window to
    // autotile: its snapped record's RECORDED screen is in snapping mode, so the
    // restore migrates cross-screen back to that monitor (mirrors main, which
    // gated the defer on the saved screen). Conversely, a snapped record whose
    // OWN recorded screen is now autotile-owned must still defer (and must not be
    // consumed), leaving the record for the autotile engine.
    // =========================================================================

    void testResolveWindowRestore_crossScreenSnap_doesNotDeferOnAutotileOpeningScreen()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        // DP-2 is an autotile-mode screen; DP-1 stays snapping (the default).
        PhosphorZones::AssignmentEntry autotile;
        autotile.mode = PhosphorZones::AssignmentEntry::Autotile;
        autotile.tilingAlgorithm = QStringLiteral("dwindle");
        m_layoutManager->setAssignmentEntryDirect(QStringLiteral("DP-2"), 0, QString(), autotile);
        QCOMPARE(m_layoutManager->modeForScreen(QStringLiteral("DP-2"), 0, QString()),
                 PhosphorZones::AssignmentEntry::Autotile);

        // A window snapped on the SNAP monitor DP-1 (its recorded screen).
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = QStringList{QStringLiteral("z1")};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        m_wts->placementStore().record(rec);

        // The session reopens the window (new uuid) on the AUTOTILE monitor DP-2.
        // The opening-screen ownership gate must NOT defer — the snapped record's
        // recorded screen (DP-1) is in snapping mode, so the restore migrates
        // cross-screen. (Geometry can't resolve in this guiless fixture, so we
        // assert via the absence of the defer log rather than result.shouldSnap.)
        PhosphorEngine::SnapResult result;
        const QStringList lines =
            captureResolveLogs(engine, QStringLiteral("app|new"), QStringLiteral("DP-2"), &result);

        QVERIFY2(!lines.join(QLatin1Char('\n')).contains(QStringLiteral("defers to the owning engine")),
                 "a pending cross-screen snap restore must NOT be deferred by the opening-screen ownership gate");
        m_wts->setSnapState(nullptr);
    }

    void testResolveWindowRestore_sameScreenAutotileRecord_defersAndPreservesRecord()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        // DP-2 is autotile mode AND the recorded screen of the snapped record
        // (the user switched DP-2 to autotile after snapping there last session).
        PhosphorZones::AssignmentEntry autotile;
        autotile.mode = PhosphorZones::AssignmentEntry::Autotile;
        autotile.tilingAlgorithm = QStringLiteral("dwindle");
        m_layoutManager->setAssignmentEntryDirect(QStringLiteral("DP-2"), 0, QString(), autotile);

        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-2"); // recorded on the now-autotile screen
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = QStringList{QStringLiteral("z1")};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        m_wts->placementStore().record(rec);

        // Reopen on DP-2. No cross-screen restore is pending (the recorded screen
        // is autotile), so the gate must defer AND must not consume the record —
        // autotile still needs it.
        PhosphorEngine::SnapResult result;
        const QStringList lines =
            captureResolveLogs(engine, QStringLiteral("app|new"), QStringLiteral("DP-2"), &result);

        QVERIFY2(!result.shouldSnap, "a window on an autotile screen with no cross-screen snap restore must not snap");
        QVERIFY2(lines.join(QLatin1Char('\n')).contains(QStringLiteral("defers to the owning engine")),
                 "the opening-screen ownership gate must defer to autotile");
        QVERIFY2(m_wts->placementStore().contains(QStringLiteral("app|orig"), QStringLiteral("app")),
                 "deferring must not consume the snapped record — autotile still needs it");
        m_wts->setSnapState(nullptr);
    }

    // =========================================================================
    // resolveWindowRestore — unsnapped-position restore gate
    // (RestorePositionPredicate)
    //
    // A FREE (never-snapped) or snap-FLOATED window persists its global
    // position keyed by its recorded screen. KWin's session restore can reopen
    // it on a DIFFERENT monitor at login. When the daemon's restore-position
    // predicate opts the window in, the record becomes eligible cross-screen and
    // the engine emits geometryRestoreRequested with the RECORDED screen's
    // geometry — which, being in global compositor coordinates, returns the
    // window to its original monitor. With the predicate unset/false the
    // historical behaviour stands: free positions are inert and a cross-screen
    // record is not consumed.
    // =========================================================================

    void testResolveWindowRestore_freeCrossScreen_restoresWhenPredicateOptsIn()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        engine.setRestorePositionPredicate([](const QString&) {
            return true;
        });

        // A genuinely-free window last seen on DP-1 at a recorded global rect.
        const QRect dp1Geo(120, 80, 800, 600);
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFree();
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        rec.freeGeometryByScreen.insert(QStringLiteral("DP-1"), dp1Geo);
        m_wts->placementStore().record(rec);

        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);

        // KWin reopens the window (new uuid) on DP-2.
        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("app|new"), QStringLiteral("DP-2"), /*sticky*/ false);

        QVERIFY2(!result.shouldSnap, "a free window is repositioned, never snapped into a zone");
        QCOMPARE(geoSpy.count(), 1);
        const QList<QVariant> args = geoSpy.takeFirst();
        QCOMPARE(args.at(1).toRect(), dp1Geo);
        QCOMPARE(args.at(2).toString(), QStringLiteral("DP-1"));
        m_wts->setSnapState(nullptr);
    }

    void testResolveWindowRestore_freeCrossScreen_inertWhenPredicateAbsent()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        // No restore-position predicate — historical behaviour.

        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFree();
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        rec.freeGeometryByScreen.insert(QStringLiteral("DP-1"), QRect(120, 80, 800, 600));
        m_wts->placementStore().record(rec);

        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);

        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("app|new"), QStringLiteral("DP-2"), /*sticky*/ false);

        QVERIFY(!result.shouldSnap);
        QCOMPARE(geoSpy.count(), 0);
        QVERIFY2(m_wts->placementStore().contains(QStringLiteral("app|orig"), QStringLiteral("app")),
                 "without opt-in, a cross-screen free record stays gated on the opening screen and is not consumed");
        m_wts->setSnapState(nullptr);
    }

    void testResolveWindowRestore_floatingCrossScreen_restoresToRecordedMonitor()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        engine.setRestorePositionPredicate([](const QString&) {
            return true;
        });

        // A snap-floated window: state floating, carrying its pre-float zone, with
        // a recorded free position on DP-1.
        const QRect dp1Geo(200, 150, 640, 480);
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFloating();
        slot.zoneIds = QStringList{QStringLiteral("z1")};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        rec.freeGeometryByScreen.insert(QStringLiteral("DP-1"), dp1Geo);
        m_wts->placementStore().record(rec);

        QSignalSpy floatSpy(&engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);
        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);

        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("app|new"), QStringLiteral("DP-2"), /*sticky*/ false);

        QVERIFY2(!result.shouldSnap, "a floated window restores its float position, not a snap");
        QCOMPARE(geoSpy.count(), 1);
        QCOMPARE(geoSpy.takeFirst().at(1).toRect(), dp1Geo);
        QCOMPARE(floatSpy.count(), 1);
        const QList<QVariant> floatArgs = floatSpy.takeFirst();
        QCOMPARE(floatArgs.at(1).toBool(), true);
        QCOMPARE(floatArgs.at(2).toString(), QStringLiteral("DP-1"));
        // A FIFO-reopened record (live id != recorded id) is RE-RECORDED under the
        // LIVE windowId so the window's float-back survives logout/login (KWin assigns
        // a new uuid at login). The stale recorded-uuid entry is rebound, not left as
        // a duplicate.
        QVERIFY2(m_wts->placementStore().contains(QStringLiteral("app|new")),
                 "a cross-screen floating restore re-records under the live window id");
        QVERIFY2(!m_wts->placementStore().contains(QStringLiteral("app|orig")),
                 "the stale recorded-uuid entry is rebound to the live id, not left behind");
        // The float-back geometry is preserved on the re-recorded placement, so a
        // later float after login restores the pre-logout position.
        const auto reRec = m_wts->placementStore().peek(QStringLiteral("app|new"), QStringLiteral("app"));
        QVERIFY(reRec.has_value());
        QCOMPARE(reRec->freeGeometryFor(QStringLiteral("DP-1")), dp1Geo);
        m_wts->setSnapState(nullptr);
    }

    // LEGACY `free` record restored as floating (the retired third state). A `free`
    // slot persisted by an older build is now treated as floating: the merged branch
    // marks it floating (windowFloatingChanged true) AND, with the predicate opted
    // in, re-applies its recorded position on its own screen.
    void testResolveWindowRestore_legacyFreeSameScreen_restoresAsFloatingWhenPredicateOptsIn()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        engine.setRestorePositionPredicate([](const QString&) {
            return true;
        });

        const QRect dp1Geo(60, 40, 1024, 768);
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFree(); // legacy token
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        rec.freeGeometryByScreen.insert(QStringLiteral("DP-1"), dp1Geo);
        m_wts->placementStore().record(rec);

        QSignalSpy floatSpy(&engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);
        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);

        // Reopens on its own recorded screen DP-1.
        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("app|new"), QStringLiteral("DP-1"), /*sticky*/ false);

        QVERIFY(!result.shouldSnap);
        // Legacy free now marks floating (the old free branch did NOT).
        QCOMPARE(floatSpy.count(), 1);
        QCOMPARE(floatSpy.takeFirst().at(1).toBool(), true);
        QCOMPARE(geoSpy.count(), 1);
        const QList<QVariant> args = geoSpy.takeFirst();
        QCOMPARE(args.at(1).toRect(), dp1Geo);
        QCOMPARE(args.at(2).toString(), QStringLiteral("DP-1"));
        m_wts->setSnapState(nullptr);
    }

    // The predicate's RETURN VALUE is honored (not merely its presence): a free
    // record reopening on its own screen with the predicate returning false is
    // still eligible (same-screen) and re-recorded under the live id, but its
    // position is NOT re-applied. This is the load-bearing re-record-but-gate-move
    // boundary the free branch documents.
    void testResolveWindowRestore_freeSameScreen_reRecordsButSkipsMoveWhenPredicateDenies()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        engine.setRestorePositionPredicate([](const QString&) {
            return false;
        });

        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFree();
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        rec.freeGeometryByScreen.insert(QStringLiteral("DP-1"), QRect(60, 40, 1024, 768));
        m_wts->placementStore().record(rec);

        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);

        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("app|new"), QStringLiteral("DP-1"), /*sticky*/ false);

        QVERIFY(!result.shouldSnap);
        // A predicate that denies restore must skip the geometry move.
        QCOMPARE(geoSpy.count(), 0);
        // The record is still rebound to the live id (float-back survives) even
        // though the move is skipped; the stale recorded-uuid entry is gone.
        QVERIFY2(m_wts->placementStore().contains(QStringLiteral("app|new")),
                 "a same-screen free record is re-recorded under the live id even when the move is skipped");
        QVERIFY2(!m_wts->placementStore().contains(QStringLiteral("app|orig")),
                 "the stale recorded-uuid entry is rebound, not left behind");
        m_wts->setSnapState(nullptr);
    }

    // Two-state model: the merged floated-restore branch ALWAYS marks the window
    // floating (windowFloatingChanged true), but the geometry MOVE is now gated on
    // the restore-position predicate for ALL floated windows (the repurposed
    // restoreUnsnappedWindowsOnLogin setting / RestorePosition rule). When the
    // predicate DENIES, the window comes back floating but stays where the
    // compositor placed it — the move is skipped.
    void testResolveWindowRestore_floatingSameScreen_marksFloatingButSkipsMoveWhenPredicateDenies()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        engine.setRestorePositionPredicate([](const QString&) {
            return false;
        });

        const QRect dp1Geo(200, 150, 640, 480);
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFloating();
        slot.zoneIds = QStringList{QStringLiteral("z1")};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        rec.freeGeometryByScreen.insert(QStringLiteral("DP-1"), dp1Geo);
        m_wts->placementStore().record(rec);

        QSignalSpy floatSpy(&engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);
        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);

        // Reopens on its own recorded screen DP-1, predicate denying.
        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("app|new"), QStringLiteral("DP-1"), /*sticky*/ false);

        QVERIFY(!result.shouldSnap);
        // Marked floating unconditionally...
        QCOMPARE(floatSpy.count(), 1);
        const QList<QVariant> floatArgs = floatSpy.takeFirst();
        QCOMPARE(floatArgs.at(1).toBool(), true);
        QCOMPARE(floatArgs.at(2).toString(), QStringLiteral("DP-1"));
        // ...but the geometry move is gated by the predicate (denied → skipped).
        QVERIFY2(geoSpy.count() == 0, "floated move is gated on the restore-position predicate; denied → no move");
        m_wts->setSnapState(nullptr);
    }

    // Opt-in cross-screen eligibility (re-record) and the geometry move are
    // SEPARATELY gated. A free record reopening cross-screen is eligible purely on
    // the predicate opt-in, but the move only fires when restoreScreen has a
    // recorded position. With geometry captured on a THIRD screen (not the
    // record's own restoreScreen), freeGeometryFor(restoreScreen) is invalid, so
    // the record is re-recorded (under the live id) without a move — the removed
    // anyFreeGeometry() cross-screen fallback must NOT resurrect some other screen's rect.
    void testResolveWindowRestore_freeCrossScreen_optInNoGeometryForRestoreScreen_reRecordsWithoutMove()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        engine.setRestorePositionPredicate([](const QString&) {
            return true;
        });

        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1"); // restoreScreen resolves to DP-1
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFree();
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        // Geometry recorded on DP-3, NOT on the record's own screen DP-1.
        rec.freeGeometryByScreen.insert(QStringLiteral("DP-3"), QRect(10, 10, 800, 600));
        m_wts->placementStore().record(rec);

        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);

        // KWin reopens on DP-2; opt-in makes the cross-screen record eligible.
        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("app|new"), QStringLiteral("DP-2"), /*sticky*/ false);

        QVERIFY(!result.shouldSnap);
        QVERIFY2(geoSpy.count() == 0,
                 "no recorded position for restoreScreen → move skipped (no anyFreeGeometry fallback)");
        QVERIFY2(m_wts->placementStore().contains(QStringLiteral("app|new")),
                 "opt-in re-records the cross-screen free record under the live id even when the move is skipped");
        QVERIFY2(!m_wts->placementStore().contains(QStringLiteral("app|orig")),
                 "the stale recorded-uuid entry is rebound, not left behind");
        m_wts->setSnapState(nullptr);
    }

    // =========================================================================
    // Two-state model: snapping has only `snapped` / `floated` — no `free`.
    // =========================================================================

    // capturePlacement of an unmanaged window (neither snapped nor floating in the
    // runtime SnapState) records a FLOATING slot — the retired `free` state. An
    // untracked window has no effective screen, so capturePlacement's snap-mode gate
    // is bypassed and it reaches the state if/else.
    void testCapturePlacement_unmanagedWindowRecordsFloating()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        const auto p = engine.capturePlacement(QStringLiteral("app|unmanaged"));
        QVERIFY(p.has_value());
        const PhosphorEngine::EngineSlot slot = p->slotFor(PhosphorEngine::WindowPlacement::snapEngineId());
        QCOMPARE(slot.state, QString(PhosphorEngine::WindowPlacement::stateFloating()));
        QVERIFY2(slot.state != PhosphorEngine::WindowPlacement::stateFree(),
                 "the retired `free` state must never be produced");
        m_wts->setSnapState(nullptr);
    }

    // A new window that matches no auto-snap rule on a snap-mode screen defaults to
    // FLOATED (not the retired `free`): resolveWindowRestore marks it floating
    // (windowFloatingChanged true) and returns noSnap. The guiless fixture wires no
    // mode providers, so the unconfigured DP-1 resolves to the default Snapping mode
    // (LayoutRegistry::resolveDefaultAssignmentEntry → default-constructed entry,
    // Mode::Snapping == 0). The window therefore deterministically reaches the
    // snap-mode no-match fallthrough; the distinctive log line pins that path.
    void testResolveWindowRestore_newWindowNoMatch_defaultsToFloating()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);

        QSignalSpy floatSpy(&engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);

        PhosphorEngine::SnapResult result;
        const QStringList lines =
            captureResolveLogs(engine, QStringLiteral("app|brand-new"), QStringLiteral("DP-1"), &result);
        const QString joined = lines.join(QLatin1Char('\n'));

        QVERIFY(!result.shouldSnap);
        QVERIFY2(joined.contains(QStringLiteral("defaulting to floated")),
                 "a no-match window on a snap-mode screen must default to floated");
        QCOMPARE(floatSpy.count(), 1);
        QCOMPARE(floatSpy.takeFirst().at(1).toBool(), true);
        m_wts->setSnapState(nullptr);
    }

    // The unfloatFallbackToZone setting GATES the fallback: with it off, a window
    // that has no pre-float zone gets no fallback target (resolveFallbackUnfloatGeometry
    // returns not-found), so unfloat keeps it floating. (The on-success geometry path
    // needs a wired ScreenManager for zoneGeometry — exercised in live verification,
    // not this guiless fixture; the empty-zone gate tests skip geometry for the same
    // reason.)
    void testResolveFallbackUnfloatGeometry_offReturnsNotFound()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);

        // A floating window with no pre-float zone, on a known screen.
        engine.snapState()->setFloatingOnScreen(QStringLiteral("app|nofloatzone"), QStringLiteral("DP-1"), 0);

        // Setting OFF (the StubSettings default) → no fallback, regardless of layout.
        m_settings->setSnapUnfloatFallbackToZone(false);
        QVERIFY2(
            !engine.resolveFallbackUnfloatGeometry(QStringLiteral("app|nofloatzone"), QStringLiteral("DP-1")).found,
            "unfloatFallbackToZone off → no fallback target");
        m_wts->setSnapState(nullptr);
    }

    // Companion to the OFF test: with the setting ON, the resolver runs the full
    // chain PAST the opt-in gate — effective-screen resolution, layout lookup, and
    // zone selection (last-used → first-empty → first zone) — and reaches the final
    // geometry gate. Under QTEST_GUILESS_MAIN there is no QScreen, so zoneGeometry()
    // returns an invalid QRect and the result is still not-found. This pins that (a)
    // the post-gate chain executes without crashing on a real layout, and (b) the
    // headless geometry limitation — not a broken gate — is what produces not-found
    // here (the on-success geometry path is covered by live verification).
    void testResolveFallbackUnfloatGeometry_onButHeadlessReturnsNotFound()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);

        engine.snapState()->setFloatingOnScreen(QStringLiteral("app|nofloatzone"), QStringLiteral("DP-1"), 0);

        m_settings->setSnapUnfloatFallbackToZone(true);
        QVERIFY2(
            !engine.resolveFallbackUnfloatGeometry(QStringLiteral("app|nofloatzone"), QStringLiteral("DP-1")).found,
            "unfloatFallbackToZone on, but headless zoneGeometry is invalid → still no fallback target");
        m_wts->setSnapState(nullptr);
    }

    // =========================================================================
    // setExcludeRuleSet + isAppIdExcluded wiring tests
    //
    // Daemon owns the filtered Exclude rule set and pushes its address into
    // SnapEngine via `setExcludeRuleSet`. SnapEngine lazily binds a
    // `RuleEvaluator` to that set on first `isAppIdExcluded` call and
    // re-binds it whenever the pointer changes. An in-place edit through
    // `WindowRuleSet::setRules` bumps the revision counter; the evaluator's
    // per-revision sort index + match cache invalidate automatically.
    //
    // These tests pin the contract the daemon's `refilterExcludeRules`
    // lambda + `Daemon::stop()` `setExcludeRuleSet(nullptr)` teardown
    // rely on:
    //   - nullptr borrow ⇒ isAppIdExcluded == false (early-init fast path)
    //   - empty set      ⇒ isAppIdExcluded == false (no-exclusions fast path)
    //   - matching rule  ⇒ isAppIdExcluded == true
    //   - pointer change ⇒ cached evaluator drops + rebinds against new set
    //   - in-place edit  ⇒ revision bump invalidates the eval cache
    // =========================================================================

    void testExcludeWiring_nullptrBorrowReturnsFalse()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        // No setExcludeRuleSet call — m_excludeRuleSet starts nullptr.
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("firefox")));
    }

    void testExcludeWiring_emptySetReturnsFalse()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        PhosphorWindowRule::WindowRuleSet emptySet;
        engine.setExcludeRuleSet(&emptySet);
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("firefox")));
    }

    void testExcludeWiring_matchingRuleReturnsTrue()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);

        PhosphorWindowRule::WindowRuleSet set;
        PhosphorWindowRule::WindowRule rule;
        rule.id = QUuid::createUuid();
        rule.name = QStringLiteral("exclude-firefox");
        rule.enabled = true;
        rule.match = PhosphorWindowRule::MatchExpression::makeLeaf(
            PhosphorWindowRule::Field::AppId, PhosphorWindowRule::Operator::AppIdMatches, QStringLiteral("firefox"));
        PhosphorWindowRule::RuleAction action;
        action.type = QString(PhosphorWindowRule::ActionType::Exclude);
        rule.actions.append(action);
        QVERIFY(set.addRule(rule));

        engine.setExcludeRuleSet(&set);
        QVERIFY(engine.isAppIdExcluded(QStringLiteral("firefox")));
        // Non-matching appId resolves to not-excluded against the same
        // bound set — the evaluator differentiates correctly.
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("konsole")));
    }

    void testExcludeWiring_pointerChangeRebindsEvaluator()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);

        PhosphorWindowRule::WindowRuleSet firefoxSet;
        PhosphorWindowRule::WindowRule firefoxRule;
        firefoxRule.id = QUuid::createUuid();
        firefoxRule.enabled = true;
        firefoxRule.match = PhosphorWindowRule::MatchExpression::makeLeaf(
            PhosphorWindowRule::Field::AppId, PhosphorWindowRule::Operator::AppIdMatches, QStringLiteral("firefox"));
        PhosphorWindowRule::RuleAction firefoxAction;
        firefoxAction.type = QString(PhosphorWindowRule::ActionType::Exclude);
        firefoxRule.actions.append(firefoxAction);
        QVERIFY(firefoxSet.addRule(firefoxRule));

        PhosphorWindowRule::WindowRuleSet konsoleSet;
        PhosphorWindowRule::WindowRule konsoleRule;
        konsoleRule.id = QUuid::createUuid();
        konsoleRule.enabled = true;
        konsoleRule.match = PhosphorWindowRule::MatchExpression::makeLeaf(
            PhosphorWindowRule::Field::AppId, PhosphorWindowRule::Operator::AppIdMatches, QStringLiteral("konsole"));
        PhosphorWindowRule::RuleAction konsoleAction;
        konsoleAction.type = QString(PhosphorWindowRule::ActionType::Exclude);
        konsoleRule.actions.append(konsoleAction);
        QVERIFY(konsoleSet.addRule(konsoleRule));

        // Wire the firefox set, prime the cached evaluator.
        engine.setExcludeRuleSet(&firefoxSet);
        QVERIFY(engine.isAppIdExcluded(QStringLiteral("firefox")));
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("konsole")));

        // Re-wire to the konsole set — the cached evaluator was bound to
        // firefoxSet by reference; without the pointer-change rebind in
        // setExcludeRuleSet, this would still resolve "firefox" as
        // excluded and "konsole" as not.
        engine.setExcludeRuleSet(&konsoleSet);
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("firefox")));
        QVERIFY(engine.isAppIdExcluded(QStringLiteral("konsole")));

        // Clear: nullptr borrow short-circuits to false again.
        engine.setExcludeRuleSet(nullptr);
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("firefox")));
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("konsole")));
    }

    // Honest scope of this test (renamed from the earlier
    // `…InvalidatesEvalCache` name): exercises that across in-place
    // `WindowRuleSet::setRules` edits at the SAME bound pointer, the
    // evaluator surfaces the post-edit rule set (not stale results
    // from the pre-edit rule set). `WindowRuleSet::setRules`
    // unconditionally bumps the revision counter, so the evaluator's
    // revision-equality guard
    // (`m_priorityOrderRevision == revision`) catches every transition
    // here independently of the also-present size guard
    // (`m_priorityOrderRulesSize == rules.size()`) — the size guard
    // only fires under a quint64 revision wraparound the production
    // path effectively never hits (~5.85 billion years of one-per-
    // second edits, see RuleEvaluator's own commentary). This test
    // doesn't pin the size guard in isolation; a fixture that did
    // would need test-only hooks to force a revision collision. The
    // grow-the-list (1 → 2) step is kept because it makes
    // false-negative regressions in the priority-order rebuild
    // visible (cached `[0]` walk would skip rules[1]), even though
    // the revision guard catches it first in the current code.
    void testExcludeWiring_inPlaceSetRulesRespectsRevisionBump()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);

        PhosphorWindowRule::WindowRuleSet set;
        // Wire BEFORE adding rules so the bound pointer doesn't change
        // when we mutate the set; this is the daemon's actual pattern
        // (setExcludeRuleSet wired once at init, edits happen via
        // setRules from the rulesChanged subscription).
        engine.setExcludeRuleSet(&set);

        const auto excludeRule = [](const QString& pattern) {
            PhosphorWindowRule::WindowRule r;
            r.id = QUuid::createUuid();
            r.enabled = true;
            r.match = PhosphorWindowRule::MatchExpression::makeLeaf(
                PhosphorWindowRule::Field::AppId, PhosphorWindowRule::Operator::AppIdMatches, pattern);
            PhosphorWindowRule::RuleAction a;
            a.type = QString(PhosphorWindowRule::ActionType::Exclude);
            r.actions.append(a);
            return r;
        };

        // Step 1: rule A exists (size 1). Querying primes the cached
        // evaluator against the bound set at its current revision; the
        // resolved priority-order index is `[0]`.
        set.setRules({excludeRule(QStringLiteral("appA"))});
        QVERIFY(engine.isAppIdExcluded(QStringLiteral("appA")));
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("appB")));

        // Step 2: swap rule A for rule B at the SAME size (1 → 1).
        // Verifies that the new rule's match is picked up — the
        // priorityOrder cache happens to remain `[0]` which still
        // indexes the only post-swap rule, so this step alone does
        // NOT discriminate revision-bump invalidation from a stale
        // cache (the broken walk visits rules[0] which IS the new
        // rule). It does verify that the engine doesn't latch onto
        // the OLD rule's pattern, which is the load-bearing user-
        // facing property.
        set.setRules({excludeRule(QStringLiteral("appB"))});
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("appA")));
        QVERIFY(engine.isAppIdExcluded(QStringLiteral("appB")));

        // Step 3: GROW the list (1 → 2). This is the step that
        // discriminates a working `priorityOrder()` rebuild from a
        // broken one. The cached permutation from Step 2 has size 1;
        // the new set has size 2. Without per-revision invalidation,
        // the cached `[0]` walk would only visit rules[0] (the
        // already-known "appB" rule) — rules[1] ("appC") would never
        // be evaluated and `isAppIdExcluded("appC")` would return
        // false (false-negative). The pass verdict requires BOTH
        // pre-existing AND newly-appended rules to resolve correctly.
        set.setRules({excludeRule(QStringLiteral("appB")), excludeRule(QStringLiteral("appC"))});
        QVERIFY(engine.isAppIdExcluded(QStringLiteral("appB")));
        QVERIFY(engine.isAppIdExcluded(QStringLiteral("appC")));
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("appA")));

        // Step 4: clear via empty setRules. This goes through the
        // `isEmpty()` fast path in `SnapEngine::isAppIdExcluded` (not
        // the evaluator), but verifies the bound pointer survives the
        // mutation cleanly.
        set.setRules({});
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("appA")));
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("appB")));
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("appC")));
    }
};

QTEST_GUILESS_MAIN(TestSnapEngine)
#include "test_snap_engine.moc"

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>
#include <QLoggingCategory>

#include <PhosphorSnapEngine/SnapEngine.h>
#include "core/windowtrackingservice.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorSnapEngine/SnapState.h>
#include "config/configbackends.h"
#include "core/interfaces.h"

using namespace PlasmaZones;
using namespace PhosphorSnapEngine;

// =========================================================================
// Minimal stubs for WTS constructor assertions
// =========================================================================

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "core/virtualdesktopmanager.h"
#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/StubSettings.h"
#include "../helpers/StubZoneDetector.h"

using StubSettingsSnap = StubSettings;

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
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettingsSnap* m_settings = nullptr;
    StubZoneDetectorSnap* m_zoneDetector = nullptr;
    WindowTrackingService* m_wts = nullptr;
    PhosphorSnapEngine::SnapState* m_snapState = nullptr;

private Q_SLOTS:

    void init()
    {
        m_layoutManager = new PhosphorZones::LayoutRegistry(PlasmaZones::createAssignmentsBackend(),
                                                            QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettingsSnap(nullptr);
        m_zoneDetector = new StubZoneDetectorSnap(nullptr);
        m_wts = new WindowTrackingService(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, nullptr);
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
    // WindowTrackingService::clearFloatingForSnap tests
    //
    // (The former SnapEngine::clearFloatingStateForSnap wrapper was removed —
    // all snap-commit paths now go through WindowTrackingService::commitSnap
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

    void testEngineBaseUnmanagedGeometry()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        m_wts->setSnapState(engine.snapState());
        const QString windowId = QStringLiteral("app|uuid-pretile-sync");

        engine.storeUnmanagedGeometry(windowId, QRect(10, 20, 300, 200), QStringLiteral("DP-1"));
        QVERIFY(engine.hasUnmanagedGeometry(windowId));

        engine.forgetWindow(windowId);
        QVERIFY(!engine.hasUnmanagedGeometry(windowId));
        m_wts->setSnapState(nullptr);
    }

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

        QSignalSpy zoneSpy(m_wts, &WindowTrackingService::windowZoneChanged);

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

        PhosphorEngineApi::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = screen;
        ctx.fromEngineId = QStringLiteral("autotile");
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

        PhosphorEngineApi::IPlacementEngine::HandoffContext ctx;
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
};

QTEST_GUILESS_MAIN(TestSnapEngine)
#include "test_snap_engine.moc"

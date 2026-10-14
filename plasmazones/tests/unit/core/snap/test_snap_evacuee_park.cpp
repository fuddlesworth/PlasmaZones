// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_snap_evacuee_park.cpp
 * @brief The snap engine's evacuee park: what parkOutput keeps of the windows
 *        on an output that disconnects, what the removed-screen prune still
 *        announces, and how readoptParked re-seats an untouched window when
 *        the output returns.
 */

#include <QSet>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>
#include <memory>

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"
#include <PhosphorEngine/DesktopRemap.h>
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>

using namespace PlasmaZones;
using PhosphorSnapEngine::SnapEngine;
using PhosphorSnapEngine::SnapState;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {
const QString kScreen = QStringLiteral("DP-1");
const QString kOther = QStringLiteral("DP-2");
const QString kActA = QStringLiteral("act-a");
const QString kActB = QStringLiteral("act-b");
} // namespace

class TestSnapEvacueePark : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new StubZoneDetector(nullptr);
        m_service = new PhosphorPlacement::WindowTrackingService(m_layoutManager, nullptr, nullptr);
        m_engine = new SnapEngine(m_layoutManager, m_service, m_zoneDetector, nullptr, nullptr);
        m_engine->setEngineSettings(m_settings);
        m_service->setSnapEngine(m_engine);
        m_service->setSnapStateResolver(PhosphorPlacement::snapStateResolverFor(m_engine));

        m_testLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(m_testLayout);
        m_layoutManager->setActiveLayout(m_testLayout);
        m_zoneIds.clear();
        for (PhosphorZones::Zone* z : m_testLayout->zones()) {
            m_zoneIds.append(z->id().toString());
        }
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
    }

    void cleanup()
    {
        m_service->setSnapState(nullptr);
        m_service->setSnapEngine(nullptr);
        delete m_engine;
        m_engine = nullptr;
        delete m_service;
        m_service = nullptr;
        delete m_zoneDetector;
        m_zoneDetector = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;
        m_testLayout = nullptr;
        m_zoneIds.clear();
        m_guard.reset();
    }

    // The basic return: a window snapped on the output that went away comes
    // back into its zone when KWin returns it untouched, as a re-statement:
    // it does not become the screen's last-used zone the way a user snap does,
    // and its apply is a re-statement, so a maximize it kept stays (F509).
    void parkedZoneComesBackWithoutTouchingLastUsed()
    {
        const QString w = QStringLiteral("app|parked-zone");
        snapOn(1, w, m_zoneIds[0]);

        QCOMPARE(m_engine->parkOutput(kScreen), QStringList{w});
        QVERIFY(m_engine->hasParked(w, kScreen));
        m_engine->pruneStatesForRemovedScreen(kScreen);
        QVERIFY(!m_engine->isWindowTracked(w));
        QVERIFY(m_engine->hasParked(w, kScreen));

        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QSignalSpy restatement(m_engine, &SnapEngine::restatementGeometryRequested);
        QSignalSpy userVerb(m_engine, &SnapEngine::applyGeometryRequested);
        QVERIFY(m_engine->readoptParked(w, kScreen, kScreen));
        QCOMPARE(restatement.count(), 1);
        QCOMPARE(userVerb.count(), 0);
        QCOMPARE(m_service->zoneForWindow(w), m_zoneIds[0]);
        QVERIFY(storeInView()->lastUsedZoneId().isEmpty());
        QVERIFY(!m_engine->hasParked(w, kScreen));
        // Taken once.
        QVERIFY(!m_engine->readoptParked(w, kScreen, kScreen));
    }

    // A parked float is not announced as ending when its store is pruned:
    // the daemon adopts it floating where KWin put it. An unparked float on
    // the same output still is.
    void pruneDoesNotEndAParkedFloat()
    {
        const QString parked = QStringLiteral("app|parked-float");
        floatOn(parked);
        QVERIFY(m_engine->parkOutput(kScreen).contains(parked));
        QSignalSpy floatSpy(m_engine, &SnapEngine::windowFloatingChanged);
        m_engine->pruneStatesForRemovedScreen(kScreen);
        for (const QList<QVariant>& args : std::as_const(floatSpy)) {
            QVERIFY2(args.at(0).toString() != parked || args.at(1).toBool(), "a parked float must not end");
        }

        // Without a park the prune announces the float ending.
        const QString unparked = QStringLiteral("app|unparked-float");
        floatOn(unparked);
        floatSpy.clear();
        m_engine->pruneStatesForRemovedScreen(kScreen);
        bool ended = false;
        for (const QList<QVariant>& args : std::as_const(floatSpy)) {
            ended = ended || (args.at(0).toString() == unparked && !args.at(1).toBool());
        }
        QVERIFY(ended);

        // The parked float comes back floating.
        QVERIFY(m_engine->readoptParked(parked, kScreen, kScreen));
        QVERIFY(m_engine->isFloating(parked));
    }

    // A window floating in view on the output that went away is still snap's
    // floating window once the prune drops its store: screenless, its home
    // there gone, until the daemon's evacuee adoption (the WindowTracking
    // adaptor's floatEvacuee) floats it on the output KWin moved it to. A
    // float the park did not see still ends with its store (F639).
    void aParkedFloatStaysFloatingThroughThePrune()
    {
        const QString parked = QStringLiteral("app|kept-float");
        floatOn(parked);
        QVERIFY(m_engine->parkOutput(kScreen).contains(parked));
        const QString unparked = QStringLiteral("app|late-float");
        floatOn(unparked);
        m_engine->pruneStatesForRemovedScreen(kScreen);

        QVERIFY(m_engine->isFloating(parked));
        QVERIFY(m_engine->isWindowTracked(parked));
        QVERIFY(m_engine->screenForTrackedWindow(parked).isEmpty());
        QVERIFY(!m_engine->isFloating(unparked));

        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = parked;
        ctx.toScreenId = kOther;
        ctx.wasFloating = true;
        m_engine->handoffReceive(ctx);
        QVERIFY(m_engine->isFloating(parked));
        QCOMPARE(m_engine->screenForTrackedWindow(parked), kOther);
        QVERIFY2(!m_engine->globalState()->isFloating(parked), "the adoption takes the float out of the global holder");
    }

    // A parked context names a desktop by number, so it follows its desktop
    // through a change of KWin's desktop list: two desktops swapped while the
    // output was away, the zone comes back on the desktop it was on (F763).
    void aParkedContextFollowsItsDesktop()
    {
        const QString w = QStringLiteral("app|parked-swap");
        snapOn(2, w, m_zoneIds[1]);
        QCOMPARE(m_engine->parkOutput(kScreen), QStringList{w});
        m_engine->pruneStatesForRemovedScreen(kScreen);

        m_engine->remapDesktops(PhosphorEngine::DesktopRemap::fromIds({QStringLiteral("a"), QStringLiteral("b")},
                                                                      {QStringLiteral("b"), QStringLiteral("a")}));

        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        QVERIFY(m_engine->readoptParked(w, kScreen, kScreen));
        QCOMPARE(zonesOn(1, w), QStringList{m_zoneIds[1]});
        QVERIFY(zonesOn(2, w).isEmpty());
    }

    // A context out of view gets its zone straight back into that desktop's
    // store; the one in view is committed.
    void hiddenDesktopGetsItsAssignmentBack()
    {
        const QString w = QStringLiteral("app|two-desktops");
        snapOn(1, w, m_zoneIds[0]);
        snapOn(2, w, m_zoneIds[1]);
        QCOMPARE(m_engine->parkOutput(kScreen), QStringList{w});
        m_engine->pruneStatesForRemovedScreen(kScreen);

        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QVERIFY(m_engine->readoptParked(w, kScreen, kScreen));
        QCOMPARE(zonesOn(1, w), QStringList{m_zoneIds[0]});
        QCOMPARE(zonesOn(2, w), QStringList{m_zoneIds[1]});
    }

    // A desktop given to a tiling mode while the output was away gets nothing
    // back: the zone belongs to a mode that no longer runs there.
    void aContextThatLeftSnappingIsNotReseated()
    {
        const QString w = QStringLiteral("app|mode-changed");
        snapOn(1, w, m_zoneIds[0]);
        QCOMPARE(m_engine->parkOutput(kScreen), QStringList{w});
        m_engine->pruneStatesForRemovedScreen(kScreen);
        m_layoutManager->assignLayoutById(kScreen, 1, QString(), QStringLiteral("autotile:bsp"));

        QVERIFY(!m_engine->readoptParked(w, kScreen, kScreen));
        QVERIFY(m_service->zoneForWindow(w).isEmpty());
        QVERIFY(!m_engine->hasParked(w, kScreen));
    }

    // A desktop the window left while parked drops that desktop's context
    // only; the rest stays parked and comes back.
    void dropByDesktopKeepsTheOtherContexts()
    {
        const QString w = QStringLiteral("app|desktop-drop");
        snapOn(1, w, m_zoneIds[0]);
        snapOn(2, w, m_zoneIds[1]);
        QCOMPARE(m_engine->parkOutput(kScreen), QStringList{w});
        m_engine->pruneStatesForRemovedScreen(kScreen);

        m_engine->dropParked(w, QString(), 2, QString());
        QVERIFY(m_engine->hasParked(w, kScreen));
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QVERIFY(m_engine->readoptParked(w, kScreen, kScreen));
        QCOMPARE(zonesOn(1, w), QStringList{m_zoneIds[0]});
        QVERIFY(zonesOn(2, w).isEmpty());

        // Dropping everything empties the park.
        snapOn(1, w, m_zoneIds[0]);
        QCOMPARE(m_engine->parkOutput(kScreen), QStringList{w});
        m_engine->dropParked(w, QString(), 0, QString());
        QVERIFY(!m_engine->hasParked(w, QString()));
    }

    // A context parked under an activity not in view goes back into that
    // activity's store, never the one showing, where the window would count
    // as an occupant of a zone it is not in (F1016).
    void aContextOfAnotherActivityGoesBackThere()
    {
        const QString w = QStringLiteral("app|other-activity");
        showActivity(kActB);
        snapOn(1, w, m_zoneIds[0]);
        QCOMPARE(m_engine->parkOutput(kScreen), QStringList{w});
        m_engine->pruneStatesForRemovedScreen(kScreen);

        showActivity(kActA);
        QVERIFY(m_engine->readoptParked(w, kScreen, kScreen));
        QVERIFY2(zonesOn(1, w).isEmpty(), "nothing lands in the activity in view");
        showActivity(kActB);
        QCOMPARE(zonesOn(1, w), QStringList{m_zoneIds[0]});
    }

    // The daemon captures a re-seated window's record straight after the
    // re-seat: a window held only on an activity not in view is recorded on
    // that activity, not the one showing (F1016). Mutation: stamp
    // currentActivity() in capturePlacementAtDesktop and the record says act-a.
    void aCaptureKeepsTheActivityOfAContextNotInView()
    {
        const QString w = QStringLiteral("app|capture-activity");
        showActivity(kActB);
        snapOn(1, w, m_zoneIds[0]);
        QCOMPARE(m_engine->parkOutput(kScreen), QStringList{w});
        m_engine->pruneStatesForRemovedScreen(kScreen);
        showActivity(kActA);
        QVERIFY(m_engine->readoptParked(w, kScreen, kScreen));

        const auto rec = m_engine->capturePlacement(w);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->activity, kActB);

        // A window held in the activity in view still records that one.
        const QString shown = QStringLiteral("app|capture-shown");
        snapOn(1, shown, m_zoneIds[1]);
        const auto shownRec = m_engine->capturePlacement(shown);
        QVERIFY(shownRec.has_value());
        QCOMPARE(shownRec->activity, kActA);
    }

    // Snapping switched off while the output was away gives nothing back
    // (F1017).
    void nothingComesBackWithSnappingSwitchedOff()
    {
        const QString w = QStringLiteral("app|snapping-off");
        snapOn(1, w, m_zoneIds[0]);
        QCOMPARE(m_engine->parkOutput(kScreen), QStringList{w});
        m_engine->pruneStatesForRemovedScreen(kScreen);
        m_settings->setSnappingEnabled(false);

        QVERIFY(!m_engine->readoptParked(w, kScreen, kScreen));
        QVERIFY(m_service->zoneForWindow(w).isEmpty());
        m_settings->setSnappingEnabled(true);
    }

    // A desktop removed while the output was away takes its parked context
    // with it, also through the prune of a shrunk desktop count, which runs
    // no renumber; the other desktop's context still comes back (F1020).
    void aRemovedDesktopTakesItsParkedContext()
    {
        const QString w = QStringLiteral("app|pruned-desktop");
        snapOn(1, w, m_zoneIds[0]);
        snapOn(2, w, m_zoneIds[1]);
        QCOMPARE(m_engine->parkOutput(kScreen), QStringList{w});
        m_engine->pruneStatesForRemovedScreen(kScreen);

        m_engine->pruneStatesForDesktop(2);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QVERIFY(m_engine->readoptParked(w, kScreen, kScreen));
        QCOMPARE(zonesOn(1, w), QStringList{m_zoneIds[0]});
        QVERIFY(zonesOn(2, w).isEmpty());
    }

    // So does a removed activity (F1024).
    void aRemovedActivityTakesItsParkedContext()
    {
        const QString w = QStringLiteral("app|pruned-activity");
        showActivity(kActB);
        snapOn(1, w, m_zoneIds[0]);
        QCOMPARE(m_engine->parkOutput(kScreen), QStringList{w});
        m_engine->pruneStatesForRemovedScreen(kScreen);

        m_engine->pruneStatesForActivities({kActA, kActB});
        QVERIFY(m_engine->hasParked(w, kScreen));
        m_engine->pruneStatesForActivities({kActA});
        QVERIFY(!m_engine->hasParked(w, kScreen));
    }

private:
    void showActivity(const QString& activity)
    {
        m_layoutManager->setCurrentActivity(activity);
        m_engine->setCurrentActivity(activity);
    }

    void snapOn(int desktop, const QString& windowId, const QString& zoneId)
    {
        m_engine->setCurrentDesktopForScreen(kScreen, desktop);
        m_service->assignWindowToZone(windowId, zoneId, kScreen, desktop);
    }

    void floatOn(const QString& windowId)
    {
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_engine->stateForWindowOnScreen(windowId, kScreen, 1)->setFloatingOnScreen(windowId, kScreen, 1);
        QVERIFY(m_engine->isFloating(windowId));
    }

    SnapState* storeInView()
    {
        return static_cast<SnapState*>(m_engine->stateForScreen(kScreen));
    }

    QStringList zonesOn(int desktop, const QString& windowId)
    {
        m_engine->setCurrentDesktopForScreen(kScreen, desktop);
        SnapState* state = storeInView();
        return state ? state->zonesForWindow(windowId) : QStringList{};
    }

    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    SnapEngine* m_engine = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestSnapEvacueePark)
#include "test_snap_evacuee_park.moc"

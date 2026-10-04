// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_windowdrag_cross_screen.cpp
 * @brief A drag that ends on another monitor than the one snap holds the
 *        window on. The effect holds every crossing back for the whole drag,
 *        so the drag adaptor's end is the only place the move is resolved:
 *        the pending (no trigger) and dead arms through
 *        WindowTrackingAdaptor::dragEndedOnScreen, an activated drop through
 *        its cross-screen block. Two snapping outputs side by side.
 */

#include <QSignalSpy>
#include <QTest>

#include "FakePhysicalScreenSource.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>

#include "config/configdefaults.h"
#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubOverlayService.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using PlasmaZones::StubSettings;
using PlasmaZones::StubZoneDetector;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
using PlasmaZones::TestHelpers::StubOverlayService;

class TestWindowDragCrossScreen : public QObject
{
    Q_OBJECT

private:
    /// DP-1 at the origin and DP-2 (the "other" monitor) to its right, both
    /// snapping one three-zone layout; a snap engine with per-screen stores;
    /// Ctrl as the activation trigger.
    struct Fixture
    {
        Fixture()
        {
            fake.addScreen(QStringLiteral("DP-1"), QRect(otherGeometry.x() - 1920, otherGeometry.y(), 1920, 1080),
                           QStringLiteral("DP-1"));
            fake.addScreen(other, otherGeometry, other);
            screenMgr = std::make_unique<PhosphorScreens::ScreenManager>(
                PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
            screenMgr->start();
            layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
            settings.setSnappingEnabled(true);
            settings.setToggleActivation(false);
            settings.setDragActivationTriggers(
                {QVariantMap{{ConfigDefaults::triggerModifierField(), static_cast<int>(DragModifier::Ctrl)},
                             {ConfigDefaults::triggerMouseButtonField(), 0}}});
            settings.setZoneSpanEnabled(false);

            wta = new WindowTrackingAdaptor(layoutManager, &detector, screenMgr.get(), &settings, nullptr, nullptr,
                                            &parent);
            snap = std::make_unique<PhosphorSnapEngine::SnapEngine>(layoutManager, wta->service(), &detector, nullptr,
                                                                    nullptr);
            snap->setEngineSettings(&settings);
            wta->service()->setSnapState(snap->snapState());
            wta->service()->setSnapEngine(snap.get());
            wta->service()->setSnapStateResolver(PhosphorPlacement::snapStateResolverFor(snap.get()));
            wta->setEngines(snap.get(), nullptr, nullptr);
            adaptor =
                new WindowDragAdaptor(&overlay, &detector, layoutManager, screenMgr.get(), &settings, wta, &parent);

            layout = createTestLayout(3, layoutManager);
            layoutManager->addLayout(layout);
            layoutManager->setActiveLayout(layout);
            snap->setCurrentDesktopForScreen(QStringLiteral("DP-1"), 1);
            snap->setCurrentDesktopForScreen(other, 1);
        }
        ~Fixture()
        {
            wta->setEngines(nullptr, nullptr, nullptr);
            wta->service()->setSnapState(nullptr);
            wta->service()->setSnapEngine(nullptr);
            snap.reset();
            delete layoutManager;
        }

        QString zone(int index) const
        {
            return layout->zones().at(index)->id().toString();
        }
        /// @p w snapped into zone 0 on @p screen at a frame there.
        void snapOn(const QString& w, const QString& screen)
        {
            wta->service()->assignWindowToZone(w, zone(0), screen, 1);
            const QRect area = screen == other ? otherGeometry : QRect(otherGeometry.x() - 1920, 0, 1920, 1080);
            wta->setFrameGeometry(w, area.x() + 50, area.y() + 50, 400, 300);
        }
        QPoint inOther() const
        {
            return otherGeometry.center();
        }
        bool snapHoldsOn(const QString& w, const QString& screen) const
        {
            for (PhosphorSnapEngine::SnapState* state : snap->allSnapStates()) {
                if (state && state->screenId() == screen && snap->holdsWindowInState(w, state)) {
                    return true;
                }
            }
            return false;
        }

        IsolatedConfigGuard guard;
        QString other = QStringLiteral("DP-2");
        QRect otherGeometry = QRect(1920, 0, 1920, 1080);
        PhosphorScreens::FakePhysicalScreenSource fake;
        std::unique_ptr<PhosphorScreens::ScreenManager> screenMgr;
        QObject parent;
        StubOverlayService overlay;
        StubZoneDetector detector;
        StubSettings settings;
        PhosphorZones::LayoutRegistry* layoutManager = nullptr;
        PhosphorZones::Layout* layout = nullptr;
        WindowTrackingAdaptor* wta = nullptr; // parent-owned
        std::unique_ptr<PhosphorSnapEngine::SnapEngine> snap;
        WindowDragAdaptor* adaptor = nullptr; // parent-owned
    };

private Q_SLOTS:
    // A snapped window dragged to the other monitor without the trigger ends
    // floating there, with nothing left on the monitor it came from (F677).
    void pendingDrag_snappedWindowFloatsOnTheOtherMonitor()
    {
        Fixture f;
        const QString w = QStringLiteral("app|pending-snapped");
        f.snapOn(w, QStringLiteral("DP-1"));
        f.adaptor->beginDrag(w, 100, 100, 400, 300, QStringLiteral("DP-1"), 0);

        const PhosphorProtocol::DragOutcome outcome =
            f.adaptor->endDrag(w, f.inOther().x(), f.inOther().y(), 0, 0, false);

        QCOMPARE(outcome.action, PhosphorProtocol::DragOutcome::NoOp);
        QVERIFY(f.snap->isFloating(w));
        QCOMPARE(f.snap->screenForTrackedWindow(w), f.other);
        QVERIFY2(!f.snapHoldsOn(w, QStringLiteral("DP-1")), "nothing may stay on the monitor the window left");
        QVERIFY(f.wta->service()->preFloatZones(w).isEmpty());
    }

    // A snap-floating window dragged across keeps floating, re-homed there.
    void pendingDrag_floatingWindowIsRehomed()
    {
        Fixture f;
        const QString w = QStringLiteral("app|pending-floating");
        f.snapOn(w, QStringLiteral("DP-1"));
        f.snap->setWindowFloat(w, true, QStringLiteral("DP-1"));
        f.adaptor->beginDrag(w, 100, 100, 400, 300, QStringLiteral("DP-1"), 0);

        f.adaptor->endDrag(w, f.inOther().x(), f.inOther().y(), 0, 0, false);

        QVERIFY(f.snap->isFloating(w));
        QCOMPARE(f.snap->screenForTrackedWindow(w), f.other);
        QVERIFY(f.wta->service()->preFloatZones(w).isEmpty());
    }

    // A dead drag (snapping off) still moved the window: it leaves its zone
    // on the monitor it came from, and the effect is told it is unsnapped
    // (F360).
    void deadDrag_snappedWindowLeavesItsZone()
    {
        Fixture f;
        const QString w = QStringLiteral("app|dead-snapped");
        f.snapOn(w, QStringLiteral("DP-1"));
        f.settings.setSnappingEnabled(false);
        f.adaptor->beginDrag(w, 100, 100, 400, 300, QStringLiteral("DP-1"), 0);

        const PhosphorProtocol::DragOutcome outcome =
            f.adaptor->endDrag(w, f.inOther().x(), f.inOther().y(), 0, 0, false);

        QCOMPARE(outcome.action, PhosphorProtocol::DragOutcome::NotifyDragOutUnsnap);
        QVERIFY(f.wta->service()->zoneForWindow(w).isEmpty());
        QVERIFY(!f.snapHoldsOn(w, QStringLiteral("DP-1")));
    }

    // An activated drag dropping a floating window on the other monitor
    // outside any zone keeps it floating there; the release used to leave it
    // untracked (F177).
    void activeDrop_floatingWindowKeepsFloating()
    {
        Fixture f;
        const QString w = QStringLiteral("app|active-floating");
        f.snapOn(w, QStringLiteral("DP-1"));
        f.snap->setWindowFloat(w, true, QStringLiteral("DP-1"));
        const int ctrl = static_cast<int>(Qt::ControlModifier);
        f.adaptor->beginDrag(w, 100, 100, 400, 300, QStringLiteral("DP-1"), 0);
        f.adaptor->updateDragCursor(w, f.inOther().x(), f.inOther().y(), ctrl, 0);

        f.adaptor->endDrag(w, f.inOther().x(), f.inOther().y(), ctrl, 0, false);

        QVERIFY(f.snap->isFloating(w));
        QCOMPARE(f.snap->screenForTrackedWindow(w), f.other);
    }

    // A drag-out on a monitor where the window has no float-back restores the
    // size it had free on the monitor it left; only the size travels (F269).
    void activeDragOut_restoresTheSizeRememberedOnAnotherMonitor()
    {
        Fixture f;
        f.settings.setRestoreOriginalSizeOnUnsnap(true);
        const QString w = QStringLiteral("app|drag-out-size");
        const QRect onFirst(200, 150, 640, 480);
        f.wta->service()->recordFreeGeometry(w, QStringLiteral("DP-1"), onFirst, true);
        f.snapOn(w, f.other);
        QVERIFY(!f.wta->service()->validatedUnmanagedGeometry(w, f.other).has_value());
        const int ctrl = static_cast<int>(Qt::ControlModifier);
        f.adaptor->beginDrag(w, f.otherGeometry.x() + 50, f.otherGeometry.y() + 50, 400, 300, f.other, 0);
        f.adaptor->updateDragCursor(w, f.inOther().x(), f.inOther().y(), ctrl, 0);

        const PhosphorProtocol::DragOutcome outcome =
            f.adaptor->endDrag(w, f.inOther().x(), f.inOther().y(), ctrl, 0, false);

        QCOMPARE(outcome.action, PhosphorProtocol::DragOutcome::RestoreSize);
        QCOMPARE(QSize(outcome.width, outcome.height), onFirst.size());
    }

    // A drag ending on the window's own monitor is the drag-out it always was.
    void pendingDrag_sameScreenIsTheUsualDragOut()
    {
        Fixture f;
        const QString w = QStringLiteral("app|same-screen");
        f.snapOn(w, f.other);
        f.adaptor->beginDrag(w, f.otherGeometry.x() + 50, f.otherGeometry.y() + 50, 400, 300, f.other, 0);

        f.adaptor->endDrag(w, f.inOther().x(), f.inOther().y(), 0, 0, false);

        QVERIFY(f.wta->service()->zoneForWindow(w).isEmpty());
        QVERIFY(f.wta->service()->isWindowFloating(w));
        QVERIFY(f.snapHoldsOn(w, f.other));
    }
};

QTEST_MAIN(TestWindowDragCrossScreen)
#include "test_windowdrag_cross_screen.moc"

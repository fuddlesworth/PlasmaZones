// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_windowdrag_cross_screen.cpp
 * @brief A drag that ends on another monitor than the one snap holds the
 *        window on. The effect holds every crossing back for the whole drag,
 *        so the drag adaptor's end is the only place the move is resolved:
 *        the pending (no trigger) and dead arms through
 *        WindowTrackingAdaptor::dragEndedOnScreen, an activated drop through
 *        its cross-screen block. Two snapping outputs side by side. The same
 *        fixture covers what a crossing changes mid-drag: Escape on a pending
 *        drag, the drag-out size preview, the drop-indicator overrides, the
 *        selector edge hover's minimum-size floor, and, with the second
 *        output turned into an autotile screen, the drag re-insert's
 *        activation (its always-on entry included), minimum-size refusal,
 *        Escape dismissal and indifference to a stale toggle setting. With
 *        the second output turned into a scrolling screen instead, it covers
 *        the same refusal for the strip and the client size an arriving
 *        window hands it.
 */

#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

#include "FakePhysicalScreenSource.h"
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorRules/ActionTypes.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleSet.h>
#include <PhosphorRules/RuleStore.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>

#include "config/configdefaults.h"
#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include "helpers/AutotileTestHelpers.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubOverlayService.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

/// A real scroll engine that also records every client size the drag adaptor
/// hands it, which is the size an arriving window's "the client decides"
/// column is given. Not in an anonymous namespace: the test class's Fixture
/// holds one, and a member of internal-linkage type trips -Wsubobject-linkage.
class RecordingScrollEngine : public PhosphorScrollEngine::ScrollEngine
{
public:
    using ScrollEngine::ScrollEngine;
    void setDragInsertClientSize(const QSize& size) override
    {
        clientSizes.append(size);
        ScrollEngine::setDragInsertClientSize(size);
    }
    QList<QSize> clientSizes;
};

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
            adaptor->setAutotileEngine(nullptr);
            adaptor->setScrollEngine(nullptr);
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
        QPoint inFirst() const
        {
            return QPoint(otherGeometry.x() - 960, otherGeometry.y() + 540);
        }
        /// Turns the other monitor into an autotile screen the drag adaptor
        /// knows, for the drag re-insert arms. No minimum window size, so a
        /// drag-start frame never refuses an adoption unless a test sets one.
        void tileTheOtherScreen()
        {
            settings.setMinimumWindowWidth(0);
            settings.setMinimumWindowHeight(0);
            tiling = std::make_unique<PhosphorTileEngine::AutotileEngine>(nullptr, nullptr, nullptr,
                                                                          PlasmaZones::TestHelpers::testRegistry());
            tiling->setAutotileScreens({other});
            adaptor->setAutotileEngine(tiling.get());
        }
        /// Turns the other monitor into a scrolling screen the drag adaptor
        /// knows, its work area read from the fixture's screen manager.
        void scrollTheOtherScreen()
        {
            settings.setMinimumWindowWidth(0);
            settings.setMinimumWindowHeight(0);
            scrolling = std::make_unique<RecordingScrollEngine>(nullptr, screenMgr.get());
            scrolling->setActiveScreens({other});
            adaptor->setScrollEngine(scrolling.get());
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
        std::unique_ptr<PhosphorTileEngine::AutotileEngine> tiling; // tileTheOtherScreen
        std::unique_ptr<RecordingScrollEngine> scrolling; // scrollTheOtherScreen
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

    // An activated drag KWin cancels over the other monitor never left the
    // first: the window keeps its zone there and nothing moves it (F1041).
    void activeDrag_cancelledOverTheOtherMonitorKeepsItsZone()
    {
        Fixture f;
        const QString w = QStringLiteral("app|active-cancelled");
        f.snapOn(w, QStringLiteral("DP-1"));
        const int ctrl = static_cast<int>(Qt::ControlModifier);
        f.adaptor->beginDrag(w, 100, 100, 400, 300, QStringLiteral("DP-1"), 0);
        f.adaptor->updateDragCursor(w, f.inOther().x(), f.inOther().y(), ctrl, 0);

        const PhosphorProtocol::DragOutcome outcome =
            f.adaptor->endDrag(w, f.inOther().x(), f.inOther().y(), ctrl, 0, true);

        QCOMPARE(outcome.action, PhosphorProtocol::DragOutcome::CancelSnap);
        QCOMPARE(f.wta->service()->zoneForWindow(w), f.zone(0));
        QVERIFY(f.snapHoldsOn(w, QStringLiteral("DP-1")));
        QVERIFY(!f.snap->isFloating(w));
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

    // Escape on a drag that never activated does not end the drag: the
    // compositor move goes on, and the drop on the other monitor is still the
    // drag-out it was. The window leaves its zone and the monitor it came
    // from. Clearing the pending drag on Escape sent its endDrag down the
    // mismatch branch, which left both.
    void pendingDrag_escapeThenDropOnTheOtherMonitorStillLeavesTheZone()
    {
        Fixture f;
        const QString w = QStringLiteral("app|pending-escaped");
        f.snapOn(w, QStringLiteral("DP-1"));
        f.adaptor->beginDrag(w, 100, 100, 400, 300, QStringLiteral("DP-1"), 0);
        f.adaptor->cancelSnap();

        f.adaptor->endDrag(w, f.inOther().x(), f.inOther().y(), 0, 0, false);

        QVERIFY(f.wta->service()->zoneForWindow(w).isEmpty());
        QVERIFY2(!f.snapHoldsOn(w, QStringLiteral("DP-1")), "nothing may stay on the monitor the window left");
    }

    // The drop-indicator colours a window's rules give it reach the overlay
    // for the drag. beginDrag's own reset clears the overrides, so the push
    // has to come after it: pushed ahead, the drag held an empty map from its
    // first tick on.
    void beginDrag_leavesTheWindowsDropIndicatorColoursOnTheOverlay()
    {
        Fixture f;
        auto* registry = new PhosphorEngine::WindowRegistry(&f.parent);
        f.wta->setWindowRegistry(registry);
        f.wta->setWindowMetadata(QStringLiteral("inst1"), QStringLiteral("dropapp"), QString(), QString(), QString(), 0,
                                 1, QString(), 0, QVariantMap());
        using namespace PhosphorRules;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("dropapp"));
        RuleAction colour;
        colour.type = QString(ActionType::DropIndicatorColor);
        colour.params.insert(QString(ActionParam::Value), QStringLiteral("#ff8800"));
        rule.actions = {colour};
        RuleStore store(ConfigDefaults::rulesFilePath());
        QVERIFY(store.addRule(rule));
        f.wta->setRuleStore(&store);
        const auto teardown = qScopeGuard([&f] {
            f.wta->setRuleStore(nullptr);
            f.wta->setWindowRegistry(nullptr);
        });
        const QString w = QStringLiteral("dropapp|inst1");
        QVERIFY2(!f.wta->dropIndicatorRuleParams(w).isEmpty(), "precondition: the rule gives the window a colour");

        f.adaptor->beginDrag(w, 100, 100, 400, 300, QStringLiteral("DP-1"), 0);

        QCOMPARE(f.overlay.dropIndicatorWindowOverrides(), f.wta->dropIndicatorRuleParams(w));

        // And they go with the drag. A pending drag ends without
        // resetDragState, so its own teardown has to take them back off.
        f.adaptor->endDrag(w, 100, 100, 0, 0, false);
        QVERIFY(f.overlay.dropIndicatorWindowOverrides().isEmpty());
    }

    // A drag-out size preview is read for the screen under the cursor. A
    // crossing from one snapping monitor to another re-reads it there, the
    // size the drop commits on the new monitor. The latch that keeps one
    // preview per stay was re-armed only for a crossing onto a tiling screen,
    // so the new monitor's size was never previewed.
    void activeDrag_crossingToAnotherSnappingMonitorPreviewsItsSize()
    {
        Fixture f;
        f.settings.setRestoreOriginalSizeOnUnsnap(true);
        const QString w = QStringLiteral("app|preview-crossing");
        const QRect onOther(f.otherGeometry.x() + 280, 150, 800, 600);
        f.wta->service()->recordFreeGeometry(w, QStringLiteral("DP-1"), QRect(200, 150, 640, 480), true);
        f.wta->service()->recordFreeGeometry(w, f.other, onOther, true);
        f.snapOn(w, QStringLiteral("DP-1"));
        const int ctrl = static_cast<int>(Qt::ControlModifier);
        f.adaptor->beginDrag(w, 50, 50, 400, 300, QStringLiteral("DP-1"), 0);
        QSignalSpy previews(f.adaptor, &WindowDragAdaptor::restoreSizeDuringDragChanged);

        f.adaptor->updateDragCursor(w, f.inFirst().x(), f.inFirst().y(), ctrl, 0);
        f.adaptor->updateDragCursor(w, f.inFirst().x() + 10, f.inFirst().y(), ctrl, 0);
        QCOMPARE(previews.count(), 1);
        f.adaptor->updateDragCursor(w, f.inOther().x(), f.inOther().y(), ctrl, 0);

        QCOMPARE(previews.count(), 2);
        QCOMPARE(QSize(previews.last().at(1).toInt(), previews.last().at(2).toInt()), onOther.size());
    }

    // A drag that has not activated snapping, crossing onto a tiling screen
    // with that screen's re-insert trigger held, activates through it: the
    // trigger lists can differ, and a pending drag never reaches the
    // re-insert block. The window then previews its place in the stack.
    void pendingDrag_insertTriggerOnATilingScreenActivatesTheReInsert()
    {
        Fixture f;
        f.tileTheOtherScreen();
        const QString w = QStringLiteral("app|pending-insert");
        f.snapOn(w, QStringLiteral("DP-1"));
        f.adaptor->beginDrag(w, 100, 100, 400, 300, QStringLiteral("DP-1"), 0);
        QVERIFY(!f.adaptor->isDragInFlight());

        const int alt = static_cast<int>(Qt::AltModifier);
        f.adaptor->updateDragCursor(w, f.inOther().x(), f.inOther().y(), alt, 0);

        QVERIFY2(f.adaptor->isDragInFlight(), "the re-insert trigger must activate the pending drag");
        QVERIFY(f.tiling->hasDragInsertPreview());
    }

    // Under "always re-insert" the always-on entry reads as held on every
    // tick, so a drag that never activated snapping promotes on a plain
    // crossing onto the tiling screen, with nothing held, and its drop
    // re-inserts the window there rather than floating it. That is what the
    // same setting does to a drag that starts on that screen.
    void pendingDrag_alwaysOnReInsertPromotesAPlainCrossing()
    {
        Fixture f;
        f.tileTheOtherScreen();
        f.settings.setAutotileDragInsertTriggers(
            {QVariantMap{{ConfigDefaults::triggerModifierField(), static_cast<int>(DragModifier::AlwaysActive)},
                         {ConfigDefaults::triggerMouseButtonField(), 0}}});
        const QString w = QStringLiteral("app|always-plain");
        f.snapOn(w, QStringLiteral("DP-1"));
        f.adaptor->beginDrag(w, 100, 100, 400, 300, QStringLiteral("DP-1"), 0);
        QVERIFY(!f.adaptor->isDragInFlight());

        f.adaptor->updateDragCursor(w, f.inOther().x(), f.inOther().y(), 0, 0);

        QVERIFY2(f.adaptor->isDragInFlight(), "the always-on entry must promote a plain crossing");
        QVERIFY(f.tiling->hasDragInsertPreview());
        const PhosphorProtocol::DragOutcome outcome =
            f.adaptor->endDrag(w, f.inOther().x(), f.inOther().y(), 0, 0, false);
        QCOMPARE(outcome.action, PhosphorProtocol::DragOutcome::NoOp);
    }

    // The selector's minimum-size floor reads the size the window holds as
    // its own, as the re-insert's adoption gate does. A snapped window in a
    // short zone is not refused the selector for the frame its zone gave it,
    // so an edge hover still promotes its pending drag. The snap exclusion
    // probe judges the floor on that live frame, and the drag latched its
    // answer, so the popup was refused to a window the adoption gate let
    // into a strip. A free window that short is still refused.
    void pendingDrag_edgeHoverJudgesTheMinimumSizeOnTheWindowsOwnSize_data()
    {
        QTest::addColumn<bool>("snapped");
        QTest::addColumn<bool>("promoted");
        QTest::newRow("snapped in a short zone") << true << true;
        QTest::newRow("free and short") << false << false;
    }
    void pendingDrag_edgeHoverJudgesTheMinimumSizeOnTheWindowsOwnSize()
    {
        QFETCH(bool, snapped);
        QFETCH(bool, promoted);
        Fixture f;
        f.settings.setZoneSelectorEnabled(true);
        f.settings.setZoneSelectorPosition(ZoneSelectorPosition::Top);
        f.settings.setMinimumWindowWidth(0);
        f.settings.setMinimumWindowHeight(150);
        // Registry metadata gives the snap exclusion probe its full query,
        // the one carrying the live frame the floor is checked against.
        auto* registry = new PhosphorEngine::WindowRegistry(&f.parent);
        f.wta->setWindowRegistry(registry);
        const auto teardown = qScopeGuard([&f] {
            f.wta->setWindowRegistry(nullptr);
        });
        f.wta->setWindowMetadata(QStringLiteral("inst-edge"), QStringLiteral("edgeapp"), QString(), QString(),
                                 QString(), 0, 1, QString(), 0, QVariantMap());
        const QString w = QStringLiteral("edgeapp|inst-edge");
        if (snapped) {
            f.snapOn(w, QStringLiteral("DP-1"));
        }
        f.wta->setFrameGeometry(w, 100, 100, 400, 100);
        QVERIFY2(f.snap->isWindowExcluded(w), "precondition: the live frame is under the minimum height");
        f.adaptor->beginDrag(w, 100, 100, 400, 100, QStringLiteral("DP-1"), 0);
        QVERIFY(!f.adaptor->isDragInFlight());

        // Inside the top edge band of DP-1, where the selector sits.
        f.adaptor->updateDragCursor(w, f.inFirst().x(), 10, 0, 0);

        QCOMPARE(f.adaptor->isDragInFlight(), promoted);
    }

    // A window under the minimum window size stays out of tiling at open, and
    // a drag must not adopt one into the stack or the strip either. The test
    // is the size the window holds as its own against the global minimum. A
    // free window's own size is the frame the drag began with. A snapped
    // window began on a frame its zone decided, which says nothing about the
    // window: a short zone used to refuse it. It is tested only once a
    // restore during the drag hands its own size back, the daemon's drag-out
    // size or the effect's drag-to-float restore, which the daemon sees only
    // as a live frame whose size moved off the one the drag began with.
    void activeDrag_insertAdoptionHonoursTheMinimumWindowSize_data()
    {
        QTest::addColumn<bool>("scrollingScreen");
        QTest::addColumn<bool>("snapped");
        QTest::addColumn<QSize>("beginSize");
        QTest::addColumn<QSize>("restoredSize");
        QTest::addColumn<QSize>("liveSize");
        QTest::addColumn<QSize>("minimumSize");
        QTest::addColumn<bool>("adopted");
        const QSize none;
        QTest::newRow("stack: free frame narrower than the minimum")
            << false << false << QSize(400, 300) << none << none << QSize(500, 0) << false;
        QTest::newRow("stack: free frame at least the minimum")
            << false << false << QSize(400, 300) << none << none << QSize(300, 0) << true;
        QTest::newRow("strip: free frame shorter than the minimum")
            << true << false << QSize(400, 128) << none << none << QSize(0, 150) << false;
        QTest::newRow("strip: short zone frame below the minimum height")
            << true << true << QSize(400, 128) << none << none << QSize(0, 150) << true;
        QTest::newRow("stack: short zone frame below the minimum height")
            << false << true << QSize(400, 128) << none << none << QSize(0, 150) << true;
        QTest::newRow("strip: restored own size narrower than the minimum")
            << true << true << QSize(400, 300) << QSize(640, 480) << none << QSize(700, 0) << false;
        QTest::newRow("strip: restored own size at least the minimum")
            << true << true << QSize(300, 128) << QSize(640, 480) << none << QSize(500, 150) << true;
        QTest::newRow("strip: effect-restored live frame narrower than the minimum")
            << true << true << QSize(400, 300) << none << QSize(640, 480) << QSize(700, 0) << false;
        QTest::newRow("strip: effect-restored live frame at least the minimum")
            << true << true << QSize(400, 300) << none << QSize(640, 480) << QSize(600, 0) << true;
    }
    void activeDrag_insertAdoptionHonoursTheMinimumWindowSize()
    {
        QFETCH(bool, scrollingScreen);
        QFETCH(bool, snapped);
        QFETCH(QSize, beginSize);
        QFETCH(QSize, restoredSize);
        QFETCH(QSize, liveSize);
        QFETCH(QSize, minimumSize);
        QFETCH(bool, adopted);
        Fixture f;
        if (scrollingScreen) {
            f.scrollTheOtherScreen();
        } else {
            f.tileTheOtherScreen();
        }
        PhosphorEngine::IPlacementEngine* engine =
            scrollingScreen ? static_cast<PhosphorEngine::IPlacementEngine*>(f.scrolling.get()) : f.tiling.get();
        f.settings.setMinimumWindowWidth(minimumSize.width());
        f.settings.setMinimumWindowHeight(minimumSize.height());
        f.settings.setRestoreOriginalSizeOnUnsnap(restoredSize.isValid());
        const QString w = QStringLiteral("app|small-insert");
        if (restoredSize.isValid()) {
            f.wta->service()->recordFreeGeometry(w, QStringLiteral("DP-1"), QRect(QPoint(200, 150), restoredSize),
                                                 true);
        }
        if (snapped) {
            f.snapOn(w, QStringLiteral("DP-1"));
        }
        // The live frame is the one the drag begins on, so no restore is
        // read off it until a row moves it.
        f.wta->setFrameGeometry(w, 100, 100, beginSize.width(), beginSize.height());
        QSignalSpy restores(f.adaptor, &WindowDragAdaptor::restoreSizeDuringDragChanged);
        f.adaptor->beginDrag(w, 100, 100, beginSize.width(), beginSize.height(), QStringLiteral("DP-1"), 0);
        f.adaptor->updateDragCursor(w, f.inFirst().x(), f.inFirst().y(), static_cast<int>(Qt::ControlModifier), 0);
        QVERIFY(f.adaptor->isDragInFlight());
        QCOMPARE(restores.count(), restoredSize.isValid() ? 1 : 0);
        if (liveSize.isValid()) {
            // The effect's own drag-to-float restore, which tells the daemon
            // nothing but the frame it reports afterwards.
            f.wta->setFrameGeometry(w, 100, 100, liveSize.width(), liveSize.height());
        }

        f.adaptor->updateDragCursor(w, f.inOther().x(), f.inOther().y(), static_cast<int>(Qt::AltModifier), 0);

        QCOMPARE(engine->hasDragInsertPreview(), adopted);
    }

    // Under "always re-insert" Escape dismisses the re-insert for the rest of
    // the drag. Nothing brings it back but a real trigger press: not the
    // always-on entry, which reads as held on every tick, and not a crossing
    // off the tiling screen and back. A preview that comes back is one the
    // drop commits.
    void activeDrag_escapeHoldsAnAlwaysOnReInsertOffAcrossACrossing()
    {
        Fixture f;
        f.tileTheOtherScreen();
        f.settings.setAutotileDragInsertTriggers(
            {QVariantMap{{ConfigDefaults::triggerModifierField(), static_cast<int>(DragModifier::AlwaysActive)},
                         {ConfigDefaults::triggerMouseButtonField(), 0}}});
        const QString w = QStringLiteral("app|always-insert");
        f.snapOn(w, QStringLiteral("DP-1"));
        const int ctrl = static_cast<int>(Qt::ControlModifier);
        f.adaptor->beginDrag(w, 100, 100, 400, 300, QStringLiteral("DP-1"), 0);
        f.adaptor->updateDragCursor(w, f.inFirst().x(), f.inFirst().y(), ctrl, 0);
        f.adaptor->updateDragCursor(w, f.inOther().x(), f.inOther().y(), 0, 0);
        QVERIFY2(f.tiling->hasDragInsertPreview(), "precondition: always-on previews the re-insert");

        f.adaptor->cancelSnap();
        QVERIFY(!f.tiling->hasDragInsertPreview());
        f.adaptor->updateDragCursor(w, f.inOther().x() + 10, f.inOther().y(), 0, 0);
        f.adaptor->updateDragCursor(w, f.inFirst().x(), f.inFirst().y(), 0, 0);
        f.adaptor->updateDragCursor(w, f.inOther().x(), f.inOther().y(), 0, 0);

        QVERIFY2(!f.tiling->hasDragInsertPreview(), "Escape's dismissal must outlast the crossing");
    }

    // A window dragged onto a scrolling screen and dropped there hands the
    // engine only a size the client chose, since the column is sized by it.
    // A snapped window arrives at a frame its zone decided, so with no
    // restore during the drag and the live frame still the one the drag began
    // with, every size handed over is empty: the column is not given the
    // zone's width. That frame used to go out as the client's own size. A
    // free window hands the frame the drag began with, and a
    // snapped window whose drag-out restored its own size hands that size.
    void activeDrag_handsAScrollingScreenOnlyTheClientsOwnSize_data()
    {
        QTest::addColumn<bool>("snapped");
        QTest::addColumn<QSize>("restoredSize");
        QTest::addColumn<QSize>("handed");
        QTest::newRow("snapped, no restore") << true << QSize() << QSize();
        QTest::newRow("free at begin") << false << QSize() << QSize(400, 300);
        QTest::newRow("snapped, own size restored") << true << QSize(640, 480) << QSize(640, 480);
    }
    void activeDrag_handsAScrollingScreenOnlyTheClientsOwnSize()
    {
        QFETCH(bool, snapped);
        QFETCH(QSize, restoredSize);
        QFETCH(QSize, handed);
        Fixture f;
        f.scrollTheOtherScreen();
        f.settings.setRestoreOriginalSizeOnUnsnap(restoredSize.isValid());
        const QString w = QStringLiteral("app|to-strip");
        if (restoredSize.isValid()) {
            f.wta->service()->recordFreeGeometry(w, QStringLiteral("DP-1"), QRect(QPoint(200, 150), restoredSize),
                                                 true);
        }
        if (snapped) {
            f.snapOn(w, QStringLiteral("DP-1"));
        }
        f.wta->setFrameGeometry(w, 50, 50, 400, 300);
        QSignalSpy restores(f.adaptor, &WindowDragAdaptor::restoreSizeDuringDragChanged);
        f.adaptor->beginDrag(w, 50, 50, 400, 300, QStringLiteral("DP-1"), 0);
        f.adaptor->updateDragCursor(w, f.inFirst().x(), f.inFirst().y(), static_cast<int>(Qt::ControlModifier), 0);
        const int alt = static_cast<int>(Qt::AltModifier);
        f.adaptor->updateDragCursor(w, f.inOther().x(), f.inOther().y(), alt, 0);
        QVERIFY2(f.scrolling->hasDragInsertPreview(), "precondition: the window previews its place in the strip");
        QCOMPARE(restores.count(), restoredSize.isValid() ? 1 : 0);
        QCOMPARE(f.wta->frameGeometry(w).size(), QSize(400, 300));

        f.adaptor->endDrag(w, f.inOther().x(), f.inOther().y(), alt, 0, false);

        QVERIFY(!f.scrolling->clientSizes.isEmpty());
        for (const QSize& size : std::as_const(f.scrolling->clientSizes)) {
            if (handed.isValid()) {
                QCOMPARE(size, handed);
            } else {
                QVERIFY2(size.isEmpty(), "a zone-decided frame must not go out as the client's size");
            }
        }
    }

    // A toggle setting left on from before the switch to "always re-insert"
    // has nothing to toggle: the always-on entry never makes the rising edge
    // a toggle waits for, so the re-insert stayed off for every window the
    // reorder latch does not cover.
    void activeDrag_alwaysOnReInsertIgnoresAStaleToggleSetting()
    {
        Fixture f;
        f.tileTheOtherScreen();
        f.settings.setAutotileDragInsertToggle(true);
        f.settings.setAutotileDragInsertTriggers(
            {QVariantMap{{ConfigDefaults::triggerModifierField(), static_cast<int>(DragModifier::AlwaysActive)},
                         {ConfigDefaults::triggerMouseButtonField(), 0}}});
        const QString w = QStringLiteral("app|stale-toggle");
        f.snapOn(w, QStringLiteral("DP-1"));
        f.adaptor->beginDrag(w, 100, 100, 400, 300, QStringLiteral("DP-1"), 0);
        f.adaptor->updateDragCursor(w, f.inFirst().x(), f.inFirst().y(), static_cast<int>(Qt::ControlModifier), 0);

        f.adaptor->updateDragCursor(w, f.inOther().x(), f.inOther().y(), 0, 0);

        QVERIFY(f.tiling->hasDragInsertPreview());
    }
};

QTEST_MAIN(TestWindowDragCrossScreen)
#include "test_windowdrag_cross_screen.moc"

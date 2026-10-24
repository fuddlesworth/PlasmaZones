// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_windowdrag_cancel_sweeps.cpp
 * @brief Adaptor-level pins for the drag-insert cancel sweeps, the
 * desktop-switch announce burst (discussion #1028 follow-ups) and the
 * placements held behind a queued announce (#1151).
 *
 * Seams that previously had no test:
 *
 *  1. WindowDragAdaptor's LIVE sweep derives dragStillActive from
 *     isDragSessionActive() — replacing the derivation with a constant false
 *     used to leave the whole suite green while the mid-drag cancel resized
 *     the window in the user's hand.
 *
 *  2. The DEAD-SESSION sweep (clearForCompositorReconnect and friends) must
 *     clear the interactive-drag mark BEFORE cancelling with an explicit
 *     false, or the snap-back it exists to emit is suppressed by the mark
 *     (or by a phantom liveness derivation from the dead session's ids).
 *
 *  3. TilingAdaptor's coalesced managed-screens announce stamps per-screen
 *     desktops at EMIT time. The daemon's screenDesktopChanged handler leans
 *     on that: a global switch fans out one report per output, each calling
 *     notifyEngineScreensChanged(true), and only emit-time stamping makes
 *     the burst's single emission carry a fully consistent desktop map (the
 *     focus-follows-mouse leak fixed by [SEQ E½] in start.cpp).
 *
 *  4. TilingAdaptor::runBehindScreensAnnounce runs its work right behind a
 *     queued announce and the tile batches held with it, at once with none
 *     queued, and never for an announce clearEngine voided.
 *
 *  5. SnapAdaptor sends every snap batch and every re-statement through that
 *     sequencer (the settings reflow as well as the engine's own batches),
 *     and drops a held placement of a window another placement went out for
 *     in the meantime.
 */

#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorRules/RuleStore.h>
#include "FakePhysicalScreenSource.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/TilingState.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorZones/LayoutRegistry.h>

#include "config/configdefaults.h"
#include "config/configkeys.h"
#include "dbus/snapadaptor/snapadaptor.h"
#include "dbus/tilingadaptor/tilingadaptor.h"
#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include "helpers/AutotileTestHelpers.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/StubOverlayService.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"
#include "wta_float_back_fixture.h"

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorProtocol/WindowMarshalling.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <functional>
#include <utility>

using namespace PlasmaZones;
using namespace PhosphorTileEngine;
using PlasmaZones::StubSettings;
using PlasmaZones::StubZoneDetector;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
using PlasmaZones::TestHelpers::StubOverlayService;

namespace {

/// True when any windowsTiled emission carries a GEOMETRY entry (an "x" key —
/// float-only entries have none) for @p windowId. Same discriminator as the
/// engine-level suite (test_autotile_drag_insert.cpp).
bool spyHasGeometryEntryFor(const QSignalSpy& spy, const QString& windowId)
{
    for (const QList<QVariant>& emission : spy) {
        const QJsonArray arr = QJsonDocument::fromJson(emission.first().toString().toUtf8()).array();
        for (const QJsonValue& v : arr) {
            const QJsonObject obj = v.toObject();
            if (obj.value(QLatin1String("windowId")).toString() == windowId && obj.contains(QLatin1String("x"))) {
                return true;
            }
        }
    }
    return false;
}

/// True when any applyGeometriesBatch emission carries an entry for @p windowId.
bool batchCarries(const QSignalSpy& spy, const QString& windowId)
{
    for (const QList<QVariant>& emission : spy) {
        const auto geometries = emission.first().value<PhosphorProtocol::WindowGeometryList>();
        for (const PhosphorProtocol::WindowGeometryEntry& entry : geometries) {
            if (entry.windowId == windowId) {
                return true;
            }
        }
    }
    return false;
}

/// A SnapAdaptor over a FloatBackFixture whose announce sequencer holds every
/// piece of work until the test runs it, as a queued screens announce does.
struct HeldSnapFixture
{
    HeldSnapFixture()
    {
        adaptor = new SnapAdaptor(f.snap.get(), f.wta, &f.settings, &adaptorParent);
        adaptor->setAnnounceSequencer([this](std::function<void()> work) {
            held.append(std::move(work));
        });
    }
    ~HeldSnapFixture()
    {
        adaptor->clearEngine();
    }
    /// The announce goes out: the held work runs, in arrival order.
    void announce()
    {
        for (const std::function<void()>& work : std::exchange(held, {})) {
            work();
        }
    }

    FloatBackFixture f;
    QObject adaptorParent; // dies before the fixture's engine and adaptor
    SnapAdaptor* adaptor = nullptr; // parent-owned
    QList<std::function<void()>> held;
};

} // namespace

class TestWindowDragCancelSweeps : public QObject
{
    Q_OBJECT

private:
    static constexpr auto Screen = "eDP-1";

    /// The full dependency set WindowDragAdaptor's ctor qFatals without,
    /// plus an autotile engine seeded with three windows and forced zones.
    struct Fixture
    {
        Fixture()
            : store(std::make_unique<PhosphorRules::RuleStore>(ConfigDefaults::rulesFilePath()))
            , registry(std::make_unique<PhosphorZones::LayoutRegistry>(store.get(), ConfigKeys::layoutsSubdir()))
        {
            const QString layoutDir = guard.dataPath() + QLatin1Char('/') + ConfigKeys::layoutsSubdir();
            QDir().mkpath(layoutDir);
            registry->setLayoutDirectory(layoutDir);

            wta = new WindowTrackingAdaptor(registry.get(), &detector, nullptr, &settings, nullptr, nullptr, &parent);
            adaptor = new WindowDragAdaptor(&overlay, &detector, registry.get(), nullptr, &settings, wta, &parent);

            engine =
                std::make_unique<AutotileEngine>(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
            const QString screen = QLatin1String(Screen);
            engine->setAutotileScreens({screen});
            for (const auto* id : {"A", "B", "C"}) {
                engine->windowOpened(QLatin1String(id), screen);
            }
            QCoreApplication::processEvents();
            PhosphorTiles::TilingState* state = engine->tilingStateForScreen(screen);
            if (state) {
                state->setCalculatedZones(
                    {QRect(0, 0, 900, 1000), QRect(900, 0, 500, 1000), QRect(1400, 0, 500, 1000)});
            }
            adaptor->setAutotileEngine(engine.get());
        }

        IsolatedConfigGuard guard;
        QObject parent;
        StubOverlayService overlay;
        StubZoneDetector detector;
        StubSettings settings;
        std::unique_ptr<PhosphorRules::RuleStore> store;
        std::unique_ptr<PhosphorZones::LayoutRegistry> registry;
        WindowTrackingAdaptor* wta = nullptr; // parent-owned
        WindowDragAdaptor* adaptor = nullptr; // parent-owned
        std::unique_ptr<AutotileEngine> engine;
    };

private Q_SLOTS:

    // Seam 1: with a live drag session, the shared sweep derives
    // dragStillActive=true and the cancel's retile must keep skipping the
    // dragged window's geometry. Hardcoding the derivation to false fails
    // this on the "A" negative.
    void liveSweepDerivesStillActiveAndSuppressesDraggedGeometry()
    {
        Fixture f;
        f.settings.setSnappingEnabled(true);
        f.adaptor->beginDrag(QStringLiteral("A"), 0, 0, 400, 300, QLatin1String(Screen), 0);
        // Drop the interactive-drag mark beginDrag installed: this slot pins
        // the SWEEP's liveness derivation, and with the mark standing the
        // geometry suppression would hold through the engine's mark skip
        // even if the derivation were hardcoded false — masking exactly the
        // mutation this test exists to catch.
        f.engine->setInteractiveDragWindow(QString());
        QVERIFY(f.engine->beginDragInsertPreview(QStringLiteral("A"), QLatin1String(Screen)));

        QSignalSpy tiledSpy(f.engine.get(), &AutotileEngine::windowsTiled);
        f.adaptor->cancelDragInsertPreviews();

        QVERIFY(!f.engine->hasDragInsertPreview());
        QVERIFY(tiledSpy.count() >= 1);
        QVERIFY2(spyHasGeometryEntryFor(tiledSpy, QStringLiteral("B")),
                 "cancel retile emitted no neighbour geometry — the negative below would be vacuous");
        QVERIFY2(!spyHasGeometryEntryFor(tiledSpy, QStringLiteral("A")),
                 "live-session sweep did not suppress the dragged window's geometry (derivation broken?)");
    }

    // Seam 2: the compositor-reconnect sweep runs with the dead session's
    // ids still populated AND the interactive-drag mark still set. It must
    // clear the mark first and cancel with an explicit false so the
    // previously-dragged window's snap-back is actually emitted. Either
    // regression — deriving liveness, or cancelling before the mark clear —
    // fails this on the "A" positive.
    void reconnectSweepClearsMarkFirstAndEmitsSnapBack()
    {
        Fixture f;
        f.settings.setSnappingEnabled(true);
        // beginDrag installs the interactive-drag mark for "A" on both
        // engines — the exact state a compositor loss mid-drag leaves behind.
        f.adaptor->beginDrag(QStringLiteral("A"), 0, 0, 400, 300, QLatin1String(Screen), 0);
        QVERIFY(f.engine->beginDragInsertPreview(QStringLiteral("A"), QLatin1String(Screen)));

        QSignalSpy tiledSpy(f.engine.get(), &AutotileEngine::windowsTiled);
        f.adaptor->clearForCompositorReconnect();

        QVERIFY(!f.engine->hasDragInsertPreview());
        QVERIFY(tiledSpy.count() >= 1);
        QVERIFY2(spyHasGeometryEntryFor(tiledSpy, QStringLiteral("A")),
                 "reconnect sweep suppressed the snap-back — the window stays parked at mid-drag geometry");
    }

    // Seam 3: the FFM fix's convergence mechanism. A global desktop switch
    // fans out one per-output report per screen; each calls
    // notifyEngineScreensChanged(true) with the OTHER screen's desktop not
    // yet updated. Emit-time stamping means the burst's single coalesced
    // emission carries the FINAL desktop for every screen — stamping at
    // queue time instead would freeze the first report's stale map and
    // recreate the rejected-announce leak ([SEQ E½] in start.cpp).
    void announceBurstStampsDesktopsAtEmitTime()
    {
        IsolatedConfigGuard guard;
        QObject parent;
        StubZoneDetector detector;
        StubSettings settings;
        auto store = std::make_unique<PhosphorRules::RuleStore>(ConfigDefaults::rulesFilePath());
        auto registry = std::make_unique<PhosphorZones::LayoutRegistry>(store.get(), ConfigKeys::layoutsSubdir());

        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080), QStringLiteral("DP-1"));
        fake.addScreen(QStringLiteral("DP-2"), QRect(1920, 0, 1920, 1080), QStringLiteral("DP-2"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();

        PhosphorWorkspaces::VirtualDesktopManager vdm;
        vdm.updateScreenDesktop(QStringLiteral("DP-1"), 1);
        vdm.updateScreenDesktop(QStringLiteral("DP-2"), 1);

        auto* wta = new WindowTrackingAdaptor(registry.get(), &detector, &screenMgr, &settings, &vdm, nullptr, &parent);
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QStringLiteral("DP-1"), QStringLiteral("DP-2")});
        TilingAdaptor adaptor(&screenMgr, &parent);
        adaptor.setWindowTrackingAdaptor(wta);
        adaptor.setLifecycleEngines({&engine});

        QSignalSpy spy(&adaptor, &TilingAdaptor::managedScreensChanged);

        // The burst: each per-output report updates ITS screen's desktop and
        // re-announces unconditionally, exactly as the daemon handler does.
        vdm.updateScreenDesktop(QStringLiteral("DP-1"), 2);
        adaptor.notifyEngineScreensChanged(/*isDesktopSwitch=*/true);
        vdm.updateScreenDesktop(QStringLiteral("DP-2"), 2);
        adaptor.notifyEngineScreensChanged(/*isDesktopSwitch=*/true);
        QCoreApplication::processEvents();

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(1).toBool(), true);
        const QVariantMap stamps = spy.first().at(2).toMap();
        // Both screens stamped, both with the FINAL desktop: the first
        // report queued the announce while DP-2 still sat on desktop 1, so
        // any capture-at-queue-time regression shows up as a stale stamp.
        QCOMPARE(stamps.value(QStringLiteral("DP-1")).toInt(), 2);
        QCOMPARE(stamps.value(QStringLiteral("DP-2")).toInt(), 2);
    }

    // #1151: work handed to runBehindScreensAnnounce while an announce is
    // queued runs right behind that announce, never ahead of it, and behind
    // the tile batches held with it even when it was handed over first; with
    // nothing queued it runs at once; clearEngine drops what the voided
    // announce held, so the next session's announce does not run it either.
    void workHeldBehindTheAnnounceRunsAfterIt()
    {
        IsolatedConfigGuard guard;
        QObject parent;
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080), QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QStringLiteral("DP-1")});
        TilingAdaptor adaptor(&screenMgr, &parent);
        adaptor.setLifecycleEngines({&engine});
        QSignalSpy spy(&adaptor, &TilingAdaptor::managedScreensChanged);

        int ranAfterAnnounces = -1;
        adaptor.notifyEngineScreensChanged(/*isDesktopSwitch=*/true);
        adaptor.runBehindScreensAnnounce([&] {
            ranAfterAnnounces = spy.count();
        });
        QCOMPARE(ranAfterAnnounces, -1);
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(ranAfterAnnounces, 1);

        bool ranAtOnce = false;
        adaptor.runBehindScreensAnnounce([&] {
            ranAtOnce = true;
        });
        QVERIFY2(ranAtOnce, "with no announce queued the work runs at once");

        // Handed over BEFORE the tile batch, and still run after it: the held
        // batches go out first, whatever order the two arrived in.
        QSignalSpy tiles(&adaptor, &TilingAdaptor::windowsTileRequested);
        int tilesWhenWorkRan = -1;
        adaptor.notifyEngineScreensChanged(/*isDesktopSwitch=*/true);
        adaptor.runBehindScreensAnnounce([&] {
            tilesWhenWorkRan = tiles.count();
        });
        adaptor.relayTileRequestsJson(QStringLiteral(
            "[{\"windowId\":\"held-tile\",\"screenId\":\"DP-1\",\"x\":0,\"y\":0,\"width\":800,\"height\":600}]"));
        QCOMPARE(tiles.count(), 0);
        QCOMPARE(adaptor.pendingHeldTileBatchCount(), 1);
        QCoreApplication::processEvents();
        QCOMPARE(tiles.count(), 1);
        QCOMPARE(tilesWhenWorkRan, 1);

        bool ranAfterClear = false;
        adaptor.notifyEngineScreensChanged(/*isDesktopSwitch=*/false);
        adaptor.runBehindScreensAnnounce([&] {
            ranAfterClear = true;
        });
        adaptor.clearEngine();
        QCoreApplication::processEvents();
        QVERIFY2(!ranAfterClear, "clearEngine drops the work held behind the announce it voided");
        // The voided announce's generation check alone would keep the work
        // from running above. A restarted session's own announce runs
        // whatever is still held, so only an emptied queue passes here.
        const int announcedBefore = spy.count();
        adaptor.setLifecycleEngines({&engine});
        adaptor.notifyEngineScreensChanged(/*isDesktopSwitch=*/false);
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), announcedBefore + 1);
        QVERIFY2(!ranAfterClear, "the next session's announce runs nothing the voided one held");
    }

    // #1151's snap half: the settings reflow is held behind a queued announce
    // like the engine's own batches, and the batch goes out when the announce
    // does.
    void snapReflowWaitsForTheAnnounce()
    {
        HeldSnapFixture h;
        const QString w = h.f.registerWindow(QStringLiteral("held-reflow"));
        h.f.snap->commitSnap(w, h.f.zone(0), kLeft);
        QSignalSpy batch(h.f.wta, &WindowTrackingAdaptor::applyGeometriesBatch);

        h.adaptor->reapplySnapZones();
        QCOMPARE(batch.count(), 0);
        QCOMPARE(h.held.size(), 1);

        h.announce();
        QCOMPARE(batch.count(), 1);
        QVERIFY(batchCarries(batch, w));
    }

    void engineResnapBatchWaitsForTheAnnounce()
    {
        HeldSnapFixture h;
        const QString w = h.f.registerWindow(QStringLiteral("held-carry"));
        PhosphorEngine::ZoneAssignmentEntry entry;
        entry.windowId = w;
        entry.targetZoneId = h.f.zone(1);
        entry.targetGeometry = h.f.zoneRect(1, kLeft);
        entry.targetScreenId = kLeft;
        QSignalSpy batch(h.f.wta, &WindowTrackingAdaptor::applyGeometriesBatch);

        h.f.snap->emitBatchedResnap({entry});
        QCOMPARE(h.f.snap->zoneForWindow(w), h.f.zone(1));
        QCOMPARE(batch.count(), 0);

        h.announce();
        QCOMPARE(batch.count(), 1);
        QVERIFY(batchCarries(batch, w));
    }

    // A re-statement of one window is judged against the managed set like a
    // batch, so it waits too (F166).
    void restatementWaitsForTheAnnounce()
    {
        HeldSnapFixture h;
        const QString w = h.f.registerWindow(QStringLiteral("held-restate"));
        const QRect rect = h.f.zoneRect(0, kLeft);
        QSignalSpy applies(h.f.wta, &WindowTrackingAdaptor::applyGeometryRequested);

        Q_EMIT h.f.snap->restatementGeometryRequested(w, rect.x(), rect.y(), rect.width(), rect.height(), h.f.zone(0),
                                                      kLeft);
        QCOMPARE(applies.count(), 0);

        h.announce();
        QCOMPARE(applies.count(), 1);
        QCOMPARE(applies.first().at(0).toString(), w);
        QCOMPARE(applies.first().at(8).toInt(), static_cast<int>(PhosphorProtocol::PlacementPurpose::Restatement));
    }

    // A placement of the window that goes out while the batch is held (a user
    // verb in the same turn) is the newer one: the held batch must not follow
    // it and put the window back (F552). The other window in the batch still
    // goes.
    void heldPlacementSupersededByANewerOneIsDropped()
    {
        HeldSnapFixture h;
        const QString moved = h.f.registerWindow(QStringLiteral("held-moved"));
        const QString kept = h.f.registerWindow(QStringLiteral("held-kept"));
        h.f.snap->commitSnap(moved, h.f.zone(0), kLeft);
        h.f.snap->commitSnap(kept, h.f.zone(1), kLeft);
        QSignalSpy batch(h.f.wta, &WindowTrackingAdaptor::applyGeometriesBatch);

        h.adaptor->reapplySnapZones();
        QCOMPARE(h.held.size(), 1);
        const QRect elsewhere = h.f.zoneRect(2, kLeft);
        Q_EMIT h.f.wta->applyGeometryRequested(moved, elsewhere.x(), elsewhere.y(), elsewhere.width(),
                                               elsewhere.height(), h.f.zone(2), kLeft, false,
                                               static_cast<int>(PhosphorProtocol::PlacementPurpose::UserVerb));

        h.announce();
        QCOMPARE(batch.count(), 1);
        QVERIFY2(!batchCarries(batch, moved), "the held batch followed the newer placement");
        QVERIFY(batchCarries(batch, kept));
    }
};

QTEST_MAIN(TestWindowDragCancelSweeps)
#include "test_windowdrag_cancel_sweeps.moc"

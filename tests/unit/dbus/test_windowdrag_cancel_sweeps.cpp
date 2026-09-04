// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_windowdrag_cancel_sweeps.cpp
 * @brief Adaptor-level pins for the drag-insert cancel sweeps and the
 * desktop-switch announce burst (discussion #1028 follow-ups).
 *
 * Three seams that previously had no test:
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
 */

#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorRules/RuleStore.h>
#include "FakeScreenProvider.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/TilingState.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorZones/LayoutRegistry.h>

#include "config/configdefaults.h"
#include "config/configkeys.h"
#include "dbus/tilingadaptor/tilingadaptor.h"
#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include "helpers/AutotileTestHelpers.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/StubOverlayService.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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

        PhosphorScreens::FakeScreenProvider fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080), QStringLiteral("DP-1"));
        fake.addScreen(QStringLiteral("DP-2"), QRect(1920, 0, 1920, 1080), QStringLiteral("DP-2"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
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
};

QTEST_MAIN(TestWindowDragCancelSweeps)
#include "test_windowdrag_cancel_sweeps.moc"

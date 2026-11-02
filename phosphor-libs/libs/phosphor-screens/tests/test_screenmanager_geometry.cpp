// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Regression tests for ScreenManager's screen add / remove / move / resize
// -> available-geometry recompute sequence. Driven through a
// FakePhysicalScreenSource so the topology changes that QScreen cannot stage in a
// unit test (the bugs behind discussions #461 / #465 escaped CI precisely
// because QScreen is uninstantiable by test code) become deterministically
// reproducible. The same topology drives the device-pixel size store
// (compositor reports, host seeds, virtual-screen shares), whose answers
// depend on which screens are tracked under which identifier.

#include "FakePhysicalScreenSource.h"

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/PhysicalScreen.h>
#include <PhosphorScreens/VirtualScreen.h>

#include <QHash>
#include <QSignalSpy>
#include <QSize>
#include <QTest>

using PhosphorScreens::FakePhysicalScreenSource;
using PhosphorScreens::PhysicalScreen;
using PhosphorScreens::ScreenManager;
using PhosphorScreens::ScreenManagerConfig;

class TestScreenManagerGeometry : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void initTestCase()
    {
        // Explicit registration so QSignalSpy can record the PhysicalScreen
        // arguments the ScreenManager signals carry.
        qRegisterMetaType<PhysicalScreen>();
    }

    // The manager's tracked set mirrors the source — both the screens
    // present before start() and hot-plug add/remove afterward.
    void testTracksSourceScreenSet()
    {
        FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));

        ScreenManager mgr(ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();
        QCOMPARE(mgr.screens().size(), 1);
        QCOMPARE(mgr.screens().first().name, QStringLiteral("DP-1"));

        QSignalSpy addedSpy(&mgr, &ScreenManager::screenAdded);
        QSignalSpy removedSpy(&mgr, &ScreenManager::screenRemoved);

        fake.addScreen(QStringLiteral("DP-2"), QRect(1920, 0, 1920, 1080));
        QCOMPARE(addedSpy.count(), 1);
        QCOMPARE(addedSpy.at(0).at(0).value<PhysicalScreen>().name, QStringLiteral("DP-2"));
        QCOMPARE(mgr.screens().size(), 2);

        fake.removeScreen(QStringLiteral("DP-1"));
        QCOMPARE(removedSpy.count(), 1);
        QCOMPARE(removedSpy.at(0).at(0).value<PhysicalScreen>().name, QStringLiteral("DP-1"));
        QCOMPARE(mgr.screens().size(), 1);
        QCOMPARE(mgr.screens().first().name, QStringLiteral("DP-2"));
    }

    // Moving an output recomputes its available geometry. Direct regression
    // for #465: before the geometry-change handler recomputed,
    // availableGeometryChanged never fired on a pure move and
    // actualAvailableGeometry kept returning the stale origin rect.
    void testMoveRecomputesAvailableGeometry()
    {
        FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));

        ScreenManager mgr(ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();

        QSignalSpy geomSpy(&mgr, &ScreenManager::screenGeometryChanged);
        QSignalSpy availSpy(&mgr, &ScreenManager::availableGeometryChanged);

        const QRect moved(1920, 0, 1920, 1080);
        fake.moveScreen(QStringLiteral("DP-1"), moved);

        QCOMPARE(geomSpy.count(), 1);
        QCOMPARE(geomSpy.at(0).at(0).value<PhysicalScreen>().geometry, moved);

        QCOMPARE(availSpy.count(), 1);
        QCOMPARE(availSpy.at(0).at(0).value<PhysicalScreen>().name, QStringLiteral("DP-1"));
        QCOMPARE(availSpy.at(0).at(1).toRect(), moved);

        // No panel source / sensors, so available geometry == full rect —
        // and it must follow the move rather than stay at the old origin.
        QCOMPARE(mgr.screenGeometry(QStringLiteral("DP-1")), moved);
        QVERIFY(mgr.physicalScreenFor(QStringLiteral("DP-1")).isValid());
        QCOMPARE(mgr.actualAvailableGeometry(mgr.physicalScreenFor(QStringLiteral("DP-1"))), moved);
    }

    // A resize (origin fixed, extent changed) runs the same recompute path.
    void testResizeRecomputesAvailableGeometry()
    {
        FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));

        ScreenManager mgr(ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();

        QSignalSpy availSpy(&mgr, &ScreenManager::availableGeometryChanged);

        const QRect resized(0, 0, 2560, 1440);
        fake.moveScreen(QStringLiteral("DP-1"), resized);

        QCOMPARE(availSpy.count(), 1);
        QCOMPARE(availSpy.at(0).at(1).toRect(), resized);
        QVERIFY(mgr.physicalScreenFor(QStringLiteral("DP-1")).isValid());
        QCOMPARE(mgr.actualAvailableGeometry(mgr.physicalScreenFor(QStringLiteral("DP-1"))), resized);

        // A no-op resize (identical geometry) is suppressed by
        // FakePhysicalScreenSource — mirroring Qt, which does not fire
        // QScreen::geometryChanged when geometry() is unchanged — so it
        // triggers no recompute and no further availableGeometryChanged.
        fake.moveScreen(QStringLiteral("DP-1"), resized);
        QCOMPARE(availSpy.count(), 1);
    }

    // The #465 DPMS-wake shape exactly: an output drops, re-appears at a
    // transient (0,0) origin, then settles to its real desktop position.
    // The settle must propagate to the available-geometry cache.
    void testReAddAtTransientOriginThenSettle()
    {
        FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));

        ScreenManager mgr(ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();

        // Output drops on DPMS-off, then re-appears at a transient origin.
        fake.removeScreen(QStringLiteral("DP-1"));
        const QRect transient(0, 0, 1920, 1080);
        fake.addScreen(QStringLiteral("DP-1"), transient);

        // The re-added output is tracked, sitting at its transient origin.
        QVERIFY(mgr.physicalScreenFor(QStringLiteral("DP-1")).isValid());
        QCOMPARE(mgr.screenGeometry(QStringLiteral("DP-1")), transient);
        // Pin the available rect at the transient stage: the re-add must not
        // carry a stale pre-removal available rect — the exact #465 failure.
        // With no panel source it equals the transient screen rect here.
        QCOMPARE(mgr.actualAvailableGeometry(mgr.physicalScreenFor(QStringLiteral("DP-1"))), transient);

        QSignalSpy availSpy(&mgr, &ScreenManager::availableGeometryChanged);

        // Compositor settles the output to its real desktop position.
        const QRect settled(2560, 0, 1920, 1080);
        fake.moveScreen(QStringLiteral("DP-1"), settled);

        QCOMPARE(availSpy.count(), 1);
        QCOMPARE(availSpy.at(0).at(1).toRect(), settled);
        QVERIFY(mgr.physicalScreenFor(QStringLiteral("DP-1")).isValid());
        QCOMPARE(mgr.actualAvailableGeometry(mgr.physicalScreenFor(QStringLiteral("DP-1"))), settled);
    }

    // Compositor-reported client area (the KWin effect's clientArea push)
    // overrides the panel-strut heuristic. Direct regression for discussion
    // #461: a top panel left the heuristic attributing the whole reservation
    // to the bottom edge — the compositor source pins the correct top inset.
    void testCompositorGeometryOverridesHeuristic()
    {
        FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));

        ScreenManager mgr(ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();

        const PhysicalScreen screen = mgr.physicalScreenFor(QStringLiteral("DP-1"));
        QVERIFY(screen.isValid());
        // No panel source / sensors: the heuristic yields the full screen rect.
        QCOMPARE(mgr.actualAvailableGeometry(screen), QRect(0, 0, 1920, 1080));

        QSignalSpy availSpy(&mgr, &ScreenManager::availableGeometryChanged);

        // KWin reports a 32px top panel.
        const QRect topPanelWorkArea(0, 32, 1920, 1048);
        mgr.setCompositorAvailableGeometry(QStringLiteral("DP-1"), topPanelWorkArea);

        QCOMPARE(availSpy.count(), 1);
        QCOMPARE(availSpy.at(0).at(1).toRect(), topPanelWorkArea);
        QCOMPARE(mgr.actualAvailableGeometry(screen), topPanelWorkArea);

        // A redundant push of the identical rect is a no-op — no extra signal.
        mgr.setCompositorAvailableGeometry(QStringLiteral("DP-1"), topPanelWorkArea);
        QCOMPARE(availSpy.count(), 1);

        // Clearing the override (empty rect) reverts to the heuristic.
        mgr.setCompositorAvailableGeometry(QStringLiteral("DP-1"), QRect());
        QCOMPARE(availSpy.count(), 2);
        QCOMPARE(mgr.actualAvailableGeometry(screen), QRect(0, 0, 1920, 1080));
    }

    // A compositor rect spilling past the output is clamped to the screen —
    // guards against a snapshot that lags a resize corrupting downstream
    // relative-geometry math.
    void testCompositorGeometryClampedToScreen()
    {
        FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));

        ScreenManager mgr(ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();

        // 32px top inset but height still the full 1080 — bottom edge spills
        // 32px past the output. The intersect clamps it back.
        mgr.setCompositorAvailableGeometry(QStringLiteral("DP-1"), QRect(0, 32, 1920, 1080));
        QCOMPARE(mgr.actualAvailableGeometry(mgr.physicalScreenFor(QStringLiteral("DP-1"))), QRect(0, 32, 1920, 1048));
    }

    // A compositor override is dropped when its screen disconnects, so a
    // same-connector reconnect starts from the heuristic rather than a stale
    // rect for the old output.
    void testCompositorGeometryDroppedOnScreenRemoval()
    {
        FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));

        ScreenManager mgr(ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();

        mgr.setCompositorAvailableGeometry(QStringLiteral("DP-1"), QRect(0, 32, 1920, 1048));
        QCOMPARE(mgr.actualAvailableGeometry(mgr.physicalScreenFor(QStringLiteral("DP-1"))), QRect(0, 32, 1920, 1048));

        fake.removeScreen(QStringLiteral("DP-1"));
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));

        // Override gone — back to the heuristic's full-screen rect.
        QCOMPARE(mgr.actualAvailableGeometry(mgr.physicalScreenFor(QStringLiteral("DP-1"))), QRect(0, 0, 1920, 1080));
    }

    // screenGeometry's THREE answers, which nothing pinned before. Consumers lean on the
    // distinction between them to decide whether a rect is a real answer or a substitution, and
    // three separate attempts at a guard in the overlay service were built on assumptions about
    // it that turned out false — one of them that a virtual index dropped by a reconfigure still
    // resolves, another that an empty id resolves to nothing.
    void testScreenGeometryDistinguishesResolvedFromUnresolvable()
    {
        FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));

        ScreenManager mgr(ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();

        const auto makeConfig = [](int count) {
            PhosphorScreens::VirtualScreenConfig cfg;
            cfg.physicalScreenId = QStringLiteral("DP-1");
            for (int i = 0; i < count; ++i) {
                PhosphorScreens::VirtualScreenDef def;
                def.physicalScreenId = QStringLiteral("DP-1");
                def.index = i;
                def.id = PhosphorIdentity::VirtualScreenId::make(QStringLiteral("DP-1"), i);
                def.displayName = QStringLiteral("Slice %1").arg(i);
                const qreal band = 1.0 / count;
                def.region = QRectF(0.0, i * band, 1.0, band);
                cfg.screens.append(def);
            }
            return cfg;
        };

        // A TRACKED BARE PHYSICAL id answers with its real rect. Callers treat an invalid answer as
        // "this id does not resolve", so a physical screen must never look unresolvable.
        QCOMPARE(mgr.screenGeometry(QStringLiteral("DP-1")), QRect(0, 0, 1920, 1080));

        // A LIVE VIRTUAL index answers with its slice.
        QVERIFY(mgr.setVirtualScreenConfig(QStringLiteral("DP-1"), makeConfig(3)));
        const QString third = PhosphorIdentity::VirtualScreenId::make(QStringLiteral("DP-1"), 2);
        QVERIFY(mgr.screenGeometry(third).isValid());

        // AN INDEX DROPPED BY A RECONFIGURE DOES NOT. This is the case the overlay service's
        // guards were written for and the one nothing covered: the physical screen is still
        // tracked and still has a config, so the rebuild does not bail — index 2 is simply no
        // longer in it. An invalid rect here is the only signal a consumer gets.
        QVERIFY(mgr.setVirtualScreenConfig(QStringLiteral("DP-1"), makeConfig(2)));
        QVERIFY(!mgr.screenGeometry(third).isValid());

        // AN EMPTY id resolves to the PRIMARY output rather than to nothing, which is deliberate
        // and was mis-stated in a consumer's comment. Pinned so the next reader does not have to
        // take a comment's word for it.
        QCOMPARE(mgr.screenGeometry(QString()), QRect(0, 0, 1920, 1080));

        // AN UNTRACKED id does not resolve.
        QVERIFY(!mgr.screenGeometry(QStringLiteral("HDMI-9")).isValid());
    }

    // ─── Screen size in device pixels ────────────────────────────────────

    // A compositor report records, a repeat is a no-op, a change and a clear
    // each announce once, and a clear of nothing announces nothing. The size
    // answers for the connector name and the identifier alike.
    void testPixelSizeReportSetChangeAndClear()
    {
        FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 2560, 1440), QStringLiteral("Dell:U27:1"));

        ScreenManager mgr(ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();
        QSignalSpy spy(&mgr, &ScreenManager::compositorPixelSizeChanged);

        QVERIFY(!mgr.pixelSize(QStringLiteral("DP-1")).has_value());
        QVERIFY(!mgr.isPixelSizeReported(QStringLiteral("DP-1")));

        mgr.setCompositorPixelSize(QStringLiteral("DP-1"), QSize(3840, 2160));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("DP-1"));
        QCOMPARE(mgr.pixelSize(QStringLiteral("DP-1")).value_or(QSize()), QSize(3840, 2160));
        QCOMPARE(mgr.pixelSize(QStringLiteral("Dell:U27:1")).value_or(QSize()), QSize(3840, 2160));
        QVERIFY(mgr.isPixelSizeReported(QStringLiteral("Dell:U27:1")));

        mgr.setCompositorPixelSize(QStringLiteral("DP-1"), QSize(3840, 2160));
        QCOMPARE(spy.count(), 1);

        // A rotation reaches the store as one change.
        mgr.setCompositorPixelSize(QStringLiteral("DP-1"), QSize(2160, 3840));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(mgr.pixelSize(QStringLiteral("DP-1")).value_or(QSize()), QSize(2160, 3840));

        mgr.setCompositorPixelSize(QStringLiteral("DP-1"), QSize());
        QCOMPARE(spy.count(), 3);
        QVERIFY(!mgr.pixelSize(QStringLiteral("DP-1")).has_value());
        QVERIFY(!mgr.isPixelSizeReported(QStringLiteral("DP-1")));

        mgr.setCompositorPixelSize(QStringLiteral("DP-1"), QSize(0, 0));
        QCOMPARE(spy.count(), 3);

        // An empty id answers nothing, unlike screenGeometry's primary
        // fallback, and an empty connector records nothing.
        mgr.setCompositorPixelSize(QStringLiteral("DP-1"), QSize(3840, 2160));
        QVERIFY(!mgr.pixelSize(QString()).has_value());
        mgr.setCompositorPixelSize(QString(), QSize(1, 1));
        QCOMPARE(spy.count(), 4);
    }

    // A virtual screen answers its region's share of the PIXEL rect, rounded
    // at the edges exactly as VirtualScreenDef::absoluteGeometry rounds them,
    // which is what the effect's own split of the same pixel rect does. A
    // thirds split of 2560 lands on 853 / 854 / 853, so the shares add up to
    // the whole output. Scaling the logical slices would not.
    void testPixelSizeVirtualShareRoundsLikeTheEffect()
    {
        const QString physId = QStringLiteral("Dell:U27:1");
        FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1707, 960), physId);

        ScreenManager mgr(ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();

        PhosphorScreens::VirtualScreenConfig cfg;
        cfg.physicalScreenId = physId;
        const qreal third = 1.0 / 3.0;
        for (int i = 0; i < 3; ++i) {
            PhosphorScreens::VirtualScreenDef def;
            def.physicalScreenId = physId;
            def.index = i;
            def.id = PhosphorIdentity::VirtualScreenId::make(physId, i);
            def.region = QRectF(i * third, 0.0, third, 1.0);
            cfg.screens.append(def);
        }
        QVERIFY(mgr.setVirtualScreenConfig(physId, cfg));

        const QString vs0 = PhosphorIdentity::VirtualScreenId::make(physId, 0);
        const QString vs1 = PhosphorIdentity::VirtualScreenId::make(physId, 1);
        const QString vs2 = PhosphorIdentity::VirtualScreenId::make(physId, 2);

        // No report and no seed: no virtual screen has a size either.
        QVERIFY(!mgr.pixelSize(vs0).has_value());

        mgr.setCompositorPixelSize(QStringLiteral("DP-1"), QSize(2560, 1440));
        QCOMPARE(mgr.pixelSize(vs0).value_or(QSize()), QSize(853, 1440));
        QCOMPARE(mgr.pixelSize(vs1).value_or(QSize()), QSize(854, 1440));
        QCOMPARE(mgr.pixelSize(vs2).value_or(QSize()), QSize(853, 1440));
        QVERIFY(mgr.isPixelSizeReported(vs1));
        // The physical id still answers the whole output.
        QCOMPARE(mgr.pixelSize(physId).value_or(QSize()), QSize(2560, 1440));

        // A virtual screen the manager does not know answers nothing, and is
        // not reported, though its monitor is.
        const QString unknown = PhosphorIdentity::VirtualScreenId::make(physId, 7);
        QVERIFY(!mgr.pixelSize(unknown).has_value());
        QVERIFY(!mgr.isPixelSizeReported(unknown));
    }

    // A seed answers until the compositor reports, the report wins while it
    // exists, and a seed change announces only when it changes the answer.
    void testPixelSizeSeedAnswersUntilAReportArrives()
    {
        const QString physId = QStringLiteral("Dell:U27:1");
        FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1707, 960), physId);

        ScreenManager mgr(ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();
        QSignalSpy spy(&mgr, &ScreenManager::compositorPixelSizeChanged);

        mgr.seedPixelSize(physId, QSize(2560, 1440));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("DP-1"));
        QCOMPARE(mgr.pixelSize(QStringLiteral("DP-1")).value_or(QSize()), QSize(2560, 1440));
        QVERIFY(!mgr.isPixelSizeReported(QStringLiteral("DP-1")));
        // A seed is not a report, so nothing is offered back for persisting.
        QVERIFY(mgr.reportedPixelSizesByIdentifier().isEmpty());

        mgr.seedPixelSize(physId, QSize(2560, 1440));
        QCOMPARE(spy.count(), 1);

        // A report that confirms the seed still announces: the size reads the
        // same, but it is now the compositor's.
        mgr.setCompositorPixelSize(QStringLiteral("DP-1"), QSize(2560, 1440));
        QCOMPARE(spy.count(), 2);
        QVERIFY(mgr.isPixelSizeReported(QStringLiteral("DP-1")));

        // A reported screen's seed is shadowed, so changing it changes no
        // answer and announces nothing.
        mgr.seedPixelSize(physId, QSize(1920, 1080));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(mgr.pixelSize(QStringLiteral("DP-1")).value_or(QSize()), QSize(2560, 1440));

        // Clearing the report falls back to the current seed.
        mgr.setCompositorPixelSize(QStringLiteral("DP-1"), QSize());
        QCOMPARE(spy.count(), 3);
        QCOMPARE(mgr.pixelSize(QStringLiteral("DP-1")).value_or(QSize()), QSize(1920, 1080));
        QVERIFY(!mgr.isPixelSizeReported(QStringLiteral("DP-1")));

        // A seed for a monitor that is not connected changes no answer.
        mgr.seedPixelSize(QStringLiteral("LG:Ultra:9"), QSize(5120, 1440));
        QCOMPARE(spy.count(), 3);

        // An empty size drops the seed.
        mgr.seedPixelSize(physId, QSize());
        QCOMPARE(spy.count(), 4);
        QVERIFY(!mgr.pixelSize(QStringLiteral("DP-1")).has_value());
    }

    // A same-model monitor joining flips the surviving one's identifier from
    // the bare form to the connector-suffixed one, and leaving flips it back.
    // The seed is that monitor's, so it follows both ways, and the answer
    // never changes underneath it.
    void testPixelSizeSeedFollowsIdentifierFlip()
    {
        const QString bare = QStringLiteral("Dell:U27:1");
        const QString suffixed = QStringLiteral("Dell:U27:1/DP-1");
        FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1707, 960), bare);

        ScreenManager mgr(ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();
        mgr.seedPixelSize(bare, QSize(2560, 1440));
        QSignalSpy spy(&mgr, &ScreenManager::compositorPixelSizeChanged);

        fake.setIdentifier(QStringLiteral("DP-1"), suffixed);
        fake.addScreen(QStringLiteral("DP-2"), QRect(1707, 0, 1707, 960), QStringLiteral("Dell:U27:1/DP-2"));
        QCOMPARE(mgr.pixelSize(suffixed).value_or(QSize()), QSize(2560, 1440));
        QCOMPARE(mgr.pixelSize(QStringLiteral("DP-1")).value_or(QSize()), QSize(2560, 1440));
        // The newcomer has no seed of its own and must not borrow the moved one.
        QVERIFY(!mgr.pixelSize(QStringLiteral("DP-2")).has_value());
        QCOMPARE(spy.count(), 0);

        fake.setIdentifier(QStringLiteral("DP-1"), bare);
        fake.removeScreen(QStringLiteral("DP-2"));
        QCOMPARE(mgr.pixelSize(bare).value_or(QSize()), QSize(2560, 1440));
        QCOMPARE(spy.count(), 0);

        // With no seed under the old id, one the host held under the new id
        // starts answering, and that is announced.
        mgr.seedPixelSize(bare, QSize());
        mgr.seedPixelSize(suffixed, QSize(3840, 2160));
        spy.clear();
        fake.setIdentifier(QStringLiteral("DP-1"), suffixed);
        fake.addScreen(QStringLiteral("DP-2"), QRect(1707, 0, 1707, 960), QStringLiteral("Dell:U27:1/DP-2"));
        QCOMPARE(mgr.pixelSize(QStringLiteral("DP-1")).value_or(QSize()), QSize(3840, 2160));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("DP-1"));
    }

    // The daemon's own QScreen removal does not drop a report: the effect is
    // the only writer, and on a KVM replug its report can land before the
    // QScreen it describes, or survive one the daemon briefly lost.
    void testPixelSizeReportSurvivesTheManagersScreenRemoval()
    {
        FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1707, 960), QStringLiteral("Dell:U27:1"));

        ScreenManager mgr(ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();

        mgr.setCompositorPixelSize(QStringLiteral("DP-1"), QSize(2560, 1440));
        fake.removeScreen(QStringLiteral("DP-1"));
        // Untracked, so no answer for now, but the record is kept.
        QVERIFY(!mgr.pixelSize(QStringLiteral("DP-1")).has_value());
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1707, 960), QStringLiteral("Dell:U27:1"));
        QCOMPARE(mgr.pixelSize(QStringLiteral("DP-1")).value_or(QSize()), QSize(2560, 1440));

        // A report for an output the manager has not seen yet is recorded and
        // answers once the output arrives.
        QSignalSpy spy(&mgr, &ScreenManager::compositorPixelSizeChanged);
        mgr.setCompositorPixelSize(QStringLiteral("HDMI-A-1"), QSize(1920, 1080));
        QCOMPARE(spy.count(), 1);
        fake.addScreen(QStringLiteral("HDMI-A-1"), QRect(1707, 0, 1920, 1080), QStringLiteral("LG:Ultra:9"));
        QCOMPARE(mgr.pixelSize(QStringLiteral("LG:Ultra:9")).value_or(QSize()), QSize(1920, 1080));
        QVERIFY(mgr.isPixelSizeReported(QStringLiteral("LG:Ultra:9")));
    }

    // clearCompositorPixelSizes drops every report and announces each
    // connector once, in a stable order. Seeds survive it.
    void testClearCompositorPixelSizesAnnouncesEachConnector()
    {
        FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-2"), QRect(0, 0, 1920, 1080), QStringLiteral("Dell:U27:2"));
        fake.addScreen(QStringLiteral("DP-1"), QRect(1920, 0, 1920, 1080), QStringLiteral("Dell:U27:1"));

        ScreenManager mgr(ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();
        mgr.setCompositorPixelSize(QStringLiteral("DP-1"), QSize(3840, 2160));
        mgr.setCompositorPixelSize(QStringLiteral("DP-2"), QSize(2560, 1440));
        mgr.seedPixelSize(QStringLiteral("Dell:U27:2"), QSize(1920, 1080));

        QSignalSpy spy(&mgr, &ScreenManager::compositorPixelSizeChanged);
        mgr.clearCompositorPixelSizes();
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("DP-1"));
        QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("DP-2"));
        QVERIFY(!mgr.pixelSize(QStringLiteral("DP-1")).has_value());
        QCOMPARE(mgr.pixelSize(QStringLiteral("DP-2")).value_or(QSize()), QSize(1920, 1080));

        mgr.clearCompositorPixelSizes();
        QCOMPARE(spy.count(), 2);
    }

    // reportedPixelSizesByIdentifier offers the host what to persist: the
    // reports of tracked screens, keyed by identifier. Not seeds, and not a
    // report for an output the manager does not track.
    void testReportedPixelSizesByIdentifier()
    {
        FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080), QStringLiteral("Dell:U27:1"));
        fake.addScreen(QStringLiteral("DP-2"), QRect(1920, 0, 1920, 1080), QStringLiteral("Dell:U27:2"));

        ScreenManager mgr(ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();
        mgr.setCompositorPixelSize(QStringLiteral("DP-1"), QSize(3840, 2160));
        mgr.seedPixelSize(QStringLiteral("Dell:U27:2"), QSize(2560, 1440));
        mgr.setCompositorPixelSize(QStringLiteral("HDMI-A-9"), QSize(1280, 1024));

        const QHash<QString, QSize> reported = mgr.reportedPixelSizesByIdentifier();
        QCOMPARE(reported.size(), 1);
        QCOMPARE(reported.value(QStringLiteral("Dell:U27:1")), QSize(3840, 2160));
    }

    // stop() drops the reports (the effect re-reports every output when it
    // registers again) and keeps the seeds, which are the host's record.
    void testStopDropsReportsAndKeepsSeeds()
    {
        FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080), QStringLiteral("Dell:U27:1"));
        fake.addScreen(QStringLiteral("DP-2"), QRect(1920, 0, 1920, 1080), QStringLiteral("Dell:U27:2"));

        ScreenManager mgr(ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();
        mgr.setCompositorPixelSize(QStringLiteral("DP-1"), QSize(3840, 2160));
        mgr.seedPixelSize(QStringLiteral("Dell:U27:2"), QSize(2560, 1440));

        mgr.stop();
        mgr.start();
        QVERIFY(!mgr.pixelSize(QStringLiteral("DP-1")).has_value());
        QCOMPARE(mgr.pixelSize(QStringLiteral("DP-2")).value_or(QSize()), QSize(2560, 1440));
    }
};

QTEST_MAIN(TestScreenManagerGeometry)
#include "test_screenmanager_geometry.moc"

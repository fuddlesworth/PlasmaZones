// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_windowdrag_drop_proxy.cpp
 * @brief The drop proxy of WindowDragAdaptor (registerDropProxy /
 * unregisterDropProxy, dropproxy.h): a shell-registered miniature of a
 * snapping screen's zones that a real drag can be dropped on.
 *
 *  1. DropProxyRegistry parses the wire shape strictly (rect, cells, ids),
 *     refuses malformed proxies without disturbing an earlier one, replaces
 *     per screen, and hit-tests a screen-local point to Outside /
 *     InsideNoCell / Cell.
 *  2. Through the adaptor, on the offscreen platform's one screen: a
 *     snap-path drag whose cursor lands on a proxy cell resolves to that
 *     cell's REAL zone (the overlay highlights it and the zone-geometry
 *     preview carries the full-size rect), a cursor inside the miniature
 *     but over no cell is no target, and endDrag inside a cell answers
 *     ApplySnap for that zone with the zone's geometry. The drag never
 *     touches a real zone: the stub detector answers nothing, so every
 *     target the test sees came through the proxy.
 */

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>
#include <memory>

#include <PhosphorProtocol/DragTypes.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>

#include "config/configdefaults.h"
#include "core/types/enums.h"
#include "dbus/windowdragadaptor/dropproxy.h"
#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubOverlayService.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
using PlasmaZones::TestHelpers::StubOverlayService;
namespace Key = PhosphorProtocol::Service::DropProxyKey;

// Named, not anonymous: the test class's Fixture (a member of a Q_OBJECT
// class with external linkage) holds one of these by value, and a field of
// anonymous-namespace type there is a -Wsubobject-linkage warning.
namespace DropProxyTestSupport {

QJsonArray rectJson(const QRect& r)
{
    return QJsonArray{r.x(), r.y(), r.width(), r.height()};
}

QString proxyJson(const QRect& rect, const QList<QPair<QString, QRect>>& cells)
{
    QJsonArray cellArr;
    for (const auto& [id, cellRect] : cells) {
        cellArr.append(QJsonObject{{QString(Key::Id), id}, {QString(Key::Rect), rectJson(cellRect)}});
    }
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{QString(Key::Rect), rectJson(rect)}, {QString(Key::Cells), cellArr}}).toJson());
}

/// Records the overlay highlight calls the drag path makes.
class RecordingOverlay : public StubOverlayService
{
public:
    void highlightZone(const QString& zoneId) override
    {
        highlighted.append(zoneId);
    }
    void clearHighlight() override
    {
        ++clears;
    }
    QStringList highlighted;
    int clears = 0;
};

} // namespace DropProxyTestSupport

using namespace DropProxyTestSupport;

class TestWindowDragDropProxy : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void registry_parsesStrictly()
    {
        DropProxyRegistry reg;
        const QString screen = QStringLiteral("DP-1");

        // Malformed shapes are refused and register nothing.
        QVERIFY(!reg.registerProxy(screen, QStringLiteral("not json")));
        QVERIFY(!reg.registerProxy(screen, QStringLiteral("[]")));
        QVERIFY(!reg.registerProxy(screen, QStringLiteral("{}")));
        QVERIFY(!reg.registerProxy(screen, QStringLiteral("{\"rect\":[0,0,10,10]}"))); // no cells
        QVERIFY(!reg.registerProxy(screen, QStringLiteral("{\"rect\":[0,0,0,10],\"cells\":[]}"))); // zero width
        QVERIFY(!reg.registerProxy(screen, QStringLiteral("{\"rect\":[0,0,10],\"cells\":[]}"))); // three numbers
        QVERIFY(!reg.registerProxy(screen, QStringLiteral("{\"rect\":[0,0,10,10],\"cells\":[{\"rect\":[0,0,5,5]}]}")));
        QVERIFY(!reg.registerProxy(
            screen, QStringLiteral("{\"rect\":[0,0,10,10],\"cells\":[{\"id\":\"z\",\"rect\":[0,0,5,0]}]}")));
        QVERIFY(!reg.registerProxy(QString(), proxyJson(QRect(0, 0, 10, 10), {})));
        QCOMPARE(reg.count(), 0);
        QVERIFY(!reg.hasProxy(screen));

        // A proxy with no cells is legal (a map with no layout yet).
        QVERIFY(reg.registerProxy(screen, proxyJson(QRect(0, 0, 10, 10), {})));
        QVERIFY(reg.hasProxy(screen));
        QCOMPARE(reg.count(), 1);

        // A later malformed registration leaves the earlier one standing.
        QVERIFY(!reg.registerProxy(screen, QStringLiteral("{}")));
        QVERIFY(reg.hasProxy(screen));

        reg.unregisterProxy(screen);
        QVERIFY(!reg.hasProxy(screen));
        reg.unregisterProxy(screen); // idempotent
        QCOMPARE(reg.count(), 0);
    }

    void registry_hitTestsAndReplaces()
    {
        DropProxyRegistry reg;
        const QString screen = QStringLiteral("DP-1");
        const QString zoneA = QStringLiteral("{aaaaaaaa-0000-0000-0000-000000000000}");
        const QString zoneB = QStringLiteral("{bbbbbbbb-0000-0000-0000-000000000000}");
        QVERIFY(reg.registerProxy(screen,
                                  proxyJson(QRect(100, 10, 200, 100),
                                            {{zoneA, QRect(100, 10, 100, 100)}, {zoneB, QRect(210, 10, 90, 100)}})));

        using Hit = DropProxyRegistry::Hit;
        QCOMPARE(reg.resolve(screen, QPoint(50, 50)).hit, Hit::Outside);
        QCOMPARE(reg.resolve(screen, QPoint(150, 50)).hit, Hit::Cell);
        QCOMPARE(reg.resolve(screen, QPoint(150, 50)).zoneId, zoneA);
        QCOMPARE(reg.resolve(screen, QPoint(250, 50)).zoneId, zoneB);
        // The 10px gutter between the cells is inside the miniature but over
        // nothing.
        QCOMPARE(reg.resolve(screen, QPoint(205, 50)).hit, Hit::InsideNoCell);
        QVERIFY(reg.resolve(screen, QPoint(205, 50)).zoneId.isEmpty());
        // Edges: QRect::contains is inclusive of its right/bottom edge.
        QCOMPARE(reg.resolve(screen, QPoint(299, 109)).zoneId, zoneB);
        QCOMPARE(reg.resolve(screen, QPoint(300, 110)).hit, Hit::Outside);
        // Another screen has no proxy.
        QCOMPARE(reg.resolve(QStringLiteral("DP-2"), QPoint(150, 50)).hit, Hit::Outside);
        QCOMPARE(reg.resolve(QString(), QPoint(150, 50)).hit, Hit::Outside);

        // Re-registering replaces: the old cells are gone, the count stays 1.
        QVERIFY(reg.registerProxy(screen, proxyJson(QRect(0, 0, 50, 50), {{zoneB, QRect(0, 0, 50, 50)}})));
        QCOMPARE(reg.count(), 1);
        QCOMPARE(reg.resolve(screen, QPoint(150, 50)).hit, Hit::Outside);
        QCOMPARE(reg.resolve(screen, QPoint(25, 25)).zoneId, zoneB);
    }

    void adaptor_resolvesProxyCellsToRealZones()
    {
        Fixture f;
        QVERIFY2(f.screen, "offscreen platform provided no screen; the drag path cannot resolve a cursor");
        const QRect screenGeom = f.screen->geometry();
        // The three test zones split the screen into thirds; the miniature
        // is a 300x90 strip near the top-left with a 5px gutter between its
        // cells, each cell standing for one zone.
        const QString zone0 = f.layout->zones().at(0)->id().toString();
        const QString zone1 = f.layout->zones().at(1)->id().toString();
        const QString zone2 = f.layout->zones().at(2)->id().toString();
        const QRect mini(20, 20, 300, 90);
        f.adaptor->registerDropProxy(
            f.screenId,
            proxyJson(
                mini,
                {{zone0, QRect(20, 20, 95, 90)}, {zone1, QRect(120, 20, 95, 90)}, {zone2, QRect(220, 20, 100, 90)}}));

        const QString windowId = QStringLiteral("app|w1");
        const int mods = static_cast<int>(Qt::ControlModifier);
        // Global cursor positions: screen origin plus the miniature-local
        // point. Inside the second cell, in the gutter, and inside the third.
        const QPoint inCell1 = screenGeom.topLeft() + QPoint(160, 60);
        const QPoint inGutter = screenGeom.topLeft() + QPoint(117, 60);
        const QPoint inCell2 = screenGeom.topLeft() + QPoint(260, 60);
        const QPoint offProxy = screenGeom.topLeft() + QPoint(400, 300);

        QSignalSpy geometrySpy(f.adaptor, &WindowDragAdaptor::zoneGeometryDuringDragChanged);

        const PhosphorProtocol::DragPolicy policy =
            f.adaptor->beginDrag(windowId, screenGeom.x() + 400, screenGeom.y() + 300, 300, 200, f.screenId, 0);
        QCOMPARE(policy.bypassReason, PhosphorProtocol::DragBypassReason::None);

        // Off the proxy, over no real zone (the stub detector answers none):
        // no target, nothing highlighted.
        f.adaptor->updateDragCursor(windowId, offProxy.x(), offProxy.y(), mods, 0);
        QVERIFY(f.overlay.highlighted.isEmpty());
        QCOMPARE(geometrySpy.count(), 0);

        // Onto the second cell: the REAL second zone lights and its full-size
        // geometry is previewed.
        f.adaptor->updateDragCursor(windowId, inCell1.x(), inCell1.y(), mods, 0);
        QCOMPARE(f.overlay.highlighted, QStringList{zone1});
        QCOMPARE(geometrySpy.count(), 1);
        const QRect previewed(geometrySpy.at(0).at(1).toInt(), geometrySpy.at(0).at(2).toInt(),
                              geometrySpy.at(0).at(3).toInt(), geometrySpy.at(0).at(4).toInt());
        QVERIFY2(previewed.width() > mini.width(), "preview carried the miniature's size, not the real zone's");
        QVERIFY(previewed.contains(screenGeom.center()));

        // Into the gutter: inside the miniature but over no cell clears the
        // target.
        f.adaptor->updateDragCursor(windowId, inGutter.x(), inGutter.y(), mods, 0);
        QCOMPARE(f.overlay.clears, 1);
        QCOMPARE(f.overlay.highlighted.size(), 1);

        // Onto the third cell and release there: the drop snaps to the third
        // zone at the zone's own geometry.
        f.adaptor->updateDragCursor(windowId, inCell2.x(), inCell2.y(), mods, 0);
        QCOMPARE(f.overlay.highlighted, (QStringList{zone1, zone2}));
        QCOMPARE(geometrySpy.count(), 2);
        const PhosphorProtocol::DragOutcome outcome =
            f.adaptor->endDrag(windowId, inCell2.x(), inCell2.y(), mods, 0, false);
        QCOMPARE(outcome.action, PhosphorProtocol::DragOutcome::ApplySnap);
        QCOMPARE(outcome.zoneId, zone2);
        const QRect dropped(outcome.x, outcome.y, outcome.width, outcome.height);
        QVERIFY2(dropped.width() > mini.width(), "drop committed the miniature's rect, not the zone's");
        QVERIFY(dropped.right() > screenGeom.x() + screenGeom.width() * 2 / 3);
        QCOMPARE(f.wta->getZoneForWindow(windowId), zone2);
    }

    void adaptor_unregisteredProxyIsInert()
    {
        Fixture f;
        QVERIFY(f.screen);
        const QRect screenGeom = f.screen->geometry();
        const QString zone0 = f.layout->zones().at(0)->id().toString();
        f.adaptor->registerDropProxy(f.screenId, proxyJson(QRect(20, 20, 100, 90), {{zone0, QRect(20, 20, 100, 90)}}));
        f.adaptor->unregisterDropProxy(f.screenId);

        const QString windowId = QStringLiteral("app|w1");
        const QPoint inCell = screenGeom.topLeft() + QPoint(60, 60);
        f.adaptor->beginDrag(windowId, screenGeom.x() + 400, screenGeom.y() + 300, 300, 200, f.screenId, 0);
        f.adaptor->updateDragCursor(windowId, inCell.x(), inCell.y(), static_cast<int>(Qt::ControlModifier), 0);
        QVERIFY(f.overlay.highlighted.isEmpty());
        const PhosphorProtocol::DragOutcome outcome =
            f.adaptor->endDrag(windowId, inCell.x(), inCell.y(), static_cast<int>(Qt::ControlModifier), 0, false);
        QVERIFY(outcome.action != PhosphorProtocol::DragOutcome::ApplySnap);
        QVERIFY(outcome.zoneId.isEmpty());
    }

private:
    /// A snap-only drag adaptor on the offscreen platform's screen: no
    /// engines, no screen manager (the adaptor falls back to the QScreen
    /// under the cursor), a three-zone active layout, a stub detector that
    /// never finds a zone, and Ctrl as the activation trigger.
    struct Fixture
    {
        Fixture()
        {
            layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
            settings.setSnappingEnabled(true);
            settings.setToggleActivation(false);
            settings.setDragActivationTriggers(
                {QVariantMap{{ConfigDefaults::triggerModifierField(), static_cast<int>(DragModifier::Ctrl)},
                             {ConfigDefaults::triggerMouseButtonField(), 0}}});
            settings.setZoneSpanEnabled(false);

            wta = new WindowTrackingAdaptor(layoutManager, &detector, nullptr, &settings, nullptr, nullptr, &parent);
            snapEngine = new PhosphorSnapEngine::SnapEngine(layoutManager, wta->service(), &detector, nullptr, nullptr);
            snapEngine->setEngineSettings(&settings);
            wta->service()->setSnapState(snapEngine->snapState());
            wta->service()->setSnapEngine(snapEngine);
            wta->setEngines(snapEngine, nullptr, nullptr);
            adaptor = new WindowDragAdaptor(&overlay, &detector, layoutManager, nullptr, &settings, wta, &parent);

            layout = createTestLayout(3, layoutManager);
            layoutManager->addLayout(layout);
            layoutManager->setActiveLayout(layout);

            screen = QGuiApplication::primaryScreen();
            if (screen) {
                screenId = PhosphorScreens::ScreenIdentity::identifierFor(screen);
            }
        }

        ~Fixture()
        {
            wta->service()->setSnapState(nullptr);
            wta->service()->setSnapEngine(nullptr);
            delete snapEngine;
            delete layoutManager;
        }

        IsolatedConfigGuard guard;
        QObject parent;
        RecordingOverlay overlay;
        StubZoneDetector detector;
        StubSettings settings;
        PhosphorZones::LayoutRegistry* layoutManager = nullptr;
        PhosphorZones::Layout* layout = nullptr;
        WindowTrackingAdaptor* wta = nullptr; // parent-owned
        PhosphorSnapEngine::SnapEngine* snapEngine = nullptr;
        WindowDragAdaptor* adaptor = nullptr; // parent-owned
        QScreen* screen = nullptr;
        QString screenId;
    };
};

QTEST_MAIN(TestWindowDragDropProxy)
#include "test_windowdrag_drop_proxy.moc"

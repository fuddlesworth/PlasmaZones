// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The workspace overview's daemon side: the model builder over fake engine
// read surfaces (three screens, mixed modes, a subdivided output, sticky and
// untracked windows), the controller's streaming gate and generation
// stamping, and the adaptor's open-state ownership.

#include "daemon/controllers/overviewcontroller.h"
#include "daemon/controllers/overviewmodelbuilder.h"
#include "dbus/overviewadaptor.h"

#include <PhosphorEngine/IOverviewModelSource.h>
#include <PhosphorWorkspaces/WorkspaceMap.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

using namespace PlasmaZones;
using PhosphorEngine::OverviewStripColumn;
using PhosphorEngine::OverviewStripEntry;
using PhosphorEngine::OverviewStripTile;
using PhosphorEngine::OverviewWindowEntry;
using PhosphorEngine::PlacementStateKey;
using PhosphorWorkspaces::WorkspaceEntry;
using PhosphorWorkspaces::WorkspaceMap;

namespace OverviewTest {

const QString kS1 = QStringLiteral("S1");
const QString kS2 = QStringLiteral("S2");
const QString kS3 = QStringLiteral("S3");
const QString kD1 = QStringLiteral("{d1}");
const QString kD2 = QStringLiteral("{d2}");
const QString kD3 = QStringLiteral("{d3}");
const QString kD4 = QStringLiteral("{d4}");

/// A scripted read surface: answers per exact key, nullopt elsewhere, and
/// counts calls so a test can prove the builder asked the right engine.
class FakeSource : public PhosphorEngine::IOverviewModelSource
{
public:
    QHash<PlacementStateKey, QList<OverviewWindowEntry>> windows;
    QHash<PlacementStateKey, OverviewStripEntry> strips;
    mutable int calls = 0;

    std::optional<QList<OverviewWindowEntry>> overviewWindowsFor(const PlacementStateKey& key) const override
    {
        ++calls;
        const auto it = windows.constFind(key);
        if (it == windows.constEnd()) {
            return std::nullopt;
        }
        return it.value();
    }
    std::optional<OverviewStripEntry> overviewStripFor(const PlacementStateKey& key) const override
    {
        const auto it = strips.constFind(key);
        if (it == strips.constEnd()) {
            return std::nullopt;
        }
        return it.value();
    }
};

OverviewWindowEntry entry(const QString& id, const QRect& rect, bool floating = false, int column = -1, int tile = -1)
{
    OverviewWindowEntry e;
    e.windowId = id;
    e.rect = rect;
    e.floating = floating;
    e.column = column;
    e.tile = tile;
    return e;
}

OverviewTrackedWindow tracked(const QString& id, const QString& screen, int desktop, const QRect& frame)
{
    OverviewTrackedWindow w;
    w.id = id;
    w.screenId = screen;
    w.desktop = desktop;
    w.frame = frame;
    return w;
}

QJsonObject workspaceOf(const QJsonObject& model, const QString& screen, const QString& desktopId)
{
    return model.value(QLatin1String("screens"))
        .toObject()
        .value(screen)
        .toObject()
        .value(QLatin1String("workspaces"))
        .toObject()
        .value(desktopId)
        .toObject();
}

QJsonObject windowRow(const QJsonObject& workspace, const QString& id)
{
    for (const QJsonValue& v : workspace.value(QLatin1String("windows")).toArray()) {
        if (v.toObject().value(QLatin1String("id")).toString() == id) {
            return v.toObject();
        }
    }
    return {};
}

QRect rectOf(const QJsonObject& row)
{
    const QJsonObject r = row.value(QLatin1String("rect")).toObject();
    return QRect(r.value(QLatin1String("x")).toInt(), r.value(QLatin1String("y")).toInt(),
                 r.value(QLatin1String("w")).toInt(), r.value(QLatin1String("h")).toInt());
}

} // namespace OverviewTest

using namespace OverviewTest;

class TestOverviewController : public QObject
{
    Q_OBJECT

private:
    /// Three screens: S1 (two workspaces, snapping then scrolling), S2 (one
    /// tiling workspace, subdivided into two virtual screens), S3 (known but
    /// empty). Desktop ints follow the ids.
    struct Fixture
    {
        WorkspaceMap map;
        FakeSource snap;
        FakeSource tile;
        FakeSource scroll;
        OverviewModelBuilder::Inputs in;

        Fixture()
        {
            map.setScreenOrder({kS1, kS2, kS3});
            map.insert(kS1, 0, WorkspaceEntry{kD1, QString(), QString()});
            map.insert(kS1, 1, WorkspaceEntry{kD2, QStringLiteral("mail"), QString()});
            map.insert(kS2, 0, WorkspaceEntry{kD3, QString(), QString()});
            in.map = &map;
            in.activity = QStringLiteral("act");
            in.currentDesktopByScreen = {{kS1, 1}, {kS2, 3}};
            in.desktopIndexOf = [](const QString& id) {
                if (id == kD1) {
                    return 1;
                }
                if (id == kD2) {
                    return 2;
                }
                if (id == kD3) {
                    return 3;
                }
                return 0;
            };
            in.screenGeometry = [](const QString& screen) {
                if (screen == kS1) {
                    return QRect(0, 0, 1000, 500);
                }
                if (screen == kS2) {
                    return QRect(1000, 0, 800, 600);
                }
                return QRect();
            };
            in.virtualScreensFor = [](const QString& screen) {
                if (screen == kS2) {
                    return QStringList{QStringLiteral("S2/vs:0"), QStringLiteral("S2/vs:1")};
                }
                return QStringList();
            };
            in.modeFor = [](const QString& screen, int desktop) {
                if (screen == kS1) {
                    return desktop == 1 ? OverviewMode::Snapping : OverviewMode::Scrolling;
                }
                if (screen == kS2) {
                    return OverviewMode::Tiling;
                }
                return OverviewMode::None;
            };
            in.snapping = &snap;
            in.tiling = &tile;
            in.scrolling = &scroll;
        }
    };

private Q_SLOTS:
    void mixedModesRouteEachWorkspaceToItsEngine()
    {
        Fixture f;
        f.snap.windows[{kS1, 1, f.in.activity}] = {entry(QStringLiteral("a"), QRect(10, 20, 300, 200))};
        f.scroll.windows[{kS1, 2, f.in.activity}] = {entry(QStringLiteral("b"), QRect(0, 0, 500, 500), false, 0, 0),
                                                     entry(QStringLiteral("c"), QRect(1200, 0, 500, 500), false, 1, 0)};
        f.in.windows = {tracked(QStringLiteral("a"), kS1, 1, QRect(11, 21, 300, 200)),
                        tracked(QStringLiteral("b"), kS1, 2, QRect()), tracked(QStringLiteral("c"), kS1, 2, QRect())};

        const QJsonObject model = OverviewModelBuilder::build(f.in);
        QCOMPARE(model.value(QLatin1String("v")).toInt(), 1);
        QCOMPARE(model.value(QLatin1String("activity")).toString(), f.in.activity);

        const QJsonObject ws1 = workspaceOf(model, kS1, kD1);
        QCOMPARE(ws1.value(QLatin1String("mode")).toString(), QStringLiteral("snapping"));
        QVERIFY(ws1.value(QLatin1String("current")).toBool());
        QCOMPARE(ws1.value(QLatin1String("sliceIndex")).toInt(), 0);
        // The engine rect wins over the tracked frame.
        QCOMPARE(rectOf(windowRow(ws1, QStringLiteral("a"))), QRect(10, 20, 300, 200));

        const QJsonObject ws2 = workspaceOf(model, kS1, kD2);
        QCOMPARE(ws2.value(QLatin1String("mode")).toString(), QStringLiteral("scrolling"));
        QCOMPARE(ws2.value(QLatin1String("name")).toString(), QStringLiteral("mail"));
        QVERIFY(!ws2.value(QLatin1String("current")).toBool());
        QCOMPARE(ws2.value(QLatin1String("windows")).toArray().size(), 2);
        const QJsonObject c = windowRow(ws2, QStringLiteral("c"));
        QCOMPARE(c.value(QLatin1String("column")).toInt(), 1);
        QCOMPARE(c.value(QLatin1String("tile")).toInt(), 0);
        // A parked column keeps its off-screen rect: that IS its position.
        QCOMPARE(rectOf(c), QRect(1200, 0, 500, 500));

        // Each engine was asked only for its own workspaces' keys.
        QCOMPARE(f.snap.calls, 1);
        QCOMPARE(f.scroll.calls, 1);
        // S3 owns no workspace yet: present with an empty workspace set.
        const QJsonObject s3 = model.value(QLatin1String("screens")).toObject().value(kS3).toObject();
        QVERIFY(s3.contains(QLatin1String("workspaces")));
        QVERIFY(s3.value(QLatin1String("workspaces")).toObject().isEmpty());
    }

    void virtualScreensOffsetIntoThePhysicalOutput()
    {
        Fixture f;
        // The tiling engine keys S2's two virtual screens separately; both
        // report global rects, and the output starts at x=1000.
        f.tile.windows[{QStringLiteral("S2/vs:0"), 3, f.in.activity}] = {
            entry(QStringLiteral("l"), QRect(1000, 0, 400, 600))};
        f.tile.windows[{QStringLiteral("S2/vs:1"), 3, f.in.activity}] = {
            entry(QStringLiteral("r"), QRect(1400, 0, 400, 600))};
        f.in.windows = {tracked(QStringLiteral("l"), kS2, 3, QRect()), tracked(QStringLiteral("r"), kS2, 3, QRect())};

        const QJsonObject ws = workspaceOf(OverviewModelBuilder::build(f.in), kS2, kD3);
        QCOMPARE(ws.value(QLatin1String("mode")).toString(), QStringLiteral("tiling"));
        QCOMPARE(rectOf(windowRow(ws, QStringLiteral("l"))), QRect(0, 0, 400, 600));
        QCOMPARE(rectOf(windowRow(ws, QStringLiteral("r"))), QRect(400, 0, 400, 600));
        QCOMPARE(f.tile.calls, 2);
        const QJsonObject size = OverviewModelBuilder::build(f.in)
                                     .value(QLatin1String("screens"))
                                     .toObject()
                                     .value(kS2)
                                     .toObject()
                                     .value(QLatin1String("logicalSize"))
                                     .toObject();
        QCOMPARE(size.value(QLatin1String("w")).toInt(), 800);
        QCOMPARE(size.value(QLatin1String("h")).toInt(), 600);
    }

    void engineSilenceFallsBackToTrackedGeometry()
    {
        Fixture f;
        // No engine state for S1 desktop 1 at all (never visited): the
        // tracked frame is reported, non-floating, mode still named.
        f.in.windows = {tracked(QStringLiteral("a"), kS1, 1, QRect(50, 60, 200, 100))};
        const QJsonObject ws = workspaceOf(OverviewModelBuilder::build(f.in), kS1, kD1);
        QCOMPARE(ws.value(QLatin1String("mode")).toString(), QStringLiteral("snapping"));
        const QJsonObject a = windowRow(ws, QStringLiteral("a"));
        QCOMPARE(rectOf(a), QRect(50, 60, 200, 100));
        QVERIFY(!a.value(QLatin1String("floating")).toBool());
    }

    void engineListedButUntrackedWindowIsDropped()
    {
        Fixture f;
        f.snap.windows[{kS1, 1, f.in.activity}] = {entry(QStringLiteral("ghost"), QRect(0, 0, 10, 10))};
        const QJsonObject ws = workspaceOf(OverviewModelBuilder::build(f.in), kS1, kD1);
        QVERIFY(ws.value(QLatin1String("windows")).toArray().isEmpty());
    }

    void engineNullRectFallsBackToTrackedFrameAndNoneModeRefusesEngines()
    {
        Fixture f;
        f.snap.windows[{kS1, 1, f.in.activity}] = {entry(QStringLiteral("a"), QRect(), true)};
        f.in.windows = {tracked(QStringLiteral("a"), kS1, 1, QRect(5, 5, 50, 50))};
        QJsonObject ws = workspaceOf(OverviewModelBuilder::build(f.in), kS1, kD1);
        QJsonObject a = windowRow(ws, QStringLiteral("a"));
        QCOMPARE(rectOf(a), QRect(5, 5, 50, 50));
        QVERIFY(a.value(QLatin1String("floating")).toBool());

        // A disabled context reports mode none and asks no engine.
        f.in.modeFor = [](const QString&, int) {
            return OverviewMode::None;
        };
        f.snap.calls = 0;
        ws = workspaceOf(OverviewModelBuilder::build(f.in), kS1, kD1);
        QCOMPARE(ws.value(QLatin1String("mode")).toString(), QStringLiteral("none"));
        QCOMPARE(f.snap.calls, 0);
        QCOMPARE(rectOf(windowRow(ws, QStringLiteral("a"))), QRect(5, 5, 50, 50));
    }

    void stickyWindowsAppearOnceOnTheCurrentWorkspace()
    {
        Fixture f;
        OverviewTrackedWindow s = tracked(QStringLiteral("s"), kS1, 0, QRect(1, 1, 9, 9));
        s.sticky = true;
        f.in.windows = {s};
        const QJsonObject model = OverviewModelBuilder::build(f.in);
        const QJsonObject row = windowRow(workspaceOf(model, kS1, kD1), QStringLiteral("s"));
        QVERIFY(row.value(QLatin1String("sticky")).toBool());
        QVERIFY(windowRow(workspaceOf(model, kS1, kD2), QStringLiteral("s")).isEmpty());
        // Even when an engine also names it on the non-current workspace it
        // is not duplicated there: the engine's entry is dropped for a
        // window the daemon holds sticky. (Engines never key a sticky window
        // to a desktop, so this is the belt on the builder's own rule.)
        f.scroll.windows[{kS1, 2, f.in.activity}] = {entry(QStringLiteral("s"), QRect(0, 0, 9, 9))};
        const QJsonObject again = OverviewModelBuilder::build(f.in);
        QVERIFY(windowRow(workspaceOf(again, kS1, kD2), QStringLiteral("s")).isEmpty());
        QVERIFY(!windowRow(workspaceOf(again, kS1, kD1), QStringLiteral("s")).isEmpty());
    }

    void omittedTypesAndForeignScreensNeverAppear()
    {
        Fixture f;
        OverviewTrackedWindow dock = tracked(QStringLiteral("dock"), kS1, 1, QRect(0, 0, 1000, 30));
        dock.omitted = true;
        f.in.windows = {dock, tracked(QStringLiteral("x"), QStringLiteral("S9"), 1, QRect(0, 0, 1, 1))};
        const QJsonObject model = OverviewModelBuilder::build(f.in);
        QVERIFY(workspaceOf(model, kS1, kD1).value(QLatin1String("windows")).toArray().isEmpty());
        QVERIFY(!model.value(QLatin1String("screens")).toObject().contains(QStringLiteral("S9")));
    }

    void scrollingStripIsTranslatedAndEmittedOnce()
    {
        Fixture f;
        OverviewStripEntry strip;
        strip.viewOffset = 300;
        OverviewStripColumn col;
        col.rect = QRect(100, 0, 400, 500);
        col.tabbed = true;
        col.activeTab = 1;
        col.tiles = {OverviewStripTile{QStringLiteral("b"), QRect(100, 0, 400, 500)},
                     OverviewStripTile{QStringLiteral("c"), QRect(100, 0, 400, 500)}};
        strip.columns = {col};
        // The strip lives on S2's output (origin 1000) to prove translation.
        f.map.insert(kS2, 1, WorkspaceEntry{kD4, QString(), QString()});
        f.in.desktopIndexOf = [](const QString& id) {
            return id == kD4 ? 4 : (id == kD3 ? 3 : (id == kD2 ? 2 : (id == kD1 ? 1 : 0)));
        };
        f.in.modeFor = [](const QString& screen, int desktop) {
            return (screen == kS2 && desktop == 4) ? OverviewMode::Scrolling : OverviewMode::None;
        };
        f.scroll.strips[{QStringLiteral("S2/vs:0"), 4, f.in.activity}] = strip;
        f.scroll.strips[{QStringLiteral("S2/vs:1"), 4, f.in.activity}] = strip;
        const QJsonObject ws = workspaceOf(OverviewModelBuilder::build(f.in), kS2, kD4);
        QVERIFY(ws.contains(QLatin1String("strip")));
        const QJsonObject s = ws.value(QLatin1String("strip")).toObject();
        QCOMPARE(s.value(QLatin1String("viewOffset")).toInt(), 300);
        const QJsonArray columns = s.value(QLatin1String("columns")).toArray();
        QCOMPARE(columns.size(), 1);
        const QJsonObject c0 = columns.at(0).toObject();
        QVERIFY(c0.value(QLatin1String("tabbed")).toBool());
        QCOMPARE(c0.value(QLatin1String("activeTab")).toInt(), 1);
        QCOMPARE(rectOf(c0), QRect(-900, 0, 400, 500));
        QCOMPARE(c0.value(QLatin1String("tiles")).toArray().size(), 2);
        // A snapping workspace never carries a strip.
        QVERIFY(!workspaceOf(OverviewModelBuilder::build(f.in), kS1, kD1).contains(QLatin1String("strip")));
    }

    void controllerNeverPublishesWhileClosedAndStampsGenerations()
    {
        FakeSource snap;
        OverviewController::Sources sources;
        sources.snapping = &snap;
        OverviewController controller(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, sources);
        QSignalSpy published(&controller, &OverviewController::modelPublished);
        QSignalSpy opened(&controller, &OverviewController::openChanged);

        QVERIFY(controller.modelJson().isEmpty());
        controller.scheduleRebuild();
        QTest::qWait(5);
        QCOMPARE(published.count(), 0);
        QCOMPARE(controller.generation(), 0u);

        controller.setOpen(true);
        QCOMPARE(opened.count(), 1);
        // Synchronous first build so the replay that follows the open on the
        // same round-trip is answered.
        QCOMPARE(published.count(), 1);
        QCOMPARE(controller.generation(), 1u);
        const QJsonObject first = QJsonDocument::fromJson(controller.modelJson().toUtf8()).object();
        QCOMPARE(first.value(QLatin1String("generation")).toInt(), 1);
        QVERIFY(first.contains(QLatin1String("workspaceMapGeneration")));

        // Same inputs: the change gate swallows the rebuild.
        controller.scheduleRebuild();
        QTest::qWait(5);
        QCOMPARE(published.count(), 1);
        QCOMPARE(controller.generation(), 1u);

        controller.setOpen(false);
        QVERIFY(controller.modelJson().isEmpty());
        controller.scheduleRebuild();
        QTest::qWait(5);
        QCOMPARE(published.count(), 1);
        // A reopen continues the counter rather than restarting it, so a
        // receiver that kept the old value still orders the new payload after.
        controller.setOpen(true);
        QCOMPARE(published.count(), 2);
        QCOMPARE(controller.generation(), 2u);
    }

    void adaptorGateFollowsTheControllerAndDetachCloses()
    {
        QObject host;
        OverviewAdaptor adaptor(&host);
        QSignalSpy state(&adaptor, &OverviewAdaptor::overviewStateChanged);
        QSignalSpy closeRequested(&adaptor, &OverviewAdaptor::closeOverviewRequested);
        QSignalSpy modelChanged(&adaptor, &OverviewAdaptor::overviewModelChanged);

        // No controller: the gate refuses and the replay is empty.
        adaptor.setOverviewOpen(true);
        QVERIFY(!adaptor.isOpen());
        QVERIFY(adaptor.overviewModel().isEmpty());
        QCOMPARE(state.count(), 0);

        OverviewController::Sources sources;
        OverviewController controller(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, sources);
        adaptor.setController(&controller);
        adaptor.setOverviewOpen(true);
        QVERIFY(adaptor.isOpen());
        QVERIFY(controller.isOpen());
        QCOMPARE(state.count(), 1);
        QVERIFY(!adaptor.overviewModel().isEmpty());
        QCOMPARE(modelChanged.count(), 1);

        // A "not running" report from the owner closes the gate.
        adaptor.reportOverviewState(false);
        QVERIFY(!adaptor.isOpen());
        QVERIFY(!controller.isOpen());
        QVERIFY(adaptor.overviewModel().isEmpty());

        // Detaching under an open overview closes it and asks the effect to
        // let go.
        adaptor.setOverviewOpen(true);
        QVERIFY(adaptor.isOpen());
        adaptor.setController(nullptr);
        QVERIFY(!adaptor.isOpen());
        QCOMPARE(closeRequested.count(), 1);
        QVERIFY(!controller.isOpen());
        // Nothing streams after the detach.
        controller.setOpen(true);
        QCOMPARE(modelChanged.count(), 2);
    }
};

QTEST_GUILESS_MAIN(TestOverviewController)
#include "test_overview_controller.moc"

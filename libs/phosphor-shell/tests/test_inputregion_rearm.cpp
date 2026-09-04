// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// The one geometry write a panel may make after materialization: a change
// to `interactiveThickness` has to move the surface's input region, and
// nothing else.
//
// A bar that grows a popout out of itself paints into its shadow strip and
// needs clicks to land there while the popout is open. The mask is applied
// once at materialization and then re-applied from a connection to
// interactiveThicknessChanged. Deleting that connection left the pocket
// dead with the whole suite green, because nothing reached
// materializePanels at all: the property setter's own test pins the
// property, not that a change re-applies the mask.
//
// Built against the mock layer-shell transport, so no compositor is needed.

#include <PhosphorLayer/SurfaceFactory.h>
#include <PhosphorShell/PanelWindow.h>
#include <PhosphorShell/ShellEngine.h>

#include "../../phosphor-layer/tests/mocks/mockscreenprovider.h"
#include "../../phosphor-layer/tests/mocks/mocktransport.h"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QRegion>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QWindow>

using namespace PhosphorShell;

namespace {

constexpr auto kShellQml = R"(
import QtQuick
import Phosphor.Shell

Item {
    PanelWindow {
        objectName: "bar"
        edge: PanelWindow.Top
        thickness: 30
        shadowSize: 40
    }
}
)";

// The panel is detached from the QML root and reparented onto its surface's
// wrapper window, so it is reachable through the window list rather than
// through the engine.
PanelWindow* findMaterializedPanel()
{
    const auto windows = QGuiApplication::allWindows();
    for (auto* w : windows) {
        if (auto* panel = w->findChild<PanelWindow*>(QStringLiteral("bar"))) {
            return panel;
        }
    }
    return nullptr;
}

} // namespace

class TestInputRegionRearm : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aChangeToInteractiveThicknessReappliesTheMask();
};

void TestInputRegionRearm::aChangeToInteractiveThicknessReappliesTheMask()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("shell.qml"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(kShellQml) > 0);
    file.close();

    PhosphorLayer::Testing::MockTransport transport;
    PhosphorLayer::Testing::MockScreenProvider screens;
    PhosphorLayer::SurfaceFactory factory(PhosphorLayer::Testing::makeDeps(&transport, &screens));

    ShellEngine::Deps deps;
    deps.surfaceFactory = &factory;
    deps.screenProvider = &screens;
    ShellEngine engine(deps);
    QVERIFY(engine.load(QUrl::fromLocalFile(path)));

    PanelWindow* panel = findMaterializedPanel();
    QVERIFY2(panel, "the panel materialized onto a surface window");
    auto* window = qobject_cast<QQuickWindow*>(panel->window());
    QVERIFY(window);

    // The band applied at materialization is `thickness` deep, because the
    // panel has not opted into a deeper interactive strip yet.
    const QRegion atMaterialization = window->mask();
    QVERIFY2(!atMaterialization.isEmpty(), "a mask was applied when the panel was materialized");
    QCOMPARE(atMaterialization.boundingRect().height(), 30);

    // The pocket opens: the bar asks for clicks throughout its shadow strip.
    panel->setInteractiveThickness(70);

    const QRegion afterOpening = window->mask();
    QVERIFY2(afterOpening != atMaterialization, "the change re-applied the input region");
    QCOMPARE(afterOpening.boundingRect().height(), 70);

    // And back, so the strip stops swallowing clicks meant for whatever is
    // tiled under the bar once the popout closes.
    panel->setInteractiveThickness(0);
    QCOMPARE(window->mask().boundingRect().height(), 30);
}

QTEST_MAIN(TestInputRegionRearm)
#include "test_inputregion_rearm.moc"

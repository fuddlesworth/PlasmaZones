// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// ShellEngine's PersistentProperties scan, for state declared INSIDE a
// PanelWindow rather than beside it.
//
// materializePanels detaches every PanelWindow from the QML root and hands
// it to a Surface, so a findChildren from the root alone cannot see into a
// panel. That made a PersistentProperties nested in a bar invisible to all
// three scans: it was never registered as a ShellGlobal singleton, and its
// state was dropped on every hot reload. The panel-side leg is what these
// cases pin.
//
// The shell is built against a mock layer-shell transport, so no compositor
// is involved and the panels materialize under the offscreen QPA.

#include <PhosphorLayer/SurfaceFactory.h>
#include <PhosphorShell/ShellEngine.h>

#include "../../phosphor-layer/tests/mocks/mockscreenprovider.h"
#include "../../phosphor-layer/tests/mocks/mocktransport.h"

#include <QDir>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace PhosphorShell;

namespace {

// A root Item with the PersistentProperties nested inside a PanelWindow,
// which is the arrangement a real bar uses: the state that has to survive a
// reload belongs to the bar, so it is declared in the bar.
constexpr auto kShellQml = R"(
import QtQuick
import Phosphor.Shell

Item {
    PanelWindow {
        edge: PanelWindow.Top
        thickness: 30

        PersistentProperties {
            reloadId: "nested-state"
            property int counter: 0
        }
    }
}
)";

} // namespace

class TestPersistentPropertiesScan : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void nestedStateIsRegisteredAsASingleton();
    void nestedStateSurvivesAReload();
    void aRootPanelWithNoScreenFailsTheLoad();

private:
    [[nodiscard]] static QObject* singletonFor(ShellEngine& engine, const QString& reloadId);
};

QObject* TestPersistentPropertiesScan::singletonFor(ShellEngine& engine, const QString& reloadId)
{
    auto* qml = engine.engine();
    if (!qml) {
        return nullptr;
    }
    // ShellGlobal reaches QML as a context property rather than a QML
    // singleton, so this is the same handle a shell author would use.
    auto* shellGlobal = qml->rootContext()->contextProperty(QStringLiteral("PhosphorShell")).value<QObject*>();
    if (!shellGlobal) {
        return nullptr;
    }
    QObject* found = nullptr;
    QMetaObject::invokeMethod(shellGlobal, "singleton", Q_RETURN_ARG(QObject*, found), Q_ARG(QString, reloadId));
    return found;
}

void TestPersistentPropertiesScan::nestedStateIsRegisteredAsASingleton()
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

    QVERIFY2(engine.load(QUrl::fromLocalFile(path)), "the shell builds against the mock transport");

    // The whole point: this resolves only if the scan reaches inside the
    // materialized panel. Before the fix it was null, and every QML lookup
    // of PhosphorShell.singleton("nested-state") returned nothing.
    QObject* state = singletonFor(engine, QStringLiteral("nested-state"));
    QVERIFY2(state, "PersistentProperties nested in a PanelWindow is registered as a singleton");
    QCOMPARE(state->property("counter").toInt(), 0);
}

void TestPersistentPropertiesScan::nestedStateSurvivesAReload()
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

    QObject* before = singletonFor(engine, QStringLiteral("nested-state"));
    QVERIFY(before);
    before->setProperty("counter", 7);

    QSignalSpy reloaded(&engine, &ShellEngine::reloaded);
    // Rewrite with a trailing comment so the content differs and the
    // watcher has something to notice. The reload is debounced, so the
    // wait has to outlast the debounce window.
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(kShellQml) > 0);
    QVERIFY(file.write("// touched\n") > 0);
    file.close();
    QVERIFY2(reloaded.wait(5000), "the file watcher rebuilt the shell");

    QObject* after = singletonFor(engine, QStringLiteral("nested-state"));
    QVERIFY2(after, "the rebuilt generation registers the nested state again");
    QVERIFY2(after != before, "the reload really did rebuild the object graph");
    QCOMPARE(after->property("counter").toInt(), 7);
}

// A root PanelWindow that cannot resolve a screen used to be skipped like
// any other panel, and load() still returned true. The QML root IS that
// panel, so the embedder was told the shell was up while nothing existed on
// screen.
void TestPersistentPropertiesScan::aRootPanelWithNoScreenFailsTheLoad()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("shell.qml"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("import QtQuick\n"
                       "import Phosphor.Shell\n"
                       "PanelWindow {\n"
                       "    edge: PanelWindow.Top\n"
                       "    thickness: 30\n"
                       "}\n")
            > 0);
    file.close();

    PhosphorLayer::Testing::MockTransport transport;
    PhosphorLayer::Testing::MockScreenProvider screens;
    // No outputs at all, which is what a session losing its last display
    // looks like from here.
    screens.setScreens({});
    screens.setFocused(nullptr);
    PhosphorLayer::SurfaceFactory factory(PhosphorLayer::Testing::makeDeps(&transport, &screens));

    ShellEngine::Deps deps;
    deps.surfaceFactory = &factory;
    deps.screenProvider = &screens;
    ShellEngine engine(deps);

    QSignalSpy failed(&engine, &ShellEngine::failed);
    QVERIFY2(!engine.load(QUrl::fromLocalFile(path)), "a root panel with nowhere to go is a failed load");
    QCOMPARE(failed.count(), 1);
    // The failed-build postcondition: everything torn down and engine() null,
    // which is how a caller tells this apart from a rejected call.
    QVERIFY(!engine.engine());
}

QTEST_MAIN(TestPersistentPropertiesScan)
#include "test_persistentproperties_scan.moc"

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every QML file the shell ships must COMPILE.
//
// Three times in this branch a single assignment to a property that does
// not exist — Accessible.value, Accessible.onShowMenuAction, detailPanelId
// on SliderTile — took a whole file out. That is not a warning in QML: the
// file fails to compile, the factory returns null, and the widget or tile
// simply is not there. The media chip was missing for weeks that way,
// because a chip that hides itself when nothing is playing and a chip that
// never mounted look identical.
//
// qmllint does report it. It was missed anyway, because the output for a
// shell QML file is dominated by unresolved Phosphor.Service.* imports —
// those modules are registered at runtime by the shell process, so a
// static lint cannot see them — and the one real line reads as more of the
// same. This test has the modules linked in and the service types
// registered, so an import resolves and the only thing left to fail on is
// the file itself.
//
// It LOADS each file rather than instantiating it. Loading is where the
// QML is compiled, so it is where a bad property assignment is caught, and
// it needs no window, no D-Bus and no service daemon. What a component
// costs to actually build is test_panel_open_cost's job.

#include "shell/ControlCenterController.h"

#include <PhosphorServiceBluetooth/QmlRegistration.h>
#include <PhosphorServiceBrightness/QmlRegistration.h>
#include <PhosphorServiceIconTheme/QmlRegistration.h>
#include <PhosphorServiceIdle/QmlRegistration.h>
#include <PhosphorServiceMpris/QmlRegistration.h>
#include <PhosphorServiceNetwork/QmlRegistration.h>
#include <PhosphorServiceNotifications/QmlRegistration.h>
#include <PhosphorServicePipeWire/QmlRegistration.h>
#include <PhosphorServicePolkit/QmlRegistration.h>
#include <PhosphorServiceSession/QmlRegistration.h>
#include <PhosphorServiceSni/QmlRegistration.h>
#include <PhosphorServiceUPower/QmlRegistration.h>
#include <PhosphorShell/QmlRegistration.h>
#include <PhosphorShell/SystemStats.h>
#include <PhosphorTheme/AppearanceStore.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QUrl>

#include <memory>

class TestShellQmlCompiles : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void everyShippedFileCompiles_data();
    void everyShippedFileCompiles();
    void aBadPropertyAssignmentIsCaught();
    void statsPagesHaveWorkingLiveBindings();
    void quickSettingsForwardsAppearanceFromForeignContext();
    void detailPanelsForwardReturnFromForeignContext();

private:
    std::unique_ptr<QQmlEngine> m_engine;
};

namespace {
constexpr auto kModuleRoot = ":/qt/qml/Phosphor";

QQuickItem* namedItem(QQuickItem* root, const QString& name)
{
    if (root->objectName() == name)
        return root;
    for (auto* child : root->childItems()) {
        if (auto* found = namedItem(child, name))
            return found;
    }
    return nullptr;
}

/// The files that must be among those collected. Guard for the guard: the
/// enumeration below walks a resource tree, and a tree that stopped being
/// populated — a module dropped from the link line, a Qt change to the
/// qrc layout — would collect nothing and pass vacuously forever. These
/// three are the files whose real breakage prompted this test.
const QStringList& sentinels()
{
    static const QStringList files{
        QStringLiteral(":/qt/qml/Phosphor/Bar/Media.qml"),
        QStringLiteral(":/qt/qml/Phosphor/ControlCenter/Tile.qml"),
        QStringLiteral(":/qt/qml/Phosphor/ControlCenter/SliderTile.qml"),
    };
    return files;
}

/// A singleton is not loadable as an ordinary component: QQmlComponent
/// refuses a file carrying `pragma Singleton` with "is not a type", which
/// says nothing about whether it compiles. The engine resolves those
/// through the module's qmldir instead, and every file here that imports
/// one exercises them anyway.
bool isSingleton(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    return file.readAll().contains("pragma Singleton");
}

QStringList shippedFiles()
{
    QStringList paths;
    QDirIterator it(QLatin1String(kModuleRoot), QStringList{QStringLiteral("*.qml")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (!isSingleton(path)) {
            paths << path;
        }
    }
    paths.sort();
    return paths;
}
} // namespace

void TestShellQmlCompiles::initTestCase()
{
    // The same registrations src/shell/main.cpp makes. Without them every
    // widget that imports a service module fails for the WRONG reason and
    // the test says nothing about the file's own correctness. Clipboard and
    // Lock are left out: no file under Phosphor/ imports them, and linking a
    // service to register types nothing asks for would only slow the case
    // down. If a future file imports one, this test says so by name rather
    // than passing.
    //
    // PhosphorShell::registerQmlTypes() is the one that is easy to forget
    // and the reason several files here would otherwise fail for nothing:
    // PanelWindow, SystemClock and SystemUsage are C++ types the shell
    // registers into Phosphor.Shell at startup, not QML files in a module,
    // so the module being linked is not enough to resolve them.
    PhosphorShell::registerQmlTypes();
    PhosphorServiceSession::registerQmlTypes();
    PhosphorServiceSni::registerQmlTypes();
    PhosphorServiceIconTheme::registerQmlTypes();
    PhosphorServiceUPower::registerQmlTypes();
    PhosphorServiceMpris::registerQmlTypes();
    PhosphorServicePipeWire::registerQmlTypes();
    PhosphorServiceNetwork::registerQmlTypes();
    PhosphorServiceBluetooth::registerQmlTypes();
    PhosphorServiceBrightness::registerQmlTypes();
    PhosphorServiceNotifications::registerQmlTypes();
    PhosphorServicePolkit::registerQmlTypes();
    PhosphorServiceIdle::registerQmlTypes();

    m_engine = std::make_unique<QQmlEngine>();

    const QStringList files = shippedFiles();
    for (const QString& sentinel : sentinels()) {
        QVERIFY2(files.contains(sentinel),
                 qPrintable(QStringLiteral("%1 was not collected; the enumeration is not seeing the shipped "
                                           "modules and this test is vacuous")
                                .arg(sentinel)));
    }
}

void TestShellQmlCompiles::everyShippedFileCompiles_data()
{
    QTest::addColumn<QString>("path");

    // One row per file, so a failure names the file rather than aborting
    // the whole sweep at the first bad one.
    const QStringList files = shippedFiles();
    for (const QString& path : files) {
        QTest::newRow(qPrintable(path.mid(QString::fromLatin1(kModuleRoot).size() + 1))) << path;
    }
}

void TestShellQmlCompiles::everyShippedFileCompiles()
{
    QFETCH(QString, path);

    QQmlComponent component(m_engine.get(), QUrl(QStringLiteral("qrc") + path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
}

void TestShellQmlCompiles::aBadPropertyAssignmentIsCaught()
{
    // The exact shape of the three bugs this file exists for, so the
    // detection itself is proven rather than assumed. If QML ever demotes
    // this to a warning, this case fails and the sweep above stops being
    // load-bearing without anyone noticing.
    QQmlComponent component(m_engine.get());
    component.setData("import QtQuick\nItem { thisPropertyDoesNotExist: 1 }\n",
                      QUrl(QStringLiteral("qrc:/badassignment.qml")));

    QVERIFY(component.isError());
    QVERIFY2(component.errorString().contains(QLatin1String("non-existent property")),
             qPrintable(component.errorString()));
}

void TestShellQmlCompiles::statsPagesHaveWorkingLiveBindings()
{
    // Unlike the compilation sweep, exercise the async snapshot, delegate
    // scopes, adaptive layouts and keyboard navigation in the real panel.
    QSignalSpy warnings(m_engine.get(), &QQmlEngine::warnings);
    QQmlComponent component(m_engine.get());
    component.setData("import Phosphor.Bar\nStatsPanel { width: 452; height: 560 }\n",
                      QUrl(QStringLiteral("qrc:/stats-runtime.qml")));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto* panel = qobject_cast<QQuickItem*>(object.get());
    QVERIFY(panel);
    QQuickWindow window;
    window.resize(452, 560);
    panel->setParentItem(window.contentItem());
    window.show();
    auto* service = m_engine->singletonInstance<PhosphorShell::SystemStats*>(QStringLiteral("Phosphor.Shell"),
                                                                             QStringLiteral("SystemStats"));
    QVERIFY(service);
    QTRY_VERIFY(service->revision() > 1);
    auto* scroller = panel->findChild<QQuickItem*>(QStringLiteral("statsScroll"));
    QVERIFY(scroller);
    for (const auto& page : {QStringLiteral("cpu"), QStringLiteral("gpu"), QStringLiteral("memory"),
                             QStringLiteral("network"), QStringLiteral("storage"), QStringLiteral("customize")}) {
        QVERIFY(panel->setProperty("page", page));
        QTest::qWait(25);
        QVERIFY(scroller->height() > 100);
        QVERIFY(scroller->property("contentHeight").toReal() > 100);
        const auto bottom = scroller->mapToItem(panel, QPointF(0, scroller->height())).y();
        QVERIFY(bottom < panel->height()); // the footer remains reachable on short screens
        panel->forceActiveFocus();
        QTest::keyClick(&window, Qt::Key_Escape);
        QCOMPARE(panel->property("page").toString(), QStringLiteral("overview"));
    }
    auto* pause = panel->findChild<QObject*>(QStringLiteral("statsPause"));
    QVERIFY(pause);
    QVERIFY(QMetaObject::invokeMethod(pause, "clicked"));
    QVERIFY(service->paused());
    QVERIFY(QMetaObject::invokeMethod(pause, "clicked"));
    QVERIFY(!service->paused());
    auto* store = PhosphorTheme::AppearanceStore::create(nullptr, nullptr);
    QVERIFY(store->beginPreview());
    panel->setProperty("page", QStringLiteral("customize"));
    for (const auto& style : {QStringLiteral("meters"), QStringLiteral("numbers"), QStringLiteral("traces")}) {
        auto* button = namedItem(panel, QStringLiteral("statsStyle_") + style);
        QVERIFY(button);
        QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
        QCOMPARE(store->values().value(QStringLiteral("statsStyle")).toString(), style);
        QTest::qWait(25);
    }
    auto* memory = namedItem(panel, QStringLiteral("statsMetric_memory"));
    auto* network = namedItem(panel, QStringLiteral("statsMetric_network"));
    QVERIFY(memory);
    QVERIFY(network);
    QVERIFY(!network->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(memory, "clicked"));
    QCOMPARE(store->values().value(QStringLiteral("statsMetrics")).toList().size(), 2);
    QVERIFY(network->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(network, "clicked"));
    QVERIFY(store->values().value(QStringLiteral("statsMetrics")).toList().contains(QStringLiteral("network")));
    QCOMPARE(store->values().value(QStringLiteral("statsMetrics")).toList().size(), 3);
    store->endPreview();
    panel->setParentItem(nullptr);
    QStringList messages;
    for (const auto& warning : warnings) {
        for (const auto& error : qvariant_cast<QList<QQmlError>>(warning.first()))
            messages.append(error.toString());
    }
    QVERIFY2(messages.isEmpty(), qPrintable(messages.join(QLatin1Char('\n'))));
}

void TestShellQmlCompiles::quickSettingsForwardsAppearanceFromForeignContext()
{
    PhosphorShellApp::ControlCenterController controller(nullptr);
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("ControlCenterRegistry"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("NotificationRegistry"),
                                             QVariantMap{{QStringLiteral("doNotDisturb"), false},
                                                         {QStringLiteral("serverActive"), false},
                                                         {QStringLiteral("unreadCount"), 0}});
    engine.rootContext()->setContextProperty(QStringLiteral("QuickSettings"),
                                             QVariantMap{{QStringLiteral("nightLightEnabled"), false},
                                                         {QStringLiteral("nightLightAvailable"), false},
                                                         {QStringLiteral("powerProfile"), QStringLiteral("balanced")}});
    QSignalSpy requests(&controller, &PhosphorShellApp::ControlCenterController::panelRequested);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    const auto source = QFINDTESTDATA("../../../examples/phosphor-shell/QuickSettingsSurface.qml");
    QVERIFY(!source.isEmpty());
    QQmlComponent hostComponent(&engine);
    hostComponent.setData(
        R"(
        import QtQuick
        import "."
        Item {
            id: owner
            property string requestedPanel: ""
            property Component popup: Component { QuickSettingsSurface { tileIds: [] } }
            Connections {
                target: ControlCenterRegistry
                function onPanelRequested(panelId: string): void { owner.requestedPanel = panelId; }
            }
        }
    )",
        QUrl::fromLocalFile(QFileInfo(source).absolutePath() + QStringLiteral("/request-routing-test.qml")));
    std::unique_ptr<QObject> host(hostComponent.create());
    QVERIFY2(host, qPrintable(hostComponent.errorString()));
    auto* popup = host->property("popup").value<QQmlComponent*>();
    QVERIFY(popup);

    // Match LayerPopoutTransport, which intentionally does not use the
    // declaring component's context. The popup cannot access `owner` here.
    std::unique_ptr<QObject> content(popup->beginCreate(engine.rootContext()));
    popup->completeCreate();
    QVERIFY2(content, qPrintable(popup->errorString()));
    auto* item = qobject_cast<QQuickItem*>(content.get());
    QVERIFY(item);
    auto* appearance = namedItem(item, QStringLiteral("quickSettingsAppearance"));
    QVERIFY(appearance);
    QVERIFY(QMetaObject::invokeMethod(appearance, "clicked"));
    QCOMPARE(requests.size(), 1);
    QCOMPARE(requests.first().first().toString(), QStringLiteral("appearance"));
    QCOMPARE(host->property("requestedPanel").toString(), QStringLiteral("appearance"));
    QVERIFY(QMetaObject::invokeMethod(appearance, "clicked"));
    QCOMPARE(requests.size(), 2);
    QStringList messages;
    for (const auto& warning : warnings)
        for (const auto& error : qvariant_cast<QList<QQmlError>>(warning.first()))
            messages.append(error.toString());
    QVERIFY2(messages.isEmpty(), qPrintable(messages.join(QLatin1Char('\n'))));
}

void TestShellQmlCompiles::detailPanelsForwardReturnFromForeignContext()
{
    // LayerPopoutTransport creates these detail panels against the engine
    // root context. Their handlers must use the registry bridge, never an
    // id from shell.qml, or Back silently dies with a ReferenceError.
    PhosphorShellApp::ControlCenterController controller(nullptr);
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("ControlCenterRegistry"), &controller);
    QSignalSpy requests(&controller, &PhosphorShellApp::ControlCenterController::controlCenterRequested);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent hostComponent(&engine);
    hostComponent.setData(
        R"(
        import QtQuick
        Item {
            property Component network: Component {
                QtObject { signal backRequested; onBackRequested: ControlCenterRegistry.requestControlCenter("network") }
            }
            property Component bluetooth: Component {
                QtObject { signal backRequested; onBackRequested: ControlCenterRegistry.requestControlCenter("bluetooth") }
            }
            property Component audio: Component {
                QtObject { signal backRequested; onBackRequested: ControlCenterRegistry.requestControlCenter("audio") }
            }
            property Component stats: Component {
                QtObject { signal networkSettingsRequested; onNetworkSettingsRequested: ControlCenterRegistry.requestControlCenter("systemmetrics") }
            }
        }
        )",
        QUrl(QStringLiteral("qrc:/detail-routing-test.qml")));
    std::unique_ptr<QObject> host(hostComponent.create());
    QVERIFY2(host, qPrintable(hostComponent.errorString()));

    const auto shellPath = QFINDTESTDATA("../../../examples/phosphor-shell/shell.qml");
    QVERIFY(!shellPath.isEmpty());
    QFile shellFile(shellPath);
    QVERIFY(shellFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const auto shellSource = shellFile.readAll();
    QCOMPARE(shellSource.count("root.toggleControlCenter(root._lastPanelSource)"), 1);
    for (const auto& panelId :
         {QByteArray("network"), QByteArray("bluetooth"), QByteArray("audio"), QByteArray("systemmetrics")}) {
        QByteArray needle("ControlCenterRegistry.requestControlCenter(\"");
        needle += panelId;
        needle += QByteArray("\")");
        QCOMPARE(shellSource.count(needle), 1);
    }

    const auto make = [&host](const char* propertyName) {
        auto* component = host->property(propertyName).value<QQmlComponent*>();
        auto object = std::unique_ptr<QObject>(component->beginCreate(component->engine()->rootContext()));
        component->completeCreate();
        return object;
    };
    const auto invoke = [&requests](QObject* object, const char* signal, const QString& panelId) {
        QVERIFY(object);
        QVERIFY(QMetaObject::invokeMethod(object, signal));
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests.last().first().toString(), panelId);
    };

    // Keep each object alive until its signal has been delivered, and reset
    // the spy so one broken route cannot make the next one look healthy.
    auto network = make("network");
    invoke(network.get(), "backRequested", QStringLiteral("network"));
    requests.clear();
    auto bluetooth = make("bluetooth");
    invoke(bluetooth.get(), "backRequested", QStringLiteral("bluetooth"));
    requests.clear();
    auto audio = make("audio");
    invoke(audio.get(), "backRequested", QStringLiteral("audio"));
    requests.clear();
    auto stats = make("stats");
    invoke(stats.get(), "networkSettingsRequested", QStringLiteral("systemmetrics"));

    QStringList messages;
    for (const auto& warning : warnings)
        for (const auto& error : qvariant_cast<QList<QQmlError>>(warning.first()))
            messages.append(error.toString());
    QVERIFY2(messages.isEmpty(), qPrintable(messages.join(QLatin1Char('\n'))));
}

QTEST_MAIN(TestShellQmlCompiles)

#include "test_shell_qml_compiles.moc"

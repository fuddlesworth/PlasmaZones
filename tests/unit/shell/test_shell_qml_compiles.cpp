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

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
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

private:
    std::unique_ptr<QQmlEngine> m_engine;
};

namespace {
constexpr auto kModuleRoot = ":/qt/qml/Phosphor";

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

QTEST_MAIN(TestShellQmlCompiles)

#include "test_shell_qml_compiles.moc"

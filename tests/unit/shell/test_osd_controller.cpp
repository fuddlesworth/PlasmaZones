// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the shell's OSD registry owner and its QML-component
// factory, in the top-level GPL test tree because the classes ship in the
// GPL shell binary (src/shell), not in the LGPL OSD module. The QML bands
// themselves are covered by libs/phosphor-shell-osd/tests; what matters
// here is the C++ contract: which kinds register, every guard on the
// createOSD path, and the host fan-out an IPC trigger rides.

#include "shell/OsdController.h"
#include "shell/QmlComponentOSDFactory.h"

#include <QQmlEngine>
#include <QQuickItem>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QVariant>

#include <memory>

using namespace PhosphorShellApp;

class TestOsdController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void registersEveryBuiltin();
    void factoryIdsAreSorted();
    void unknownKindYieldsNull();
    void nullParentYieldsNull();
    void factoryRejectsNullEngine();
    void factoryRejectsNullParent();
    void noResolvableEngineYieldsNull();
    void registryChangesRefreshTheIdSet();
    void showFansOutToEveryHost();
    void showWithNoHostFails();
    void deadHostsAreDropped();
};

namespace {

// Stand-in for OSDHost: the same show(kind, value, active, targetScreen)
// surface, recording what arrived. QVariant parameters because that is
// how a QML-declared function is exposed to invokeMethod, which is the
// path the controller takes.
class FakeHost : public QObject
{
    Q_OBJECT

public:
    QString screenName;
    QString lastKind;
    QVariant lastValue;
    QVariant lastActive;
    int calls = 0;

    Q_INVOKABLE QVariant show(const QVariant& kind, const QVariant& value, const QVariant& active,
                              const QVariant& targetScreen)
    {
        ++calls;
        const QString target = targetScreen.toString();
        if (!target.isEmpty() && target != screenName) {
            return false;
        }
        lastKind = kind.toString();
        lastValue = value;
        lastActive = active;
        return kind.toString() != QLatin1String("nope");
    }
};

} // namespace

void TestOsdController::registersEveryBuiltin()
{
    OsdController controller;
    // Hand-written rather than derived from builtinOsds(), so adding or
    // dropping a kind has to be a deliberate edit in two places.
    const QStringList expected{
        QStringLiteral("brightness"),
        QStringLiteral("caps"),
        QStringLiteral("mic"),
        QStringLiteral("volume"),
    };
    QCOMPARE(controller.factoryIds(), expected);
}

void TestOsdController::factoryIdsAreSorted()
{
    OsdController controller;
    QStringList ids = controller.factoryIds();
    QStringList sorted = ids;
    sorted.sort();
    QCOMPARE(ids, sorted);
}

void TestOsdController::unknownKindYieldsNull()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no OSD registered")));
    OsdController controller;
    QQmlEngine engine;
    QQuickItem parent;
    QQmlEngine::setContextForObject(&parent, engine.rootContext());
    QCOMPARE(controller.createOSD(QStringLiteral("nope"), &parent), nullptr);
}

void TestOsdController::nullParentYieldsNull()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("null parent")));
    OsdController controller;
    QCOMPARE(controller.createOSD(QStringLiteral("volume"), nullptr), nullptr);
}

void TestOsdController::factoryRejectsNullEngine()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("null engine")));
    QmlComponentOSDFactory factory(QStringLiteral("volume"), QStringLiteral("Volume"), OsdController::moduleUri(),
                                   QStringLiteral("VolumeOSD"));
    QQuickItem parent;
    QCOMPARE(factory.createOSD(nullptr, &parent), nullptr);
}

void TestOsdController::factoryRejectsNullParent()
{
    // Pin the null-parent branch by its message: this binary does not
    // register Phosphor.OSD, so a missing guard would still yield null via
    // a component error and the leak it prevents would go unnoticed.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("null parent for")));
    QmlComponentOSDFactory factory(QStringLiteral("volume"), QStringLiteral("Volume"), OsdController::moduleUri(),
                                   QStringLiteral("VolumeOSD"));
    QQmlEngine engine;
    QCOMPARE(factory.createOSD(&engine, nullptr), nullptr);
}

void TestOsdController::noResolvableEngineYieldsNull()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no QML engine resolvable")));
    OsdController controller;
    QQuickItem parent;
    QCOMPARE(controller.createOSD(QStringLiteral("volume"), &parent), nullptr);
}

void TestOsdController::registryChangesRefreshTheIdSet()
{
    OsdController controller;
    QSignalSpy idsSpy(&controller, &OsdController::factoryIdsChanged);

    const bool registered = controller.registry().registerFactory(
        std::make_shared<QmlComponentOSDFactory>(QStringLiteral("zzz-dynamic"), QStringLiteral("Dynamic"),
                                                 OsdController::moduleUri(), QStringLiteral("NoSuchDelegate")));
    QVERIFY(registered);
    QCOMPARE(idsSpy.count(), 1);
    QVERIFY(controller.factoryIds().contains(QStringLiteral("zzz-dynamic")));

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("duplicate id")));
    const bool duplicate = controller.registry().registerFactory(
        std::make_shared<QmlComponentOSDFactory>(QStringLiteral("zzz-dynamic"), QStringLiteral("Dynamic"),
                                                 OsdController::moduleUri(), QStringLiteral("NoSuchDelegate")));
    QVERIFY(!duplicate);
    QCOMPARE(idsSpy.count(), 1);

    QVERIFY(controller.registry().unregisterFactory(QStringLiteral("zzz-dynamic")));
    QCOMPARE(idsSpy.count(), 2);
    QVERIFY(!controller.factoryIds().contains(QStringLiteral("zzz-dynamic")));
}

void TestOsdController::showFansOutToEveryHost()
{
    OsdController controller;
    FakeHost left;
    left.screenName = QStringLiteral("DP-1");
    FakeHost right;
    right.screenName = QStringLiteral("DP-2");
    controller.attachHost(&left);
    controller.attachHost(&right);
    // Attaching twice must not double the fan-out.
    controller.attachHost(&left);
    QCOMPARE(controller.hostCount(), 2);

    // Broadcast: both hosts hear it, a value-based kind carries no active.
    QVERIFY(controller.show(QStringLiteral("volume"), 62, QString()));
    QCOMPARE(left.calls, 1);
    QCOMPARE(right.calls, 1);
    QCOMPARE(left.lastKind, QStringLiteral("volume"));
    QCOMPARE(left.lastValue.toInt(), 62);
    QVERIFY(!left.lastActive.isValid());

    // A stateful kind reads the value as a toggle.
    QVERIFY(controller.show(QStringLiteral("mic"), 1, QString()));
    QCOMPARE(right.lastActive, QVariant(true));
    QVERIFY(controller.show(QStringLiteral("caps"), 0, QString()));
    QCOMPARE(right.lastActive, QVariant(false));

    // Targeted: every host is asked, only the named one accepts, and the
    // result is still true because one did.
    QVERIFY(controller.show(QStringLiteral("brightness"), 40, QStringLiteral("DP-2")));
    QCOMPARE(left.lastKind, QStringLiteral("caps"));
    QCOMPARE(right.lastKind, QStringLiteral("brightness"));

    // Refused by every host reports failure over the wire.
    QVERIFY(!controller.show(QStringLiteral("nope"), 1, QString()));

    controller.detachHost(&left);
    QCOMPARE(controller.hostCount(), 1);
    QVERIFY(controller.show(QStringLiteral("volume"), 10, QString()));
    QCOMPARE(left.calls, 5);
    QCOMPARE(right.calls, 6);
}

void TestOsdController::showWithNoHostFails()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no OSD host attached")));
    OsdController controller;
    QVERIFY(!controller.show(QStringLiteral("volume"), 50, QString()));
}

void TestOsdController::deadHostsAreDropped()
{
    // A hot reload destroys every host; the controller outlives them and
    // must neither dereference nor count the corpse.
    OsdController controller;
    auto host = std::make_unique<FakeHost>();
    controller.attachHost(host.get());
    QCOMPARE(controller.hostCount(), 1);
    host.reset();
    QCOMPARE(controller.hostCount(), 0);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no OSD host attached")));
    QVERIFY(!controller.show(QStringLiteral("volume"), 50, QString()));
}

QTEST_MAIN(TestOsdController)

#include "test_osd_controller.moc"

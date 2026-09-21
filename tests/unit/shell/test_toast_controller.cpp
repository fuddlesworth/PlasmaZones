// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the shell's toast broker: which host a wire call lands
// on, the shape it hands ToastHost.show, and the guards around a missing
// or dead host.

#include "shell/ToastController.h"

#include <QRegularExpression>
#include <QTest>
#include <QVariant>
#include <QVariantMap>

#include <memory>

using namespace PhosphorShellApp;

class TestToastController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void sendPrefersThePrimaryHost();
    void sendFallsBackToTheFirstHost();
    void sendWithNoHostFails();
    void deadHostsAreDropped();
    void reattachUpdatesTheFlags();
};

namespace {

// Stand-in for ToastHost: show(toast) -> id, the QML function's shape.
class FakeHost : public QObject
{
    Q_OBJECT

public:
    QVariantMap lastToast;
    int calls = 0;
    int nextId = 7;

    Q_INVOKABLE QVariant show(const QVariant& toast)
    {
        ++calls;
        lastToast = toast.toMap();
        return nextId++;
    }
};

} // namespace

void TestToastController::sendPrefersThePrimaryHost()
{
    ToastController controller;
    FakeHost secondary;
    FakeHost primary;
    controller.attachHost(&secondary, QStringLiteral("DP-2"), false);
    controller.attachHost(&primary, QStringLiteral("DP-1"), true);
    QCOMPARE(controller.hostCount(), 2);

    QCOMPARE(controller.send(QStringLiteral("Hi"), QStringLiteral("There")), 7);
    QCOMPARE(primary.calls, 1);
    QCOMPARE(secondary.calls, 0);
    QCOMPARE(primary.lastToast.value(QStringLiteral("summary")).toString(), QStringLiteral("Hi"));
    QCOMPARE(primary.lastToast.value(QStringLiteral("body")).toString(), QStringLiteral("There"));
    QCOMPARE(primary.lastToast.value(QStringLiteral("urgency")).toInt(), 1);
    // The id is whatever the host assigned, so a caller can dismiss it.
    QCOMPARE(controller.send(QStringLiteral("Again"), QString()), 8);
}

void TestToastController::sendFallsBackToTheFirstHost()
{
    ToastController controller;
    FakeHost first;
    FakeHost second;
    controller.attachHost(&first, QStringLiteral("DP-1"), false);
    controller.attachHost(&second, QStringLiteral("DP-2"), false);
    QCOMPARE(controller.send(QStringLiteral("Hi"), QString()), 7);
    QCOMPARE(first.calls, 1);
    QCOMPARE(second.calls, 0);

    controller.detachHost(&first);
    QCOMPARE(controller.send(QStringLiteral("Hi"), QString()), 7);
    QCOMPARE(second.calls, 1);
}

void TestToastController::sendWithNoHostFails()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no toast host attached")));
    ToastController controller;
    QCOMPARE(controller.send(QStringLiteral("Hi"), QString()), -1);
}

void TestToastController::deadHostsAreDropped()
{
    ToastController controller;
    auto host = std::make_unique<FakeHost>();
    controller.attachHost(host.get(), QStringLiteral("DP-1"), true);
    QCOMPARE(controller.hostCount(), 1);
    host.reset();
    QCOMPARE(controller.hostCount(), 0);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no toast host attached")));
    QCOMPARE(controller.send(QStringLiteral("Hi"), QString()), -1);
}

void TestToastController::reattachUpdatesTheFlags()
{
    // A primary change re-attaches the same host with a new flag; it must
    // update in place rather than register a second entry.
    ToastController controller;
    FakeHost a;
    FakeHost b;
    controller.attachHost(&a, QStringLiteral("DP-1"), true);
    controller.attachHost(&b, QStringLiteral("DP-2"), false);
    controller.attachHost(&a, QStringLiteral("DP-1"), false);
    controller.attachHost(&b, QStringLiteral("DP-2"), true);
    QCOMPARE(controller.hostCount(), 2);
    controller.send(QStringLiteral("Hi"), QString());
    QCOMPARE(a.calls, 0);
    QCOMPARE(b.calls, 1);
}

QTEST_MAIN(TestToastController)

#include "test_toast_controller.moc"

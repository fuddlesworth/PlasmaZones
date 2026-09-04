// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SocketPopoutTransport, the transport for a popout painted INTO the bar
// rather than given a surface of its own. It creates nothing: its whole job
// is to be the only writer of ControlCenterController::openScreen, so the
// pocket's open state goes through PopoutController's arbitration like every
// other popout.
//
// This exists because the routing test's fake is more permissive than the
// real thing. The fake accepts every open; this transport refuses a second
// one by design, and that refusal, plus the screen-name handling that decides
// whether anything paints at all, had no coverage anywhere in the repo.

#include "shell/ControlCenterController.h"
#include "shell/SocketPopoutTransport.h"

#include <PhosphorPopout/PopoutRequest.h>

#include <QGuiApplication>
#include <QScreen>
#include <QtTest/QtTest>

using PhosphorPopout::PopoutRequest;
using PhosphorShellApp::ControlCenterController;
using PhosphorShellApp::SocketPopoutTransport;

class TestSocketPopoutTransport : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void refusesWithoutAController();
    void refusesAnOutputWithNoName();
    void opensOnTheRequestedScreenAndWritesOpenScreenOnce();
    void refusesASecondOpenWhileOneIsUp();
    void closeClearsTheScreenAndReleasesTheSocket();
    void closeIgnoresAStaleOrEmptyHandle();
    void drainClearsWithoutNotifying();
    void handlesUseTheSocketPrefix();

private:
    [[nodiscard]] static PopoutRequest requestFor(const QString& id, QScreen* screen);
};

PopoutRequest TestSocketPopoutTransport::requestFor(const QString& id, QScreen* screen)
{
    PopoutRequest request;
    request.popoutId = id;
    request.targetScreen = screen;
    return request;
}

void TestSocketPopoutTransport::refusesWithoutAController()
{
    SocketPopoutTransport transport(nullptr);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no controller to drive")));
    QVERIFY(transport.openSurface(requestFor(QStringLiteral("control-center"), nullptr)).isEmpty());
}

// An empty name means "closed everywhere" to the controller, so opening with
// one would record a live handle for a pocket that paints nothing, and the
// one-socket rule would then refuse every later open for the process
// lifetime. Refusing up front is what keeps that unreachable.
void TestSocketPopoutTransport::refusesAnOutputWithNoName()
{
    ControlCenterController controller(nullptr);
    SocketPopoutTransport transport(&controller);
    transport.setScreenNameResolver([](QScreen*) {
        return QString();
    });

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no named output")));
    QVERIFY(transport.openSurface(requestFor(QStringLiteral("control-center"), nullptr)).isEmpty());
    QCOMPARE(controller.openScreen(), QString());

    // Nothing was latched, so a later open on a real output still works.
    transport.setScreenNameResolver([](QScreen*) {
        return QStringLiteral("DP-1");
    });
    QVERIFY(!transport.openSurface(requestFor(QStringLiteral("control-center"), nullptr)).isEmpty());
    QCOMPARE(controller.openScreen(), QStringLiteral("DP-1"));
}

void TestSocketPopoutTransport::opensOnTheRequestedScreenAndWritesOpenScreenOnce()
{
    ControlCenterController controller(nullptr);
    SocketPopoutTransport transport(&controller);
    // No headless Qt platform names its screens, so resolve through the
    // injectable seam rather than QScreen::name().
    transport.setScreenNameResolver([](QScreen*) {
        return QStringLiteral("DP-1");
    });
    QSignalSpy spy(&controller, &ControlCenterController::openScreenChanged);

    const QString handle = transport.openSurface(requestFor(QStringLiteral("control-center"), nullptr));
    QVERIFY(!handle.isEmpty());

    // The controller names the output whose bar should grow the pocket, and
    // it is written exactly once: the property is change-gated, so a second
    // identical write must not re-notify a bound binding.
    QCOMPARE(controller.openScreen(), QStringLiteral("DP-1"));
    QCOMPARE(spy.count(), 1);
}

void TestSocketPopoutTransport::refusesASecondOpenWhileOneIsUp()
{
    ControlCenterController controller(nullptr);
    SocketPopoutTransport transport(&controller);
    // No headless Qt platform names its screens, so resolve through the
    // injectable seam rather than QScreen::name().
    transport.setScreenNameResolver([](QScreen*) {
        return QStringLiteral("DP-1");
    });

    const QString first = transport.openSurface(requestFor(QStringLiteral("control-center"), nullptr));
    QVERIFY(!first.isEmpty());

    // The bar has one pocket. A second open is refused rather than moved,
    // and it must not disturb the one already up. The routing test's fake
    // cannot express this, which is why it needs pinning here.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("socket is already open")));
    QVERIFY(transport.openSurface(requestFor(QStringLiteral("other"), nullptr)).isEmpty());
    QCOMPARE(controller.openScreen(), QStringLiteral("DP-1"));
}

void TestSocketPopoutTransport::closeClearsTheScreenAndReleasesTheSocket()
{
    ControlCenterController controller(nullptr);
    SocketPopoutTransport transport(&controller);
    // No headless Qt platform names its screens, so resolve through the
    // injectable seam rather than QScreen::name().
    transport.setScreenNameResolver([](QScreen*) {
        return QStringLiteral("DP-1");
    });

    const QString first = transport.openSurface(requestFor(QStringLiteral("control-center"), nullptr));
    QVERIFY(!first.isEmpty());
    transport.closeSurface(first);
    // Empty means "closed everywhere" to the controller.
    QCOMPARE(controller.openScreen(), QString());

    // And the socket is genuinely free again, not merely blanked.
    const QString second = transport.openSurface(requestFor(QStringLiteral("control-center"), nullptr));
    QVERIFY(!second.isEmpty());
    QVERIFY(second != first);
    QCOMPARE(controller.openScreen(), QStringLiteral("DP-1"));
}

void TestSocketPopoutTransport::closeIgnoresAStaleOrEmptyHandle()
{
    ControlCenterController controller(nullptr);
    SocketPopoutTransport transport(&controller);
    // No headless Qt platform names its screens, so resolve through the
    // injectable seam rather than QScreen::name().
    transport.setScreenNameResolver([](QScreen*) {
        return QStringLiteral("DP-1");
    });

    const QString first = transport.openSurface(requestFor(QStringLiteral("control-center"), nullptr));
    QVERIFY(!first.isEmpty());
    transport.closeSurface(first);
    const QString second = transport.openSurface(requestFor(QStringLiteral("control-center"), nullptr));
    QVERIFY(!second.isEmpty());

    // A handle from an earlier cycle must not close a later one, or a
    // delayed close would tear down a pocket the user has since reopened.
    transport.closeSurface(first);
    QCOMPARE(controller.openScreen(), QStringLiteral("DP-1"));
    transport.closeSurface(QString());
    QCOMPARE(controller.openScreen(), QStringLiteral("DP-1"));
}

void TestSocketPopoutTransport::drainClearsWithoutNotifying()
{
    ControlCenterController controller(nullptr);
    SocketPopoutTransport transport(&controller);
    // No headless Qt platform names its screens, so resolve through the
    // injectable seam rather than QScreen::name().
    transport.setScreenNameResolver([](QScreen*) {
        return QStringLiteral("DP-1");
    });

    int dismissals = 0;
    transport.setSurfaceDismissedCallback([&dismissals](const QString&) {
        ++dismissals;
    });

    QVERIFY(!transport.openSurface(requestFor(QStringLiteral("control-center"), nullptr)).isEmpty());
    transport.drain();

    // Drain is the shutdown and hot-reload path: the state goes, but nothing
    // is reported upward, because the caller is the one doing the tearing
    // down and its own tables are already gone.
    QCOMPARE(controller.openScreen(), QString());
    QCOMPARE(dismissals, 0);
    // Draining twice is a no-op rather than an error.
    transport.drain();
    QCOMPARE(controller.openScreen(), QString());
}

void TestSocketPopoutTransport::handlesUseTheSocketPrefix()
{
    ControlCenterController controller(nullptr);
    SocketPopoutTransport transport(&controller);
    // No headless Qt platform names its screens, so resolve through the
    // injectable seam rather than QScreen::name().
    transport.setScreenNameResolver([](QScreen*) {
        return QStringLiteral("DP-1");
    });

    // RoutingPopoutTransport keys close-routing on the handle string and
    // documents the two transports' prefixes as disjoint by construction.
    // Nothing enforced that, so a copy-paste that made this mint "popout-"
    // would route closes to the wrong transport with the suite still green.
    const QString handle = transport.openSurface(requestFor(QStringLiteral("control-center"), nullptr));
    QVERIFY2(handle.startsWith(QLatin1String("socket-")), qPrintable(QStringLiteral("handle was ") + handle));
}

QTEST_MAIN(TestSocketPopoutTransport)
#include "test_socket_popout_transport.moc"

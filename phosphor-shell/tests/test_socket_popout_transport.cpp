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

#include "ControlCenterController.h"
#include "SocketPopoutTransport.h"

#include <PhosphorPopout/PopoutRequest.h>

#include <QGuiApplication>
#include <QScreen>
#include <QtTest/QtTest>

using PhosphorPopout::PopoutRequest;
using PhosphorShellApp::ControlCenterController;
using PhosphorShellApp::SocketPopoutTransport;

namespace {
// Six of the eight slots open against a named output, and each was repeating
// the same four lines to get there. The two that do not are refusesWithoutA-
// Controller, which constructs the transport with no controller at all, and
// refusesAnOutputWithNoName, whose whole subject is the empty-name resolver.
// Both keep their own setup.
//
// The bare offscreen platform gives its screen an EMPTY name (measured: length
// 0), and an empty name is exactly what the transport treats as "closed
// everywhere" and refuses on. So these slots inject a fixed name through the
// seam rather than depending on QScreen::name().
//
// theDefaultResolverNamesTheRequestedOutput is the one slot that must exercise
// the real resolver, and it therefore needs genuinely named outputs. The CMake
// entry for this target passes offscreen a configfile (two-screens.json,
// TEST-0 / TEST-1) for precisely that slot.
//
// Declaration order matters: transport takes &controller in its initialiser,
// so controller has to be declared first.
struct Harness
{
    ControlCenterController controller{nullptr};
    SocketPopoutTransport transport{&controller};

    // What the transport handed the resolver on the last open. Recorded rather
    // than discarded because otherwise nothing in the suite pins that
    // PopoutRequest::targetScreen reaches the resolver at all: every slot
    // passed a null screen, so replacing the real argument with a literal
    // nullptr in the transport left every assertion true.
    QScreen* sawScreen = nullptr;

    Harness()
    {
        transport.setScreenNameResolver([this](QScreen* requested) {
            sawScreen = requested;
            return QStringLiteral("DP-1");
        });
    }
};
} // namespace

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
    void theDefaultResolverNamesTheRequestedOutput();

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
    Harness h;
    QSignalSpy spy(&h.controller, &ControlCenterController::openScreenChanged);

    // A REAL screen, not nullptr: this is the slot that claims to open on the
    // requested one, so the request has to carry a screen for that claim to
    // mean anything.
    QScreen* const requested = QGuiApplication::primaryScreen();
    QVERIFY(requested);
    const QString handle = h.transport.openSurface(requestFor(QStringLiteral("control-center"), requested));
    QVERIFY(!handle.isEmpty());

    // The transport must route the request's screen to the resolver rather
    // than resolving against the primary and ignoring what it was handed.
    QCOMPARE(h.sawScreen, requested);

    // The controller names the output whose bar should grow the pocket, and
    // it is written exactly once: the property is change-gated, so a second
    // identical write must not re-notify a bound binding.
    QCOMPARE(h.controller.openScreen(), QStringLiteral("DP-1"));
    QCOMPARE(spy.count(), 1);
}

void TestSocketPopoutTransport::refusesASecondOpenWhileOneIsUp()
{
    Harness h;

    const QString first = h.transport.openSurface(requestFor(QStringLiteral("control-center"), nullptr));
    QVERIFY(!first.isEmpty());

    // The bar has one pocket. A second open is refused rather than moved,
    // and it must not disturb the one already up. The routing test's fake
    // cannot express this, which is why it needs pinning here.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("socket is already open")));
    QVERIFY(h.transport.openSurface(requestFor(QStringLiteral("other"), nullptr)).isEmpty());
    QCOMPARE(h.controller.openScreen(), QStringLiteral("DP-1"));
}

void TestSocketPopoutTransport::closeClearsTheScreenAndReleasesTheSocket()
{
    Harness h;

    const QString first = h.transport.openSurface(requestFor(QStringLiteral("control-center"), nullptr));
    QVERIFY(!first.isEmpty());
    h.transport.closeSurface(first);
    // Empty means "closed everywhere" to the controller.
    QCOMPARE(h.controller.openScreen(), QString());

    // And the socket is genuinely free again, not merely blanked.
    const QString second = h.transport.openSurface(requestFor(QStringLiteral("control-center"), nullptr));
    QVERIFY(!second.isEmpty());
    QVERIFY(second != first);
    QCOMPARE(h.controller.openScreen(), QStringLiteral("DP-1"));
}

void TestSocketPopoutTransport::closeIgnoresAStaleOrEmptyHandle()
{
    Harness h;

    const QString first = h.transport.openSurface(requestFor(QStringLiteral("control-center"), nullptr));
    QVERIFY(!first.isEmpty());
    h.transport.closeSurface(first);
    const QString second = h.transport.openSurface(requestFor(QStringLiteral("control-center"), nullptr));
    QVERIFY(!second.isEmpty());

    // A handle from an earlier cycle must not close a later one, or a
    // delayed close would tear down a pocket the user has since reopened.
    h.transport.closeSurface(first);
    QCOMPARE(h.controller.openScreen(), QStringLiteral("DP-1"));
    h.transport.closeSurface(QString());
    QCOMPARE(h.controller.openScreen(), QStringLiteral("DP-1"));
}

void TestSocketPopoutTransport::drainClearsWithoutNotifying()
{
    Harness h;

    int dismissals = 0;
    h.transport.setSurfaceDismissedCallback([&dismissals](const QString&) {
        ++dismissals;
    });

    QVERIFY(!h.transport.openSurface(requestFor(QStringLiteral("control-center"), nullptr)).isEmpty());
    h.transport.drain();

    // Drain is the shutdown and hot-reload path: the state goes, but nothing
    // is reported upward, because the caller is the one doing the tearing
    // down and its own tables are already gone.
    QCOMPARE(h.controller.openScreen(), QString());
    QCOMPARE(dismissals, 0);
    // Draining twice is a no-op rather than an error.
    h.transport.drain();
    QCOMPARE(h.controller.openScreen(), QString());
}

// The transport installs a default screen-name resolver in its constructor, and
// until this slot existed nothing ever ran it: refusesWithoutAController returns
// before reaching it, refusesAnOutputWithNoName replaces it, and every Harness
// slot replaces it too. So the production path -- the one the shell actually
// uses -- had no coverage at all while the suite reported green.
//
// No Harness here, deliberately: installing a resolver is exactly what this
// slot must not do.
void TestSocketPopoutTransport::theDefaultResolverNamesTheRequestedOutput()
{
    ControlCenterController controller(nullptr);
    SocketPopoutTransport transport(&controller);

    QScreen* const requested = QGuiApplication::primaryScreen();
    QVERIFY(requested);
    // The offscreen platform names its screen, so this is a real name rather
    // than the empty string the refusal path keys on.
    QVERIFY(!requested->name().isEmpty());

    QVERIFY(!transport.openSurface(requestFor(QStringLiteral("control-center"), requested)).isEmpty());
    QCOMPARE(controller.openScreen(), requested->name());
}

void TestSocketPopoutTransport::handlesUseTheSocketPrefix()
{
    Harness h;

    // RoutingPopoutTransport keys close-routing on the handle string and
    // documents the two transports' prefixes as disjoint by construction.
    // Nothing enforced that, so a copy-paste that made this mint "popout-"
    // would route closes to the wrong transport with the suite still green.
    const QString handle = h.transport.openSurface(requestFor(QStringLiteral("control-center"), nullptr));
    QVERIFY2(handle.startsWith(QLatin1String("socket-")), qPrintable(QStringLiteral("handle was ") + handle));
}

QTEST_MAIN(TestSocketPopoutTransport)
#include "test_socket_popout_transport.moc"

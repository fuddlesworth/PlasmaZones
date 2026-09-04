// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// RoutingPopoutTransport — one IPopoutTransport in front of the layer and
// socket transports, routed by popout id. Pure C++ over two fakes: what is
// pinned is the routing decision, that close goes back to the transport
// that issued the handle, that a refused open records nothing, and that
// self-dismissals from either side arrive on the controller's one
// callback and detach cleanly.

#include "shell/RoutingPopoutTransport.h"

#include <PhosphorPopout/PopoutRequest.h>

#include <QSet>
#include <QStringList>
#include <QTest>

#include <functional>

using PhosphorPopout::IPopoutTransport;
using PhosphorPopout::PopoutRequest;
using PhosphorShellApp::RoutingPopoutTransport;

namespace {

// Records every call, issues "<prefix>-N" handles, and can fire its stored
// dismissed callback on demand to simulate a surface closing itself.
class FakeTransport : public IPopoutTransport
{
public:
    explicit FakeTransport(QString prefix)
        : m_prefix(std::move(prefix))
    {
    }

    QString openSurface(const PopoutRequest& request) override
    {
        opened.append(request.popoutId);
        if (refuse) {
            return {};
        }
        return QStringLiteral("%1-%2").arg(m_prefix).arg(++m_counter);
    }

    void closeSurface(const QString& handle) override
    {
        closed.append(handle);
    }

    void setSurfaceDismissedCallback(std::function<void(const QString&)> callback) override
    {
        dismissed = std::move(callback);
        installs++;
    }

    void selfDismiss(const QString& handle)
    {
        if (dismissed) {
            dismissed(handle);
        }
    }

    QStringList opened;
    QStringList closed;
    std::function<void(const QString&)> dismissed;
    int installs = 0;
    bool refuse = false;

private:
    QString m_prefix;
    int m_counter = 0;
};

PopoutRequest requestFor(const QString& id)
{
    PopoutRequest r;
    r.popoutId = id;
    return r;
}

} // namespace

class TestRoutingPopoutTransport : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void socketHostedIdsGoToTheSocketTransport();
    void everythingElseGoesToTheLayerTransport();
    void closeRoutesBackToTheIssuingTransport();
    void closeOfAnUnknownHandleTouchesNeither();
    void refusedOpenRecordsNoHandle();
    void dismissalsFromEitherSideReachTheOneCallback();
    void detachingTheCallbackDropsLaterDismissals();
};

void TestRoutingPopoutTransport::socketHostedIdsGoToTheSocketTransport()
{
    FakeTransport layer(QStringLiteral("popout"));
    FakeTransport socket(QStringLiteral("socket"));
    RoutingPopoutTransport router(&layer, &socket, {QStringLiteral("control-center")});

    const QString handle = router.openSurface(requestFor(QStringLiteral("control-center")));
    QCOMPARE(handle, QStringLiteral("socket-1"));
    QCOMPARE(socket.opened, QStringList{QStringLiteral("control-center")});
    QVERIFY(layer.opened.isEmpty());
}

void TestRoutingPopoutTransport::everythingElseGoesToTheLayerTransport()
{
    FakeTransport layer(QStringLiteral("popout"));
    FakeTransport socket(QStringLiteral("socket"));
    RoutingPopoutTransport router(&layer, &socket, {QStringLiteral("control-center")});

    // The power menu, and an id nobody registered: both are layer
    // surfaces. Routing is by declared id, so "not declared" is the layer
    // side by default rather than an error.
    QCOMPARE(router.openSurface(requestFor(QStringLiteral("power"))), QStringLiteral("popout-1"));
    QCOMPARE(router.openSurface(requestFor(QStringLiteral("launcher"))), QStringLiteral("popout-2"));
    QCOMPARE(layer.opened, (QStringList{QStringLiteral("power"), QStringLiteral("launcher")}));
    QVERIFY(socket.opened.isEmpty());
}

void TestRoutingPopoutTransport::closeRoutesBackToTheIssuingTransport()
{
    FakeTransport layer(QStringLiteral("popout"));
    FakeTransport socket(QStringLiteral("socket"));
    RoutingPopoutTransport router(&layer, &socket, {QStringLiteral("control-center")});

    const QString socketHandle = router.openSurface(requestFor(QStringLiteral("control-center")));
    const QString layerHandle = router.openSurface(requestFor(QStringLiteral("power")));

    router.closeSurface(socketHandle);
    QCOMPARE(socket.closed, QStringList{socketHandle});
    QVERIFY(layer.closed.isEmpty());

    router.closeSurface(layerHandle);
    QCOMPARE(layer.closed, QStringList{layerHandle});
    QCOMPARE(socket.closed.size(), 1);

    // Closing twice: the second is a no-op at the router (the handle was
    // forgotten on the first close), so neither inner transport sees it
    // again. Idempotence is the contract; this pins that the router
    // does not merely rely on the inner transport to absorb the repeat.
    router.closeSurface(socketHandle);
    QCOMPARE(socket.closed.size(), 1);
}

void TestRoutingPopoutTransport::closeOfAnUnknownHandleTouchesNeither()
{
    FakeTransport layer(QStringLiteral("popout"));
    FakeTransport socket(QStringLiteral("socket"));
    RoutingPopoutTransport router(&layer, &socket, {QStringLiteral("control-center")});

    router.closeSurface(QStringLiteral("never-issued"));
    router.closeSurface(QString());
    QVERIFY(layer.closed.isEmpty());
    QVERIFY(socket.closed.isEmpty());
}

void TestRoutingPopoutTransport::refusedOpenRecordsNoHandle()
{
    FakeTransport layer(QStringLiteral("popout"));
    FakeTransport socket(QStringLiteral("socket"));
    socket.refuse = true;
    RoutingPopoutTransport router(&layer, &socket, {QStringLiteral("control-center")});

    // The inner refusal (empty handle) must surface as a refusal, and must
    // leave no mapping behind: a later close of "" must not reach anyone.
    QVERIFY(router.openSurface(requestFor(QStringLiteral("control-center"))).isEmpty());
    router.closeSurface(QString());
    QVERIFY(socket.closed.isEmpty());
    QVERIFY(layer.closed.isEmpty());
}

void TestRoutingPopoutTransport::dismissalsFromEitherSideReachTheOneCallback()
{
    FakeTransport layer(QStringLiteral("popout"));
    FakeTransport socket(QStringLiteral("socket"));
    RoutingPopoutTransport router(&layer, &socket, {QStringLiteral("control-center")});

    // The router installs its forwarders on both inner transports at
    // construction, exactly once each.
    QCOMPARE(layer.installs, 1);
    QCOMPARE(socket.installs, 1);

    QStringList seen;
    router.setSurfaceDismissedCallback([&seen](const QString& h) {
        seen.append(h);
    });

    const QString lh = router.openSurface(requestFor(QStringLiteral("power")));
    const QString sh = router.openSurface(requestFor(QStringLiteral("control-center")));
    layer.selfDismiss(lh);
    socket.selfDismiss(sh);
    QCOMPARE(seen, (QStringList{lh, sh}));

    // A self-dismissed handle is forgotten too, so the controller's
    // follow-up close (if any) cannot re-close it on the inner side.
    router.closeSurface(lh);
    QVERIFY(layer.closed.isEmpty());
}

void TestRoutingPopoutTransport::detachingTheCallbackDropsLaterDismissals()
{
    FakeTransport layer(QStringLiteral("popout"));
    FakeTransport socket(QStringLiteral("socket"));
    RoutingPopoutTransport router(&layer, &socket, {QStringLiteral("control-center")});

    int calls = 0;
    router.setSurfaceDismissedCallback([&calls](const QString&) {
        calls++;
    });
    const QString h = router.openSurface(requestFor(QStringLiteral("power")));

    // The controller detaches with an empty function in its destructor. A
    // dismissal after that must not crash and must not be delivered — and
    // the inner wiring stays in place (no re-install), so this is the
    // router dropping it, not the fake.
    router.setSurfaceDismissedCallback({});
    layer.selfDismiss(h);
    QCOMPARE(calls, 0);
    QCOMPARE(layer.installs, 1);
}

QTEST_GUILESS_MAIN(TestRoutingPopoutTransport)

#include "test_routing_popout_transport.moc"

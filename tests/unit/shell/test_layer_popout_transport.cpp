// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// LayerPopoutTransport — the shell's real popout transport, previously
// covered only through fakes on the controller side. What is drivable
// headless with the phosphor-layer mock transport is pinned here: every
// openSurface refusal leg, the successful open (surface attached, host
// built, handle returned), closeSurface's controller-initiated-close
// suppression of the dismissed callback plus its idempotence, drain()
// emptying the entries without invoking the callback, and the
// failed-surface path routing through onSurfaceGone into the callback.

#include "shell/LayerPopoutTransport.h"

#include <PhosphorLayer/SurfaceFactory.h>
#include <PhosphorPopout/PopoutRequest.h>

#include "mocks/mockscreenprovider.h"
#include "mocks/mocktransport.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QStringList>
#include <QTest>

#include <memory>

using PhosphorLayer::SurfaceFactory;
using PhosphorLayer::Testing::MockScreenProvider;
using PhosphorLayer::Testing::MockTransport;
using PhosphorPopout::PopoutRequest;
using PhosphorShellApp::LayerPopoutTransport;

class TestLayerPopoutTransport : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void refusesWithoutAFactory();
    void refusesWithoutAnEngine();
    void refusesWithoutContent();
    void refusesWithoutAScreen();
    void opensASurfaceAndReturnsAHandle();
    void controllerInitiatedCloseSuppressesTheCallback();
    void closeIsIdempotentForUnknownHandles();
    void drainEmptiesWithoutInvokingTheCallback();
    void aFailedSurfaceRoutesToTheCallback();

private:
    PopoutRequest makeRequest();

    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<MockTransport> m_wire;
    std::unique_ptr<MockScreenProvider> m_screens;
    std::unique_ptr<SurfaceFactory> m_factory;
    std::unique_ptr<QQmlComponent> m_content;
    QStringList m_dismissed;
};

void TestLayerPopoutTransport::init()
{
    m_engine = std::make_unique<QQmlEngine>();
    m_wire = std::make_unique<MockTransport>();
    m_screens = std::make_unique<MockScreenProvider>();
    m_factory = std::make_unique<SurfaceFactory>(PhosphorLayer::Testing::makeDeps(m_wire.get(), m_screens.get()));
    m_content = std::make_unique<QQmlComponent>(m_engine.get());
    m_content->setData("import QtQuick\nItem { implicitWidth: 10; implicitHeight: 10 }",
                       QUrl(QStringLiteral("qrc:/popout_test_content.qml")));
    QVERIFY2(m_content->isReady(), qPrintable(m_content->errorString()));
    m_dismissed.clear();
}

void TestLayerPopoutTransport::cleanup()
{
    m_content.reset();
    m_factory.reset();
    m_screens.reset();
    m_wire.reset();
    m_engine.reset();
}

PopoutRequest TestLayerPopoutTransport::makeRequest()
{
    PopoutRequest request;
    request.popoutId = QStringLiteral("test-popout");
    request.content = m_content.get();
    request.anchor = PhosphorPopout::Anchor::ScreenCenter;
    request.exclusive = PhosphorPopout::ExclusiveMode::Modal;
    return request;
}

void TestLayerPopoutTransport::refusesWithoutAFactory()
{
    LayerPopoutTransport transport(nullptr, m_screens.get());
    transport.setEngine(m_engine.get());
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no surface factory")));
    QVERIFY(transport.openSurface(makeRequest()).isEmpty());
}

void TestLayerPopoutTransport::refusesWithoutAnEngine()
{
    LayerPopoutTransport transport(m_factory.get(), m_screens.get());
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no QML engine yet")));
    QVERIFY(transport.openSurface(makeRequest()).isEmpty());
}

void TestLayerPopoutTransport::refusesWithoutContent()
{
    LayerPopoutTransport transport(m_factory.get(), m_screens.get());
    transport.setEngine(m_engine.get());
    PopoutRequest request = makeRequest();
    request.content = nullptr;
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no content component")));
    QVERIFY(transport.openSurface(request).isEmpty());
}

void TestLayerPopoutTransport::refusesWithoutAScreen()
{
    m_screens->setScreens({});
    m_screens->setFocused(nullptr);
    LayerPopoutTransport transport(m_factory.get(), m_screens.get());
    transport.setEngine(m_engine.get());
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no screen available")));
    QVERIFY(transport.openSurface(makeRequest()).isEmpty());
}

void TestLayerPopoutTransport::opensASurfaceAndReturnsAHandle()
{
    LayerPopoutTransport transport(m_factory.get(), m_screens.get());
    transport.setEngine(m_engine.get());

    const QString handle = transport.openSurface(makeRequest());
    QVERIFY(!handle.isEmpty());
    // The layer surface really attached through the wire.
    QTRY_VERIFY(m_wire->m_attachCount >= 1);

    transport.drain();
}

void TestLayerPopoutTransport::controllerInitiatedCloseSuppressesTheCallback()
{
    LayerPopoutTransport transport(m_factory.get(), m_screens.get());
    transport.setEngine(m_engine.get());
    transport.setSurfaceDismissedCallback([this](const QString& h) {
        m_dismissed.append(h);
    });

    const QString handle = transport.openSurface(makeRequest());
    QVERIFY(!handle.isEmpty());

    // A controller-initiated close marks the entry `closing`, so the host's
    // eventual `dismissed` must tear the entry down WITHOUT reporting back —
    // the controller already knows. The host's dismissEmitter guarantees the
    // emission after its close duration; give it a generous window.
    transport.closeSurface(handle);
    QTest::qWait(1500);
    QVERIFY2(m_dismissed.isEmpty(), "controller-initiated close was reported back as a dismissal");

    // The entry is gone: a second close of the same handle is a no-op.
    transport.closeSurface(handle);
    QVERIFY(m_dismissed.isEmpty());
}

void TestLayerPopoutTransport::closeIsIdempotentForUnknownHandles()
{
    LayerPopoutTransport transport(m_factory.get(), m_screens.get());
    transport.setEngine(m_engine.get());
    // Unknown and empty handles are documented no-ops.
    transport.closeSurface(QStringLiteral("never-issued"));
    transport.closeSurface(QString());
}

void TestLayerPopoutTransport::drainEmptiesWithoutInvokingTheCallback()
{
    LayerPopoutTransport transport(m_factory.get(), m_screens.get());
    transport.setEngine(m_engine.get());
    transport.setSurfaceDismissedCallback([this](const QString& h) {
        m_dismissed.append(h);
    });

    QVERIFY(!transport.openSurface(makeRequest()).isEmpty());
    transport.drain();
    // The teardown is deferred (deleteLater) but the disconnect is not:
    // nothing may reach the callback afterwards, including the hosts'
    // destruction-time emissions.
    QTest::qWait(200);
    QVERIFY(m_dismissed.isEmpty());

    // A drained transport still opens fresh surfaces.
    QVERIFY(!transport.openSurface(makeRequest()).isEmpty());
    transport.drain();
}

void TestLayerPopoutTransport::aFailedSurfaceRoutesToTheCallback()
{
    LayerPopoutTransport transport(m_factory.get(), m_screens.get());
    transport.setEngine(m_engine.get());
    transport.setSurfaceDismissedCallback([this](const QString& h) {
        m_dismissed.append(h);
    });

    // The wire refuses the attach, so the Surface enters Failed after
    // show(); the transport must tear the entry down and report the
    // self-dismissal so the controller drops its row.
    m_wire->rejectNextAttach();
    const QString handle = transport.openSurface(makeRequest());
    if (handle.isEmpty()) {
        // Some factory configurations surface the refusal synchronously as
        // a failed create; that is the same contract honoured earlier
        // (refusal = empty handle, controller row untouched), so nothing
        // further to assert.
        return;
    }
    QTRY_COMPARE(m_dismissed, QStringList{handle});
    // The entry is gone; closing it again is a no-op.
    transport.closeSurface(handle);
}

QTEST_MAIN(TestLayerPopoutTransport)

#include "test_layer_popout_transport.moc"

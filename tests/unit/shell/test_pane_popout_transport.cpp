// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PanePopoutTransport: the engine-placed pane (A2 §4). Opening
// "control-center" on a screen with a placement engine creates ONE toplevel
// carrying the pane app id, at the pane's size and with that size as its
// minimum, and marks the control center external on the controller;
// closing releases it and unmaps. A screen without an engine, or a request
// the toplevel cannot be built for, goes to the floating fallback instead.
//
// Offscreen: the app id reaches the compositor only on Wayland, so what is
// checked here is the identity the window carries and the transport's
// bookkeeping around it.

#include "shell/ControlCenterController.h"
#include "shell/PanePopoutTransport.h"

#include <PhosphorPopout/IPopoutTransport.h>
#include <PhosphorPopout/PopoutRequest.h>
#include <PhosphorShell/FloatingWindow.h>

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QScreen>
#include <QStringList>
#include <QtTest/QtTest>

#include <functional>
#include <memory>

using PhosphorPopout::IPopoutTransport;
using PhosphorPopout::PopoutRequest;
using PhosphorShellApp::ControlCenterController;
using PhosphorShellApp::PanePopoutTransport;

namespace {
constexpr int kReleaseCeilingMs = 1500;
const QString kScreen = QStringLiteral("DP-1");
const QString kPopout = QStringLiteral("control-center");
} // namespace

// The floating fallback, recording what reaches it. Named (not in the
// anonymous namespace) so the test class holding one has no field of
// internal-linkage type.
namespace PaneTestSupport {
class FakeFallback : public IPopoutTransport
{
public:
    QString openSurface(const PopoutRequest& request) override
    {
        opened.append(request.popoutId);
        return QStringLiteral("socket-%1").arg(++counter);
    }
    void closeSurface(const QString& handle) override
    {
        closed.append(handle);
    }
    void setSurfaceDismissedCallback(std::function<void(const QString&)> callback) override
    {
        dismissed = std::move(callback);
    }

    QStringList opened;
    QStringList closed;
    std::function<void(const QString&)> dismissed;
    int counter = 0;
};
} // namespace PaneTestSupport

using PaneTestSupport::FakeFallback;

class TestPanePopoutTransport : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();
    void appIdIsThePanePrefixPlusThePopoutId();
    void opensOneToplevelWithThePaneIdentity();
    void closeReleasesThenUnmapsWithoutNotifying();
    void escapeInsideThePaneReportsUpward();
    void compositorCloseReportsUpward();
    void noEngineOnTheScreenGoesToTheFallback();
    void noContentGoesToTheFallback();
    void fallbackHandlesCloseThroughTheFallback();
    void drainClearsSilently();

private:
    [[nodiscard]] PopoutRequest makeRequest(bool withContent = true) const;
    [[nodiscard]] QQuickItem* hostOf(PhosphorShell::FloatingWindow* window) const;

    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQmlComponent> m_content;
    std::unique_ptr<ControlCenterController> m_controller;
    std::unique_ptr<FakeFallback> m_fallback;
    std::unique_ptr<PanePopoutTransport> m_transport;
    QStringList m_dismissed;
};

void TestPanePopoutTransport::init()
{
    m_engine = std::make_unique<QQmlEngine>();
    m_content = std::make_unique<QQmlComponent>(m_engine.get());
    m_content->setData("import QtQuick\nItem { implicitWidth: 10; implicitHeight: 10 }",
                       QUrl(QStringLiteral("qrc:/pane_test_content.qml")));
    QVERIFY2(m_content->isReady(), qPrintable(m_content->errorString()));
    m_controller = std::make_unique<ControlCenterController>(nullptr);
    m_controller->reportScreenMode(kScreen, 1);
    m_fallback = std::make_unique<FakeFallback>();
    m_transport = std::make_unique<PanePopoutTransport>(m_controller.get(), m_fallback.get());
    m_transport->setEngine(m_engine.get());
    m_transport->setScreenNameResolver([](QScreen*) {
        return kScreen;
    });
    m_transport->setSurfaceDismissedCallback([this](const QString& handle) {
        m_dismissed.append(handle);
    });
    m_dismissed.clear();
}

void TestPanePopoutTransport::cleanup()
{
    m_transport.reset();
    m_fallback.reset();
    m_controller.reset();
    m_content.reset();
    m_engine.reset();
}

PopoutRequest TestPanePopoutTransport::makeRequest(bool withContent) const
{
    PopoutRequest request;
    request.popoutId = kPopout;
    request.content = withContent ? m_content.get() : nullptr;
    request.targetScreen = QGuiApplication::primaryScreen();
    return request;
}

QQuickItem* TestPanePopoutTransport::hostOf(PhosphorShell::FloatingWindow* window) const
{
    if (!window || !window->quickWindow()) {
        return nullptr;
    }
    const auto children = window->quickWindow()->contentItem()->childItems();
    return children.isEmpty() ? nullptr : children.first();
}

void TestPanePopoutTransport::appIdIsThePanePrefixPlusThePopoutId()
{
    QCOMPARE(PanePopoutTransport::appIdFor(kPopout), QStringLiteral("org.phosphor.shell.pane.control-center"));
}

void TestPanePopoutTransport::opensOneToplevelWithThePaneIdentity()
{
    const QString handle = m_transport->openSurface(makeRequest());
    QVERIFY(handle.startsWith(QLatin1String("pane-")));
    QVERIFY(m_fallback->opened.isEmpty());

    PhosphorShell::FloatingWindow* window = m_transport->windowFor(handle);
    QVERIFY(window);
    QCOMPARE(window->appId(), QStringLiteral("org.phosphor.shell.pane.control-center"));
    QVERIFY(window->isWindowVisible());
    QVERIFY(window->quickWindow());
    QCOMPARE(window->windowWidth(), 380);
    QCOMPARE(window->windowHeight(), 460);
    QCOMPARE(window->minimumWidth(), 380);
    QCOMPARE(window->minimumHeight(), 460);
    QCOMPARE(window->quickWindow()->minimumSize(), QSize(380, 460));

    // The content sits inside a PaneHost inside the toplevel's scene.
    QQuickItem* host = hostOf(window);
    QVERIFY(host);
    QCOMPARE(host->property("open").toBool(), true);
    QVERIFY(host->property("contentItem").value<QQuickItem*>());

    // The bar on that screen learns the pane is external.
    QCOMPARE(m_controller->openScreen(), kScreen);
    QVERIFY(m_controller->isPaneExternal());
}

void TestPanePopoutTransport::closeReleasesThenUnmapsWithoutNotifying()
{
    const QString handle = m_transport->openSurface(makeRequest());
    QPointer<PhosphorShell::FloatingWindow> window = m_transport->windowFor(handle);
    QVERIFY(window);
    QPointer<QQuickWindow> toplevel = window->quickWindow();

    m_transport->closeSurface(handle);
    // The open state clears as the release begins, before the unmap.
    QCOMPARE(m_controller->openScreen(), QString());
    QVERIFY(!m_controller->isPaneExternal());
    QVERIFY(!m_transport->windowFor(handle));

    QTRY_VERIFY_WITH_TIMEOUT(window.isNull(), kReleaseCeilingMs);
    QVERIFY(toplevel.isNull());
    QVERIFY(m_dismissed.isEmpty());

    // Idempotent for a handle that is gone.
    m_transport->closeSurface(handle);
    QVERIFY(m_dismissed.isEmpty());
}

void TestPanePopoutTransport::escapeInsideThePaneReportsUpward()
{
    const QString handle = m_transport->openSurface(makeRequest());
    QPointer<PhosphorShell::FloatingWindow> window = m_transport->windowFor(handle);
    QQuickItem* host = hostOf(window);
    QVERIFY(host);

    QVERIFY(QMetaObject::invokeMethod(host, "dismiss"));
    QTRY_COMPARE_WITH_TIMEOUT(m_dismissed, QStringList{handle}, kReleaseCeilingMs);
    QTRY_VERIFY_WITH_TIMEOUT(window.isNull(), kReleaseCeilingMs);
    QCOMPARE(m_controller->openScreen(), QString());
}

void TestPanePopoutTransport::compositorCloseReportsUpward()
{
    const QString handle = m_transport->openSurface(makeRequest());
    QPointer<PhosphorShell::FloatingWindow> window = m_transport->windowFor(handle);
    QVERIFY(window);

    // What xdg_toplevel.close becomes on the client: the window closes.
    window->quickWindow()->close();
    QTRY_COMPARE_WITH_TIMEOUT(m_dismissed, QStringList{handle}, kReleaseCeilingMs);
    QTRY_VERIFY_WITH_TIMEOUT(window.isNull(), kReleaseCeilingMs);
    QCOMPARE(m_controller->openScreen(), QString());
    QVERIFY(!m_controller->isPaneExternal());
}

void TestPanePopoutTransport::noEngineOnTheScreenGoesToTheFallback()
{
    m_controller->reportScreenMode(kScreen, -1);
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("floating fallback")));
    const QString handle = m_transport->openSurface(makeRequest());
    QCOMPARE(handle, QStringLiteral("socket-1"));
    QCOMPARE(m_fallback->opened, QStringList{kPopout});
    QVERIFY(!m_transport->windowFor(handle));
    // The fallback owns the open state; this transport did not touch it.
    QVERIFY(!m_controller->isPaneExternal());
}

void TestPanePopoutTransport::noContentGoesToTheFallback()
{
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("no content component")));
    const QString handle = m_transport->openSurface(makeRequest(false));
    QCOMPARE(handle, QStringLiteral("socket-1"));
    QCOMPARE(m_fallback->opened, QStringList{kPopout});
}

void TestPanePopoutTransport::fallbackHandlesCloseThroughTheFallback()
{
    m_controller->reportScreenMode(kScreen, -1);
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("floating fallback")));
    const QString handle = m_transport->openSurface(makeRequest());
    m_transport->closeSurface(handle);
    QCOMPARE(m_fallback->closed, QStringList{handle});
    // A fallback pane dismissing itself surfaces through this transport.
    m_controller->reportScreenMode(kScreen, -1);
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("floating fallback")));
    const QString second = m_transport->openSurface(makeRequest());
    QVERIFY(m_fallback->dismissed);
    m_fallback->dismissed(second);
    QCOMPARE(m_dismissed, QStringList{second});
}

void TestPanePopoutTransport::drainClearsSilently()
{
    const QString handle = m_transport->openSurface(makeRequest());
    QPointer<PhosphorShell::FloatingWindow> window = m_transport->windowFor(handle);
    QVERIFY(window);
    m_transport->drain();
    QVERIFY(!m_transport->windowFor(handle));
    QCOMPARE(m_controller->openScreen(), QString());
    QVERIFY(!m_controller->isPaneExternal());
    QTRY_VERIFY_WITH_TIMEOUT(window.isNull(), kReleaseCeilingMs);
    QVERIFY(m_dismissed.isEmpty());
}

QTEST_MAIN(TestPanePopoutTransport)
#include "test_pane_popout_transport.moc"

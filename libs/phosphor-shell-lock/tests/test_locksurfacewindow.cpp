// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// LockSurface's one hard rule: it refuses to exist while no session lock
// is held. Under the offscreen platform there is no Wayland integration,
// so PhosphorWayland::SessionLock::canCreateSurfaces() is false and a show
// must be refused: the window reports it and hides itself. Built through a
// QQmlEngine, the way the shell builds it, so the QML registration of the
// type (`Phosphor.Lock.LockSurface`) is exercised as well.

#include <PhosphorShellLock/LockSurfaceWindow.h>

#include <PhosphorWayland/LockSurface.h>
#include <PhosphorWayland/SessionLock.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

using PhosphorShellLock::LockSurfaceWindow;

class TestLockSurfaceWindow : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void noLockIsHeldOffscreen();
    void windowIsMarkedAsALockSurface();
    void showIsRefusedWithoutALock();
    void qmlTypeBuildsAndRefuses();
};

void TestLockSurfaceWindow::noLockIsHeldOffscreen()
{
    // The premise of the other cases: no compositor, no lock.
    QVERIFY(!PhosphorWayland::SessionLock::canCreateSurfaces());
    QVERIFY(!PhosphorWayland::SessionLock::isSupported());
}

void TestLockSurfaceWindow::windowIsMarkedAsALockSurface()
{
    LockSurfaceWindow window;
    // The marker the QPA plugin keys on, set at construction so it precedes
    // the platform window's creation.
    QVERIFY(window.property(PhosphorWayland::LockSurfaceProps::IsSessionLock).toBool());
    QVERIFY(PhosphorWayland::LockSurface::find(&window) != nullptr);
    QVERIFY(!window.isConfigured());
    QVERIFY(!window.canExist());
}

void TestLockSurfaceWindow::showIsRefusedWithoutALock()
{
    LockSurfaceWindow window;
    QSignalSpy refused(&window, &LockSurfaceWindow::refused);
    window.show();
    QCOMPARE(refused.count(), 1);
    // The hide is queued behind the show that is still unwinding.
    QTRY_VERIFY(!window.isVisible());
}

void TestLockSurfaceWindow::qmlTypeBuildsAndRefuses()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(QByteArrayLiteral("import Phosphor.Lock\n"
                                        "LockSurface { visible: true }\n"),
                      QUrl(QStringLiteral("qrc:/test/lock.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY(object);
    auto* window = qobject_cast<LockSurfaceWindow*>(object.get());
    QVERIFY(window);
    QTRY_VERIFY(!window->isVisible());
}

QTEST_MAIN(TestLockSurfaceWindow)
#include "test_locksurfacewindow.moc"

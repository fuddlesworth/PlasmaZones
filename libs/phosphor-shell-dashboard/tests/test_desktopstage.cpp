// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../src/desktopstage.h"
#include <QDBusConnection>
#include <QTest>
using PhosphorShellDashboard::DesktopStage;
class Compositor : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.ShellOverview")
public:
    QRectF lastRect;
    QString lastScreen;
    bool opened = false;
public Q_SLOTS:
    bool begin(const QString& screen, double x, double y, double width, double height, bool)
    {
        lastScreen = screen;
        lastRect = QRectF(x, y, width, height);
        opened = true;
        return true;
    }
    void end()
    {
        opened = false;
    }
};
class TestDesktopStage : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void lifetimeAndPendingReply()
    {
        auto bus = QDBusConnection::sessionBus();
        Compositor compositor;
        QVERIFY(bus.registerService(QStringLiteral("org.kde.KWin")));
        QVERIFY(bus.registerObject(QStringLiteral("/PlasmaZones/ShellOverview"), &compositor,
                                   QDBusConnection::ExportAllSlots));
        {
            DesktopStage stage;
            stage.show(QStringLiteral("test-output"), QRectF(0.2, 0.15, 0.5, 0.5), false);
            QTRY_VERIFY(stage.active());
            QCOMPARE(compositor.lastScreen, QStringLiteral("test-output"));
            QCOMPARE(compositor.lastRect, QRectF(0.2, 0.15, 0.5, 0.5));
            stage.hide();
            QTRY_VERIFY(!compositor.opened);
            stage.show(QStringLiteral("test-output"), QRectF(0.2, 0.15, 0.5, 0.5), false);
            stage.hide();
            QTest::qWait(40);
            QVERIFY(!stage.active());
            QVERIFY(!compositor.opened);
            stage.show(QStringLiteral("test-output"), QRectF(0.2, 0.15, 0.5, 0.5), false);
            QTRY_VERIFY(stage.active());
        }
        QTRY_VERIFY(!compositor.opened);
        bus.unregisterObject(QStringLiteral("/PlasmaZones/ShellOverview"));
        DesktopStage unavailable;
        unavailable.show(QStringLiteral("test-output"), QRectF(0.2, 0.15, 0.5, 0.5), false);
        QTest::qWait(40);
        QVERIFY(!unavailable.active());
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
    }
};
QTEST_GUILESS_MAIN(TestDesktopStage)
#include "test_desktopstage.moc"

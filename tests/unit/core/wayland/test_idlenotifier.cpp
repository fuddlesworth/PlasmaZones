// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorWayland/IdleNotifier.h>

#include <QTest>
#include <QSignalSpy>
#include <QGuiApplication>

using namespace PhosphorWayland;

class TestIdleNotifier : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testIsSupported_offscreen_returnsFalse()
    {
        QVERIFY(!IdleNotifier::isSupported());
    }

    void testDefaultTimeout_isZero()
    {
        IdleNotifier notifier;
        QCOMPARE(notifier.timeout(), std::chrono::milliseconds(0));
    }

    void testDefaultIdle_isFalse()
    {
        IdleNotifier notifier;
        QVERIFY(!notifier.isIdle());
    }

    void testSetTimeout_emitsOnChange()
    {
        IdleNotifier notifier;
        QSignalSpy spy(&notifier, &IdleNotifier::timeoutChanged);

        notifier.setTimeout(std::chrono::milliseconds(5000));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(notifier.timeout(), std::chrono::milliseconds(5000));

        notifier.setTimeout(std::chrono::milliseconds(5000));
        QCOMPARE(spy.count(), 1);
    }

    void testSetTimeout_zero_emitsAndResets()
    {
        IdleNotifier notifier;
        notifier.setTimeout(std::chrono::milliseconds(5000));

        QSignalSpy spy(&notifier, &IdleNotifier::timeoutChanged);
        notifier.setTimeout(std::chrono::milliseconds(0));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(notifier.timeout(), std::chrono::milliseconds(0));
    }
};

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    TestIdleNotifier tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_idlenotifier.moc"

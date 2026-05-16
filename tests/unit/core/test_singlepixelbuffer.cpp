// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorWayland/SinglePixelBuffer.h>

#include <QTest>
#include <QSignalSpy>
#include <QGuiApplication>

using namespace PhosphorWayland;

class TestSinglePixelBuffer : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testIsSupported_offscreen_returnsFalse()
    {
        QVERIFY(!SinglePixelBuffer::isSupported());
    }

    void testConstruction_setsColor()
    {
        SinglePixelBuffer buffer(QColor(255, 0, 0));
        QCOMPARE(buffer.color(), QColor(255, 0, 0));
    }

    void testSetColor_emitsOnChange()
    {
        SinglePixelBuffer buffer(QColor(255, 0, 0));
        QSignalSpy spy(&buffer, &SinglePixelBuffer::colorChanged);

        buffer.setColor(QColor(0, 255, 0));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(buffer.color(), QColor(0, 255, 0));

        buffer.setColor(QColor(0, 255, 0));
        QCOMPARE(spy.count(), 1);
    }

    void testAttachTo_noCompositor_returnsFalse()
    {
        SinglePixelBuffer buffer(QColor(255, 0, 0));
        QWindow window;
        QVERIFY(!buffer.attachTo(&window));
    }

    void testAttachTo_null_returnsFalse()
    {
        SinglePixelBuffer buffer(QColor(255, 0, 0));
        QVERIFY(!buffer.attachTo(nullptr));
    }
};

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    TestSinglePixelBuffer tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_singlepixelbuffer.moc"

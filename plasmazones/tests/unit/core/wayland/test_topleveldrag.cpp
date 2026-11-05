// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorWayland/ToplevelDrag.h>

#include <QTest>
#include <QGuiApplication>

class TestToplevelDrag : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testIsSupported_offscreen_returnsFalse()
    {
        QVERIFY(!PhosphorWayland::isToplevelDragSupported());
    }
};

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    TestToplevelDrag tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_topleveldrag.moc"

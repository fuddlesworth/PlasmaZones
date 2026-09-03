// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// PanelWindow property setters: the clamps and the change-gated notifies.
// These matter because ShellEngine feeds thickness() straight into
// visibleBand and the exclusive zone: a broken clamp bypasses the tested
// degenerate-input guard in the band math, and an over-eager notify makes
// every QML binding on the panel re-evaluate for a write that changed
// nothing.

#include <PhosphorShell/PanelWindow.h>

#include <QRegularExpression>
#include <QSignalSpy>
#include <QtTest/QtTest>

using PhosphorShell::PanelWindow;

class TestPanelWindowSetters : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void thicknessClampsToOneAndWarns();
    void shadowSizeAndCarveClampToZero();
    void settersNotifyOncePerRealChange();
};

void TestPanelWindowSetters::thicknessClampsToOneAndWarns()
{
    PanelWindow panel;
    QSignalSpy spy(&panel, &PanelWindow::thicknessChanged);

    // Wayland rejects 0-thickness surfaces, so 0 and negatives coerce to 1,
    // with a diagnostic (the silent 1 px panel was the bug).
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("clamped to")));
    panel.setThickness(0);
    QCOMPARE(panel.thickness(), 1);
    QCOMPARE(spy.count(), 1);

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("clamped to")));
    panel.setThickness(-5);
    // Already 1, so the clamped re-write must not notify again.
    QCOMPARE(panel.thickness(), 1);
    QCOMPARE(spy.count(), 1);

    panel.setThickness(44);
    QCOMPARE(panel.thickness(), 44);
    QCOMPARE(spy.count(), 2);
}

void TestPanelWindowSetters::shadowSizeAndCarveClampToZero()
{
    PanelWindow panel;

    panel.setShadowSize(-3);
    QCOMPARE(panel.shadowSize(), 0);
    panel.setShadowSize(12);
    QCOMPARE(panel.shadowSize(), 12);

    panel.setCornerCarveRadius(-1);
    QCOMPARE(panel.cornerCarveRadius(), 0);
    panel.setCornerCarveRadius(8);
    QCOMPARE(panel.cornerCarveRadius(), 8);
}

void TestPanelWindowSetters::settersNotifyOncePerRealChange()
{
    PanelWindow panel;

    QSignalSpy edgeSpy(&panel, &PanelWindow::edgeChanged);
    panel.setEdge(PanelWindow::Bottom);
    panel.setEdge(PanelWindow::Bottom);
    QCOMPARE(edgeSpy.count(), 1);

    QSignalSpy shadowSpy(&panel, &PanelWindow::shadowSizeChanged);
    panel.setShadowSize(6);
    panel.setShadowSize(6);
    QCOMPARE(shadowSpy.count(), 1);

    QSignalSpy zoneSpy(&panel, &PanelWindow::exclusiveZoneChanged);
    panel.setExclusiveZone(30);
    panel.setExclusiveZone(30);
    QCOMPARE(zoneSpy.count(), 1);

    QSignalSpy lengthSpy(&panel, &PanelWindow::panelLengthChanged);
    panel.setPanelLength(400);
    panel.setPanelLength(400);
    QCOMPARE(lengthSpy.count(), 1);
}

QTEST_MAIN(TestPanelWindowSetters)

#include "test_panelwindow_setters.moc"

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
    void interactiveThicknessFollowsThicknessUntilSet();
};

// The input band's depth is the ONE panel geometry ShellEngine samples
// live, so the "0 means follow thickness" rule has to hold exactly: a
// wrong answer here either freezes a bar's popout pocket click-through or
// makes the shadow strip permanently clickable.
void TestPanelWindowSetters::interactiveThicknessFollowsThicknessUntilSet()
{
    PanelWindow panel;
    panel.setThickness(44);
    QCOMPARE(panel.interactiveThickness(), 0);
    QCOMPARE(panel.effectiveInputThickness(), 44);

    // A live thickness change still shows through while unset.
    panel.setThickness(60);
    QCOMPARE(panel.effectiveInputThickness(), 60);

    // Set: the override wins, and it may exceed thickness — that is the
    // whole point, since the extra depth is the shadow strip the popout
    // paints into.
    QSignalSpy spy(&panel, &PanelWindow::interactiveThicknessChanged);
    panel.setInteractiveThickness(400);
    QCOMPARE(panel.effectiveInputThickness(), 400);
    QCOMPARE(spy.count(), 1);

    // Change-gated, like every other setter here.
    panel.setInteractiveThickness(400);
    QCOMPARE(spy.count(), 1);

    // Negative clamps to 0, which reverts to following thickness rather
    // than producing an empty band.
    panel.setInteractiveThickness(-5);
    QCOMPARE(panel.interactiveThickness(), 0);
    QCOMPARE(panel.effectiveInputThickness(), 60);
    QCOMPARE(spy.count(), 2);

    // Setting it back to 0 explicitly is the documented "close the pocket"
    // path, and must not leave a stale override behind.
    panel.setInteractiveThickness(300);
    panel.setInteractiveThickness(0);
    QCOMPARE(panel.effectiveInputThickness(), 60);
}

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

    // Each of these reads the value back as well as counting the notify. A
    // setter that emits exactly once but stores the wrong value, or nothing
    // at all, passes a count-only assertion.
    QSignalSpy edgeSpy(&panel, &PanelWindow::edgeChanged);
    panel.setEdge(PanelWindow::Bottom);
    panel.setEdge(PanelWindow::Bottom);
    QCOMPARE(edgeSpy.count(), 1);
    QCOMPARE(panel.edge(), PanelWindow::Bottom);

    QSignalSpy shadowSpy(&panel, &PanelWindow::shadowSizeChanged);
    panel.setShadowSize(6);
    panel.setShadowSize(6);
    QCOMPARE(shadowSpy.count(), 1);
    QCOMPARE(panel.shadowSize(), 6);

    QSignalSpy zoneSpy(&panel, &PanelWindow::exclusiveZoneChanged);
    panel.setExclusiveZone(30);
    panel.setExclusiveZone(30);
    QCOMPARE(zoneSpy.count(), 1);
    QCOMPARE(panel.exclusiveZone(), 30);

    QSignalSpy lengthSpy(&panel, &PanelWindow::panelLengthChanged);
    panel.setPanelLength(400);
    panel.setPanelLength(400);
    QCOMPARE(lengthSpy.count(), 1);
    QCOMPARE(panel.panelLength(), 400);

    // cornerCarveRadius was the one setter in this file with no change-gating
    // coverage at all, though the file's own header claims the gated notifies
    // as its subject.
    QSignalSpy carveSpy(&panel, &PanelWindow::cornerCarveRadiusChanged);
    panel.setCornerCarveRadius(12);
    panel.setCornerCarveRadius(12);
    QCOMPARE(carveSpy.count(), 1);
    QCOMPARE(panel.cornerCarveRadius(), 12);

    // exclusiveZoneEnabled is the one boolean here, and the only property
    // whose flip changes what the panel advertises to the compositor.
    QSignalSpy enabledSpy(&panel, &PanelWindow::exclusiveZoneEnabledChanged);
    QVERIFY(panel.exclusiveZoneEnabled());
    panel.setExclusiveZoneEnabled(false);
    panel.setExclusiveZoneEnabled(false);
    QCOMPARE(enabledSpy.count(), 1);
    QVERIFY(!panel.exclusiveZoneEnabled());
}

QTEST_MAIN(TestPanelWindowSetters)

#include "test_panelwindow_setters.moc"

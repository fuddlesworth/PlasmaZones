// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_screen_decisions.cpp
 * @brief The screen of a window filling a split output
 *        (plasmazoneseffect/windowscreendecisions.h): an axis-maximized
 *        window's at its frame on the free axis (F1057), else the engine's for
 *        a tiled window, else the virtual screen holding its restore rect, else
 *        its last stamp, and only ever a live virtual screen of that output
 *        (F507, F527, F528).
 */

#include "plasmazoneseffect/windowscreendecisions.h"

#include <QTest>

using namespace PlasmaZones::WindowScreenDecisions;

namespace {
FillingWindow onSplitOutput()
{
    FillingWindow in;
    in.outputScreens = {QStringLiteral("DP-1/vs:0"), QStringLiteral("DP-1/vs:1")};
    return in;
}
} // namespace

class TestWindowScreenDecisions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // A monocle tile moved between two monocle virtual screens keeps the
    // restore rect of the one it left, so the engine's screen comes first.
    void engineScreenFirst()
    {
        FillingWindow in = onSplitOutput();
        in.engineScreen = QStringLiteral("DP-1/vs:1");
        in.restoreScreen = QStringLiteral("DP-1/vs:0");
        in.trackedScreen = QStringLiteral("DP-1/vs:0");
        QCOMPARE(resolveFilling(in), QStringLiteral("DP-1/vs:1"));
    }

    void restoreRectForAnUntrackedWindow()
    {
        FillingWindow in = onSplitOutput();
        in.restoreScreen = QStringLiteral("DP-1/vs:0");
        in.trackedScreen = QStringLiteral("DP-1/vs:1");
        QCOMPARE(resolveFilling(in), QStringLiteral("DP-1/vs:0"));
    }

    void trackedOnlyAsATieBreak()
    {
        FillingWindow in = onSplitOutput();
        in.trackedScreen = QStringLiteral("DP-1/vs:0");
        QCOMPARE(resolveFilling(in), QStringLiteral("DP-1/vs:0"));
    }

    // A screen that no longer exists is never answered; the caller keeps the
    // positional answer.
    void deadIdsFallThrough()
    {
        FillingWindow in = onSplitOutput();
        in.engineScreen = QStringLiteral("DP-1/vs:2");
        in.trackedScreen = QStringLiteral("DP-1/vs:3");
        QVERIFY(resolveFilling(in).isEmpty());
    }

    void nothingKnownFallsThrough()
    {
        QVERIFY(resolveFilling(onSplitOutput()).isEmpty());
    }

    // A vertically maximized window moved from the left half to the right
    // keeps its maximize and the restore rect of the half it left, so the
    // frame decides, even over a tile's engine screen (F1057).
    void axisScreenFirst()
    {
        FillingWindow in = onSplitOutput();
        in.axisScreen = QStringLiteral("DP-1/vs:1");
        in.engineScreen = QStringLiteral("DP-1/vs:0");
        in.restoreScreen = QStringLiteral("DP-1/vs:0");
        in.trackedScreen = QStringLiteral("DP-1/vs:0");
        QCOMPARE(resolveFilling(in), QStringLiteral("DP-1/vs:1"));
    }

    // Vertically maximized: x from the frame, which KWin moved without
    // touching the restore rect, y from the restore rect.
    void axisProbe_verticalTakesTheFrameX()
    {
        const QRectF output(0, 0, 2560, 1440);
        const QRectF frame(1600, 0, 600, 1440);
        const QRectF restore(200, 300, 600, 500);
        QCOMPARE(axisMaximizedProbe(frame, restore, output, /*verticallyMaximized=*/true), QPointF(1900, 550));
    }

    // A vertically maximized window whose restore rect hangs below the
    // output: its y is clamped inside, so a top/bottom split still names the
    // bottom half rather than a point off the output.
    void axisProbe_verticalClampsTheRestoreY()
    {
        const QRectF output(0, 0, 2560, 1440);
        const QRectF frame(1600, 0, 600, 1440);
        const QRectF restore(200, 1200, 600, 800);
        QCOMPARE(axisMaximizedProbe(frame, restore, output, /*verticallyMaximized=*/true), QPointF(1900, 1439));
    }

    // Horizontally maximized: y from the frame, x from the restore rect,
    // clamped into the output when the restore rect lies beyond it.
    void axisProbe_horizontalTakesTheFrameYClamped()
    {
        const QRectF output(0, 0, 2560, 1440);
        const QRectF frame(0, 900, 2560, 400);
        const QRectF restore(2400, 100, 800, 400);
        QCOMPARE(axisMaximizedProbe(frame, restore, output, /*verticallyMaximized=*/false), QPointF(2559, 1100));
    }
};

QTEST_MAIN(TestWindowScreenDecisions)
#include "test_window_screen_decisions.moc"

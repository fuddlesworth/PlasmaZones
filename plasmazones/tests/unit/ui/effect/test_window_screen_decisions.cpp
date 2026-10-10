// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_screen_decisions.cpp
 * @brief The screen of a window filling a split output
 *        (plasmazoneseffect/windowscreendecisions.h): the engine's for a tiled
 *        window, else the virtual screen holding its restore rect, else its
 *        last stamp, and only ever a live virtual screen of that output
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
};

QTEST_MAIN(TestWindowScreenDecisions)
#include "test_window_screen_decisions.moc"

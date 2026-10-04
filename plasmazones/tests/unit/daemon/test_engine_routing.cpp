// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Pins EngineRouting::windowEngineMode, the one answer to "which engine owns
// this window" that the float reader and writer, the autotile-mode predicate
// and the synthesized-slot engine id share.

#include <QObject>
#include <QTest>

#include "daemon/daemon/enginerouting.h"
#include <PhosphorEngine/WindowPlacement.h>

using namespace PlasmaZones;
using Mode = PhosphorZones::AssignmentEntry::Mode;

class TestEngineRouting : public QObject
{
    Q_OBJECT

private:
    /// Screen DP-1 runs @p dp1Mode on desktop 1, every other context snapping.
    static EngineRouting::Inputs inputs(Mode dp1Mode)
    {
        EngineRouting::Inputs in;
        in.trackedScreen = [](const QString& windowId) {
            return windowId.startsWith(QLatin1String("tracked")) ? QStringLiteral("DP-1") : QString();
        };
        in.placeOnScreen = [](const QString&, const QString&) {
            return EngineRouting::WindowPlace{1, QString()};
        };
        in.configuredMode = [dp1Mode](const QString& screenId, int desktop, const QString&) {
            return (screenId == QLatin1String("DP-1") && desktop == 1) ? dp1Mode
                                                                       : PhosphorZones::AssignmentEntry::Snapping;
        };
        return in;
    }

private Q_SLOTS:
    // A tracked screen answers with that screen's configured mode.
    void trackedScreen_answersItsConfiguredMode()
    {
        const EngineRouting::Inputs in = inputs(PhosphorZones::AssignmentEntry::Scrolling);
        QCOMPARE(EngineRouting::windowEngineMode(in, QStringLiteral("tracked|a")),
                 PhosphorZones::AssignmentEntry::Scrolling);
    }

    // An explicit screen replaces the tracked one (a close names its screen).
    void screenOverride_winsOverTheTrackedScreen()
    {
        const EngineRouting::Inputs in = inputs(PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(EngineRouting::windowEngineMode(in, QStringLiteral("tracked|a"), QStringLiteral("DP-2")),
                 PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(EngineRouting::windowEngineMode(in, QStringLiteral("untracked|a"), QStringLiteral("DP-1")),
                 PhosphorZones::AssignmentEntry::Autotile);
    }

    // No screen at all: a tiling engine tracking the window wins, else Snapping.
    void noScreen_fallsBackToTheTrackingEngineThenSnapping()
    {
        EngineRouting::Inputs in = inputs(PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(EngineRouting::windowEngineMode(in, QStringLiteral("untracked|a")),
                 PhosphorZones::AssignmentEntry::Snapping);
        in.scrollTracks = [](const QString&) {
            return true;
        };
        QCOMPARE(EngineRouting::windowEngineMode(in, QStringLiteral("untracked|a")),
                 PhosphorZones::AssignmentEntry::Scrolling);
    }

    void engineIdForMode_namesEachEngine()
    {
        QCOMPARE(EngineRouting::engineIdForMode(PhosphorZones::AssignmentEntry::Autotile),
                 QString(PhosphorEngine::WindowPlacement::autotileEngineId()));
        QCOMPARE(EngineRouting::engineIdForMode(PhosphorZones::AssignmentEntry::Scrolling),
                 QString(PhosphorEngine::WindowPlacement::scrollingEngineId()));
        QCOMPARE(EngineRouting::engineIdForMode(PhosphorZones::AssignmentEntry::Snapping),
                 QString(PhosphorEngine::WindowPlacement::snapEngineId()));
    }
};

QTEST_GUILESS_MAIN(TestEngineRouting)
#include "test_engine_routing.moc"

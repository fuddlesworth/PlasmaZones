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

    // No screen and no hold in view: snapping. A tiling engine tracking the
    // window only in another context does not own it here (F113).
    void noScreen_noHold_isSnapping()
    {
        const EngineRouting::Inputs in = inputs(PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(EngineRouting::windowEngineMode(in, QStringLiteral("untracked|a")),
                 PhosphorZones::AssignmentEntry::Snapping);
    }

    // A tiling engine holding the window in view owns it, whatever screen the
    // tracking service still has on record for it (F113).
    void heldInView_winsOverAStaleTrackedScreen()
    {
        EngineRouting::Inputs in = inputs(PhosphorZones::AssignmentEntry::Snapping);
        in.autotileHeldScreen = [](const QString& windowId) {
            return windowId == QLatin1String("tracked|held") ? QStringLiteral("DP-2") : QString();
        };
        QCOMPARE(EngineRouting::windowEngineMode(in, QStringLiteral("tracked|held")),
                 PhosphorZones::AssignmentEntry::Autotile);
        in.autotileHeldScreen = nullptr;
        in.scrollHeldScreen = [](const QString&) {
            return QStringLiteral("DP-2");
        };
        QCOMPARE(EngineRouting::windowEngineMode(in, QStringLiteral("tracked|held")),
                 PhosphorZones::AssignmentEntry::Scrolling);
        // An explicit screen skips the hold check: a close names its screen.
        QCOMPARE(EngineRouting::windowEngineMode(in, QStringLiteral("tracked|held"), QStringLiteral("DP-1")),
                 PhosphorZones::AssignmentEntry::Snapping);
    }

    // In view, the mode the screen RUNS: scrolling configured but switched off
    // runs snapping, so the float routes to snap (F112). Out of view (another
    // desktop of a multi-desktop window), the configured mode of that context.
    void inView_answersTheLiveMode_outOfView_theConfiguredOne()
    {
        EngineRouting::Inputs in = inputs(PhosphorZones::AssignmentEntry::Scrolling);
        in.liveMode = [](const QString&) {
            return PhosphorZones::AssignmentEntry::Snapping;
        };
        QCOMPARE(EngineRouting::windowEngineMode(in, QStringLiteral("tracked|a")),
                 PhosphorZones::AssignmentEntry::Snapping);
        in.placeOnScreen = [](const QString&, const QString&) {
            return EngineRouting::WindowPlace{1, QString(), false};
        };
        QCOMPARE(EngineRouting::windowEngineMode(in, QStringLiteral("tracked|a")),
                 PhosphorZones::AssignmentEntry::Scrolling);
    }

    // The float list names each window once, and only where its owning engine
    // floats it: a background store's float bit is not a user float (F309).
    void ownedFloatingWindows_dedupsAndKeepsOnlyOwnedFloats()
    {
        const QStringList candidates{QStringLiteral("a|1"), QStringLiteral("b|2"), QStringLiteral("a|1"),
                                     QStringLiteral("c|3")};
        const QStringList owned = EngineRouting::ownedFloatingWindows(candidates, [](const QString& windowId) {
            return windowId != QLatin1String("b|2");
        });
        QCOMPARE(owned, (QStringList{QStringLiteral("a|1"), QStringLiteral("c|3")}));
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

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorContext/ContextResolver.h>
#include <PhosphorContext/IContextInputs.h>

#include <QObject>
#include <QString>
#include <QTest>

#include <set>
#include <tuple>

using PhosphorContext::ContextHandle;
using PhosphorContext::ContextResolver;
using PhosphorContext::DisabledReason;
using PhosphorContext::IContextGateSource;
using PhosphorContext::IModeProvider;
using PhosphorContext::IWorkspaceState;
using PhosphorZones::AssignmentEntry;

namespace {

/// Mutable test fake — the test scenario sets fields directly to drive
/// the resolver. No QObject so we can construct one in test stack scope.
class FakeWorkspaceState : public IWorkspaceState
{
public:
    int desktop = 0;
    QString act;

    int currentVirtualDesktop() const override
    {
        return desktop;
    }
    QString currentActivity() const override
    {
        return act;
    }
};

class FakeModeProvider : public IModeProvider
{
public:
    QHash<QString, AssignmentEntry::Mode> modes;
    AssignmentEntry::Mode fallback = AssignmentEntry::Snapping;

    AssignmentEntry::Mode modeFor(const QString& screenId) const override
    {
        // Unknown screen id → documented fallback (per IModeProvider's
        // contract). Tests asserting fallback behaviour leave the hash
        // empty and read the fallback directly.
        return modes.value(screenId, fallback);
    }
};

/// Programmable gate source — every check returns whatever the test
/// scenario sets in the corresponding `*Hits` set. Allows isolating one
/// leg of the cascade per test case.
class FakeGateSource : public IContextGateSource
{
public:
    // std::set/tuple instead of QSet because QHash needs a qHash overload
    // for tuple<...>; std::tuple has a built-in operator< that std::set
    // uses out of the box. The (int) cast on Mode keeps the lookup-key
    // type stable across enum-value additions.
    std::set<std::tuple<int, QString>> monitorDisabled;
    std::set<std::tuple<int, QString, int>> desktopDisabled;
    std::set<std::tuple<int, QString, QString>> activityDisabled;
    std::set<std::tuple<int, QString, int, QString>> locked;

    bool isMonitorDisabled(AssignmentEntry::Mode mode, const QString& screenId) const override
    {
        return monitorDisabled.count(std::make_tuple(static_cast<int>(mode), screenId)) > 0;
    }
    bool isDesktopDisabled(AssignmentEntry::Mode mode, const QString& screenId, int desktop) const override
    {
        return desktopDisabled.count(std::make_tuple(static_cast<int>(mode), screenId, desktop)) > 0;
    }
    bool isActivityDisabled(AssignmentEntry::Mode mode, const QString& screenId, const QString& activity) const override
    {
        return activityDisabled.count(std::make_tuple(static_cast<int>(mode), screenId, activity)) > 0;
    }
    bool isContextLocked(AssignmentEntry::Mode mode, const QString& screenId, int desktop,
                         const QString& activity) const override
    {
        return locked.count(std::make_tuple(static_cast<int>(mode), screenId, desktop, activity)) > 0;
    }
};

} // namespace

class TestContextResolver : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void handleForCapturesAllThreeAxes()
    {
        FakeWorkspaceState ws;
        ws.desktop = 3;
        ws.act = QStringLiteral("activity-a");
        FakeModeProvider mp;
        mp.modes[QStringLiteral("HDMI-1")] = AssignmentEntry::Autotile;
        FakeGateSource gs;
        ContextResolver resolver(&ws, &mp, &gs);

        const auto handle = resolver.handleFor(QStringLiteral("HDMI-1"));
        QCOMPARE(handle.screenId, QStringLiteral("HDMI-1"));
        QCOMPARE(handle.virtualDesktop, 3);
        QCOMPARE(handle.activity, QStringLiteral("activity-a"));
        QCOMPARE(handle.mode, AssignmentEntry::Autotile);
    }

    void handleForEmptyScreenFallsBackToDefaultMode()
    {
        FakeWorkspaceState ws;
        FakeModeProvider mp;
        FakeGateSource gs;
        ContextResolver resolver(&ws, &mp, &gs);

        const auto handle = resolver.handleFor(QString());
        QCOMPARE(handle.screenId, QString());
        QCOMPARE(handle.mode, AssignmentEntry::Snapping);
    }

    void globalHandleHasNoScreenButSnapshotsWorkspace()
    {
        FakeWorkspaceState ws;
        ws.desktop = 2;
        ws.act = QStringLiteral("activity-b");
        FakeModeProvider mp;
        FakeGateSource gs;
        ContextResolver resolver(&ws, &mp, &gs);

        const auto handle = resolver.globalHandle();
        QCOMPARE(handle.screenId, QString());
        QCOMPARE(handle.virtualDesktop, 2);
        QCOMPARE(handle.activity, QStringLiteral("activity-b"));
        QCOMPARE(handle.mode, AssignmentEntry::Snapping);
    }

    void handleForModeOverridesProvider()
    {
        FakeWorkspaceState ws;
        FakeModeProvider mp;
        mp.modes[QStringLiteral("HDMI-1")] = AssignmentEntry::Snapping;
        FakeGateSource gs;
        ContextResolver resolver(&ws, &mp, &gs);

        const auto handle = resolver.handleForMode(QStringLiteral("HDMI-1"), AssignmentEntry::Autotile);
        QCOMPARE(handle.screenId, QStringLiteral("HDMI-1"));
        QCOMPARE(handle.mode, AssignmentEntry::Autotile);
    }

    void globalHandleDefaultsToModeProviderEmptyScreenContract()
    {
        // `globalHandle` delegates the "no-screen → default mode" decision
        // to `IModeProvider::modeFor(QString())` instead of hardcoding
        // Snapping; a regression that re-hardcoded the resolver-side
        // fallback would pass `globalHandleHasNoScreenButSnapshotsWorkspace`
        // (which uses the default Snapping fallback) but fail this test.
        FakeWorkspaceState ws;
        FakeModeProvider mp;
        mp.fallback = AssignmentEntry::Autotile;
        FakeGateSource gs;
        ContextResolver resolver(&ws, &mp, &gs);

        const auto handle = resolver.globalHandle();
        QCOMPARE(handle.mode, AssignmentEntry::Autotile);
    }

    void handleForPersistedClampsNegativeVirtualDesktop()
    {
        // Persisted-on-disk virtualDesktop comes through a system boundary
        // — a hand-edited config with a negative value must clamp to 0
        // (the "pinned across all desktops" sentinel) rather than reach
        // the IContextGateSource adapter with an undefined value.
        FakeWorkspaceState ws;
        FakeModeProvider mp;
        FakeGateSource gs;
        ContextResolver resolver(&ws, &mp, &gs);

        const auto handle =
            resolver.handleForPersisted(QStringLiteral("HDMI-1"), /*virtualDesktop=*/-5, QStringLiteral("act"));
        QCOMPARE(handle.virtualDesktop, 0);
    }

    void handleForPersistedResolvesModeFromCurrentRouting()
    {
        // Persisted (desktop, activity) come from disk; mode is the
        // screen's CURRENT routing. So `handleForPersisted("HDMI-1", 3, act)`
        // on a screen the mode provider routes to Autotile must yield a
        // handle whose `mode == Autotile`, even though the caller did
        // not pass any Mode argument.
        FakeWorkspaceState ws;
        FakeModeProvider mp;
        mp.modes[QStringLiteral("HDMI-1")] = AssignmentEntry::Autotile;
        FakeGateSource gs;
        ContextResolver resolver(&ws, &mp, &gs);

        const auto handle = resolver.handleForPersisted(QStringLiteral("HDMI-1"), 3, QStringLiteral("act"));
        QCOMPARE(handle.mode, AssignmentEntry::Autotile);
        QCOMPARE(handle.virtualDesktop, 3);
        QCOMPARE(handle.activity, QStringLiteral("act"));
    }

    void disabledReasonNotDisabledByDefault()
    {
        FakeWorkspaceState ws;
        FakeModeProvider mp;
        FakeGateSource gs;
        ContextResolver resolver(&ws, &mp, &gs);

        const auto handle = resolver.handleFor(QStringLiteral("HDMI-1"));
        QCOMPARE(resolver.disabledReason(handle), DisabledReason::NotDisabled);
        QVERIFY(!resolver.isDisabled(handle));
    }

    void disabledReasonReturnsMonitorWhenMonitorTrips()
    {
        FakeWorkspaceState ws;
        FakeModeProvider mp;
        FakeGateSource gs;
        gs.monitorDisabled.insert(
            std::make_tuple(static_cast<int>(AssignmentEntry::Snapping), QStringLiteral("HDMI-1")));
        ContextResolver resolver(&ws, &mp, &gs);

        const auto handle = resolver.handleFor(QStringLiteral("HDMI-1"));
        QCOMPARE(resolver.disabledReason(handle), DisabledReason::MonitorDisabled);
        QVERIFY(resolver.isDisabled(handle));
    }

    void disabledReasonMonitorShortCircuitsDesktopAndActivity()
    {
        // All three legs would trip — verify the cascade reports only
        // the highest-priority one.
        FakeWorkspaceState ws;
        ws.desktop = 2;
        ws.act = QStringLiteral("activity-a");
        FakeModeProvider mp;
        FakeGateSource gs;
        const int snapping = static_cast<int>(AssignmentEntry::Snapping);
        gs.monitorDisabled.insert(std::make_tuple(snapping, QStringLiteral("HDMI-1")));
        gs.desktopDisabled.insert(std::make_tuple(snapping, QStringLiteral("HDMI-1"), 2));
        gs.activityDisabled.insert(std::make_tuple(snapping, QStringLiteral("HDMI-1"), QStringLiteral("activity-a")));
        ContextResolver resolver(&ws, &mp, &gs);

        const auto handle = resolver.handleFor(QStringLiteral("HDMI-1"));
        QCOMPARE(resolver.disabledReason(handle), DisabledReason::MonitorDisabled);
    }

    void disabledReasonReturnsDesktopWhenOnlyDesktopTrips()
    {
        FakeWorkspaceState ws;
        ws.desktop = 5;
        FakeModeProvider mp;
        FakeGateSource gs;
        gs.desktopDisabled.insert(
            std::make_tuple(static_cast<int>(AssignmentEntry::Snapping), QStringLiteral("HDMI-1"), 5));
        ContextResolver resolver(&ws, &mp, &gs);

        const auto handle = resolver.handleFor(QStringLiteral("HDMI-1"));
        QCOMPARE(resolver.disabledReason(handle), DisabledReason::DesktopDisabled);
    }

    void disabledReasonReturnsActivityWhenOnlyActivityTrips()
    {
        FakeWorkspaceState ws;
        ws.act = QStringLiteral("activity-z");
        FakeModeProvider mp;
        FakeGateSource gs;
        gs.activityDisabled.insert(std::make_tuple(static_cast<int>(AssignmentEntry::Snapping),
                                                   QStringLiteral("HDMI-1"), QStringLiteral("activity-z")));
        ContextResolver resolver(&ws, &mp, &gs);

        const auto handle = resolver.handleFor(QStringLiteral("HDMI-1"));
        QCOMPARE(resolver.disabledReason(handle), DisabledReason::ActivityDisabled);
    }

    void disabledReasonRespectsModeAxis()
    {
        // A monitor disabled for Snapping is NOT disabled for Autotile —
        // each mode has its own list.
        FakeWorkspaceState ws;
        FakeModeProvider mp;
        FakeGateSource gs;
        gs.monitorDisabled.insert(
            std::make_tuple(static_cast<int>(AssignmentEntry::Snapping), QStringLiteral("HDMI-1")));
        ContextResolver resolver(&ws, &mp, &gs);

        const auto snapHandle = resolver.handleForMode(QStringLiteral("HDMI-1"), AssignmentEntry::Snapping);
        const auto autoHandle = resolver.handleForMode(QStringLiteral("HDMI-1"), AssignmentEntry::Autotile);
        QCOMPARE(resolver.disabledReason(snapHandle), DisabledReason::MonitorDisabled);
        QCOMPARE(resolver.disabledReason(autoHandle), DisabledReason::NotDisabled);
    }

    void isLockedRoutesThroughGateSource()
    {
        FakeWorkspaceState ws;
        ws.desktop = 1;
        ws.act = QStringLiteral("activity-a");
        FakeModeProvider mp;
        FakeGateSource gs;
        gs.locked.insert(std::make_tuple(static_cast<int>(AssignmentEntry::Snapping), QStringLiteral("HDMI-1"), 1,
                                         QStringLiteral("activity-a")));
        ContextResolver resolver(&ws, &mp, &gs);

        const auto handle = resolver.handleFor(QStringLiteral("HDMI-1"));
        QVERIFY(resolver.isLocked(handle));
    }

    void isGatedCombinesDisableAndLock()
    {
        FakeWorkspaceState ws;
        FakeModeProvider mp;
        FakeGateSource gs;
        ContextResolver resolver(&ws, &mp, &gs);

        const auto clean = resolver.handleFor(QStringLiteral("HDMI-1"));
        QVERIFY(!resolver.isGated(clean));

        // Disabled alone → gated.
        gs.monitorDisabled.insert(
            std::make_tuple(static_cast<int>(AssignmentEntry::Snapping), QStringLiteral("HDMI-1")));
        QVERIFY(resolver.isGated(resolver.handleFor(QStringLiteral("HDMI-1"))));

        // Disabled cleared, locked alone → still gated.
        gs.monitorDisabled.clear();
        gs.locked.insert(
            std::make_tuple(static_cast<int>(AssignmentEntry::Snapping), QStringLiteral("HDMI-1"), 0, QString()));
        QVERIFY(resolver.isGated(resolver.handleFor(QStringLiteral("HDMI-1"))));

        // Both clear → not gated.
        gs.locked.clear();
        QVERIFY(!resolver.isGated(resolver.handleFor(QStringLiteral("HDMI-1"))));
    }

    void rawWorkspaceAccessorsMatchSnapshot()
    {
        FakeWorkspaceState ws;
        ws.desktop = 4;
        ws.act = QStringLiteral("activity-c");
        FakeModeProvider mp;
        FakeGateSource gs;
        ContextResolver resolver(&ws, &mp, &gs);

        QCOMPARE(resolver.currentVirtualDesktop(), 4);
        QCOMPARE(resolver.currentActivity(), QStringLiteral("activity-c"));
    }
};

QTEST_GUILESS_MAIN(TestContextResolver)
#include "test_contextresolver.moc"

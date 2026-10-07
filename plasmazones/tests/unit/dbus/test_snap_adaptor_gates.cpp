// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_snap_adaptor_gates.cpp
 * @brief The bus snap gate (SnapAdaptor::admitBusSnap): a D-Bus snap of one
 *        window lands only where a keyboard snap could, on the window's own
 *        desktop, and Control.snapWindowToZone resolves its screen the same
 *        way (F20, F142, F179, F21, F22, F26, F85). The focused-window verbs
 *        and a swap pass the same gates (F362, F79), and the effect's
 *        confirmations commit only real zones on real screens (F459).
 */

#include "wta_float_back_fixture.h"
#include "helpers/VirtualScreenTestHelpers.h"
#include "dbus/controladaptor.h"
#include "dbus/snapadaptor/snapadaptor.h"

#include <PhosphorContext/IContextResolver.h>
#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorRules/WindowQuery.h>

#include <QSignalSpy>
#include <QUuid>

namespace {

/// Answers MonitorDisabled for the screens in m_disabled.
class ScreenDisablingResolver : public PhosphorContext::IContextResolver
{
public:
    QSet<QString> m_disabled;

    PhosphorContext::ContextHandle handleFor(const QString& screenId) const override
    {
        PhosphorContext::ContextHandle h;
        h.screenId = screenId;
        return h;
    }
    PhosphorContext::ContextHandle globalHandle() const override
    {
        return {};
    }
    PhosphorContext::ContextHandle handleForMode(const QString& screenId,
                                                 PhosphorZones::AssignmentEntry::Mode mode) const override
    {
        PhosphorContext::ContextHandle h;
        h.screenId = screenId;
        h.mode = mode;
        return h;
    }
    PhosphorContext::ContextHandle handleForPersisted(const QString& screenId, int virtualDesktop,
                                                      const QString& activity) const override
    {
        PhosphorContext::ContextHandle h;
        h.screenId = screenId;
        h.virtualDesktop = virtualDesktop;
        h.activity = activity;
        return h;
    }
    int currentVirtualDesktop() const override
    {
        return 1;
    }
    QString currentActivity() const override
    {
        return QString();
    }
    PhosphorContext::DisabledReason disabledReason(const PhosphorContext::ContextHandle& h) const override
    {
        return m_disabled.contains(h.screenId) ? PhosphorContext::DisabledReason::MonitorDisabled
                                               : PhosphorContext::DisabledReason::NotDisabled;
    }
    bool isLocked(const PhosphorContext::ContextHandle&) const override
    {
        return false;
    }
};

struct GateFixture : FloatBackFixture
{
    GateFixture()
    {
        adaptor = new SnapAdaptor(snap.get(), wta, &settings, &parent);
        adaptor->setContextResolver(&resolver);
        layouts->assignLayout(kLeft, 1, QString(), layout);
        layouts->assignLayout(kRight, 1, QString(), layout);
    }
    ~GateFixture()
    {
        adaptor->setContextResolver(nullptr);
        adaptor->clearEngine();
    }
    /// A live window on @p desktop (0: every desktop), its frame on @p frame.
    QString registerOn(const QString& instance, int desktop, const QRect& frame = QRect(100, 100, 400, 300))
    {
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.virtualDesktop = desktop;
        meta.isMinimized = false;
        registry.upsert(instance, meta);
        const QString windowId = QStringLiteral("app|") + instance;
        registry.canonicalizeWindowId(windowId);
        setFrame(windowId, frame);
        return windowId;
    }
    /// The screen runs autotile, not snapping.
    void tile(const QString& screen)
    {
        snap->setLiveModeResolver([screen](const QString& screenId) {
            return screenId == screen ? PhosphorZones::AssignmentEntry::Mode::Autotile
                                      : PhosphorZones::AssignmentEntry::Mode::Snapping;
        });
    }
    ControlAdaptor* control()
    {
        return new ControlAdaptor(wta, adaptor, nullptr, layouts, nullptr, nullptr, nullptr, nullptr, nullptr, &parent);
    }

    SnapAdaptor* adaptor = nullptr; // parent-owned
    ScreenDisablingResolver resolver;
};

} // namespace

class TestSnapAdaptorGates : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void controlSnapsAFreeWindow()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("free-1"), 1);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->moveWindowToZoneOnScreen(w, f.zone(0), kLeft);
        QCOMPARE(applies.count(), 1);
        QCOMPARE(applies.first().at(6).toString(), kLeft);
        QCOMPARE(f.snap->zoneForWindow(w), f.zone(0));
    }

    void refusesAnUnregisteredWindow()
    {
        GateFixture f;
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->moveWindowToZoneOnScreen(QStringLiteral("app|ghost"), f.zone(0), kLeft);
        QCOMPARE(applies.count(), 0);
        QVERIFY(!f.snap->isWindowTracked(QStringLiteral("app|ghost")));
    }

    void refusesATilingScreen()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("tile-1"), 1);
        f.tile(kLeft);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->moveWindowToZoneOnScreen(w, f.zone(0), kLeft);
        QCOMPARE(applies.count(), 0);
        QVERIFY(f.snap->zoneForWindow(w).isEmpty());
    }

    void refusesWithSnappingOff()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("off-1"), 1);
        f.settings.setSnappingEnabled(false);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->moveWindowToZoneOnScreen(w, f.zone(0), kLeft);
        QCOMPARE(applies.count(), 0);
    }

    void refusesADisabledContext()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("disabled-1"), 1);
        f.resolver.m_disabled.insert(kLeft);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->moveWindowToZoneOnScreen(w, f.zone(0), kLeft);
        QCOMPARE(applies.count(), 0);
    }

    void refusesAnExcludedWindow()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("small-1"), 1);
        f.settings.setMinimumWindowWidth(500);
        f.snap->setExclusionQueryProvider([](const QString&, const QString&) {
            PhosphorRules::WindowQuery q;
            q.width = 100;
            return std::optional<PhosphorRules::WindowQuery>(q);
        });
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->moveWindowToZoneOnScreen(w, f.zone(0), kLeft);
        QCOMPARE(applies.count(), 0);
        f.snap->setExclusionQueryProvider({});
    }

    void refusesAnUnknownScreen()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("unknown-1"), 1);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->moveWindowToZoneOnScreen(w, f.zone(0), QStringLiteral("DP-9"));
        QCOMPARE(applies.count(), 0);
    }

    // A zone of a layout the landing context does not run commits nothing:
    // that is the ghost layout membership of #1104.
    void refusesAZoneTheLandingLayoutLacks()
    {
        GateFixture f;
        auto* other = createTestLayout(2, f.layouts);
        f.layouts->addLayout(other);
        f.layouts->assignLayout(kRight, 1, QString(), other);
        const QString w = f.registerOn(QStringLiteral("ghost-1"), 1);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->moveWindowToZoneOnScreen(w, f.zone(0), kRight);
        QCOMPARE(applies.count(), 0);
        QVERIFY(f.snap->zoneForWindow(w).isEmpty());
    }

    // A window on a desktop not in view is snapped into that desktop's layout
    // and store, not the one in view.
    void aHiddenWindowLandsOnItsOwnDesktop()
    {
        GateFixture f;
        auto* second = createTestLayout(2, f.layouts);
        f.layouts->addLayout(second);
        f.layouts->assignLayout(kLeft, 2, QString(), second);
        f.snap->setCurrentDesktopForScreen(kLeft, 1);
        const QString w = f.registerOn(QStringLiteral("hidden-1"), 2);
        const QString zone = second->zones().first()->id().toString();
        f.adaptor->moveWindowToZoneOnScreen(w, zone, kLeft);
        const PhosphorSnapEngine::SnapState* store = f.snap->stateForWindow(w);
        QVERIFY(store);
        QCOMPARE(f.snap->keyForState(store).value_or(PhosphorEngine::PlacementStateKey{}).desktop, 2);
        QCOMPARE(store->zoneForWindow(w), zone);
    }

    // On several desktops none of which is in view: which one is meant cannot
    // be told, so the snap is refused (user decision l13_bus_snap_hidden_multidesktop).
    void aWindowOnSeveralHiddenDesktopsIsRefused()
    {
        GateFixture f;
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.virtualDesktop = 2;
        meta.virtualDesktops = {2, 3};
        f.registry.upsert(QStringLiteral("span-1"), meta);
        const QString w = QStringLiteral("app|span-1");
        f.registry.canonicalizeWindowId(w);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->moveWindowToZoneOnScreen(w, f.zone(0), kLeft);
        QCOMPARE(applies.count(), 0);
    }

    // One layout on both outputs: a call naming no screen snaps on the one the
    // window is on, not the first that runs the layout (F21).
    void anEmptyHintPrefersTheWindowsScreen()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("right-1"), 1, QRect(2400, 200, 400, 300));
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->moveWindowToZone(w, f.zone(1));
        QCOMPARE(applies.count(), 1);
        QCOMPARE(applies.first().at(6).toString(), kRight);
    }

    // A connector name of a split monitor resolves to the virtual screen the
    // window is in (F22).
    void controlResolvesAConnectorOfASplitMonitor()
    {
        GateFixture f;
        QVERIFY(f.screenMgr->setVirtualScreenConfig(kRight, TestHelpers::makeSplitConfig(kRight)));
        const QString vs1 = QStringLiteral("DP-2/vs:1");
        f.layouts->assignLayout(vs1, 1, QString(), f.layout);
        const QString w = f.registerOn(QStringLiteral("split-1"), 1, QRect(3000, 200, 400, 300));
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.control()->snapWindowToZone(w, 1, kRight);
        QCOMPARE(applies.count(), 1);
        QCOMPARE(applies.first().at(6).toString(), vs1);
    }

    // No screen named: the window's own (F26).
    void controlWithNoScreenUsesTheWindowsScreen()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("own-1"), 1, QRect(2400, 200, 400, 300));
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.control()->snapWindowToZone(w, 1, QString());
        QCOMPARE(applies.count(), 1);
        QCOMPARE(applies.first().at(6).toString(), kRight);
    }

    // Any zone the layout has, not only the first nine (F85).
    void controlTakesZoneTen()
    {
        GateFixture f;
        auto* ten = createTestLayout(10, f.layouts);
        f.layouts->addLayout(ten);
        f.layouts->assignLayout(kLeft, 1, QString(), ten);
        const QString w = f.registerOn(QStringLiteral("ten-1"), 1);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.control()->snapWindowToZone(w, 10, kLeft);
        QCOMPARE(applies.count(), 1);
        QCOMPARE(applies.first().at(5).toString(), ten->zoneByNumber(10)->id().toString());
    }

    // The placement map names the monitor its cell is on: a zone number on
    // another monitor moves the focused window there (F362).
    void zoneNumberOnAnotherMonitorMovesThere()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("map-1"), 1);
        f.snap->commitSnap(w, f.zone(0), kLeft);
        f.wta->windowActivated(w, kLeft);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->snapToZoneByNumber(2, kRight);
        QCOMPARE(applies.count(), 1);
        QCOMPARE(applies.first().at(6).toString(), kRight);
        QCOMPARE(applies.first().at(5).toString(), f.layout->zoneByNumber(2)->id().toString());
    }

    void zoneNumberTenOnANamedScreen()
    {
        GateFixture f;
        auto* ten = createTestLayout(10, f.layouts);
        f.layouts->addLayout(ten);
        f.layouts->assignLayout(kRight, 1, QString(), ten);
        const QString w = f.registerOn(QStringLiteral("map-10"), 1);
        f.wta->windowActivated(w, kLeft);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->snapToZoneByNumber(10, kRight);
        QCOMPARE(applies.count(), 1);
        QCOMPARE(applies.first().at(5).toString(), ten->zoneByNumber(10)->id().toString());
    }

    void pushOnANamedScreenTakesItsFirstEmptyZone()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("push-1"), 1);
        f.wta->windowActivated(w, kLeft);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->pushToEmptyZone(kRight);
        QCOMPARE(applies.count(), 1);
        QCOMPARE(applies.first().at(6).toString(), kRight);
    }

    // The focused-window verbs pass the keyboard gates: a snapped window on a
    // monitor that now tiles is not re-snapped from its frozen memory.
    void forwardersRefuseATilingScreen()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("frozen-1"), 1, QRect(2400, 200, 400, 300));
        f.snap->commitSnap(w, f.zone(0), kRight);
        f.wta->windowActivated(w, kRight);
        f.tile(kRight);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->snapToZoneByNumber(2, QString());
        f.adaptor->moveWindowToAdjacentZone(QStringLiteral("right"));
        QCOMPARE(applies.count(), 0);
        QCOMPARE(f.snap->zoneForWindow(w), f.zone(0));
    }

    void forwardersRespectTheMasterSwitch()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("switch-1"), 1);
        f.snap->commitSnap(w, f.zone(0), kLeft);
        f.wta->windowActivated(w, kLeft);
        f.settings.setSnappingEnabled(false);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->snapToZoneByNumber(2, QString());
        QCOMPARE(applies.count(), 0);
    }
    // A bus swap commits both windows, each on the other's screen (F848).
    void swapCommitsBothAndPairsTheScreens()
    {
        GateFixture f;
        const QString a = f.registerOn(QStringLiteral("swap-a"), 1);
        const QString b = f.registerOn(QStringLiteral("swap-b"), 1, QRect(2400, 200, 400, 300));
        f.snap->commitSnap(a, f.zone(0), kLeft);
        f.snap->commitSnap(b, f.zone(1), kRight);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->swapWindowsById(a, b);
        QCOMPARE(applies.count(), 2);
        QCOMPARE(f.snap->zoneForWindow(a), f.zone(1));
        QCOMPARE(f.wta->service()->screenForWindow(a), kRight);
        QCOMPARE(f.snap->zoneForWindow(b), f.zone(0));
        QCOMPARE(f.wta->service()->screenForWindow(b), kLeft);
        QCOMPARE(applies.at(0).at(8).toInt(), static_cast<int>(PhosphorProtocol::PlacementPurpose::UserVerb));
        QCOMPARE(applies.at(1).at(8).toInt(), static_cast<int>(PhosphorProtocol::PlacementPurpose::Restatement));
    }

    // A window spanning two zones hands the whole span to its partner (F79).
    void swapKeepsASpan()
    {
        GateFixture f;
        const QString a = f.registerOn(QStringLiteral("span-a"), 1);
        const QString b = f.registerOn(QStringLiteral("span-b"), 1);
        const QStringList span{f.zone(0), f.zone(1)};
        f.snap->commitMultiZoneSnap(a, span, kLeft);
        f.snap->commitSnap(b, f.zone(2), kLeft);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->swapWindowsById(a, b);
        QCOMPARE(applies.count(), 2);
        QCOMPARE(f.wta->service()->zonesForWindow(b), span);
        QCOMPARE(f.wta->service()->zonesForWindow(a), QStringList{f.zone(2)});
        const QRect unionRect = f.wta->service()->multiZoneGeometry(span, kLeft);
        QCOMPARE(QRect(applies.at(1).at(1).toInt(), applies.at(1).at(2).toInt(), applies.at(1).at(3).toInt(),
                       applies.at(1).at(4).toInt()),
                 unionRect);
    }

    void swapRefusesATilingScreen()
    {
        GateFixture f;
        const QString a = f.registerOn(QStringLiteral("tswap-a"), 1);
        const QString b = f.registerOn(QStringLiteral("tswap-b"), 1, QRect(2400, 200, 400, 300));
        f.snap->commitSnap(a, f.zone(0), kLeft);
        f.snap->commitSnap(b, f.zone(1), kRight);
        f.tile(kRight);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->swapWindowsById(a, b);
        QCOMPARE(applies.count(), 0);
        QCOMPARE(f.snap->zoneForWindow(a), f.zone(0));
        QCOMPARE(f.snap->zoneForWindow(b), f.zone(1));
    }

    void swapRefusesAnUnregisteredPartner()
    {
        GateFixture f;
        const QString a = f.registerOn(QStringLiteral("live-a"), 1);
        const QString ghost = QStringLiteral("app|swap-ghost");
        f.snap->commitSnap(a, f.zone(0), kLeft);
        f.snap->commitSnap(ghost, f.zone(1), kLeft);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->swapWindowsById(a, ghost);
        QCOMPARE(applies.count(), 0);
        QCOMPARE(f.snap->zoneForWindow(a), f.zone(0));
    }

    // Two windows snapped on a desktop not in view swap there and leave no
    // membership on the desktop in view (F179).
    void swapOnAHiddenDesktopStaysThere()
    {
        GateFixture f;
        f.layouts->assignLayout(kLeft, 2, QString(), f.layout);
        f.snap->setCurrentDesktopForScreen(kLeft, 1);
        const QString a = f.registerOn(QStringLiteral("hswap-a"), 2);
        const QString b = f.registerOn(QStringLiteral("hswap-b"), 2);
        f.snap->commitSnap(a, f.zone(0), kLeft, PhosphorEngine::SnapIntent::UserInitiated, 2);
        f.snap->commitSnap(b, f.zone(1), kLeft, PhosphorEngine::SnapIntent::UserInitiated, 2);
        f.adaptor->swapWindowsById(a, b);
        for (const QString& w : {a, b}) {
            const PhosphorSnapEngine::SnapState* store = f.snap->stateForWindow(w);
            QVERIFY(store);
            QCOMPARE(f.snap->keyForState(store).value_or(PhosphorEngine::PlacementStateKey{}).desktop, 2);
        }
        QCOMPARE(f.snap->stateForWindow(a)->zoneForWindow(a), f.zone(1));
        QCOMPARE(f.snap->stateForWindow(b)->zoneForWindow(b), f.zone(0));
    }

    void swapRefusesItself()
    {
        GateFixture f;
        const QString a = f.registerOn(QStringLiteral("self-a"), 1);
        f.snap->commitSnap(a, f.zone(0), kLeft);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);
        f.adaptor->swapWindowsById(a, a);
        QCOMPARE(applies.count(), 0);
    }

    // The bus float toggle passes the keyboard gates (F404).
    void floatToggleRefusesWithSnappingOff()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("foff-1"), 1);
        f.snap->commitSnap(w, f.zone(0), kLeft);
        f.settings.setSnappingEnabled(false);
        f.adaptor->toggleFloatForWindow(w, kLeft);
        QVERIFY(!f.snap->isFloating(w));
        QCOMPARE(f.snap->zoneForWindow(w), f.zone(0));
    }

    void floatToggleRefusesADisabledContext()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("fdis-1"), 1);
        f.snap->commitSnap(w, f.zone(0), kLeft);
        f.resolver.m_disabled.insert(kLeft);
        f.adaptor->toggleFloatForWindow(w, kLeft);
        QVERIFY(!f.snap->isFloating(w));
    }

    // Frozen snap memory on a monitor that now tiles is not floated from.
    void floatToggleRefusesATilingScreen()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("ftile-1"), 1, QRect(2400, 200, 400, 300));
        f.snap->commitSnap(w, f.zone(0), kRight);
        f.tile(kRight);
        f.adaptor->toggleFloatForWindow(w, kRight);
        QVERIFY(!f.snap->isFloating(w));
        QCOMPARE(f.snap->zoneForWindow(w), f.zone(0));
    }

    void floatToggleRefusesAnUnknownScreen()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("funk-1"), 1);
        f.snap->commitSnap(w, f.zone(0), kLeft);
        f.adaptor->toggleFloatForWindow(w, QStringLiteral("DP-9"));
        QVERIFY(!f.snap->isFloating(w));
    }

    // A window snapping does not track yet floats on the monitor its frame
    // is on, not the one last focused (F28).
    void setWindowFloatUsesTheWindowsScreen()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("fscreen-1"), 1);
        f.snap->windowFocused(QStringLiteral("app|elsewhere"), kRight);
        QSignalSpy floated(f.snap.get(), &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);
        f.adaptor->setWindowFloat(w, true);
        QVERIFY(f.snap->isFloating(w));
        QCOMPARE(floated.count(), 1);
        QCOMPARE(floated.first().at(2).toString(), kLeft);
    }

    void floatToggleOnAnEmptyIdReportsInvalidWindow()
    {
        GateFixture f;
        QSignalSpy feedback(f.wta, &WindowTrackingAdaptor::navigationFeedback);
        f.adaptor->toggleFloatForWindow(QString(), kLeft);
        QCOMPARE(feedback.count(), 1);
        QCOMPARE(feedback.first().at(2).toString(), QStringLiteral("invalid_window"));
    }

    // The confirmations commit only a live window, a known screen and a zone
    // that screen's layout holds (F459).
    void windowSnappedRefusesAnUnknownZone()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("conf-1"), 1);
        f.adaptor->windowSnapped(w, QUuid::createUuid().toString(), kLeft);
        QVERIFY(f.snap->zoneForWindow(w).isEmpty());
    }

    void windowSnappedRefusesTheRestoreSentinel()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("conf-2"), 1);
        f.adaptor->windowSnapped(w, PhosphorEngine::RestoreSentinel, kLeft);
        QVERIFY(f.snap->zoneForWindow(w).isEmpty());
    }

    void windowSnappedRefusesAnUnknownScreen()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("conf-3"), 1);
        f.adaptor->windowSnapped(w, f.zone(0), QStringLiteral("DP-9"));
        QVERIFY(f.snap->zoneForWindow(w).isEmpty());
    }

    void windowSnappedRefusesAnUnregisteredWindow()
    {
        GateFixture f;
        const QString ghost = QStringLiteral("app|conf-ghost");
        f.adaptor->windowSnapped(ghost, f.zone(0), kLeft);
        QVERIFY(!f.snap->isWindowTracked(ghost));
    }

    void windowSnappedMultiZoneRefusesAnEmptyMember()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("conf-4"), 1);
        f.adaptor->windowSnappedMultiZone(w, {f.zone(0), QString()}, kLeft);
        QVERIFY(f.snap->zoneForWindow(w).isEmpty());
    }

    // Control: the snap-all confirmation still commits.
    void snapAllConfirmationStillCommits()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("conf-5"), 1);
        PhosphorProtocol::SnapConfirmationEntry entry;
        entry.windowId = w;
        entry.zoneId = f.zone(1);
        entry.screenId = kLeft;
        entry.isRestore = false;
        f.adaptor->windowsSnappedBatch({entry});
        QCOMPARE(f.snap->zoneForWindow(w), f.zone(1));
    }

    // A restore confirmation drops the float-back on its own screen only (F366).
    void restoreConfirmationKeepsTheOtherScreensFloatBack()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("conf-6"), 1);
        const QRect leftFree(100, 100, 400, 300);
        const QRect rightFree(2100, 100, 400, 300);
        f.wta->service()->recordFreeGeometry(w, kLeft, leftFree, true);
        f.wta->service()->recordFreeGeometry(w, kRight, rightFree, true);
        f.snap->commitSnap(w, f.zone(0), kLeft);
        PhosphorProtocol::SnapConfirmationEntry entry;
        entry.windowId = w;
        entry.screenId = kLeft;
        entry.isRestore = true;
        f.adaptor->windowsSnappedBatch({entry});
        QVERIFY(f.snap->zoneForWindow(w).isEmpty());
        QVERIFY(!f.floatBack(w, kLeft).isValid());
        QCOMPARE(f.floatBack(w, kRight), rightFree);
    }

    // An unsnap leaves the window free, so the record's snap slot is
    // released and a reopen does not restore the zone just left.
    void windowUnsnappedReleasesTheSnapSlot()
    {
        GateFixture f;
        const QString w = f.registerOn(QStringLiteral("conf-7"), 1);
        f.snap->commitSnap(w, f.zone(0), kLeft);
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = w;
        rec.appId = QStringLiteral("app");
        rec.screenId = kLeft;
        rec.virtualDesktop = 1;
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = {f.zone(0)};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        f.wta->service()->placementStore().record(rec);
        f.adaptor->windowUnsnapped(w);
        const auto after = f.wta->service()->placementStore().peekExact(w);
        QVERIFY(after.has_value());
        QCOMPARE(after->engines.value(PhosphorEngine::WindowPlacement::snapEngineId()).state,
                 QString(PhosphorEngine::WindowPlacement::stateReleased()));
    }
};

QTEST_MAIN(TestSnapAdaptorGates)
#include "test_snap_adaptor_gates.moc"

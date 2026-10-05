// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_snap_adaptor_gates.cpp
 * @brief The bus snap gate (SnapAdaptor::admitBusSnap): a D-Bus snap of one
 *        window lands only where a keyboard snap could, on the window's own
 *        desktop, and Control.snapWindowToZone resolves its screen the same
 *        way (F20, F142, F179, F21, F22, F26, F85).
 */

#include "wta_float_back_fixture.h"
#include "helpers/VirtualScreenTestHelpers.h"
#include "dbus/controladaptor.h"
#include "dbus/snapadaptor/snapadaptor.h"

#include <PhosphorContext/IContextResolver.h>
#include <PhosphorRules/WindowQuery.h>

#include <QSignalSpy>

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
};

QTEST_MAIN(TestSnapAdaptorGates)
#include "test_snap_adaptor_gates.moc"

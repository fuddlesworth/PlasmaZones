// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_shell_desktop.cpp
 * @brief The per-desktop reads of WindowTrackingAdaptor's Phosphor shell
 * surface (shellsurface_desktop.cpp).
 *
 *  1. getWindowStatesForDesktop keeps the rows of getAllWindowStates
 *     whose last metadata push named the desktop, lands sticky windows
 *     (IsSticky, or a desktop of 0) on every desktop, honours a spanned
 *     desktop list, drops windows the registry never saw, and filters on
 *     the screen when one is given.
 *  2. Desktop 0 reads the screen's current desktop and answers nothing
 *     without a screen or without a desktop manager; a negative desktop
 *     answers nothing.
 *  3. findWindowByPid answers the window pushed most recently among those
 *     sharing the pid, empty for an unknown pid, and forgets a closed
 *     window.
 */

#include <QTest>
#include <QVariantMap>
#include <memory>

#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/WindowTypeEnum.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorZones/LayoutRegistry.h>

#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
namespace Key = PhosphorProtocol::Service::WindowMetadataKey;

class TestWtaShellDesktop : public QObject
{
    Q_OBJECT

private:
    static constexpr auto App = "org.kde.dolphin";
    static constexpr auto InstanceA = "cef1ba31-3316-4f05-84f5-ef627674b504";
    static constexpr auto InstanceB = "2b7a6a1e-9d2e-4a0c-9b3e-1f0d3c5e7a91";
    static constexpr auto InstanceC = "7f1c2d3e-4a5b-4c6d-8e9f-0a1b2c3d4e5f";

    static QString windowId(const char* instance)
    {
        return QLatin1String(App) + QLatin1Char('|') + QLatin1String(instance);
    }

    /// One metadata push. @p desktop is the 1-based desktop (0 = all), and
    /// @p extended the a{sv} snapshot (non-empty, so it is a full push).
    void push(const char* instance, int pid, int desktop, const QVariantMap& extended)
    {
        m_wta->setWindowMetadata(QLatin1String(instance), QLatin1String(App), QStringLiteral("org.kde.dolphin.desktop"),
                                 QStringLiteral("Home"), QString(), pid, desktop, QString(),
                                 static_cast<int>(PhosphorProtocol::WindowType::Normal), extended);
    }

    static QVariantMap snapshot(int width = 640)
    {
        QVariantMap extended;
        extended.insert(QString(Key::Width), width);
        return extended;
    }

    /// The service tracks snapped and floating windows only; floating is
    /// the one state a test can set without a layout, so every window here
    /// floats to appear in getAllWindowStates at all.
    void track(const char* instance)
    {
        m_wta->setWindowFloating(windowId(instance), true);
    }

    static QStringList ids(const PhosphorProtocol::WindowStateList& states)
    {
        QStringList out;
        for (const auto& s : states) {
            out.append(s.windowId);
        }
        out.sort();
        return out;
    }

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new StubZoneDetector(nullptr);
        m_registry = new PhosphorEngine::WindowRegistry(nullptr);
        m_parent = new QObject(nullptr);
        m_wta =
            new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, nullptr, m_parent);
        m_wta->setWindowRegistry(m_registry);
        // The service asserts on a snap state for every zone / float read,
        // so the harness gives it a real (inert) SnapEngine, as
        // test_wta_reactive_metadata does.
        m_snapEngine =
            new PhosphorSnapEngine::SnapEngine(m_layoutManager, m_wta->service(), m_zoneDetector, nullptr, nullptr);
        m_snapEngine->setEngineSettings(m_settings);
        m_wta->service()->setSnapState(m_snapEngine->snapState());
        m_wta->service()->setSnapEngine(m_snapEngine);
        m_wta->setEngines(m_snapEngine, nullptr, nullptr);
    }

    void cleanup()
    {
        m_wta->service()->setSnapState(nullptr);
        m_wta->service()->setSnapEngine(nullptr);
        delete m_snapEngine;
        m_snapEngine = nullptr;
        delete m_parent;
        m_parent = nullptr;
        m_wta = nullptr;
        delete m_registry;
        m_registry = nullptr;
        delete m_zoneDetector;
        m_zoneDetector = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;
        m_guard.reset();
    }

    void statesForDesktop_filtersOnThePushedDesktop()
    {
        push(InstanceA, 100, 1, snapshot());
        push(InstanceB, 200, 2, snapshot());
        track(InstanceA);
        track(InstanceB);
        QCOMPARE(m_wta->getAllWindowStates().size(), 2);

        QCOMPARE(ids(m_wta->getWindowStatesForDesktop(QString(), 1)), QStringList{windowId(InstanceA)});
        QCOMPARE(ids(m_wta->getWindowStatesForDesktop(QString(), 2)), QStringList{windowId(InstanceB)});
        QVERIFY(m_wta->getWindowStatesForDesktop(QString(), 3).isEmpty());

        // The rows are getAllWindowStates rows: same shape, same fields.
        const auto rows = m_wta->getWindowStatesForDesktop(QString(), 1);
        QCOMPARE(rows.size(), 1);
        QVERIFY(rows.first().isFloating);
        QVERIFY(rows.first().zoneId.isEmpty());

        // A later push moving A to desktop 2 is followed.
        push(InstanceA, 100, 2, snapshot(800));
        QCOMPARE(ids(m_wta->getWindowStatesForDesktop(QString(), 2)).size(), 2);
        QVERIFY(m_wta->getWindowStatesForDesktop(QString(), 1).isEmpty());
    }

    void statesForDesktop_stickyAndSpannedLandEverywhere()
    {
        // Desktop 0 in the push is "all desktops".
        push(InstanceA, 100, 0, snapshot());
        // An explicit sticky flag on a window pinned to desktop 1.
        QVariantMap sticky = snapshot();
        sticky.insert(QString(Key::IsSticky), true);
        push(InstanceB, 200, 1, sticky);
        // A window spanning desktops 2 and 3 (virtualDesktop is the first).
        QVariantMap spanned = snapshot();
        spanned.insert(QString(Key::VirtualDesktops), QVariantList{2, 3});
        push(InstanceC, 300, 2, spanned);
        track(InstanceA);
        track(InstanceB);
        track(InstanceC);

        QStringList everywhere{windowId(InstanceA), windowId(InstanceB)};
        everywhere.sort();
        QCOMPARE(ids(m_wta->getWindowStatesForDesktop(QString(), 1)), everywhere);
        QStringList two = everywhere;
        two.append(windowId(InstanceC));
        two.sort();
        QCOMPARE(ids(m_wta->getWindowStatesForDesktop(QString(), 2)), two);
        QCOMPARE(ids(m_wta->getWindowStatesForDesktop(QString(), 3)), two);
        QCOMPARE(ids(m_wta->getWindowStatesForDesktop(QString(), 4)), everywhere);

        // The service's own sticky record is the same fact from the other
        // feed: a window the push pinned to desktop 1 but the service marks
        // sticky lands everywhere too.
        push(InstanceB, 200, 1, snapshot(900));
        QCOMPARE(ids(m_wta->getWindowStatesForDesktop(QString(), 4)), QStringList{windowId(InstanceA)});
        m_wta->setWindowSticky(windowId(InstanceB), true);
        QCOMPARE(ids(m_wta->getWindowStatesForDesktop(QString(), 4)), everywhere);
    }

    void statesForDesktop_dropsUnregisteredAndFiltersScreen()
    {
        // Tracked by the service but never pushed: no desktop to compare.
        track(InstanceA);
        QCOMPARE(m_wta->getAllWindowStates().size(), 1);
        QVERIFY(m_wta->getWindowStatesForDesktop(QString(), 1).isEmpty());

        push(InstanceA, 100, 1, snapshot());
        QCOMPARE(m_wta->getWindowStatesForDesktop(QString(), 1).size(), 1);
        // A screen the row is not on filters it out.
        QVERIFY(m_wta->getWindowStatesForDesktop(QStringLiteral("DP-9"), 1).isEmpty());

        // A closed window leaves the ledger with the registry.
        m_registry->remove(QLatin1String(InstanceA));
        QVERIFY(m_wta->getWindowStatesForDesktop(QString(), 1).isEmpty());
    }

    void statesForDesktop_zeroAndNegativeDesktops()
    {
        push(InstanceA, 100, 1, snapshot());
        track(InstanceA);
        QVERIFY(m_wta->getWindowStatesForDesktop(QString(), -1).isEmpty());
        // 0 is "current", which needs a screen, and then a desktop manager
        // to resolve it; this harness has neither.
        QVERIFY(m_wta->getWindowStatesForDesktop(QString(), 0).isEmpty());
        QVERIFY(m_wta->getWindowStatesForDesktop(QStringLiteral("DP-1"), 0).isEmpty());
    }

    void findWindowByPid_mostRecentPushWins()
    {
        QVERIFY(m_wta->findWindowByPid(100).isEmpty());
        QVERIFY(m_wta->findWindowByPid(0).isEmpty());
        QVERIFY(m_wta->findWindowByPid(-5).isEmpty());

        push(InstanceA, 100, 1, snapshot());
        QCOMPARE(m_wta->findWindowByPid(100), windowId(InstanceA));
        QVERIFY(m_wta->findWindowByPid(200).isEmpty());

        // A second window of the same process, pushed later, wins.
        push(InstanceB, 100, 1, snapshot());
        QCOMPARE(m_wta->findWindowByPid(100), windowId(InstanceB));
        // Until the first is touched again.
        push(InstanceA, 100, 1, snapshot(800));
        QCOMPARE(m_wta->findWindowByPid(100), windowId(InstanceA));

        // No service tracking is needed: the pid comes from the push alone.
        QVERIFY(m_wta->getAllWindowStates().isEmpty());

        // A closed window is forgotten.
        m_registry->remove(QLatin1String(InstanceA));
        QCOMPARE(m_wta->findWindowByPid(100), windowId(InstanceB));
        m_registry->remove(QLatin1String(InstanceB));
        QVERIFY(m_wta->findWindowByPid(100).isEmpty());
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    PhosphorEngine::WindowRegistry* m_registry = nullptr;
    PhosphorSnapEngine::SnapEngine* m_snapEngine = nullptr;
    QObject* m_parent = nullptr;
    WindowTrackingAdaptor* m_wta = nullptr;
};

QTEST_MAIN(TestWtaShellDesktop)
#include "test_wta_shell_desktop.moc"

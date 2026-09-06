// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_shell_surface.cpp
 * @brief The Phosphor shell surface of WindowTrackingAdaptor
 * (shellsurface.cpp): the placement map's click/drag verbs and the
 * identity/urgency feed behind its cell labels.
 *
 *  1. activateWindow emits activateWindowRequested for a registered window
 *     and stays silent for one the registry never saw.
 *  2. getWindowMetadata round-trips what setWindowMetadata stored, answers
 *     empty for an unknown id, and windowMetadataChanged fires on
 *     registration and on a title change but not on an unrelated edge.
 *  3. moveWindowToDesktop emits windowDesktopMoveRequested for a
 *     registered window and a 1-based desktop, silent otherwise.
 *  4. windowUrgencyChanged fires once per edge (set, repeat, clear) and
 *     once more with false when an urgent window closes; getUrgentWindows
 *     mirrors the set.
 */

#include <QSignalSpy>
#include <QTest>
#include <QVariantMap>
#include <memory>

#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/WindowTypeEnum.h>
#include <PhosphorZones/LayoutRegistry.h>

#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
namespace Key = PhosphorProtocol::Service::WindowMetadataKey;

class TestWtaShellSurface : public QObject
{
    Q_OBJECT

private:
    static constexpr auto Instance = "cef1ba31-3316-4f05-84f5-ef627674b504";
    static constexpr auto App = "org.kde.dolphin";

    /// The composite id the engines' models carry, and the one the shell
    /// hands back: appId|instanceId, frozen from the first push.
    static QString windowId()
    {
        return QLatin1String(App) + QLatin1Char('|') + QLatin1String(Instance);
    }

    /// One metadata push with the fields these tests vary. @p extended is
    /// the a{sv} snapshot; a non-empty map is a full push (an empty one is
    /// a caption-only refresh in the adaptor's contract).
    void push(const QString& title, const QVariantMap& extended,
              const QString& desktopFile = QStringLiteral("org.kde.dolphin.desktop"))
    {
        m_wta->setWindowMetadata(QLatin1String(Instance), QLatin1String(App), desktopFile, title, QString(), 1234, 1,
                                 QString(), static_cast<int>(PhosphorProtocol::WindowType::Normal), extended);
    }

    static QVariantMap snapshot(bool urgent, int width = 640)
    {
        QVariantMap extended;
        extended.insert(QString(Key::IsDemandingAttention), urgent);
        extended.insert(QString(Key::Width), width);
        return extended;
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
    }

    void cleanup()
    {
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

    void activateWindow_emitsForTrackedOnly()
    {
        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::activateWindowRequested);

        // Unknown before any push, and an empty id: both silent.
        m_wta->activateWindow(windowId());
        m_wta->activateWindow(QString());
        QCOMPARE(spy.count(), 0);

        push(QStringLiteral("Home"), snapshot(false));
        m_wta->activateWindow(windowId());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), windowId());

        // A different instance under the same app is still unknown.
        m_wta->activateWindow(QLatin1String(App) + QStringLiteral("|00000000-0000-0000-0000-000000000000"));
        QCOMPARE(spy.count(), 1);
    }

    void getWindowMetadata_roundTripsAndAnnouncesChanges()
    {
        QSignalSpy changed(m_wta, &WindowTrackingAdaptor::windowMetadataChanged);

        QString title;
        QString desktopFile;
        QCOMPARE(m_wta->getWindowMetadata(windowId(), title, desktopFile), QString());
        QVERIFY(title.isEmpty());
        QVERIFY(desktopFile.isEmpty());

        // Registration announces once, carrying the app id and title.
        push(QStringLiteral("Home"), snapshot(false));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.at(0).at(0).toString(), windowId());
        QCOMPARE(changed.at(0).at(1).toString(), QLatin1String(App));
        QCOMPARE(changed.at(0).at(2).toString(), QStringLiteral("Home"));

        QCOMPARE(m_wta->getWindowMetadata(windowId(), title, desktopFile), QLatin1String(App));
        QCOMPARE(title, QStringLiteral("Home"));
        QCOMPARE(desktopFile, QStringLiteral("org.kde.dolphin.desktop"));

        // An edge that leaves app id and title alone (a geometry change) is
        // silent; a title change announces the new title.
        push(QStringLiteral("Home"), snapshot(false, 800));
        QCOMPARE(changed.count(), 1);
        push(QStringLiteral("Downloads"), snapshot(false, 800));
        QCOMPARE(changed.count(), 2);
        QCOMPARE(changed.at(1).at(2).toString(), QStringLiteral("Downloads"));
        QCOMPARE(m_wta->getWindowMetadata(windowId(), title, desktopFile), QLatin1String(App));
        QCOMPARE(title, QStringLiteral("Downloads"));

        // The out-arguments are cleared for an unknown id even when the
        // caller passes them in populated.
        title = QStringLiteral("stale");
        desktopFile = QStringLiteral("stale");
        QCOMPARE(m_wta->getWindowMetadata(QStringLiteral("nobody|nothing"), title, desktopFile), QString());
        QVERIFY(title.isEmpty());
        QVERIFY(desktopFile.isEmpty());
    }

    void moveWindowToDesktop_emitsForTrackedAndValidDesktop()
    {
        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);

        m_wta->moveWindowToDesktop(windowId(), 2); // unknown yet
        QCOMPARE(spy.count(), 0);

        push(QStringLiteral("Home"), snapshot(false));
        m_wta->moveWindowToDesktop(windowId(), 0); // 0 is "all / unknown", not a destination
        m_wta->moveWindowToDesktop(windowId(), -1);
        QCOMPARE(spy.count(), 0);

        m_wta->moveWindowToDesktop(windowId(), 3);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), windowId());
        QCOMPARE(spy.at(0).at(1).toInt(), 3);
    }

    void urgency_emitsOncePerEdgeAndClearsOnClose()
    {
        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::windowUrgencyChanged);

        // Registered calm: no edge, empty set.
        push(QStringLiteral("Home"), snapshot(false));
        QCOMPARE(spy.count(), 0);
        QVERIFY(m_wta->getUrgentWindows().isEmpty());

        // Set.
        push(QStringLiteral("Home"), snapshot(true));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), windowId());
        QCOMPARE(spy.at(0).at(1).toBool(), true);
        QCOMPARE(m_wta->getUrgentWindows(), QStringList{windowId()});

        // A repeat push that stays urgent (with another field moving so the
        // registry sees a change at all) is not a second edge.
        push(QStringLiteral("Home"), snapshot(true, 800));
        QCOMPARE(spy.count(), 1);

        // Clear.
        push(QStringLiteral("Home"), snapshot(false, 800));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(1).toBool(), false);
        QVERIFY(m_wta->getUrgentWindows().isEmpty());

        // A disengaged field (the compositor could not report it) reads as
        // not urgent, so a push without the key does not light anything.
        QVariantMap noField;
        noField.insert(QString(Key::Width), 900);
        push(QStringLiteral("Home"), noField);
        QCOMPARE(spy.count(), 2);

        // Set again, then close while urgent: exactly one false edge and an
        // empty set afterwards.
        push(QStringLiteral("Home"), snapshot(true, 900));
        QCOMPARE(spy.count(), 3);
        m_registry->remove(QLatin1String(Instance));
        QCOMPARE(spy.count(), 4);
        QCOMPARE(spy.at(3).at(0).toString(), windowId());
        QCOMPARE(spy.at(3).at(1).toBool(), false);
        QVERIFY(m_wta->getUrgentWindows().isEmpty());

        // A calm window closing emits nothing.
        push(QStringLiteral("Home"), snapshot(false));
        m_registry->remove(QLatin1String(Instance));
        QCOMPARE(spy.count(), 4);
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    PhosphorEngine::WindowRegistry* m_registry = nullptr;
    QObject* m_parent = nullptr;
    WindowTrackingAdaptor* m_wta = nullptr;
};

QTEST_MAIN(TestWtaShellSurface)
#include "test_wta_shell_surface.moc"

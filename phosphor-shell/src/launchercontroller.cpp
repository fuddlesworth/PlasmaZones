// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LauncherController.h"

#include <PhosphorServiceClipboard/ClipboardService.h>
#include <PhosphorShell/Toplevels.h>
#include <PhosphorShell/PlacementMap.h>
#include <PhosphorShell/Workspaces.h>
#include <PhosphorShellLauncher/AppsProvider.h>
#include <PhosphorShellLauncher/CalculatorProvider.h>
#include <PhosphorShellLauncher/ClipboardProvider.h>
#include <PhosphorShellLauncher/CommandProvider.h>
#include <PhosphorShellLauncher/LauncherModel.h>
#include <PhosphorShellLauncher/WindowsProvider.h>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QLoggingCategory>

#include <memory>
#include <algorithm>
#include <utility>

namespace {
Q_LOGGING_CATEGORY(lcLauncher, "phosphorshell.launcher")
}

namespace PhosphorShellApp {

using PhosphorRegistry::ILauncherProvider;

FunctionProviderFactory::FunctionProviderFactory(QString id, QString displayName, QStringList capabilities,
                                                 Create create)
    : m_id(std::move(id))
    , m_displayName(std::move(displayName))
    , m_capabilities(std::move(capabilities))
    , m_create(std::move(create))
{
}

QString FunctionProviderFactory::id() const
{
    return m_id;
}

QString FunctionProviderFactory::displayName() const
{
    return m_displayName;
}

QStringList FunctionProviderFactory::capabilities() const
{
    return m_capabilities;
}

ILauncherProvider* FunctionProviderFactory::createProvider(QObject* parent)
{
    return m_create ? m_create(parent) : nullptr;
}

LauncherController::LauncherController(QObject* parent)
    : QObject(parent)
    , m_clipboard(new PhosphorServiceClipboard::ClipboardService(this))
    , m_toplevels(new PhosphorShell::Toplevels(this))
    , m_model(new PhosphorShellLauncher::LauncherModel(this))
{
    // STARTUP COST, stated because this runs before the first frame.
    // Every provider is materialised here rather than on first open, so
    // that opening the launcher is instant: a user pressing the shortcut
    // does not wait for an applications scan. The two costs that buys
    // are the clipboard service's constructor, which reads persisted
    // history from disk and binds a Wayland data-control source, and the
    // applications provider's first scan. The scan is already posted to
    // the event loop rather than run inline, so it lands after the first
    // frame; the clipboard service is not, and moving it would mean
    // teaching the provider to attach to a service that arrives later.
    //
    // Registration order is the pill order and the tie-break between
    // providers with equal best scores. Capabilities are advisory until
    // the Phase 5 runtime, declared so the built-ins exercise the manifest
    // surface a plugin will.
    const auto reg = [this](const QString& id, const QString& name, const QString& capability,
                            FunctionProviderFactory::Create create) {
        m_registry.registerFactory(
            std::make_shared<FunctionProviderFactory>(id, name, QStringList{capability}, std::move(create)));
    };
    reg(QStringLiteral("apps"), QCoreApplication::translate("PhosphorShellLauncher", "Applications"),
        QStringLiteral("apps.launch"), [this](QObject* p) {
            m_apps = new PhosphorShellLauncher::AppsProvider(p);
            m_apps->setListOnEmptyQuery(true);
            return m_apps;
        });
    reg(QStringLiteral("windows"), QCoreApplication::translate("PhosphorShellLauncher", "Windows"),
        QStringLiteral("windows.activate"), [this](QObject* p) {
            m_windows = new PhosphorShellLauncher::WindowsProvider(m_toplevels->model(), p);
            return m_windows;
        });
    reg(QStringLiteral("calculator"), QCoreApplication::translate("PhosphorShellLauncher", "Calculator"),
        QStringLiteral("clipboard.write"), [](QObject* p) {
            return new PhosphorShellLauncher::CalculatorProvider(p);
        });
    reg(QStringLiteral("clipboard"), QCoreApplication::translate("PhosphorShellLauncher", "Clipboard"),
        QStringLiteral("clipboard.read"), [this](QObject* p) {
            return new PhosphorShellLauncher::ClipboardProvider(m_clipboard, p);
        });
    reg(QStringLiteral("command"), QCoreApplication::translate("PhosphorShellLauncher", "Run Command"),
        QStringLiteral("process.spawn"), [](QObject* p) {
            return new PhosphorShellLauncher::CommandProvider(p);
        });

    for (const QString& id : m_registry.ids()) {
        const auto factory = m_registry.factory(id);
        if (!factory) {
            continue;
        }
        ILauncherProvider* provider = factory->createProvider(m_model);
        if (!provider) {
            qCInfo(lcLauncher) << "provider" << id << "unavailable in this environment";
            continue;
        }
        m_model->addProvider(provider);
    }
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                           + QStringLiteral("/phosphor-shell/launcher.ini"),
                       QSettings::IniFormat);
    m_pinnedIds = settings
                      .value(QStringLiteral("Launcher/pinnedApplications"),
                             QStringList{QStringLiteral("firefox"), QStringLiteral("org.kde.kate"),
                                         QStringLiteral("org.kde.dolphin"), QStringLiteral("org.kde.konsole")})
                      .toStringList();
    connect(m_apps, &PhosphorShellLauncher::AppsProvider::entriesChanged, this,
            &LauncherController::pinnedApplicationsChanged);
    if (!m_toplevels->isSupported()) {
        m_placement = new PhosphorShell::PlacementMap(this);
        auto* refreshTimer = new QTimer(this);
        refreshTimer->setSingleShot(true);
        refreshTimer->setInterval(16);
        const auto schedule = [refreshTimer] {
            refreshTimer->start();
        };
        connect(refreshTimer, &QTimer::timeout, this, &LauncherController::refreshNativeWindows);
        auto* desktops = m_placement->workspaces();
        connect(desktops, &PhosphorShell::Workspaces::countChanged, this, schedule);
        connect(desktops, &PhosphorShell::Workspaces::activeChanged, this, schedule);
        connect(desktops->model(), &QAbstractItemModel::dataChanged, this, schedule);
        connect(qGuiApp, &QGuiApplication::screenAdded, this, schedule);
        connect(qGuiApp, &QGuiApplication::screenRemoved, this, schedule);
        connect(m_windows, &PhosphorShellLauncher::WindowsProvider::nativeWindowActivated, this,
                [this](const QString& id) {
                    if (auto* map = m_windowMaps.value(id)) {
                        map->activateNavigationWindow(id);
                    }
                });
        refreshTimer->start();
    }
    qCDebug(lcLauncher) << "launcher ready with" << m_model->providerObjects().size() << "provider(s)";
}

LauncherController::~LauncherController() = default;

PhosphorShellLauncher::LauncherModel* LauncherController::model() const
{
    return m_model;
}

QVariantList LauncherController::pinnedApplications() const
{
    QVariantList result;
    const auto entries = m_apps->entries();
    for (const auto& id : m_pinnedIds) {
        for (const auto& entry : entries) {
            if (entry.id == id) {
                result.append(QVariantMap{{QStringLiteral("id"), id},
                                          {QStringLiteral("name"), entry.name},
                                          {QStringLiteral("iconName"), entry.icon}});
                break;
            }
        }
    }
    return result;
}

bool LauncherController::isPinned(const QString& id) const
{
    return m_pinnedIds.contains(id);
}

void LauncherController::togglePinned(const QString& id)
{
    const auto entries = m_apps->entries();
    if (std::none_of(entries.cbegin(), entries.cend(), [&id](const auto& entry) {
            return entry.id == id;
        })) {
        return;
    }
    if (!m_pinnedIds.removeOne(id)) {
        m_pinnedIds.append(id);
    }
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                           + QStringLiteral("/phosphor-shell/launcher.ini"),
                       QSettings::IniFormat);
    settings.setValue(QStringLiteral("Launcher/pinnedApplications"), m_pinnedIds);
    Q_EMIT pinnedApplicationsChanged();
}

bool LauncherController::launchPinned(const QString& id)
{
    if (!isPinned(id)) {
        return false;
    }
    for (const auto& entry : m_apps->entries()) {
        if (entry.id == id) {
            return PhosphorShellLauncher::AppsProvider::launch(entry);
        }
    }
    return false;
}

void LauncherController::refreshNativeWindows()
{
    QVariantList windows;
    m_windowMaps.clear();
    auto* desktops = m_placement->workspaces();
    for (auto* screen : QGuiApplication::screens()) {
        for (int desktop = 0; desktop < desktops->count(); ++desktop) {
            auto* map = m_placement->forScreenDesktop(screen->name(), desktop);
            if (!m_watchedMaps.contains(map)) {
                m_watchedMaps.insert(map);
                connect(map, &PhosphorShell::PlacementMapScreen::windowsChanged, this,
                        &LauncherController::refreshNativeWindows, Qt::QueuedConnection);
            }
            const QString workspace =
                desktops->model()
                    ->data(desktops->model()->index(desktop, 0), PhosphorShell::WorkspaceListModel::NameRole)
                    .toString();
            for (const QVariant& value : map->windows()) {
                QVariantMap window = value.toMap();
                const QString id = window.value(QStringLiteral("windowId")).toString();
                if (id.isEmpty() || m_windowMaps.contains(id)) {
                    continue;
                }
                m_windowMaps.insert(id, map);
                QString app = window.value(QStringLiteral("appId")).toString().section(u'.', -1);
                if (!app.isEmpty()) {
                    app[0] = app[0].toUpper();
                }
                window.insert(QStringLiteral("subtitle"), QString(app + QStringLiteral(" · ") + workspace));
                windows.append(window);
            }
        }
    }
    m_windows->setNativeWindows(windows);
}

} // namespace PhosphorShellApp

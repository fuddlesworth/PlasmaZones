// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorRegistry/ILauncherProviderFactory.h>
#include <PhosphorRegistry/Registry.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QHash>
#include <QSet>

#include <functional>

namespace PhosphorServiceClipboard {
class ClipboardService;
}

namespace PhosphorShell {
class Toplevels;
class PlacementMap;
class PlacementMapScreen;
}

namespace PhosphorShellLauncher {
class LauncherModel;
class AppsProvider;
class WindowsProvider;
}

namespace PhosphorShellApp {

// ILauncherProviderFactory over a construction function, for the shell's
// built-in providers. Same helper the launcher demo carries (each host
// keeps its own copy, as the bar's and control center's factories do).
class FunctionProviderFactory : public PhosphorRegistry::ILauncherProviderFactory
{
public:
    using Create = std::function<PhosphorRegistry::ILauncherProvider*(QObject*)>;

    FunctionProviderFactory(QString id, QString displayName, QStringList capabilities, Create create);
    ~FunctionProviderFactory() override = default;
    Q_DISABLE_COPY_MOVE(FunctionProviderFactory)

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] QStringList capabilities() const override;
    [[nodiscard]] PhosphorRegistry::ILauncherProvider* createProvider(QObject* parent) override;

private:
    QString m_id;
    QString m_displayName;
    QStringList m_capabilities;
    Create m_create;
};

// The shell's launcher registry owner. Owns the
// Registry<ILauncherProviderFactory>, registers the built-in providers,
// materialises each into the LauncherModel the launcher popout binds
// (exposed to QML as the LauncherResults context property), and owns the
// two services the providers read that nothing else in the shell owns
// yet: the clipboard service and a Toplevels instance.
//
// Process-global, like BarController and ControlCenterController: it
// outlives every hot-reload engine rebuild, so the apps scan, clipboard
// history and window list are not re-enumerated on each reload. The
// Toplevels here is a second instance beside the per-engine QML singleton
// on purpose — the singleton dies with its engine, and a provider bound
// to it would go inert on the first reload.
class LauncherController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList pinnedApplications READ pinnedApplications NOTIFY pinnedApplicationsChanged)

public:
    explicit LauncherController(QObject* parent = nullptr);
    ~LauncherController() override;

    [[nodiscard]] PhosphorShellLauncher::LauncherModel* model() const;
    [[nodiscard]] QVariantList pinnedApplications() const;
    Q_INVOKABLE bool launchPinned(const QString& id);
    Q_INVOKABLE bool isPinned(const QString& id) const;
    Q_INVOKABLE void togglePinned(const QString& id);

Q_SIGNALS:
    void pinnedApplicationsChanged();

private:
    PhosphorRegistry::Registry<PhosphorRegistry::ILauncherProviderFactory> m_registry;
    PhosphorServiceClipboard::ClipboardService* m_clipboard = nullptr;
    PhosphorShell::Toplevels* m_toplevels = nullptr;
    PhosphorShellLauncher::LauncherModel* m_model = nullptr;
    PhosphorShellLauncher::AppsProvider* m_apps = nullptr;
    PhosphorShellLauncher::WindowsProvider* m_windows = nullptr;
    PhosphorShell::PlacementMap* m_placement = nullptr;
    QHash<QString, PhosphorShell::PlacementMapScreen*> m_windowMaps;
    QSet<PhosphorShell::PlacementMapScreen*> m_watchedMaps;
    QStringList m_pinnedIds;
    void refreshNativeWindows();
};

} // namespace PhosphorShellApp

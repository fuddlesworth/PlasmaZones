// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// PlasmaZones ShaderRegistry — wraps PhosphorShell::ShaderRegistry with
// hardcoded plasmazones shader paths and QML-side Q_INVOKABLE helpers.
//
// Per-process ownership: composition roots (daemon) own one via unique_ptr
// and inject it into every consumer. The previous `instance() / s_instance`
// singleton was removed in the singleton-sweep #2 refactor — same playbook
// as AlgorithmRegistry, see project_plugin_based_compositor.md.

#pragma once

#include <PhosphorShell/ShaderRegistry.h>

#include "plasmazones_export.h"

#include <QDesktopServices>
#include <QDir>
#include <QStandardPaths>
#include <QUrl>

namespace PlasmaZones {

/// PlasmaZones-specific ShaderRegistry that adds:
/// - Hardcoded system/user shader paths (plasmazones/shaders)
/// - userShadersEnabled(), userShaderDirectory(), openUserShaderDirectory()
class PLASMAZONES_EXPORT ShaderRegistry : public PhosphorShell::ShaderRegistry
{
    Q_OBJECT

public:
    explicit ShaderRegistry(QObject* parent = nullptr)
        : PhosphorShell::ShaderRegistry(parent)
    {
        // Ensure user shader dir exists BEFORE registering — so the
        // initial scan picks up any user-shipped shaders without an
        // additional rescan, and so the watcher attaches a direct watch
        // (vs. a parent-watch promotion).
        const QString userDir = userShaderDir();
        QDir(userDir).mkpath(QStringLiteral("."));

        const QStringList systemDirs =
            QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/shaders"),
                                      QStandardPaths::LocateDirectory);

        // Build the full registration list once: system dirs first
        // (lower priority), user dir last (last-writer-wins lets the
        // user override bundled shaders). Single batched call so the
        // base runs exactly one scan instead of N+1.
        QStringList paths = systemDirs;
        std::reverse(paths.begin(), paths.end());
        if (!systemDirs.contains(userDir)) {
            paths.append(userDir);
        }
        addSearchPaths(paths);
    }

    ~ShaderRegistry() override = default;

    Q_INVOKABLE bool userShadersEnabled() const
    {
        return shadersEnabled();
    }

    Q_INVOKABLE QString userShaderDirectory() const
    {
        return userShaderDir();
    }

    Q_INVOKABLE void openUserShaderDirectory() const
    {
        QDir dir(userShaderDir());
        if (!dir.exists()) {
            dir.mkpath(QStringLiteral("."));
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(userShaderDir()));
    }

private:
    static QString userShaderDir()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/plasmazones/shaders");
    }
};

} // namespace PlasmaZones

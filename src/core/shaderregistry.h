// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// PlasmaZones ShaderRegistry — wraps PhosphorShell::ShaderRegistry with
// singleton pattern and hardcoded plasmazones shader paths.

#pragma once

#include <PhosphorShell/ShaderRegistry.h>

#include "plasmazones_export.h"

#include <QDesktopServices>
#include <QDir>
#include <QStandardPaths>
#include <QUrl>

namespace PlasmaZones {

/// PlasmaZones-specific ShaderRegistry that adds:
/// - Singleton pattern (instance() / s_instance)
/// - Hardcoded system/user shader paths (plasmazones/shaders)
/// - userShadersEnabled(), userShaderDirectory(), openUserShaderDirectory()
class PLASMAZONES_EXPORT ShaderRegistry : public PhosphorShell::ShaderRegistry
{
    Q_OBJECT

public:
    explicit ShaderRegistry(QObject* parent = nullptr)
        : PhosphorShell::ShaderRegistry(parent)
    {
        s_instance = this;

        // Register PlasmaZones-specific search paths
        const QString userDir = userShaderDir();
        const QStringList systemDirs =
            QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/shaders"),
                                      QStandardPaths::LocateDirectory);

        // System dirs first (lower priority), user dir last (overrides)
        QStringList reversed = systemDirs;
        std::reverse(reversed.begin(), reversed.end());
        for (const QString& dir : std::as_const(reversed)) {
            addSearchPath(dir);
        }
        if (!systemDirs.contains(userDir)) {
            addSearchPath(userDir);
        }

        // Ensure user shader dir exists
        QDir dir(userDir);
        if (!dir.exists()) {
            dir.mkpath(QStringLiteral("."));
        }

        refresh();
    }

    ~ShaderRegistry() override
    {
        if (s_instance == this) {
            s_instance = nullptr;
        }
    }

    static ShaderRegistry* instance()
    {
        return s_instance;
    }

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

    static inline ShaderRegistry* s_instance = nullptr;
};

} // namespace PlasmaZones

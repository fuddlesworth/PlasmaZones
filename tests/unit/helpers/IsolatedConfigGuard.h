// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QTest>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QUuid>

#include "configmigration.h"

namespace PlasmaZones {
namespace TestHelpers {

/**
 * @brief RAII guard that isolates both config and data directories for tests.
 *
 * Sets XDG_CONFIG_HOME and XDG_DATA_HOME so that config backends
 * (createDefaultConfigBackend() / QSettingsConfigBackend::createDefault())
 * and QStandardPaths::GenericDataLocation resolve inside a temporary directory
 * instead of the real user directories (~/.config, ~/.local/share).
 *
 * This prevents tests from polluting the user's install with layout files,
 * shader presets, or other data written via QStandardPaths.
 */
class IsolatedConfigGuard
{
public:
    IsolatedConfigGuard()
        : m_configFileName(QStringLiteral("config-") + QUuid::createUuid().toString(QUuid::Id128).left(8))
    {
        QVERIFY2(m_tempDir.isValid(), "Failed to create temporary directory for isolated config");
        m_oldConfigHome = qEnvironmentVariable("XDG_CONFIG_HOME");
        m_oldDataHome = qEnvironmentVariable("XDG_DATA_HOME");
        qputenv("XDG_CONFIG_HOME", m_tempDir.path().toUtf8());
        qputenv("XDG_DATA_HOME", m_tempDir.path().toUtf8());
        // ensureJsonConfig() caches its "migration already done" verdict in a
        // process-level atomic (see configmigration.cpp). That's correct for
        // production — migration is one-shot — but each test case in this
        // process points the config dir at a fresh tempdir with different
        // state, so the cached verdict is stale. Clear it here so the
        // next ensureJsonConfig() call re-runs the full logic.
        ConfigMigration::resetMigrationGuardForTesting();
    }

    ~IsolatedConfigGuard()
    {
        if (m_oldConfigHome.isEmpty()) {
            qunsetenv("XDG_CONFIG_HOME");
        } else {
            qputenv("XDG_CONFIG_HOME", m_oldConfigHome.toUtf8());
        }
        if (m_oldDataHome.isEmpty()) {
            qunsetenv("XDG_DATA_HOME");
        } else {
            qputenv("XDG_DATA_HOME", m_oldDataHome.toUtf8());
        }
    }

    /// Path to the temporary XDG_CONFIG_HOME directory.
    QString configPath() const
    {
        return m_tempDir.path();
    }

    /// Path to the temporary XDG_DATA_HOME directory (same as configPath).
    QString dataPath() const
    {
        return m_tempDir.path();
    }

    /**
     * A unique config file name for this guard instance.
     *
     * Use this instead of a hardcoded config file name when possible, so each
     * test gets its own config file within the temporary directory.
     */
    QString configFileName() const
    {
        return m_configFileName;
    }

    // Non-copyable, non-movable
    IsolatedConfigGuard(const IsolatedConfigGuard&) = delete;
    IsolatedConfigGuard& operator=(const IsolatedConfigGuard&) = delete;
    IsolatedConfigGuard(IsolatedConfigGuard&&) = delete;
    IsolatedConfigGuard& operator=(IsolatedConfigGuard&&) = delete;

private:
    QTemporaryDir m_tempDir;
    QString m_oldConfigHome;
    QString m_oldDataHome;
    QString m_configFileName;
};

} // namespace TestHelpers
} // namespace PlasmaZones

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QTest>
#include <QTemporaryDir>
#include <QUuid>
#include <KSharedConfig>

namespace PlasmaZones {
namespace TestHelpers {

/**
 * @brief RAII guard that redirects KSharedConfig to a temporary directory.
 *
 * Sets XDG_CONFIG_HOME so that KSharedConfig::openConfig("plasmazonesrc") reads/writes
 * inside a temporary directory instead of the real user config at ~/.config.
 *
 * ## KSharedConfig caching limitation
 *
 * KSharedConfig::openConfig(name) caches the config object by name as a per-process
 * singleton. Once opened, subsequent calls return the same KSharedConfig instance even
 * if XDG_CONFIG_HOME has changed. This means:
 *
 * 1. The first test slot that opens "plasmazonesrc" pins the file path for the
 *    entire process lifetime.
 * 2. Later test slots with different IsolatedConfigGuard instances will get the
 *    SAME cached KSharedConfig, pointing at the FIRST temp directory.
 *
 * To work around this, use configFileName() which returns a unique name per guard
 * instance (e.g. "plasmazonesrc-<uuid>"). Pass this to KSharedConfig::openConfig()
 * and to Settings constructors that accept a config file name, so each test gets
 * its own uncached config object.
 *
 * For tests that cannot control the config file name (because the code under test
 * hardcodes "plasmazonesrc"), verify round-trips by reading from the in-memory
 * KSharedConfig state rather than re-opening the file.
 */
class IsolatedConfigGuard
{
public:
    IsolatedConfigGuard()
        : m_configFileName(QStringLiteral("plasmazonesrc-") + QUuid::createUuid().toString(QUuid::Id128).left(8))
    {
        QVERIFY2(m_tempDir.isValid(), "Failed to create temporary directory for isolated config");
        m_oldConfigHome = qEnvironmentVariable("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", m_tempDir.path().toUtf8());
    }

    ~IsolatedConfigGuard()
    {
        if (m_oldConfigHome.isEmpty()) {
            qunsetenv("XDG_CONFIG_HOME");
        } else {
            qputenv("XDG_CONFIG_HOME", m_oldConfigHome.toUtf8());
        }
    }

    /// Path to the temporary XDG_CONFIG_HOME directory.
    QString configPath() const
    {
        return m_tempDir.path();
    }

    /**
     * A unique config file name for this guard instance.
     *
     * Use this instead of the hardcoded "plasmazonesrc" when possible, to avoid
     * the KSharedConfig per-process singleton cache returning stale objects from
     * a previous test slot's temp directory.
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
    QString m_configFileName;
};

} // namespace TestHelpers
} // namespace PlasmaZones

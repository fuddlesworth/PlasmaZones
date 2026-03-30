// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <cstdlib>

/**
 * @brief RAII guard for XDG environment variables -- restores on destruction even if test fails
 */
class XdgEnvGuard
{
public:
    XdgEnvGuard()
    {
        m_savedDataDirs = qgetenv("XDG_DATA_DIRS");
        m_savedDataHome = qgetenv("XDG_DATA_HOME");
        m_hadDataDirs = qEnvironmentVariableIsSet("XDG_DATA_DIRS");
        m_hadDataHome = qEnvironmentVariableIsSet("XDG_DATA_HOME");
    }
    ~XdgEnvGuard()
    {
        if (m_hadDataDirs)
            qputenv("XDG_DATA_DIRS", m_savedDataDirs);
        else
            qunsetenv("XDG_DATA_DIRS");
        if (m_hadDataHome)
            qputenv("XDG_DATA_HOME", m_savedDataHome);
        else
            qunsetenv("XDG_DATA_HOME");
    }
    XdgEnvGuard(const XdgEnvGuard&) = delete;
    XdgEnvGuard& operator=(const XdgEnvGuard&) = delete;

private:
    QByteArray m_savedDataDirs;
    QByteArray m_savedDataHome;
    bool m_hadDataDirs = false;
    bool m_hadDataHome = false;
};

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include "autotile/algorithms/ScriptedAlgorithmLoader.h"

namespace PlasmaZones {
namespace TestHelpers {

/**
 * @brief RAII helper that loads JS-based builtin algorithms from data/algorithms/
 *
 * Creates a temporary XDG data directory with a symlink to the project's
 * data/algorithms/ directory, sets XDG env vars, and runs the ScriptedAlgorithmLoader.
 * Restores the original environment on destruction.
 *
 * Usage in test class:
 *   ScriptedAlgoTestSetup m_scriptSetup;
 *   void initTestCase() { QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR))); }
 */
class ScriptedAlgoTestSetup
{
public:
    ScriptedAlgoTestSetup() = default;
    ~ScriptedAlgoTestSetup()
    {
        if (m_initialized) {
            if (m_hadDataDirs)
                qputenv("XDG_DATA_DIRS", m_savedDataDirs);
            else
                qunsetenv("XDG_DATA_DIRS");
            if (m_hadDataHome)
                qputenv("XDG_DATA_HOME", m_savedDataHome);
            else
                qunsetenv("XDG_DATA_HOME");
        }
    }

    ScriptedAlgoTestSetup(const ScriptedAlgoTestSetup&) = delete;
    ScriptedAlgoTestSetup& operator=(const ScriptedAlgoTestSetup&) = delete;

    /**
     * @brief Set up XDG environment and load scripted algorithms
     * @param sourceDir Project source directory (PZ_SOURCE_DIR)
     * @return true if setup succeeded
     */
    bool init(const QString& sourceDir)
    {
        if (!m_xdgRoot.isValid())
            return false;

        QDir().mkpath(m_xdgRoot.path() + QStringLiteral("/plasmazones"));
        QFile::link(sourceDir + QStringLiteral("/data/algorithms"),
                    m_xdgRoot.path() + QStringLiteral("/plasmazones/algorithms"));

        m_savedDataDirs = qgetenv("XDG_DATA_DIRS");
        m_savedDataHome = qgetenv("XDG_DATA_HOME");
        m_hadDataDirs = qEnvironmentVariableIsSet("XDG_DATA_DIRS");
        m_hadDataHome = qEnvironmentVariableIsSet("XDG_DATA_HOME");
        qputenv("XDG_DATA_DIRS", m_xdgRoot.path().toUtf8());
        qputenv("XDG_DATA_HOME", m_xdgRoot.path().toUtf8());

        m_loader = std::make_unique<ScriptedAlgorithmLoader>();
        m_loader->scanAndRegister();
        m_initialized = true;
        return true;
    }

private:
    std::unique_ptr<ScriptedAlgorithmLoader> m_loader;
    QTemporaryDir m_xdgRoot;
    QByteArray m_savedDataDirs;
    QByteArray m_savedDataHome;
    bool m_hadDataDirs = false;
    bool m_hadDataHome = false;
    bool m_initialized = false;
};

} // namespace TestHelpers
} // namespace PlasmaZones

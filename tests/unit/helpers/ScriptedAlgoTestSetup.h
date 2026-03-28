// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include "autotile/algorithms/ScriptedAlgorithmLoader.h"

#include "XdgEnvGuard.h"

namespace PlasmaZones {
namespace TestHelpers {

/**
 * @brief RAII helper that loads JS-based builtin algorithms from data/algorithms/
 *
 * Creates a temporary XDG data directory with a symlink to the project's
 * data/algorithms/ directory, sets XDG env vars, and runs the ScriptedAlgorithmLoader.
 * Restores the original environment on destruction via XdgEnvGuard.
 *
 * Usage in test class:
 *   ScriptedAlgoTestSetup m_scriptSetup;
 *   void initTestCase() { QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR))); }
 */
class ScriptedAlgoTestSetup
{
public:
    ScriptedAlgoTestSetup() = default;
    ~ScriptedAlgoTestSetup() = default;

    ScriptedAlgoTestSetup(const ScriptedAlgoTestSetup&) = delete;
    ScriptedAlgoTestSetup& operator=(const ScriptedAlgoTestSetup&) = delete;
    ScriptedAlgoTestSetup(ScriptedAlgoTestSetup&&) = delete;
    ScriptedAlgoTestSetup& operator=(ScriptedAlgoTestSetup&&) = delete;

    /**
     * @brief Set up XDG environment and load scripted algorithms
     * @param sourceDir Project source directory (PZ_SOURCE_DIR)
     * @return true if setup succeeded
     */
    bool init(const QString& sourceDir)
    {
        if (!m_xdgRoot.isValid())
            return false;

        if (!QDir().mkpath(m_xdgRoot.path() + QStringLiteral("/plasmazones")))
            return false;
        const QString sourceAlgoDir = sourceDir + QStringLiteral("/data/algorithms");
        const QString algoLink = m_xdgRoot.path() + QStringLiteral("/plasmazones/algorithms");
        if (!QFile::link(sourceAlgoDir, algoLink)) {
            // Symlink failed (Windows, restricted FS) — fall back to recursive copy
            QDir srcAlgoDir(sourceAlgoDir);
            QDir destDir(algoLink);
            if (!destDir.mkpath(QStringLiteral(".")))
                return false;
            const auto entries = srcAlgoDir.entryInfoList(QDir::Files);
            for (const auto& entry : entries) {
                if (!QFile::copy(entry.absoluteFilePath(), destDir.filePath(entry.fileName())))
                    return false;
            }
        }

        // XdgEnvGuard saves the current env; we overwrite for the test
        m_envGuard = std::make_unique<XdgEnvGuard>();
        qputenv("XDG_DATA_DIRS", m_xdgRoot.path().toUtf8());
        qputenv("XDG_DATA_HOME", m_xdgRoot.path().toUtf8());

        m_loader = std::make_unique<ScriptedAlgorithmLoader>();
        m_loader->scanAndRegister();
        return true;
    }

private:
    std::unique_ptr<ScriptedAlgorithmLoader> m_loader;
    std::unique_ptr<XdgEnvGuard> m_envGuard;
    QTemporaryDir m_xdgRoot;
};

} // namespace TestHelpers
} // namespace PlasmaZones

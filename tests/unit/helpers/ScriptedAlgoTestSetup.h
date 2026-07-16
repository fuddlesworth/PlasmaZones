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

#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>

#include "AutotileTestHelpers.h"
#include "XdgEnvGuard.h"

namespace PlasmaZones {
namespace TestHelpers {

/**
 * @brief RAII helper that loads the Luau-scripted algorithms from data/algorithms/
 *
 * Creates a temporary XDG data directory with a symlink to the project's
 * data/algorithms/ directory, sets XDG env vars, and runs the PhosphorTiles::ScriptedAlgorithmLoader.
 * Restores the original environment on destruction via XdgEnvGuard.
 *
 * Loads into the test-process-wide registry returned by
 * PlasmaZones::TestHelpers::testRegistry(), which @ref registry() exposes.
 * The loader does not own a registry of its own: AlgorithmRegistry is no
 * longer a singleton, so an engine built against testRegistry() would not see
 * algorithms loaded into a separately owned instance.
 *
 * Usage in test class:
 *   ScriptedAlgoTestSetup m_scriptSetup;
 *   void initTestCase() { QVERIFY(m_scriptSetup.init(QStringLiteral(P_SOURCE_DIR))); }
 *   // then: m_scriptSetup.registry()->algorithm(id);
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
     * @param sourceDir Project source directory (P_SOURCE_DIR)
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

        // Reuse the test-process-wide testRegistry() so engines built
        // via createEngineWithWindows (or any direct AutotileEngine
        // construction with testRegistry()) see the same registered
        // algorithms this loader populates. The previous design owned
        // a separate registry; that worked when AlgorithmRegistry was
        // a singleton, but per-instance ownership means engines built
        // against testRegistry() can't see scripts loaded into a
        // different instance.
        m_loader = std::make_unique<PhosphorTiles::ScriptedAlgorithmLoader>(QStringLiteral("plasmazones/algorithms"),
                                                                            registry());
        m_loader->scanAndRegister();

        // Verify a minimum number of algorithms loaded to catch silent load failures
        if (registry()->availableAlgorithms().size() < 25) {
            qWarning() << "ScriptedAlgoTestSetup: Only" << registry()->availableAlgorithms().size()
                       << "algorithms loaded, expected at least 25 bundled Luau algorithms";
            return false;
        }
        return true;
    }

    /// Borrowed access to the test-process algorithm registry. Same
    /// instance as @c PlasmaZones::TestHelpers::testRegistry() — every
    /// helper-using test sees the same set of registered algorithms.
    PhosphorTiles::AlgorithmRegistry* registry() const
    {
        return PlasmaZones::TestHelpers::testRegistry();
    }

private:
    std::unique_ptr<PhosphorTiles::ScriptedAlgorithmLoader> m_loader;
    std::unique_ptr<XdgEnvGuard> m_envGuard;
    QTemporaryDir m_xdgRoot;
};

} // namespace TestHelpers
} // namespace PlasmaZones

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <PhosphorProtocol/AutotileMarshalling.h>
#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QString>
#include <QStringList>

namespace PhosphorTiles {
class ITileAlgorithmRegistry;
}

namespace PhosphorTileEngine {
class AutotileEngine;
}

namespace PlasmaZones {

/**
 * @brief D-Bus adaptor for the autotile placement engine
 *
 * Provides D-Bus interface: org.plasmazones.Autotile
 *
 * The autotile-SPECIFIC wire surface, sibling to org.plasmazones.Scrolling:
 * algorithm selection, master-area ratio/count, master promote/demote,
 * focus cycling, and the autotile config properties. Window lifecycle and
 * tile-request traffic for autotile screens deliberately stays on
 * org.plasmazones.Tiling — the effect keeps ONE engine-managed screen set
 * and one geometry pipeline for the whole tiling family, and TilingAdaptor
 * routes per screen through IPlacementEngine.
 */
class PLASMAZONES_EXPORT AutotileAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Autotile")

    Q_PROPERTY(QString algorithm READ algorithm WRITE setAlgorithm NOTIFY algorithmChanged)
    Q_PROPERTY(double masterRatio READ masterRatio WRITE setMasterRatio NOTIFY configChanged)
    Q_PROPERTY(int masterCount READ masterCount WRITE setMasterCount NOTIFY configChanged)
    // Gaps are rule-backed (the managed baseline gap rule, edited over
    // org.plasmazones.Rules) and synced into the engine on every settings
    // change, so they are READ-ONLY here: a direct write would be reverted on
    // the next sync. Mirrors the read-only gap getters on the Settings adaptor.
    Q_PROPERTY(int innerGap READ innerGap NOTIFY configChanged)
    Q_PROPERTY(int outerGap READ outerGap NOTIFY configChanged)
    Q_PROPERTY(bool smartGaps READ smartGaps WRITE setSmartGaps NOTIFY configChanged)
    Q_PROPERTY(bool focusNewWindows READ focusNewWindows WRITE setFocusNewWindows NOTIFY configChanged)

public:
    /**
     * @brief Construct an AutotileAdaptor
     *
     * @param engine The AutotileEngine to expose via D-Bus. Borrowed.
     * @param algorithmRegistry Injected tile-algorithm registry. Borrowed —
     *        composition root owns lifetime, must outlive the adaptor.
     *        Used for algorithm validation/enumeration on the D-Bus surface.
     * @param parent Parent QObject (typically the daemon)
     */
    explicit AutotileAdaptor(PhosphorTileEngine::AutotileEngine* engine,
                             PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry, QObject* parent = nullptr);
    ~AutotileAdaptor() override = default;

    // Property accessors
    QString algorithm() const;
    void setAlgorithm(const QString& algorithmId);

    double masterRatio() const;
    void setMasterRatio(double ratio);

    int masterCount() const;
    void setMasterCount(int count);

    // Read-only: gaps are rule-backed (see the Q_PROPERTY note above).
    int innerGap() const;
    int outerGap() const;

    bool smartGaps() const;
    void setSmartGaps(bool enabled);

    bool focusNewWindows() const;
    void setFocusNewWindows(bool enabled);

public Q_SLOTS:
    /**
     * @brief Swap positions of two tiled windows
     * @param windowId1 First window ID
     * @param windowId2 Second window ID
     */
    void swapWindows(const QString& windowId1, const QString& windowId2);

    /**
     * @brief Promote a window to the master area
     * @param windowId Window ID to promote
     */
    void promoteToMaster(const QString& windowId);

    /**
     * @brief Demote a window from master to stack area
     * @param windowId Window ID to demote
     */
    void demoteFromMaster(const QString& windowId);

    /**
     * @brief Focus the master window on the current screen
     */
    void focusMaster();

    /**
     * @brief Cycle focus to the next tiled window
     */
    void focusNext();

    /**
     * @brief Cycle focus to the previous tiled window
     */
    void focusPrevious();

    /**
     * @brief Increase the master area ratio
     * @param delta Amount to increase (e.g., 0.05 for 5%)
     */
    void increaseMasterRatio(double delta);

    /**
     * @brief Decrease the master area ratio
     * @param delta Amount to decrease (e.g., 0.05 for 5%)
     */
    void decreaseMasterRatio(double delta);

    /**
     * @brief Increase the number of master windows
     */
    void increaseMasterCount();

    /**
     * @brief Decrease the number of master windows
     */
    void decreaseMasterCount();

    /**
     * @brief Get list of available tiling algorithm IDs
     * @return List of algorithm IDs
     */
    QStringList availableAlgorithms();

    /**
     * @brief Get information about a specific algorithm
     * @param algorithmId Algorithm ID to query
     * @return PhosphorProtocol::AlgorithmInfoEntry struct with algorithm metadata
     */
    PhosphorProtocol::AlgorithmInfoEntry algorithmInfo(const QString& algorithmId);

Q_SIGNALS:
    /**
     * @brief Emitted when the tiling algorithm changes
     * @param algorithmId New algorithm ID
     */
    void algorithmChanged(const QString& algorithmId);

    /**
     * @brief Emitted when any autotile configuration property changes
     */
    void configChanged();

public:
    /**
     * @brief Clear the engine pointer during shutdown
     *
     * Called by Daemon::stop() before the AutotileEngine unique_ptr is reset,
     * so that any late D-Bus calls (arriving between engine destruction and
     * adaptor destruction) hit ensureEngine()'s null check instead of a
     * dangling pointer.
     */
    void clearEngine();

private:
    /**
     * @brief Check if engine is available, logging warning if not
     * @param methodName Name of the calling method for logging
     * @return true if engine is available
     */
    bool ensureEngine(const char* methodName) const;

    /**
     * @brief Check if engine and config are available, logging warning if not
     * @param methodName Name of the calling method for logging
     * @return true if both engine and config are available
     */
    bool ensureEngineAndConfig(const char* methodName) const;

    PhosphorTileEngine::AutotileEngine* m_engine = nullptr; ///< Borrowed; cleared on shutdown
    PhosphorTiles::ITileAlgorithmRegistry* m_algorithmRegistry = nullptr; ///< Borrowed; outlives adaptor
};

} // namespace PlasmaZones

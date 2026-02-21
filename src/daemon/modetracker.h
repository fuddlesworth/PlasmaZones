// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../core/layout.h"
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUuid>

namespace PlasmaZones {

class Settings;

/**
 * @brief Tiling mode: manual zone layouts or automatic tiling algorithms
 */
enum class TilingMode {
    Manual = 0,    ///< Traditional zone-based layout
    Autotile = 1   ///< Dynamic auto-tiling algorithm
};

/**
 * @brief Tracks the last-used manual layout, tiling mode, and autotile algorithm.
 *
 * Provides smart toggle between manual and autotile modes, persisting state
 * across sessions via KConfig.
 */
class ModeTracker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString lastManualLayoutId READ lastManualLayoutId)
    Q_PROPERTY(TilingMode currentMode READ currentMode NOTIFY currentModeChanged)
    Q_PROPERTY(QString lastAutotileAlgorithm READ lastAutotileAlgorithm NOTIFY lastAutotileAlgorithmChanged)

public:
    explicit ModeTracker(Settings* settings, QObject* parent = nullptr);
    ~ModeTracker() override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Current mode
    // ═══════════════════════════════════════════════════════════════════════════

    TilingMode currentMode() const { return m_currentMode; }
    void setCurrentMode(TilingMode mode);

    bool isAutotileMode() const { return m_currentMode == TilingMode::Autotile; }
    bool isManualMode() const { return m_currentMode == TilingMode::Manual; }

    /**
     * @brief Toggle between Manual and Autotile modes
     * @return The new mode after toggling
     */
    TilingMode toggleMode();

    // ═══════════════════════════════════════════════════════════════════════════
    // Layout tracking
    // ═══════════════════════════════════════════════════════════════════════════

    QString lastManualLayoutId() const { return m_lastManualLayoutId; }
    void recordManualLayout(const QString& layoutId);
    void recordManualLayout(const QUuid& layoutId);

    QString lastAutotileAlgorithm() const { return m_lastAutotileAlgorithm; }
    void recordAutotileAlgorithm(const QString& algorithmId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Persistence
    // ═══════════════════════════════════════════════════════════════════════════

    void load();
    void save();

Q_SIGNALS:
    void currentModeChanged(TilingMode mode);
    void lastAutotileAlgorithmChanged(const QString& algorithmId);
    void modeToggled(TilingMode newMode);

private:
    QPointer<Settings> m_settings;
    TilingMode m_currentMode = TilingMode::Manual;
    QString m_lastManualLayoutId;
    QString m_lastAutotileAlgorithm = QStringLiteral("master-stack");
};

} // namespace PlasmaZones

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
 * @brief Tracks the last-used manual layout and persists it to settings.
 */
class ModeTracker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString lastManualLayoutId READ lastManualLayoutId NOTIFY lastManualLayoutIdChanged)

public:
    explicit ModeTracker(Settings* settings, QObject* parent = nullptr);
    ~ModeTracker() override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Current mode
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Get the UUID of the last manually selected layout
     */
    QString lastManualLayoutId() const { return m_lastManualLayoutId; }

    /**
     * @brief Record a manual layout selection
     *
     * @param layoutId UUID of the selected layout
     */
    void recordManualLayout(const QString& layoutId);

    void recordManualLayout(const QUuid& layoutId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Persistence
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Load state from settings
     */
    void load();

    /**
     * @brief Save state to settings
     */
    void save();

Q_SIGNALS:
    void lastManualLayoutIdChanged(const QString& layoutId);

private:
    QPointer<Settings> m_settings;
    QString m_lastManualLayoutId;
};

} // namespace PlasmaZones

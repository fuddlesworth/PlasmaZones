// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QUndoCommand>
#include <QPointer>

namespace PlasmaZones {

class ZoneManager;

/**
 * @brief Base class for zone-related undo/redo commands
 *
 * Provides common functionality for all zone commands.
 * Uses QPointer<ZoneManager> for safe non-owning access.
 */
class BaseZoneCommand : public QUndoCommand
{
public:
    explicit BaseZoneCommand(QPointer<ZoneManager> zoneManager, const QString& text, QUndoCommand* parent = nullptr);
    ~BaseZoneCommand() override = default;

protected:
    /**
     * @brief Non-owning pointer to ZoneManager
     *
     * ZoneManager is owned by EditorController.
     * QPointer provides safe access (becomes null if ZoneManager is deleted).
     */
    QPointer<ZoneManager> m_zoneManager;
};

} // namespace PlasmaZones

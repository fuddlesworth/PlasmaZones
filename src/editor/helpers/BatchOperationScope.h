// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../undo/UndoController.h"
#include "../services/ZoneManager.h"
#include <QString>

namespace PlasmaZones {

/**
 * @brief RAII wrapper for batch operations with undo macro
 *
 * Automatically calls beginMacro()/endMacro() on UndoController
 * and beginBatchUpdate()/endBatchUpdate() on ZoneManager.
 * Ensures proper cleanup even if exceptions occur.
 *
 * Usage:
 * @code
 * {
 *     BatchOperationScope scope(m_undoController, m_zoneManager, i18nc("@action", "Move %1 Zones", count));
 *     // ... perform operations ...
 * } // endMacro and endBatchUpdate called automatically
 * @endcode
 */
class BatchOperationScope
{
public:
    /**
     * @brief Start a batch operation with undo macro
     * @param undoController UndoController to manage macro (can be null)
     * @param zoneManager ZoneManager to batch updates (can be null)
     * @param macroName Display name for the undo action
     */
    BatchOperationScope(UndoController* undoController, ZoneManager* zoneManager, const QString& macroName)
        : m_undoController(undoController)
        , m_zoneManager(zoneManager)
    {
        if (m_undoController) {
            m_undoController->beginMacro(macroName);
        }
        if (m_zoneManager) {
            m_zoneManager->beginBatchUpdate();
        }
    }

    ~BatchOperationScope()
    {
        if (m_zoneManager) {
            m_zoneManager->endBatchUpdate();
        }
        if (m_undoController) {
            m_undoController->endMacro();
        }
    }

    // Non-copyable, non-movable
    BatchOperationScope(const BatchOperationScope&) = delete;
    BatchOperationScope& operator=(const BatchOperationScope&) = delete;
    BatchOperationScope(BatchOperationScope&&) = delete;
    BatchOperationScope& operator=(BatchOperationScope&&) = delete;

private:
    UndoController* m_undoController;
    ZoneManager* m_zoneManager;
};

} // namespace PlasmaZones

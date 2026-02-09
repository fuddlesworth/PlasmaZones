// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace PlasmaZones {

/**
 * @brief Command type IDs for command merging
 *
 * Used by QUndoCommand::mergeWith() to identify command types
 * that can be merged together (e.g., consecutive geometry updates).
 */
enum CommandId {
    UpdateGeometry = 1, // UpdateZoneGeometryCommand
    UpdateAppearance = 2, // UpdateZoneAppearanceCommand
    ChangeZOrder = 3, // ChangeZOrderCommand
    UpdateLayoutName = 4, // UpdateLayoutNameCommand
    ChangeSelection = 5, // ChangeSelectionCommand
    UpdateShaderId = 6, // UpdateShaderIdCommand
    UpdateShaderParams = 7, // UpdateShaderParamsCommand (single param mode only)
    UpdateGapOverride = 8 // UpdateGapOverrideCommand (same gap type merges)
};

} // namespace PlasmaZones

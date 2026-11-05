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
    // 6 and 7 were UpdateShaderId / UpdateShaderParams; retired when overlay
    // shader assignment moved to the settings app. Ids stay reserved so
    // mergeWith() semantics of the survivors never shift.
    UpdateGapOverride = 8, // UpdateGapOverrideCommand (same gap type merges)
    UpdateFixedGeometry = 9, // UpdateFixedGeometryCommand (fixed pixel spinbox edits)
    UpdateTemplate = 10 // UpdateTemplateCommand (scrolling-template edits; merges per mergeKey)
};

} // namespace PlasmaZones

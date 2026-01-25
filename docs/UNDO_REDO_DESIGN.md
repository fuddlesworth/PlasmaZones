# Undo/Redo System Design

**Date:** 2026  
**Version:** 1.1  
**Status:** Design Phase - Updated for .cursorrules Compliance  
**Related:** [EDITOR_FEATURE_ANALYSIS.md](./EDITOR_FEATURE_ANALYSIS.md), [UNDO_REDO_DESIGN_COMPLIANCE.md](./UNDO_REDO_DESIGN_COMPLIANCE.md)

---

## Executive Summary

This document outlines the design for implementing an undo/redo system for the PlasmaZones Layout Editor. The system uses Qt's `QUndoStack` and `QUndoCommand` framework to provide a robust, memory-efficient command history for all zone and layout operations.

---

## Design Goals

1. **Memory Efficiency**: Store minimal state (not full snapshots)
2. **Performance**: Support large undo stacks (default: 50 commands)
3. **User Experience**: Provide clear visual feedback and operation descriptions
4. **Integration**: Seamlessly integrate with existing EditorController operations
5. **Extensibility**: Easy to add new command types as features are added
6. **Atomic Operations**: Support command merging for continuous operations (e.g., drag operations)

---

## Architecture Overview

### Components

1. **UndoStack** (`QUndoStack`): Manages command history
2. **Command Classes** (`QUndoCommand` subclasses): Encapsulate individual operations
3. **UndoController**: Manages undo stack and exposes state to QML
4. **EditorController Integration**: Wrap operations with undo commands
5. **QML UI**: Undo/Redo buttons, menu items, keyboard shortcuts

### Command Pattern

All editor operations will be wrapped in command objects that:
- Store the minimal state needed to undo/redo
- Implement `undo()` and `redo()` methods
- Provide human-readable descriptions for UI
- Support optional merging for atomic operations

---

## Command Classes

### Base Command Structure

All commands inherit from `QUndoCommand` and follow this pattern:

```cpp
class BaseZoneCommand : public QUndoCommand
{
public:
    BaseZoneCommand(QPointer<ZoneManager> zoneManager, const QString& text, QUndoCommand* parent = nullptr);
    virtual void undo() override = 0;
    virtual void redo() override = 0;
    virtual int id() const override = 0;  // For merging commands

protected:
    QPointer<ZoneManager> m_zoneManager;  // Non-owning pointer (ZoneManager owned by EditorController)
};
```

**Note**: Commands use `QPointer<ZoneManager>` (not raw pointer) because:
- Commands don't own ZoneManager (ZoneManager is owned by EditorController)
- `QPointer` provides safe access (becomes null if ZoneManager is deleted)
- Commands are owned by `QUndoStack`, which manages their lifecycle

### Command Types

#### 1. Zone CRUD Commands

**AddZoneCommand**
- **State**: Zone data (geometry, name, number, colors, appearance)
- **Undo**: Delete the zone
- **Redo**: Re-add the zone
- **Merge**: No (discrete operations)

**DeleteZoneCommand**
- **State**: Complete zone data (for restoration)
- **Undo**: Re-add the zone with original data
- **Redo**: Delete the zone
- **Merge**: No (discrete operations)

**UpdateZoneGeometryCommand**
- **State**: Zone ID, old geometry (x, y, width, height), new geometry
- **Undo**: Restore old geometry
- **Redo**: Apply new geometry
- **Merge**: Yes (ID: `CommandId::UpdateGeometry`) - merge with previous geometry updates for same zone

**UpdateZoneNameCommand**
- **State**: Zone ID, old name, new name
- **Undo**: Restore old name
- **Redo**: Apply new name
- **Merge**: No (discrete operations, but could merge if same zone)

**UpdateZoneNumberCommand**
- **State**: Zone ID, old number, new number
- **Undo**: Restore old number (may trigger renumbering)
- **Redo**: Apply new number
- **Merge**: No

**UpdateZoneAppearanceCommand** (colors, opacity, border, etc.)
- **State**: Zone ID, property name, old value, new value
- **Undo**: Restore old value
- **Redo**: Apply new value
- **Merge**: Yes (ID: `CommandId::UpdateAppearance`) - merge property updates for same zone

#### 2. Zone Operation Commands

**DuplicateZoneCommand**
- **State**: Source zone ID, duplicated zone ID
- **Undo**: Delete duplicated zone
- **Redo**: Re-duplicate the zone
- **Merge**: No

**SplitZoneCommand**
- **State**: Original zone data, new zones data (2 zones after split)
- **Undo**: Delete new zones, restore original zone
- **Redo**: Delete original zone, add new zones
- **Merge**: No

**FillZoneCommand**
- **State**: Zone ID, old geometry, new geometry
- **Undo**: Restore old geometry
- **Redo**: Apply new geometry
- **Merge**: No

**DeleteZoneWithFillCommand**
- **State**: Deleted zone data, affected zones (old/new geometries)
- **Undo**: Restore deleted zone, restore affected zones' geometries
- **Redo**: Delete zone, apply fill to affected zones
- **Merge**: No

#### 3. Z-Order Commands

**ChangeZOrderCommand** (bringToFront, sendToBack, bringForward, sendBackward)
- **State**: Zone ID, old z-order index, new z-order index
- **Undo**: Restore old z-order
- **Redo**: Apply new z-order
- **Merge**: No (or merge consecutive z-order changes for same zone)

#### 4. Layout Commands

**ApplyTemplateCommand**
- **State**: Old zones list (complete), new zones list (complete)
- **Undo**: Restore old zones
- **Redo**: Apply new zones
- **Merge**: No

**ClearAllZonesCommand**
- **State**: Complete zones list (for restoration)
- **Undo**: Restore all zones
- **Redo**: Clear all zones
- **Merge**: No

**UpdateLayoutNameCommand**
- **State**: Old name, new name
- **Undo**: Restore old name
- **Redo**: Apply new name
- **Merge**: No (or merge consecutive name changes)

#### 5. Batch Commands

**MacroCommand** (for complex operations)
- **State**: List of sub-commands
- **Undo**: Undo all sub-commands in reverse order
- **Redo**: Redo all sub-commands in order
- **Merge**: No

---

## Implementation Details

### File Structure

```
src/editor/undo/
├── UndoController.h
├── UndoController.cpp
├── commands/
│   ├── BaseZoneCommand.h
│   ├── AddZoneCommand.h
│   ├── AddZoneCommand.cpp
│   ├── DeleteZoneCommand.h
│   ├── DeleteZoneCommand.cpp
│   ├── UpdateZoneGeometryCommand.h
│   ├── UpdateZoneGeometryCommand.cpp
│   ├── UpdateZoneNameCommand.h
│   ├── UpdateZoneNameCommand.cpp
│   ├── UpdateZoneAppearanceCommand.h
│   ├── UpdateZoneAppearanceCommand.cpp
│   ├── DuplicateZoneCommand.h
│   ├── DuplicateZoneCommand.cpp
│   ├── SplitZoneCommand.h
│   ├── SplitZoneCommand.cpp
│   ├── FillZoneCommand.h
│   ├── FillZoneCommand.cpp
│   ├── DeleteZoneWithFillCommand.h
│   ├── DeleteZoneWithFillCommand.cpp
│   ├── ChangeZOrderCommand.h
│   ├── ChangeZOrderCommand.cpp
│   ├── ApplyTemplateCommand.h
│   ├── ApplyTemplateCommand.cpp
│   ├── ClearAllZonesCommand.h
│   ├── ClearAllZonesCommand.cpp
│   └── CommandId.h  // Command type IDs for merging
```

### UndoController

**Purpose**: Manages `QUndoStack` and exposes undo/redo state to QML

**Memory Management**:
- `UndoController` owns the `QUndoStack` (parent-based ownership or `std::unique_ptr`)
- `QUndoStack` owns all command objects (manages their lifecycle)
- Commands use `QPointer<ZoneManager>` (non-owning, safe access)
- Commands should NOT be deleted manually (QUndoStack handles cleanup)

**Properties** (Q_PROPERTY):
- `canUndo` (bool): Whether undo is available
- `canRedo` (bool): Whether redo is available
- `undoText` (QString): Description of next undo operation
- `redoText` (QString): Description of next redo operation
- `undoStackDepth` (int): Current undo stack depth
- `maxUndoStackDepth` (int): Maximum undo stack depth (configurable, default: 50)

**Methods**:
- `void undo()`: Undo last operation
- `void redo()`: Redo last undone operation
- `void clear()`: Clear undo stack
- `void setClean()`: Mark stack as clean (after save)
- `bool isClean()`: Check if stack is clean
- `void push(QUndoCommand* command)`: Push command onto stack (QUndoStack takes ownership)

**Signals**:
- `canUndoChanged()` (emitted when undo availability changes)
- `canRedoChanged()` (emitted when redo availability changes)
- `undoTextChanged()` (emitted when undo text changes)
- `redoTextChanged()` (emitted when redo text changes)
- `undoStackDepthChanged()` (emitted when stack depth changes)

### EditorController Integration

**Approach**: Wrap operations with command creation and execution

**Pattern**:
```cpp
void EditorController::updateZoneGeometry(const QString &zoneId, qreal x, qreal y, qreal width, qreal height)
{
    if (!m_undoController || !m_zoneManager) {
        qWarning() << "EditorController: Cannot update zone geometry - undo controller or zone manager is null";
        return;
    }

    // Get current geometry for undo state
    QVariantMap zone = m_zoneManager->getZoneById(zoneId);
    if (zone.isEmpty()) {
        qWarning() << "EditorController: Zone not found for geometry update:" << zoneId;
        Q_EMIT layoutSaveFailed(i18nc("@info", "Zone not found"));
        return;
    }

    // Use QLatin1String for JSON key access (Qt6 requirement)
    using namespace JsonKeys;
    QRectF oldGeometry(
        zone[QLatin1String(X)].toReal(),
        zone[QLatin1String(Y)].toReal(),
        zone[QLatin1String(Width)].toReal(),
        zone[QLatin1String(Height)].toReal()
    );
    QRectF newGeometry(x, y, width, height);

    // Create and push command
    auto* command = new UpdateZoneGeometryCommand(m_zoneManager, zoneId, oldGeometry, newGeometry);
    m_undoController->push(command);
}
```

**Operations to Wrap**:
1. All ZoneManager operations (add, delete, update, duplicate, split, fill, z-order)
2. Layout operations (apply template, clear all, update layout name)
3. Clipboard operations (paste zones)

**Operations NOT to Wrap**:
- Selection changes (not undoable)
- View changes (zoom, pan, grid visibility)
- Loading layouts (clears undo stack anyway)

### Command Merging

**Purpose**: Combine consecutive operations of the same type for better UX (e.g., continuous drag operations)

**Implementation**: Use `QUndoCommand::mergeWith()`

**Example**: `UpdateZoneGeometryCommand`
```cpp
bool UpdateZoneGeometryCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) {
        return false;
    }
    
    const auto* cmd = static_cast<const UpdateZoneGeometryCommand*>(other);
    if (cmd->m_zoneId != m_zoneId) {
        return false;  // Different zones, don't merge
    }
    
    // Merge: keep old geometry, update new geometry
    m_newGeometry = cmd->m_newGeometry;
    return true;
}
```

**Merging Strategy**:
- **Geometry updates**: Merge consecutive updates for same zone (drag operations)
- **Appearance updates**: Merge consecutive property updates for same zone
- **Z-order changes**: Merge consecutive changes for same zone
- **Layout name**: Merge consecutive name changes
- **Other operations**: No merging (discrete operations)

### State Storage

**Principle**: Store minimal state needed to restore

**Zone State** (for deletion/restoration):
- Store complete zone data (QVariantMap) - needed for full restoration
- For geometry updates: Only store old/new geometries (not full zone data)

**Memory Considerations**:
- Each command stores only necessary data
- Zone data stored as QVariantMap (lightweight)
- Maximum stack depth limits memory usage (default: 50 commands)
- Commands are cleaned up automatically by QUndoStack

---

## QML Integration

### UndoController Exposure

**EditorWindow.qml**:
```qml
EditorController {
    id: editorController
    // ...
}

UndoController {
    id: undoController
    zoneManager: editorController.zoneManager
}
```

### UI Components

#### 1. Toolbar Buttons

**Location**: `TopBar.qml`

```qml
ToolButton {
    icon.name: "edit-undo"
    text: i18n("Undo")
    tooltipText: undoController.canUndo ? 
        i18nc("@action:tooltip", "Undo: %1", undoController.undoText) : 
        i18nc("@action:tooltip", "Undo")
    enabled: undoController.canUndo
    onClicked: undoController.undo()
    Accessible.name: text
    Accessible.description: tooltipText
}

ToolButton {
    icon.name: "edit-redo"
    text: i18n("Redo")
    tooltipText: undoController.canRedo ? 
        i18nc("@action:tooltip", "Redo: %1", undoController.redoText) : 
        i18nc("@action:tooltip", "Redo")
    enabled: undoController.canRedo
    onClicked: undoController.redo()
    Accessible.name: text
    Accessible.description: tooltipText
}
```

#### 2. Keyboard Shortcuts

**Location**: `EditorShortcuts.qml`

```qml
Shortcut {
    sequence: "Ctrl+Z"
    enabled: undoController.canUndo
    onActivated: undoController.undo()
}

Shortcut {
    sequence: "Ctrl+Shift+Z"
    enabled: undoController.canRedo
    onActivated: undoController.redo()
}

// Alternative: Ctrl+Y (Windows-style)
Shortcut {
    sequence: "Ctrl+Y"
    enabled: undoController.canRedo
    onActivated: undoController.redo()
}
```

#### 3. Context Menu Items

**Location**: `ZoneContextMenu.qml` or `EditorWindow.qml`

```qml
Action {
    text: undoController.canUndo ? 
        i18nc("@action:inmenu", "Undo %1", undoController.undoText) : 
        i18nc("@action:inmenu", "Undo")
    enabled: undoController.canUndo
    shortcut: "Ctrl+Z"
    onTriggered: undoController.undo()
}

Action {
    text: undoController.canRedo ? 
        i18nc("@action:inmenu", "Redo %1", undoController.redoText) : 
        i18nc("@action:inmenu", "Redo")
    enabled: undoController.canRedo
    shortcut: "Ctrl+Shift+Z"
    onTriggered: undoController.redo()
}
```

#### 4. Help Dialog

**Location**: `HelpDialogContent.qml`

Add to keyboard shortcuts section:
```qml
RowLayout {
    Label {
        text: "Ctrl+Z"
        font.family: "monospace"
    }
    Label {
        text: i18nc("@info:keyboard-shortcut", "Undo last operation")
    }
}

RowLayout {
    Label {
        text: "Ctrl+Shift+Z"
        font.family: "monospace"
    }
    Label {
        text: i18nc("@info:keyboard-shortcut", "Redo last undone operation")
    }
}
```

---

## Integration Points

### EditorController Changes

**Minimal Changes Required**:
1. Add `UndoController* m_undoController` member
2. Initialize undo controller in constructor
3. Wrap operations with command creation/pushing
4. Clear undo stack on layout load
5. Mark stack as clean on save

**Pattern Example**:
```cpp
// Before:
void EditorController::deleteZone(const QString &zoneId)
{
    m_zoneManager->deleteZone(zoneId);
    markUnsaved();
}

// After:
void EditorController::deleteZone(const QString &zoneId)
{
    if (!m_undoController || !m_zoneManager) {
        qWarning() << "EditorController: Cannot delete zone - undo controller or zone manager is null";
        return;
    }

    // Get zone data for undo
    QVariantMap zoneData = m_zoneManager->getZoneById(zoneId);
    if (zoneData.isEmpty()) {
        qWarning() << "EditorController: Zone not found for deletion:" << zoneId;
        Q_EMIT layoutSaveFailed(i18nc("@info", "Zone not found"));
        return;
    }
    
    // Create and push command
    auto* command = new DeleteZoneCommand(m_zoneManager, zoneId, zoneData);
    m_undoController->push(command);
    
    markUnsaved();
}
```

### ZoneManager Extensions

**New Methods Needed**:
- `QVariantMap getZoneById(const QString& zoneId) const`: Get complete zone data (for undo state)
- `void setZoneData(const QString& zoneId, const QVariantMap& zoneData)`: Set complete zone data (for undo restoration)
- `void restoreZones(const QVariantList& zones)`: Restore multiple zones (for template/layout operations)

**Note**: These methods should:
- Emit appropriate signals for QML updates (use `Q_EMIT` macro)
- Use `QLatin1String()` or `JsonKeys` constants for JSON key access (Qt6 requirement)
- Include error handling with `qWarning()` logging
- Follow signal emission patterns (only emit when values actually change)

---

## Command Descriptions (i18n)

All command descriptions should use `i18n()` for translation:

```cpp
// Examples:
new AddZoneCommand(zoneManager, i18nc("@action", "Add Zone"), ...);
new DeleteZoneCommand(zoneManager, i18nc("@action", "Delete Zone"), ...);
new UpdateZoneGeometryCommand(zoneManager, i18nc("@action", "Move Zone"), ...);
new UpdateZoneNameCommand(zoneManager, i18nc("@action", "Rename Zone"), ...);
new DuplicateZoneCommand(zoneManager, i18nc("@action", "Duplicate Zone"), ...);
new SplitZoneCommand(zoneManager, i18nc("@action", "Split Zone"), ...);
new ApplyTemplateCommand(zoneManager, i18nc("@action", "Apply Template: %1", templateName), ...);
new ClearAllZonesCommand(zoneManager, i18nc("@action", "Clear All Zones"), ...);
```

---

## Configuration

### Settings (KConfig)

**Group**: `Editor`

**Keys**:
- `MaxUndoStackDepth` (int, default: 50): Maximum number of undo commands
- `ClearUndoStackOnSave` (bool, default: false): Clear undo stack after save (optional)

**Usage**:
```cpp
void UndoController::loadSettings()
{
    KConfigGroup group(KSharedConfig::openConfig(), "Editor");
    int maxDepth = group.readEntry("MaxUndoStackDepth", 50);
    m_undoStack->setUndoLimit(maxDepth);
}
```

---

## Testing Strategy

### Unit Tests

**Test Cases**:
1. **Command Execution**:
   - Test each command type (undo/redo cycle)
   - Test command descriptions
   - Test command merging (where applicable)

2. **UndoController**:
   - Test stack state (canUndo/canRedo)
   - Test stack depth limits
   - Test clean state management

3. **Integration**:
   - Test operations wrapped with commands
   - Test undo stack clearing on layout load
   - Test clean state on save

### Test Files

```
tests/
├── test_undo_commands.cpp
├── test_undo_controller.cpp
└── test_undo_integration.cpp
```

---

## Migration Strategy

### Phase 1: Core Infrastructure
1. Create `UndoController` class
2. Create base command classes
3. Create command ID enum
4. Add to CMakeLists.txt

### Phase 2: Zone Commands
1. Implement zone CRUD commands (add, delete, update)
2. Implement zone operation commands (duplicate, split, fill)
3. Integrate with EditorController zone operations

### Phase 3: Layout Commands
1. Implement layout commands (template, clear, name)
2. Integrate with EditorController layout operations

### Phase 4: UI Integration
1. Add UndoController to QML
2. Add toolbar buttons
3. Add keyboard shortcuts
4. Add context menu items
5. Update help dialog

### Phase 5: Polish
1. Command merging optimization
2. Performance testing
3. Memory profiling
4. Documentation

---

## Edge Cases & Considerations

### 1. Layout Loading
- **Behavior**: Clear undo stack when loading a layout
- **Rationale**: Undo history doesn't apply across different layouts

### 2. Layout Saving
- **Behavior**: Optionally clear undo stack after save (user preference)
- **Default**: Keep undo stack (allows undo after save)

### 3. Template Application
- **Behavior**: Single command (not individual zone operations)
- **Rationale**: Template application is atomic (single undo operation)

### 4. Zone Deletion with Fill
- **Behavior**: Single command (deletion + fill operations)
- **Rationale**: Atomic operation from user perspective

### 5. Continuous Drag Operations
- **Behavior**: Merge consecutive geometry updates
- **Rationale**: Better UX (single undo for entire drag operation)

### 6. Memory Limits
- **Behavior**: Limit undo stack depth (default: 50)
- **Rationale**: Prevent excessive memory usage

### 7. QML Signal Updates
- **Behavior**: Commands should trigger ZoneManager signals (use `Q_EMIT` macro)
- **Rationale**: QML bindings need updates for undo/redo
- **Implementation**: ZoneManager methods called by commands should emit signals normally
- **Note**: Signals should only be emitted when values actually change (follow .cursorrules pattern)

---

## Performance Considerations

### Memory Usage
- **Estimate**: ~1-5 KB per command (depending on command type)
- **Maximum**: 50 commands × 5 KB = ~250 KB (acceptable)
- **Optimization**: Command merging reduces number of commands

### Execution Time
- **Undo/Redo**: Should be fast (simple state restoration)
- **Concern**: Large template applications (many zones)
- **Solution**: Use macro commands (single undo operation)

### Signal Emissions
- **Concern**: Excessive signal emissions during undo/redo
- **Solution**: ZoneManager should emit signals normally (QML needs updates)

---

## Future Enhancements

### Potential Additions
1. **Undo History View**: Visual list of undoable operations
2. **Undo to Point**: Undo to specific command (not just last)
3. **Command Macros**: Record and replay operation sequences
4. **Undo Stack Persistence**: Save undo stack to disk (for crash recovery)
5. **Selective Undo**: Undo specific operations (not just sequential)

---

## References

- Qt Documentation: [QUndoStack](https://doc.qt.io/qt-6/qundostack.html)
- Qt Documentation: [QUndoCommand](https://doc.qt.io/qt-6/qundocommand.html)
- [EDITOR_FEATURE_ANALYSIS.md](./EDITOR_FEATURE_ANALYSIS.md) - Feature requirements
- [.cursorrules](../.cursorrules) - Coding standards

---

## Approval & Sign-off

**Design Status**: ✅ Ready for Implementation  
**Review Status**: Pending  
**Implementation Status**: Not Started  

---

**Document Version**: 1.1  
**Last Updated**: 2026  
**Author**: AI Design  
**Status**: Design Complete - Updated for .cursorrules Compliance

**Changes in v1.1:**
- Fixed memory management: Changed raw pointers to `QPointer<ZoneManager>` in command classes
- Fixed string literal handling: Added `QLatin1String()` wrapper for JSON key access (Qt6 requirement)
- Added error handling examples: Included `qWarning()` logging and error signal emissions
- Clarified ownership relationships: Documented that QUndoStack owns commands, commands use non-owning QPointer
- Updated code examples: All examples now follow .cursorrules standards

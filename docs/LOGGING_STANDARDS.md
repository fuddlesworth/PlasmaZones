# PlasmaZones Logging Standards

This document defines the logging standards and best practices for the PlasmaZones codebase. Following these standards ensures consistent, filterable, and useful log output across all modules.

## Table of Contents

1. [Current State Analysis](#current-state-analysis)
2. [Qt Logging Categories](#qt-logging-categories)
3. [Category Structure](#category-structure)
4. [Message Format Standards](#message-format-standards)
5. [Severity Level Guidelines](#severity-level-guidelines)
6. [Runtime Filtering](#runtime-filtering)
7. [Implementation Guide](#implementation-guide)
8. [Migration Plan](#migration-plan)

---

## Current State Analysis

### Existing Logging Patterns

The codebase currently uses direct `qDebug()`, `qWarning()`, and `qCritical()` calls without Qt logging categories. Analysis of the codebase shows:

- **~460+ logging statements** across the codebase
- **Inconsistent prefixes**: Some use class names (e.g., `"LayoutAdaptor:"`, `"ZoneManager:"`), others don't
- **No runtime filtering**: All messages go to the same output regardless of module
- **Mixed verbosity**: Debug messages mixed with important warnings

### Issues with Current Approach

1. **No module-level filtering**: Cannot enable/disable logging per component
2. **Inconsistent formatting**: Hard to parse logs programmatically
3. **Performance overhead**: Debug statements always evaluated even when not needed
4. **Difficult debugging**: Cannot isolate issues to specific modules

---

## Qt Logging Categories

### Overview

Qt provides `QLoggingCategory` for categorized logging. Benefits include:

- **Runtime filtering** via environment variables or config files
- **Per-category enable/disable** without recompilation
- **Zero overhead** when category is disabled (message not evaluated)
- **Hierarchical categories** (e.g., `plasmazones.daemon.dbus`)

### Basic Usage

```cpp
// In header file (declare category)
#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcDaemon)

// In source file (define category)
Q_LOGGING_CATEGORY(lcDaemon, "plasmazones.daemon")

// Usage
qCDebug(lcDaemon) << "Daemon initialized";
qCWarning(lcDaemon) << "Connection failed:" << error;
qCCritical(lcDaemon) << "Fatal error, shutting down";
```

---

## Category Structure

### Proposed Logging Categories

```
plasmazones                    # Root category (rarely used directly)
  |
  +-- plasmazones.core         # Core library (layout, zone, detection)
  |     +-- plasmazones.core.layout      # Layout management
  |     +-- plasmazones.core.zone        # Zone operations
  |     +-- plasmazones.core.detection   # Zone detection algorithms
  |     +-- plasmazones.core.screen      # Screen management
  |
  +-- plasmazones.daemon       # Daemon process
  |     +-- plasmazones.daemon.overlay   # Overlay service
  |     +-- plasmazones.daemon.shortcuts # Shortcut handling
  |     +-- plasmazones.daemon.selector  # Zone selector
  |
  +-- plasmazones.dbus         # D-Bus communication
  |     +-- plasmazones.dbus.layout      # Layout adaptor
  |     +-- plasmazones.dbus.settings    # Settings adaptor
  |     +-- plasmazones.dbus.window      # Window tracking adaptor
  |     +-- plasmazones.dbus.overlay     # Overlay adaptor
  |
  +-- plasmazones.editor       # Layout editor
  |     +-- plasmazones.editor.controller # Editor controller
  |     +-- plasmazones.editor.zones     # Zone manager
  |     +-- plasmazones.editor.undo      # Undo/redo system
  |
  +-- plasmazones.effect       # KWin effect
  |     +-- plasmazones.effect.snap      # Window snapping
  |     +-- plasmazones.effect.nav       # Keyboard navigation
  |
  +-- plasmazones.config       # Settings/configuration
```

### Category Header File

Create `/home/nlavender/Documents/PlasmaZones/src/core/logging.h`:

```cpp
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PLASMAZONES_LOGGING_H
#define PLASMAZONES_LOGGING_H

#include <QLoggingCategory>

// Core categories
Q_DECLARE_LOGGING_CATEGORY(lcCore)
Q_DECLARE_LOGGING_CATEGORY(lcCoreLayout)
Q_DECLARE_LOGGING_CATEGORY(lcCoreZone)
Q_DECLARE_LOGGING_CATEGORY(lcCoreDetection)
Q_DECLARE_LOGGING_CATEGORY(lcCoreScreen)

// Daemon categories
Q_DECLARE_LOGGING_CATEGORY(lcDaemon)
Q_DECLARE_LOGGING_CATEGORY(lcDaemonOverlay)
Q_DECLARE_LOGGING_CATEGORY(lcDaemonShortcuts)
Q_DECLARE_LOGGING_CATEGORY(lcDaemonSelector)

// D-Bus categories
Q_DECLARE_LOGGING_CATEGORY(lcDBus)
Q_DECLARE_LOGGING_CATEGORY(lcDBusLayout)
Q_DECLARE_LOGGING_CATEGORY(lcDBusSettings)
Q_DECLARE_LOGGING_CATEGORY(lcDBusWindow)
Q_DECLARE_LOGGING_CATEGORY(lcDBusOverlay)

// Editor categories
Q_DECLARE_LOGGING_CATEGORY(lcEditor)
Q_DECLARE_LOGGING_CATEGORY(lcEditorController)
Q_DECLARE_LOGGING_CATEGORY(lcEditorZones)
Q_DECLARE_LOGGING_CATEGORY(lcEditorUndo)

// KWin effect categories
Q_DECLARE_LOGGING_CATEGORY(lcEffect)
Q_DECLARE_LOGGING_CATEGORY(lcEffectSnap)
Q_DECLARE_LOGGING_CATEGORY(lcEffectNav)

// Config categories
Q_DECLARE_LOGGING_CATEGORY(lcConfig)

#endif // PLASMAZONES_LOGGING_H
```

### Category Definition File

Create `/home/nlavender/Documents/PlasmaZones/src/core/logging.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "logging.h"

// Core categories
Q_LOGGING_CATEGORY(lcCore,          "plasmazones.core")
Q_LOGGING_CATEGORY(lcCoreLayout,    "plasmazones.core.layout")
Q_LOGGING_CATEGORY(lcCoreZone,      "plasmazones.core.zone")
Q_LOGGING_CATEGORY(lcCoreDetection, "plasmazones.core.detection")
Q_LOGGING_CATEGORY(lcCoreScreen,    "plasmazones.core.screen")

// Daemon categories
Q_LOGGING_CATEGORY(lcDaemon,          "plasmazones.daemon")
Q_LOGGING_CATEGORY(lcDaemonOverlay,   "plasmazones.daemon.overlay")
Q_LOGGING_CATEGORY(lcDaemonShortcuts, "plasmazones.daemon.shortcuts")
Q_LOGGING_CATEGORY(lcDaemonSelector,  "plasmazones.daemon.selector")

// D-Bus categories
Q_LOGGING_CATEGORY(lcDBus,         "plasmazones.dbus")
Q_LOGGING_CATEGORY(lcDBusLayout,   "plasmazones.dbus.layout")
Q_LOGGING_CATEGORY(lcDBusSettings, "plasmazones.dbus.settings")
Q_LOGGING_CATEGORY(lcDBusWindow,   "plasmazones.dbus.window")
Q_LOGGING_CATEGORY(lcDBusOverlay,  "plasmazones.dbus.overlay")

// Editor categories
Q_LOGGING_CATEGORY(lcEditor,           "plasmazones.editor")
Q_LOGGING_CATEGORY(lcEditorController, "plasmazones.editor.controller")
Q_LOGGING_CATEGORY(lcEditorZones,      "plasmazones.editor.zones")
Q_LOGGING_CATEGORY(lcEditorUndo,       "plasmazones.editor.undo")

// KWin effect categories
Q_LOGGING_CATEGORY(lcEffect,     "plasmazones.effect")
Q_LOGGING_CATEGORY(lcEffectSnap, "plasmazones.effect.snap")
Q_LOGGING_CATEGORY(lcEffectNav,  "plasmazones.effect.nav")

// Config categories
Q_LOGGING_CATEGORY(lcConfig, "plasmazones.config")
```

---

## Message Format Standards

### Standard Message Pattern

Set in `main()` of each executable:

```cpp
// Recommended message pattern
qSetMessagePattern("[%{time yyyy-MM-dd hh:mm:ss.zzz}] [%{type}] [%{category}] %{message}");

// Example output:
// [2026-01-15 14:32:45.123] [debug] [plasmazones.daemon] Daemon initialized
// [2026-01-15 14:32:45.456] [warning] [plasmazones.dbus.layout] Layout not found: abc-123
```

### Message Content Guidelines

#### Do:

```cpp
// Include relevant context
qCDebug(lcCoreLayout) << "Loading layout" << layout->name()
                      << "with" << layout->zoneCount() << "zones";

// Include identifiers for tracking
qCWarning(lcDBusLayout) << "Layout not found:" << id;

// Include error details
qCCritical(lcDaemon) << "D-Bus registration failed:" << error.message();
```

#### Don't:

```cpp
// Too vague
qCDebug(lcCore) << "Done";

// Redundant class name (category already shows this)
qCDebug(lcCoreLayout) << "LayoutManager: Loading...";  // BAD

// Too verbose for debug level
qCDebug(lcCore) << "Entering function foo()";  // Use trace or remove
```

### Contextual Information

Include relevant context but avoid redundancy:

| Information Type | Include in Message | Example |
|-----------------|-------------------|---------|
| Object ID/Name | Yes | `layout->name()`, `zone->id()` |
| Class Name | No (use category) | Don't prefix with class name |
| Function Name | Rarely | Only for complex call chains |
| Parameter Values | Yes (key ones) | Important inputs/outputs |
| Counts/Sizes | Yes | `zoneCount()`, `screens.size()` |
| File Paths | Yes (on errors) | For I/O operations |
| Error Messages | Always | From Qt, D-Bus, system calls |

---

## Severity Level Guidelines

### qCDebug - Development and Troubleshooting

**Use for:** Information useful during development and debugging
**Typical state:** Disabled in production
**Performance:** Zero overhead when disabled

```cpp
// State changes
qCDebug(lcDaemon) << "Overlay visibility changed:" << visible;

// Operation completion
qCDebug(lcCoreLayout) << "Loaded" << count << "layouts from" << directory;

// Configuration loading
qCDebug(lcConfig) << "Settings loaded - DragModifier:" << modifier;

// Connection events
qCDebug(lcDBus) << "Connected to session bus";
```

### qCInfo - Significant Events

**Use for:** Important milestones that operators should know about
**Typical state:** Enabled in production
**Performance:** Minimal overhead

```cpp
// Service lifecycle
qCInfo(lcDaemon) << "PlasmaZones daemon started";
qCInfo(lcDaemon) << "Daemon shutdown complete";

// Feature activation
qCInfo(lcEditor) << "Layout editor opened for screen:" << screenName;

// Configuration changes
qCInfo(lcConfig) << "Settings saved successfully";
```

### qCWarning - Recoverable Problems

**Use for:** Issues that are handled but indicate potential problems
**Typical state:** Always enabled
**Action:** Review periodically, may indicate configuration issues

```cpp
// Invalid input (handled gracefully)
qCWarning(lcDBusLayout) << "Invalid UUID format:" << id;

// Resource not found (fallback used)
qCWarning(lcCoreLayout) << "Layout not found, using default";

// Configuration issues
qCWarning(lcConfig) << "Invalid opacity value:" << opacity << "- using default";

// Transient failures
qCWarning(lcDaemon) << "D-Bus call failed, retrying:" << error;
```

### qCCritical - Severe Errors

**Use for:** Errors that prevent normal operation
**Typical state:** Always enabled, may trigger alerts
**Action:** Requires investigation

```cpp
// Service failures
qCCritical(lcDaemon) << "Cannot connect to session D-Bus";

// Unrecoverable errors
qCCritical(lcDaemon) << "Failed to register D-Bus service after"
                     << maxRetries << "attempts";

// Data corruption
qCCritical(lcCoreLayout) << "Layout file corrupted:" << filePath;

// Required dependency missing
qCCritical(lcDaemonOverlay) << "LayerShellQt not available - cannot create overlay";
```

### Decision Tree

```
Is the application functioning normally?
  |
  +-- Yes -> Is this information useful for operators?
  |            |
  |            +-- Yes -> qCInfo
  |            +-- No  -> qCDebug
  |
  +-- No  -> Can the application recover?
               |
               +-- Yes -> qCWarning
               +-- No  -> qCCritical
```

---

## Runtime Filtering

### Environment Variable

Use `QT_LOGGING_RULES` to control logging at runtime:

```bash
# Enable all PlasmaZones logging
export QT_LOGGING_RULES="plasmazones.*=true"

# Enable only daemon logging
export QT_LOGGING_RULES="plasmazones.daemon.*=true"

# Enable warnings and above for all, debug for D-Bus
export QT_LOGGING_RULES="plasmazones.*.debug=false;plasmazones.dbus.*=true"

# Disable all debug, enable specific component
export QT_LOGGING_RULES="*.debug=false;plasmazones.core.detection=true"
```

### Configuration File

Create `~/.config/QtProject/qtlogging.ini`:

```ini
[Rules]
; Disable all debug by default
*.debug=false

; Enable PlasmaZones warnings and above
plasmazones.*=true
plasmazones.*.debug=false

; Enable specific debug categories as needed
plasmazones.daemon=true
plasmazones.dbus.layout=true
```

### Systemd Service Configuration

For the daemon service, configure logging in the unit file:

```ini
[Service]
Environment="QT_LOGGING_RULES=plasmazones.*.debug=false"
```

### Debugging Scripts

Create helper scripts for common scenarios:

```bash
#!/bin/bash
# debug-daemon.sh - Run daemon with full logging
export QT_LOGGING_RULES="plasmazones.daemon.*=true;plasmazones.dbus.*=true"
exec plasmazonesd "$@"
```

```bash
#!/bin/bash
# debug-editor.sh - Run editor with full logging
export QT_LOGGING_RULES="plasmazones.editor.*=true"
exec plasmazones-editor "$@"
```

---

## Implementation Guide

### Step 1: Add Logging Infrastructure

1. Create `src/core/logging.h` and `src/core/logging.cpp`
2. Add `logging.cpp` to CMakeLists.txt
3. Set message pattern in `main()` functions

### Step 2: Migrate Existing Logging

Transform existing calls:

```cpp
// Before
qDebug() << "LayoutManager: Loading layouts from" << directory;

// After
#include "logging.h"
qCDebug(lcCoreLayout) << "Loading layouts from" << directory;
```

### Step 3: Remove Redundant Prefixes

The logging category replaces class name prefixes:

```cpp
// Before
qWarning() << "ZoneManager: Zone not found:" << zoneId;

// After
qCWarning(lcEditorZones) << "Zone not found:" << zoneId;
```

### Step 4: Review Severity Levels

Audit each log statement for appropriate severity:

| Current Pattern | New Category | Notes |
|----------------|--------------|-------|
| `qDebug() << "Loaded..."` | `qCDebug(lcCoreLayout)` | Keep as debug |
| `qWarning() << "Invalid..."` | `qCWarning(lcConfig)` | Keep as warning |
| `qCritical() << "Cannot connect..."` | `qCCritical(lcDaemon)` | Keep as critical |
| `qDebug() << "Starting..."` | `qCInfo(lcDaemon)` | Upgrade to info |

---

## Migration Plan

### Phase 1: Infrastructure (Week 1)

- [ ] Create logging.h and logging.cpp
- [ ] Update CMakeLists.txt
- [ ] Add message pattern to main() functions
- [ ] Document in CONTRIBUTING.md

### Phase 2: Core Module (Week 2)

- [ ] Migrate src/core/*.cpp
- [ ] Migrate src/config/*.cpp
- [ ] Test filtering works correctly

### Phase 3: Daemon Module (Week 3)

- [ ] Migrate src/daemon/*.cpp
- [ ] Migrate src/dbus/*.cpp
- [ ] Update systemd service file

### Phase 4: Editor and Effect (Week 4)

- [ ] Migrate src/editor/*.cpp
- [ ] Migrate kwin-effect/*.cpp
- [ ] Final testing and documentation

### Code Review Checklist

For each migrated file:

- [ ] Includes `logging.h`
- [ ] Uses appropriate category for the module
- [ ] No class name prefix in messages
- [ ] Appropriate severity level
- [ ] Useful contextual information included
- [ ] No sensitive data logged (API keys, etc.)

---

## Appendix: Quick Reference

### Category to File Mapping

| Category | Primary Files |
|----------|---------------|
| `lcCoreLayout` | layoutmanager.cpp, layout.cpp |
| `lcCoreZone` | zone.cpp |
| `lcCoreDetection` | zonedetector.cpp |
| `lcCoreScreen` | screenmanager.cpp |
| `lcDaemon` | daemon.cpp |
| `lcDaemonOverlay` | overlayservice.cpp |
| `lcDaemonShortcuts` | shortcutmanager.cpp |
| `lcDaemonSelector` | zoneselectorcontroller.cpp |
| `lcDBusLayout` | layoutadaptor.cpp |
| `lcDBusWindow` | windowtrackingadaptor.cpp |
| `lcEditorController` | EditorController.cpp |
| `lcEditorZones` | ZoneManager.cpp |
| `lcConfig` | settings.cpp |
| `lcEffect` | plasmazoneseffect.cpp |

### Common Filtering Commands

```bash
# Debug specific component
QT_LOGGING_RULES="plasmazones.daemon.overlay=true" plasmazonesd

# Verbose mode (all debug)
QT_LOGGING_RULES="plasmazones.*=true" plasmazonesd

# Production mode (warnings only)
QT_LOGGING_RULES="plasmazones.*.debug=false;plasmazones.*.info=false" plasmazonesd

# Debug D-Bus communication
QT_LOGGING_RULES="plasmazones.dbus.*=true" plasmazonesd
```

---

## References

- [Qt Logging Categories Documentation](https://doc.qt.io/qt-6/qloggingcategory.html)
- [Qt Message Handling](https://doc.qt.io/qt-6/debug.html)
- [qSetMessagePattern](https://doc.qt.io/qt-6/qtglobal.html#qSetMessagePattern)

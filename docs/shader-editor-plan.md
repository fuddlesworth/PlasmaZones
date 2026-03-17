# PlasmaZones Shader Package Editor — Implementation Plan

## Overview

A standalone application (`plasmazones-shader-editor`) for creating and editing shader packages with integrated code editing, live preview, metadata authoring, and parameter tuning.

## Architecture

The editor is a **thin frontend** over existing infrastructure. It does not reimplement compilation, preview rendering, parameter validation, or include resolution — all of that already exists in the core library and is used directly.

### Key Insight: No Files Needed for Preview

The existing `ShaderSettingsDialog.qml` already embeds a `ZoneShaderItem` directly in-process — no D-Bus, no daemon, no file I/O. The shader editor uses the same pattern:

```
Type in KTextEditor → debounce (~300ms) → set shaderPreview.shaderSource = expandedGlsl → instant render
```

Files are only written on explicit Save to `~/.local/share/plasmazones/shaders/`.

### Existing Infrastructure (DO NOT Reimplement)

| Component | Location | What It Does |
|---|---|---|
| `ZoneShaderItem` | `src/daemon/rendering/zoneshaderitem.h` | QQuickItem that renders zones with shaders — embed directly in QML |
| `ZoneShaderNodeRhi` | `src/daemon/rendering/zoneshadernoderhi.cpp` | Compiles GLSL 450 via QShaderBaker, caches results, reports errors |
| `ShaderIncludeResolver` | `src/core/shaderincluderesolver.h` | Expands `#include` directives (depth 10, cycle detection) |
| `ShaderRegistry` | `src/core/shaderregistry.h` | Metadata loading, param validation, uniform translation, file watching |
| `EditorController` | `src/editor/controller/shader.cpp` | `zonesForShaderPreview()`, `translateShaderParams()`, preset save/load |
| `ShaderSettingsDialog` | `src/editor/qml/ShaderSettingsDialog.qml` | Reference for embedded preview + parameter UI patterns |
| Parameter delegates | `src/editor/qml/ParameterDelegate.qml` | Slider/color/bool/int controls — reusable |

### New Dependencies

| Dependency | Purpose |
|---|---|
| `KF6::TextEditor` | Kate editor component (syntax highlighting, code folding, search/replace, undo/redo) |

`KF6::SyntaxHighlighting` comes along with KTextEditor. The system already has `/usr/share/org.kde.syntax-highlighting/syntax-bundled/glsl.xml`.

## UI Layout

See `docs/shader-editor-mockup.svg` for visual mockup.

```
┌──────────────────────────────────────────────────────────────────────┐
│ Menu: File | Edit | Shader | View | Help                             │
│ Toolbar: [New][Open][Save] | [▶ Compile][Validate] | Layout: [▾]    │
├─────────────────────────────────┬────────────────────────────────────┤
│                                 │                                    │
│   CODE EDITOR (KTextEditor)     │   LIVE PREVIEW (ZoneShaderItem)    │
│                                 │                                    │
│   Tabs: effect.frag | zone.vert │   Embedded in-process rendering    │
│         | pass0.frag | ...      │   FPS counter, play/pause          │
│                                 │   Test zone layout selector        │
│                                 │                                    │
│                                 ├────────────────────────────────────┤
│                                 │                                    │
│                                 │   PARAMETERS / METADATA / PRESETS  │
│   ┌─────────────────────────┐   │                                    │
│   │ Problems | Output       │   │   Accordion-grouped controls       │
│   │ Compilation status      │   │   Sliders, colors, bools           │
│   └─────────────────────────┘   │   "Insert →" uniform snippets      │
│                                 │   Metadata form (name, category)   │
├─────────────────────────────────┴────────────────────────────────────┤
│ Status: GLSL 450 | Ln 22 Col 38 | cosmic-flow v1.0 | Compiled ✓     │
└──────────────────────────────────────────────────────────────────────┘
```

## Implementation Phases

### Phase 1: Core Editor (MVP)

**Goal:** Standalone app with KTextEditor, tabbed file editing, open/save shader packages.

**Files to create:**
```
src/shader-editor/
├── CMakeLists.txt                    # Build config, link KF6::TextEditor
├── main.cpp                          # App entry, KAboutData, QApplication
├── shadereditorwindow.h/cpp          # QMainWindow with KTextEditor embedded
└── shaderpackageio.h/cpp             # Load/save shader package directories
```

**Deliverables:**
- Standalone `plasmazones-shader-editor` binary
- KTextEditor with GLSL syntax highlighting (system glsl.xml)
- Tabbed editing: effect.frag, zone.vert, and buffer passes
- File → New Shader (from template), Open Shader, Save Shader
- Metadata.json shown as a tab (raw JSON editing initially)
- Top-level CMakeLists.txt updated with `add_subdirectory(src/shader-editor)`

### Phase 2: Live Preview

**Goal:** Embedded ZoneShaderItem preview with auto-recompile on keystroke.

**Files to create/modify:**
```
src/shader-editor/
├── qml/
│   └── PreviewPane.qml               # ZoneShaderItem embed + zone layout
├── previewcontroller.h/cpp            # Bridges KTextEditor ↔ QML preview
└── shadereditorwindow.cpp             # Add QQuickWidget for preview pane
```

**Deliverables:**
- Right panel with embedded `ZoneShaderItem`
- Auto-recompile on text change (300ms debounce)
- `ShaderIncludeResolver` expands includes before passing to preview
- Compilation errors displayed in problems panel
- Test zone layout selector (1-zone, 2-column, 4-grid)
- FPS counter and play/pause

### Phase 3: Metadata Editor

**Goal:** Visual form replacing raw JSON editing for metadata.json.

**Files to create:**
```
src/shader-editor/
├── metadataeditor.h/cpp               # QWidget form for metadata fields
├── parametermodel.h/cpp               # QAbstractListModel for parameters[]
└── parametereditordialog.h/cpp        # Add/edit parameter dialog
```

**Deliverables:**
- Form fields: name, id, category, author, version, description
- Parameter table: add/remove/reorder with type-appropriate editors
- Auto slot assignment (next available slot per type)
- Slot conflict detection and warnings
- "Insert uniform" button → inserts GLSL reference into code editor
- Multipass toggle with buffer shader file management

### Phase 4: Parameter Tuning

**Goal:** Live parameter controls that update the preview in real-time.

**Files to create/modify:**
```
src/shader-editor/
├── qml/
│   └── ParameterTuner.qml            # Reuse ParameterDelegate patterns
└── previewcontroller.cpp              # Wire param changes → ZoneShaderItem
```

**Deliverables:**
- Accordion-grouped parameter controls (same UX as ShaderSettingsDialog)
- Sliders for float, spinbox for int, checkbox for bool, color picker for color
- Values update preview in real-time via ZoneShaderItem properties
- "Copy as defaults" → writes current values to metadata parameter defaults
- Lock toggles per parameter
- Preset save/load (reuse EditorController patterns)

### Phase 5: Polish

**Deliverables:**
- Custom syntax highlighting extension for PlasmaZones uniforms (iTime, customParams, etc.)
- Auto-completion for uniforms and `#include` paths
- Shader validation indicator in status bar
- Export shader package as .tar.gz for sharing
- "Edit Shader" button in existing ShaderSettingsDialog launches editor
- Desktop entry + icon for app launcher
- Integration with ShaderRegistry file watcher (saved shaders auto-appear in layout editor)

## Shader Template (New Shader)

```
my-shader/
├── metadata.json
│   {
│     "id": "my-shader",
│     "name": "My Shader",
│     "category": "Custom",
│     "author": "",
│     "version": "1.0",
│     "fragmentShader": "effect.frag",
│     "vertexShader": "zone.vert",
│     "multipass": false,
│     "parameters": []
│   }
├── zone.vert        (standard boilerplate)
└── effect.frag      (minimal with common.glsl include)
```

## Technical Notes

- `QShaderBaker` is NOT thread-safe — compilation serialized (1 thread pool in daemon, same constraint here)
- Shader cache: 64 entries max, LRU eviction, keyed by path+mtime
- Max 4 buffer passes, 32 float slots, 16 color slots, 4 image slots
- Include depth limit: 10 levels
- File watcher debounce: 500ms in ShaderRegistry
- Preview debounce in editor: 300ms (faster than file watcher since no disk I/O)

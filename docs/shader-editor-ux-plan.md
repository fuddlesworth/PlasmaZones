# Shader Editor UX Polish Plan

Gap analysis between mockup (`docs/shader-editor-mockup.svg`) and current implementation.

## 1. Toolbar (NEW)

Currently: no toolbar, only a File menu.
Mockup: full toolbar with icon buttons and a layout selector dropdown.

**Add a QToolBar below the menu bar:**
- [New] [Open] [Save] — standard file actions with icons
- Separator
- [▶ Compile] — green-accented button, triggers recompile
- [Validate] — runs compilation check without preview update
- Separator
- "Test Layout:" label + QComboBox dropdown (Single Zone, 2-Column Split, 3-Column, 4-Grid)
  - Replaces the zone cycle button currently in the QML preview header
  - Dropdown is more discoverable than a cycle button

## 2. Problems Panel (NEW — bottom of code editor)

Currently: shader errors only show in the QML preview error overlay.
Mockup: tabbed panel under the code editor with Problems | Output | Compiler tabs.

**Add a QTabWidget below m_tabWidget in a vertical splitter (left side):**
- **Problems tab**: QTableWidget with columns [Severity, Line, Message]
  - Populated from ZoneShaderItem::errorLog after compilation
  - Double-click a row → jumps to line in KTextEditor
  - Shows "0" badge when no problems, red badge with count when errors
- **Output tab**: QTextEdit (read-only) showing general messages
  - "Shader compiled successfully (GLSL 450, N lines)"
  - "Uniforms: iTime, iResolution, customParams1, ..."
  - "Includes resolved: common.glsl (100 lines)"
- **Compiler tab**: QTextEdit (read-only) raw compiler output

This means the left panel becomes a vertical splitter:
- Top: m_tabWidget (code editor tabs)
- Bottom: problems/output panel (collapsible, ~120px default)

## 3. Status Bar Enrichment

Currently: only shows filename and cursor position (Line N, Col M).
Mockup: rich status bar with GLSL version, shader name, compilation status, FPS.

**Add these permanent widgets to the status bar:**
- Left: `GLSL 450` | `Ln N, Col M` | `UTF-8`
- Center: `cosmic-flow v1.0` | `User Shader` (or `System Shader`)
- Right: `Shader compiled ✓` (or `Compilation error ✗`) | `60 FPS`

Feed from PreviewController signals: status, fps, and metadata editor fields.

## 4. Preview Info Bar

Currently: nothing below the preview rendering area.
Mockup: small text line `t = 12.47s | 1920×1080 | 2 zones`

**Add a Label below the ZoneShaderItem in PreviewPane.qml:**
```qml
Label {
    text: "t = " + root.pc.iTime.toFixed(2) + "s | "
        + Math.floor(previewBackground.width) + "×" + Math.floor(previewBackground.height)
        + " | " + (root.pc.zones ? root.pc.zones.length : 0) + " zones"
    font.family: "monospace"
    font.pixelSize: 10
    opacity: 0.5
}
```

## 5. Parameter Lock Toggles

Currently: no lock icons on parameter rows.
Mockup: each parameter row has a 🔓 toggle on the right side.

**Add a lock QPushButton (icon toggle) to each parameter row in ParameterPanel:**
- Unlocked (default): parameter can be changed, "Reset All" affects it
- Locked: slider/control is disabled, "Reset All" skips it
- Store locked state in ParamControl struct: `bool locked = false`
- Lock icon: use `object-locked` / `object-unlocked` from icon theme

## 6. Metadata Tab Cleanup

Current problems:
- "Shader Info" and "Parameters" sections are in a single scrollable form with QGroupBox
- The parameter table with 8 columns is cramped and hard to read
- Fragment/Vertex shader labels are unnecessary (always effect.frag/zone.vert)
- Add/Remove/Insert buttons at the bottom are disconnected from the table

**Redesign the Metadata tab:**

Split into two collapsible sections with cleaner layout:

**Section: Shader Info** (compact form, no QGroupBox border)
- ID (read-only for existing shaders, shown as a label not a disabled input)
- Name (QLineEdit)
- Category (QComboBox, editable)
- Author (QLineEdit)
- Version (QLineEdit, small width)
- Description (QLineEdit, single line — QTextEdit was overkill)
- Multipass (QCheckBox) — only show if relevant

Remove Fragment/Vertex shader labels entirely — they're always the same.

**Section: Parameters** (better table)
- Simplify to 5 columns: Name | Type | Slot | Default | Group
  - Remove ID column (redundant, shown in tooltip)
  - Remove separate Min/Max columns (show as "0.0 – 1.0" in the Default column or tooltip)
- Inline edit on double-click (instead of read-only table + separate dialog)
- Right-click context menu: Edit, Remove, Insert Uniform, Move Up, Move Down
- Keep Add Parameter button, but move Insert Uniform to context menu

## 7. Menu Bar Additions

Currently: only File menu.
Mockup: File | Edit | Shader | View | Help

**Edit menu:**
- Delegated to KTextEditor (undo, redo, cut, copy, paste, find, replace)
- KTextEditor views already handle Ctrl+Z, Ctrl+F etc. via right-click
- We can populate Edit menu from the active KTextEditor::View's action collection

**Shader menu:**
- Compile (Ctrl+B) — trigger recompile
- Validate — check for errors without updating preview
- Reset Parameters — reset all parameter sliders to defaults
- Separator
- New Shader From Template — same as File > New

**View menu:**
- Toggle Problems Panel (Ctrl+J)
- Toggle Preview (Ctrl+P)
- Toggle Parameter Panel
- Separator
- Increase Font Size / Decrease Font Size (delegated to KTextEditor)

**Help menu:**
- About PlasmaZones Shader Editor
- Shader Uniform Reference — could open a tooltip/dialog listing all available uniforms

## 8. Preview Header Cleanup

Currently: "Live Preview" label + FPS + 4 icon buttons (play, reset, grid, recompile).
With the toolbar adding compile and layout controls, the preview header becomes redundant.

**Simplify the preview header:**
- Remove "Live Preview" text label (the splitter position makes it obvious)
- Keep only: FPS counter + play/pause button
- Move recompile and zone layout to the toolbar (item 1)
- Or: make the header thinner and more compact

## Implementation Order

1. **Problems panel** — highest impact, makes shader errors visible and actionable
2. **Toolbar** — second most visible, consolidates actions
3. **Preview info bar** — quick win, single QML label
4. **Status bar enrichment** — moderate effort, high polish
5. **Metadata tab redesign** — cleanup existing mess
6. **Parameter lock toggles** — minor UX addition
7. **Menu additions** — Edit/Shader/View/Help menus
8. **Preview header cleanup** — depends on toolbar being done first

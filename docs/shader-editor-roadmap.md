# Shader Editor Roadmap

## Quick Wins

- [x] **Ctrl+Tab tab switching** — Ctrl+Tab / Ctrl+Shift+Tab cycle through code editor tabs
- [x] **File > Recent Packages** — KRecentFilesAction submenu, persisted via KSharedConfig
- [ ] **Screenshot preview** — Requires QQuickRenderControl offscreen rendering with QSGRenderNode; deferred pending Qt upstream investigation

## Medium Effort

- [x] **`#line` directives in include expansion** — Emit `#line N "filename"` in `ShaderIncludeResolver` so GLSL errors map back to original source files, not the expanded temp file
- [ ] **Drag-and-drop open** — Override `dragEnterEvent`/`dropEvent` on ShaderEditorWindow to open shader package folders
- [ ] **Fullscreen preview toggle** — Hide code/params panels and expand preview to fill the window (Ctrl+E or F11)
- [ ] **External file watching** — Detect when shader files are modified outside the editor and prompt to reload

## Larger Features

- [ ] **Template selection dialog** — Offer basic, multipass, audio-reactive, and wallpaper templates when creating a new shader package
- [ ] **Install to shaders directory** — One-click action to copy the current package to `~/.local/share/plasmazones/shaders/`
- [ ] **Auto-save / crash recovery** — Periodic backup to temp dir with recovery dialog on next launch
- [ ] **Offline GLSL validation** — Optional `glslangValidator` integration for syntax checking without GPU context

# PlasmaZones Media Checklist

Track progress on screenshots and videos for README and marketing.

## Screenshots (PNG)

Save to `docs/media/screenshots/`

### KCM Tabs
- [x] `kcm-layouts.png` - System Settings (Layouts tab)
- [x] `kcm-editor.png` - System Settings (Editor tab)
- [x] `kcm-assignments.png` - System Settings (Assignments tab)
- [x] `kcm-snapping.png` - System Settings (Snapping tab)
- [x] `kcm-tiling.png` - System Settings (Tiling tab)
- [x] `kcm-general.png` - System Settings (General tab)
- [x] `kcm-exclusions.png` - System Settings (Exclusions tab)
- [x] `kcm-about.png` - System Settings (About tab)

### Other
- [x] `shaders-gallery.png` - Grid showing shader effects
- [x] `layout-osd.png` - Visual layout OSD on screen
- [x] `layout-popup.png` - Layout picker fullscreen overlay
- [x] `multi-monitor.png` - Dual monitor setup with different layouts

## GIFs (Optimized, 800px wide max, under 5MB each)

Save to `docs/media/videos/`. Embed in README with `![Description](docs/media/videos/name.gif)`.

- [x] `drag-snap.gif` - Window being dragged with zones highlighted
- [x] `editor.gif` - Layout editor with zones being drawn/edited
- [x] `keyboard-nav.gif` - Moving window between zones with shortcuts
- [x] `layout-switch.gif` - Cycling layouts with OSD feedback
- [x] `shaders.gif` - Showcase shader effects
- [x] `zone-selector.gif` - Drag to edge → selector appears → pick zone

### Still needed
- [ ] Autotiling in action (windows auto-arranging with an algorithm)
- [ ] Snap Assist overlay (window thumbnails after a snap)
- [ ] Navigation OSD (move/focus/swap feedback overlays)

## Long-form Videos (MP4/WebM, 1080p)

Host on YouTube, link in README.

- [ ] Quick Demo (30s) - Fast overview of main features
- [ ] Full Tutorial (5min) - Installation to first layout
- [ ] Shader Showcase (1min) - All built-in shaders with music

## Progress Summary

**Screenshots:** 12/12 complete ✓
**GIFs:** 6/9 complete
**Long-form videos:** 0/3 complete

## Recording Tips

### GIFs

- Record at 30fps, optimize to 15fps for file size
- Use `peek` or `kooha` for recording
- Optimize with `gifsicle` or `gifski`
- Target under 5MB for quick loading
- 800px wide max for README display

### Tools

- Screenshots: `spectacle` (KDE native)
- GIF recording: `peek`, `kooha`, `byzanz`
- GIF optimization: `gifsicle -O3 --lossy=80`
- Long-form video: OBS Studio

# PlasmaZones Media Checklist

Track progress on screenshots and videos for README and marketing.

## Screenshots (PNG)

Save to `docs/media/screenshots/`

- [x] `shaders-gallery.png` - Grid showing shader effects
- [x] `layout-osd.png` - Visual layout OSD on screen
- [x] `kcm-settings.png` - System Settings module (Layouts tab)
- [x] `kcm-appearance.png` - System Settings (Appearance tab)
- [x] `kcm-behavior.png` - System Settings (Behavior tab)
- [x] `kcm-zoneselector.png` - System Settings (Zone Selector tab)
- [x] `kcm-shortcuts.png` - System Settings (Shortcuts tab)
- [x] `multi-monitor.png` - Dual monitor setup with different layouts

## GIFs (Optimized, 800px wide max, under 5MB each)

Save to `docs/media/videos/`. Embed in README with `![Description](docs/media/videos/name.gif)`.

- [ ] `hero.gif` - Core workflow: drag window → zones appear → snap (5–8s)
- [ ] `drag-snap.gif` - Window being dragged with zones highlighted
- [ ] `editor.gif` - Layout editor with zones being drawn/edited
- [ ] `zone-selector.gif` - Drag to edge → selector appears → pick zone (6–8s)
- [x] `keyboard-nav.gif` - Moving window between zones with shortcuts (8–10s)
- [ ] `layout-switch.gif` - Cycling layouts with OSD feedback (5s)
- [ ] `shaders.gif` - Showcase 3–4 shader effects (10–15s)

## Long-form Videos (MP4/WebM, 1080p)

Host on YouTube, link in README.

- [ ] Quick Demo (30s) - Fast overview of main features
- [ ] Full Tutorial (5min) - Installation to first layout
- [ ] Shader Showcase (1min) - All built-in shaders with music

## Progress Summary

**Screenshots:** 8/8 complete ✓  
**GIFs:** 1/7 complete  
**Long-form videos:** 0/3 complete

### Priority Order for GIFs

1. `hero.gif` - Main eye-catcher at top of README
2. `drag-snap.gif` - Shows core functionality
3. `editor.gif` - Layout editor in action
4. `zone-selector.gif` - Zone selector animation
5. `keyboard-nav.gif` ✓ - Power user feature
6. `layout-switch.gif` - OSD in action
7. `shaders.gif` - Eye candy showcase

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

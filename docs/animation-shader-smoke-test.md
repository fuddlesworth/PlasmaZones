# Animation Shader Wire-Up — Smoke Test

After installing the `feat/animation-shader-wireup` branch and restarting the daemon + kwin:

```bash
sudo cmake --install build
systemctl --user restart plasmazonesd
kwin_wayland --replace &  # from a TTY, or relog
```

Drop the JSON below into `~/.config/plasmazones/config.json` under the `Animations` group as a JSON-encoded string under the key `ShaderProfileTree`:

```bash
jq '.Animations.ShaderProfileTree = $tree' \
  --argjson tree '"{\"baseline\":{},\"overrides\":[{\"path\":\"window.open\",\"profile\":{\"effectId\":\"popin\",\"parameters\":{\"scaleFrom\":0.85,\"overshoot\":0.08}}},{\"path\":\"window.close\",\"profile\":{\"effectId\":\"dissolve\",\"parameters\":{\"grain\":0.04}}},{\"path\":\"window.minimize\",\"profile\":{\"effectId\":\"slidefade\",\"parameters\":{\"direction\":2}}},{\"path\":\"window.unminimize\",\"profile\":{\"effectId\":\"slidefade\",\"parameters\":{\"direction\":0}}},{\"path\":\"window.maximize\",\"profile\":{\"effectId\":\"morph\",\"parameters\":{\"warpStrength\":0.12}}},{\"path\":\"window.unmaximize\",\"profile\":{\"effectId\":\"morph\",\"parameters\":{\"warpStrength\":0.08}}},{\"path\":\"window.focus\",\"profile\":{\"effectId\":\"dissolve\",\"parameters\":{\"grain\":0.02,\"softness\":0.15}}},{\"path\":\"window.move\",\"profile\":{\"effectId\":\"glitch\"}},{\"path\":\"window.resize\",\"profile\":{\"effectId\":\"morph\",\"parameters\":{\"warpStrength\":0.05,\"warpFrequency\":6}}},{\"path\":\"zone.snapIn\",\"profile\":{\"effectId\":\"popin\",\"parameters\":{\"scaleFrom\":0.8}}},{\"path\":\"zone.snapOut\",\"profile\":{\"effectId\":\"slidefade\",\"parameters\":{\"direction\":2,\"fadeWidth\":0.3}}},{\"path\":\"zone.snapResize\",\"profile\":{\"effectId\":\"morph\",\"parameters\":{\"warpStrength\":0.05}}},{\"path\":\"zone.layoutSwitchIn\",\"profile\":{\"effectId\":\"glitch\"}},{\"path\":\"osd.show\",\"profile\":{\"effectId\":\"slide\",\"parameters\":{\"direction\":1,\"parallax\":0.2}}},{\"path\":\"osd.hide\",\"profile\":{\"effectId\":\"slidefade\",\"parameters\":{\"direction\":1,\"fadeWidth\":0.3}}},{\"path\":\"panel.popup.zoneSelector\",\"profile\":{\"effectId\":\"dissolve\",\"parameters\":{\"grain\":0.05}}},{\"path\":\"panel.popup.layoutPicker\",\"profile\":{\"effectId\":\"morph\",\"parameters\":{\"warpStrength\":0.15,\"warpFrequency\":4}}},{\"path\":\"panel.popup.snapAssist\",\"profile\":{\"effectId\":\"popin\",\"parameters\":{\"scaleFrom\":0.9,\"overshoot\":0.05}}},{\"path\":\"cursor.drag\",\"profile\":{\"effectId\":\"dissolve\",\"parameters\":{\"grain\":0.03}}}]}"' \
  ~/.config/plasmazones/config.json > /tmp/pz-config.json && mv /tmp/pz-config.json ~/.config/plasmazones/config.json
systemctl --user restart plasmazonesd
```

### Trigger sequence and expected effect

| User action | Path that fires | Effect | What you should see |
|---|---|---|---|
| Open a new app | `window.open` | popin | Scale-pop-in with overshoot |
| Close a window | `window.close` | dissolve | Noise-dissolve fade |
| Minimize | `window.minimize` | slidefade dir=2 (down) | Slide down + fade |
| Unminimize | `window.unminimize` | slidefade dir=0 (up) | Slide up + fade in |
| Maximize | `window.maximize` | morph | Smooth warp into max |
| Unmaximize | `window.unmaximize` | morph (lighter warp) | Smooth warp back |
| Focus another window | `window.focus` | dissolve | Subtle dissolve flash |
| Start a non-snap drag | `window.move` | glitch | Pixel-shift glitch |
| Drag a window edge | `window.resize` | morph | Warp during resize |
| Alt-drag → zone snap | `zone.snapIn` | popin | Popin into zone |
| Drag a snapped window out | `zone.snapOut` | slidefade dir=2 | Slide-down fade out |
| Alt-drag in-zone resize | `zone.snapResize` | morph | Warp during snap-resize |
| Cycle layout (Meta+Alt+]) | `zone.layoutSwitchIn` | glitch | Glitch flash on every resnap |
| Layout-cycle OSD shows | `osd.show` (slide dir=1 right) | slide | OSD slides in from left |
| Layout OSD hides | `osd.hide` | slidefade dir=1 | Slide right + fade out |
| Drag near zone edge (selector pops in) | `panel.popup.zoneSelector` | dissolve | Selector strip dissolves in |
| Open layout picker (Meta+Alt+Space) | `panel.popup.layoutPicker` | morph | Picker morphs in |
| Snap-assist appears after a snap | `panel.popup.snapAssist` | popin | Snap-assist pops |
| Drag start (shader-only) | `cursor.drag` | dissolve | Brief dissolve at drag start |

### Live-reload verification

While the daemon is running, edit `Animations.ShaderProfileTree` to swap an effect — e.g. change `osd.show`'s effectId from `slide` to `dissolve`. Save the file. Trigger an OSD (cycle layouts). The OSD should now dissolve instead of slide — no daemon restart needed.

The kwin-effect picks up tree changes via the daemon's `settingsChanged` D-Bus signal (kwin-effect/plasmazoneseffect.cpp:1556). The daemon picks up tree changes via `ISettings::shaderProfileTreeChanged` (src/daemon/overlayservice/settings.cpp).

### Walk-up inheritance check

Replace the JSON above with just:

```json
{"baseline":{"effectId":"dissolve"},"overrides":[]}
```

Now every event resolves to `dissolve` via the baseline. Override only `panel.popup.layoutPicker` to `morph`:

```json
{"baseline":{"effectId":"dissolve"},"overrides":[{"path":"panel.popup.layoutPicker","profile":{"effectId":"morph"}}]}
```

Layout picker morphs; everything else (zone selector, snap-assist, OSD, all `window.*`, all `zone.*`, `cursor.drag`) dissolves.

### Caveats

- **kwin-effect uniform contract** — only `iTime`, `iResolution`, and the effect's declared `parameters` are guaranteed. Effects that read `iMouse`, `iTimeDelta`, `iFrame`, or buffer-shader inputs work on the daemon path only.
- **Per-window event conflict** — last-event-wins on overlap. If `window.move` and `zone.snapIn` fire on the same window in quick succession, the second wins.
- **Daemon-overlay hide leg on snap-assist** — `panel.popup.snapAssist` only fires on **show**; the hide leg is empty because snap-assist destroys-on-hide, the hide animation never paints a frame.
- **Workspace events** — `workspace.switchIn`, `workspace.switchOut`, `workspace.overview` are **not** consulted by either the kwin-effect or daemon. Stock KWin effects own those transitions.

See `docs/animation-shader-wireup-plan.md` for the full plan and the deferred Phase 4 / Phase 5 work.

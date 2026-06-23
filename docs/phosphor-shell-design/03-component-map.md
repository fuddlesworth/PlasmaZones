<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 03: Phosphor Shell Component Map

The proposed architecture, justified by the patterns in `01-feature-inventory.md` and the gaps in `02-gap-analysis.md`. Written so each section can drive a discrete PR or set of PRs.

## Layout

```
src/phosphor-shell/                        # The shell process (currently examples/phosphor-shell/)
  main.cpp                                 # Loads QML; owns ShellEngine
  qml/
    Phosphor/
      Theme/
        Theme.qml                          # M3 color tokens (singleton)
        Tokens.qml                         # radius/spacing/font/elevation (singleton)
        Motion.qml                         # M3 emphasized/standard/expressive durations + curves
        StateLayer.qml                     # M3 hover/pressed state overlay primitive
      Widgets/                             # Reusable atoms (Phosphor* prefix)
        PhosphorButton.qml  PhosphorSlider.qml  PhosphorCard.qml  PhosphorRipple.qml  PhosphorTextField.qml
        ElevationShadow.qml  ConnectedCorner.qml  ConnectedShape.qml
      Bar/
        BarCanvas.qml                      # Connected-corner geometry
        BarHost.qml                        # PerScreen { delegate: PanelWindow { ... } }
        Slot.qml                           # left/center/right slot abstraction
        Widgets/                           # Bar widget catalog (registered to IBarWidgetFactory)
          Clock.qml  Workspaces.qml  FocusedApp.qml  SystemMetrics.qml
          Network.qml  Bluetooth.qml  Audio.qml  Battery.qml  Tray.qml
          Media.qml  PrivacyIndicator.qml  IdleInhibitor.qml  ControlCenterButton.qml
          NotificationButton.qml  PowerButton.qml  Spacer.qml
      Launcher/
        Launcher.qml                       # Spotlight / Connected / Standalone skins
        Result.qml  ProviderHeader.qml
        Providers/                         # Registered to ILauncherProviderFactory
          AppsProvider.qml  CalculatorProvider.qml  WindowsProvider.qml
          EmojiProvider.qml  CommandProvider.qml  ClipboardProvider.qml
      ControlCenter/
        ControlCenter.qml                  # Popout root
        Tile.qml  DetailPanel.qml
        Tiles/                             # Registered to IControlCenterTileFactory
          NetworkTile.qml  BluetoothTile.qml  AudioTile.qml  BrightnessTile.qml
          NightModeTile.qml  DarkModeTile.qml  AirplaneTile.qml  IdleTile.qml
          PowerProfileTile.qml  WallpaperTile.qml
        Cards/                             # Reusable rich cards (calendar, weather, sysmon, media)
      Notifications/
        Toast.qml  ToastHost.qml           # Layer-shell surface per screen
        NotificationCenter.qml             # History popout
        NotificationItem.qml  RuleEditor.qml
      Lock/
        LockSurface.qml                    # ext-session-lock-v1 surface per screen
        LockClock.qml  LockAuthField.qml  LockMediaCard.qml
      OSD/
        OSDHost.qml                        # Single layer-shell surface per screen
        VolumeOSD.qml  BrightnessOSD.qml  MicOSD.qml  CapsLockOSD.qml
        IdleInhibitorOSD.qml  PowerProfileOSD.qml
      Dashboard/                           # Optional second-surface dash (DankDash-style)
        Dashboard.qml  OverviewTab.qml  WallpaperTab.qml  WeatherTab.qml  MediaTab.qml
      Wallpaper/
        WallpaperPicker.qml  GalleryView.qml  CyclingScheduler.qml
        Transitions/                       # Wallpaper transition shaders (disc/fade/etc)
      Themes/
        ThemeBrowser.qml  ColorPickerModal.qml
      Power/
        PowerMenu.qml
      Polkit/
        PolkitAuthModal.qml
      Tools/                               # Misc surfaces
        ColorPicker.qml  Screenshot.qml  Cheatsheet.qml
      Helpers/
        PerScreen.qml                      # Variants-style per-monitor helper

libs/                                      # C++ libraries, every concern is its own lib (existing pattern)
  phosphor-shell/                          # Already exists; extend
  phosphor-theme/                          # NEW: token store + matugen runner + template engine
  phosphor-popout/                         # NEW: PopoutService
  phosphor-registry/                       # NEW: Widget/provider registry primitives
  phosphor-ipc/                            # NEW: IpcRouter + IpcTarget + schema

  # NEW integration libs (Phase 2 of 04-implementation-plan.md).
  # All DBus-based ones depend on the existing phosphor-dbus for shared plumbing.
  # The `phosphor-service-*` prefix marks shell-domain integration services
  # as a group, distinct from foundation libs above.
  phosphor-service-pipewire/               # PipeWire mixer (sinks/sources/volume/mute)
  phosphor-service-network/                # NetworkManager (DBus org.freedesktop.NetworkManager)
  phosphor-service-bluetooth/              # BlueZ (DBus org.bluez)
  phosphor-service-brightness/             # /sys/class/backlight + logind brightness
  phosphor-service-idle/                   # idle-inhibit + idle-state machine
  phosphor-service-notifications/          # org.freedesktop.Notifications daemon
  phosphor-service-polkit/                 # polkit-qt6 agent
  phosphor-service-clipboard/              # wlr-data-control + cliphist-style history
  phosphor-service-lock/                   # PAM + ext-session-lock-v1 coordination
  phosphor-service-session/                # logind (DBus org.freedesktop.login1)

  # EXTRACTED from the original phosphor-services umbrella (Phase 2.0 of plan).
  # Phase 2.0 complete: umbrella deleted, no compat shim.
  phosphor-service-sni/                    # ✓ StatusNotifierItem host + watcher + dbusmenu (shipped)
  phosphor-service-mpris/                  # ✓ MPRIS2 controller (shipped)
  phosphor-service-upower/                 # ✓ battery + power-supply readouts (shipped)
  phosphor-service-icontheme/              # ✓ icon-theme resolver + Qt image provider (shipped, kept standalone)

  # Existing libs, touched only where the surface work requires.
  # Already follow the one-concern-per-lib pattern; this plan extends it consistently.
  (existing) phosphor-config, phosphor-layer, phosphor-shaders, phosphor-zones,
             phosphor-snap-engine, phosphor-tile-engine, phosphor-wayland,
             phosphor-screens, phosphor-geometry, phosphor-overlay, phosphor-compositor,
             phosphor-fsloader, phosphor-identity, phosphor-shortcuts,
             phosphor-rendering, phosphor-audio (cava spectrum, NOT the mixer),
             phosphor-engine, phosphor-protocol, phosphor-dbus (the -common for DBus),
             phosphor-shell-patterns, phosphor-animation, ...

bin/
  phosphorctl/                             # NEW: Go (or Rust) typed IPC CLI
```

## The theme token system

We adopt **upstream Material 3 token names verbatim**. State aliases layer on top rather than being baked into the names. That was a DMS mistake. The **built-in default values come from the canonical Phosphor palette** at https://phosphor-works.github.io/palette/. The palette is M3 plus ANSI 16 under CC-BY-SA 4.0. Matugen replaces these values at runtime from any wallpaper. The names stay stable.

**Shipped (PR #534).** The lib at `libs/phosphor-theme` ships the MVP subset of the token set sketched below. That subset is the surface ramp, primary/secondary/tertiary accents, error, outline, status ramp from ANSI 16, and the four brand-gradient stops. The full M3 ramp is aspirational. Missing tokens include `surfaceContainerLowest/Highest`, the `inverse*` triad, `on_secondary_container`, and the explicit ANSI 16 namespace. Future tokens land via the same `TokenNames` contract. Snake_case is the wire format because it matches the matugen JSON keys. Examples are `on_surface` and `surface_container_high`. The camelCase forms below are sketch shorthand only.

```qml
// libs/phosphor-theme/qml/Phosphor/Theme/Theme.qml, pragma Singleton
// Tokens are reached via `palette["<snake_case_key>"]` against the
// tracked QVariantMap from the C++ PaletteStore. The camelCase form
// here is a sketch alias for readability.
QtObject {
    // Core M3 palette (every token matugen emits)
    property color primary:                  "#3B82F6"   // blue-500
    property color onPrimary:                "#F0F9FF"
    property color primaryContainer:         "#1E3A8A"
    property color onPrimaryContainer:       "#DBEAFE"
    property color secondary:                "#A855F7"   // purple-500
    property color onSecondary:              "#FAF5FF"
    property color secondaryContainer:       "#581C87"
    property color onSecondaryContainer:     "#F3E8FF"
    property color tertiary:                 "#22D3EE"   // cyan-400
    property color onTertiary:               "#ECFEFF"
    property color tertiaryContainer:        "#164E63"
    property color onTertiaryContainer:      "#CFFAFE"
    property color error:                    "#F43F5E"   // rose-500
    property color onError:                  "#FFF1F2"
    property color errorContainer:           "#881337"
    property color onErrorContainer:         "#FFE4E6"
    property color background:               "#050916"   // void
    property color onBackground:             "#E6EDFF"
    property color surface:                  "#0B1730"   // navy
    property color onSurface:                "#E6EDFF"
    property color surfaceVariant:           "#1E293B"   // slate-800
    property color onSurfaceVariant:         "#94A3B8"
    property color outline:                  "#3B82F6"   // = primary
    property color outlineVariant:           "#1E3A8A"
    property color surfaceContainerLowest:   "#050916"
    property color surfaceContainerLow:      "#050916"
    property color surfaceContainer:         "#070F22"   // abyss
    property color surfaceContainerHigh:     "#101A36"
    property color surfaceContainerHighest:  "#101A36"
    property color inverseSurface:           "#E6EDFF"
    property color inverseOnSurface:         "#0B1730"
    property color inversePrimary:           "#1E3A8A"
    property color shadow:                   "#000000"
    property color scrim:                    "#000000"

    // ANSI 16 (drives terminal templates AND status colors in shell UI)
    property color ansiBlack:                "#050916"
    property color ansiRed:                  "#F43F5E"
    property color ansiGreen:                "#10B981"
    property color ansiYellow:               "#FBBF24"
    property color ansiBlue:                 "#3B82F6"
    property color ansiMagenta:              "#A855F7"
    property color ansiCyan:                 "#22D3EE"
    property color ansiWhite:                "#CBD5E1"
    property color ansiBrightBlack:          "#1E293B"
    property color ansiBrightRed:            "#FB7185"
    property color ansiBrightGreen:          "#34D399"
    property color ansiBrightYellow:         "#FCD34D"
    property color ansiBrightBlue:           "#60A5FA"
    property color ansiBrightMagenta:        "#C084FC"
    property color ansiBrightCyan:           "#67E8F9"
    property color ansiBrightWhite:          "#F1F5F9"

    // Semantic shell status (derived from ANSI by default; matugen may override)
    readonly property color success:         ansiGreen
    readonly property color warning:         ansiYellow
    readonly property color info:            ansiBrightCyan

    // Phosphor brand-gradient accents (used for hero surfaces, wallpaper)
    property color brandCyan:                "#22D3EE"
    property color brandBlue:                "#3B82F6"
    property color brandPurple:              "#A855F7"
    property color brandRose:                "#F43F5E"
    property color brandNavy:                "#0B1730"
    property color brandAbyss:               "#070F22"
    property color brandVoid:                "#050916"

    // State layers (M3 spec opacities)
    function stateLayer(base, state) { ... }   // hover 0.08, focus 0.12, pressed 0.12, drag 0.16
}

// qml/Phosphor/Theme/Tokens.qml, pragma Singleton
QtObject {
    readonly property QtObject radius: QtObject {
        property real xs: 4
        property real sm: 8
        property real md: 12
        property real lg: 16
        property real xl: 24
        property real full: 9999
    }
    readonly property QtObject spacing: QtObject {
        property real xs: 4
        property real sm: 8
        property real md: 12
        property real lg: 16
        property real xl: 24
        property real xxl: 32
    }
    readonly property QtObject font: QtObject {
        property string family: "Inter"
        property string mono: "JetBrains Mono"
        property real labelSmall: 11
        property real labelMedium: 12
        property real labelLarge: 14
        property real bodySmall: 12
        property real bodyMedium: 14
        property real bodyLarge: 16
        property real titleSmall: 14
        property real titleMedium: 16
        property real titleLarge: 22
        property real headlineSmall: 24
        property real headlineMedium: 28
        property real headlineLarge: 32
        property real displaySmall: 36
        property real displayMedium: 45
        property real displayLarge: 57
    }
    readonly property QtObject elevation: QtObject {
        // Maps to ElevationShadow.qml configurations
        property int level0: 0
        property int level1: 1
        property int level2: 3
        property int level3: 6
        property int level4: 8
        property int level5: 12
    }
}

// qml/Phosphor/Theme/Motion.qml, pragma Singleton
QtObject {
    // M3 motion spec
    readonly property QtObject duration: QtObject {
        property int short1: 50  ; property int short2: 100 ; property int short3: 150 ; property int short4: 200
        property int medium1: 250; property int medium2: 300; property int medium3: 350; property int medium4: 400
        property int long1: 450  ; property int long2: 500  ; property int long3: 550  ; property int long4: 600
        property int extraLong1: 700; property int extraLong2: 800; property int extraLong3: 900; property int extraLong4: 1000
    }
    readonly property QtObject easing: QtObject {
        // emphasized / standard / expressive bezier curves
        property var emphasized: [0.2, 0.0, 0.0, 1.0]
        property var emphasizedDecelerate: [0.05, 0.7, 0.1, 1.0]
        property var emphasizedAccelerate: [0.3, 0.0, 0.8, 0.15]
        property var standard: [0.2, 0.0, 0.0, 1.0]
        property var expressiveSpatial: [0.34, 0.8, 0.34, 1.0]
    }
}
```

C++ backing in `phosphor-theme` is sketched below. The shipped interface lives at `libs/phosphor-theme/include/PhosphorTheme/IThemeService.h` and is narrower than this sketch. It is pure-virtual with no QObject base. It returns `QVariantMap` instead of `QJsonObject`. Load splits into `loadFromJson(QByteArray)` and `loadFromFile(QString)`. The wallpaper trigger lives in a separate `MatugenRunner` rather than on `IThemeService`. The shipped interface also exposes `applyTokens(QVariantMap)`, `token(QString)`, and `resetToDefaults()`.

```cpp
namespace PhosphorTheme {

class IThemeService {
public:
    virtual ~IThemeService() = default;
    virtual QJsonObject currentPalette() const = 0;
    virtual void setMode(ThemeMode mode) = 0;             // Light / Dark / Auto
    virtual void loadFromJson(const QString& path) = 0;
    virtual void loadFromWallpaper(const QString& path) = 0;   // triggers matugen
};

class MatugenRunner;        // QProcess wrapper
class TemplateEngine;       // mustache-ish renderer
class PaletteStore;         // owns the current JSON, watches the file

}
```

## PopoutService, central popout coordinator

Lifted from DMS, structured for C++ ownership and QML attachment.

```cpp
// libs/phosphor-popout/include/PhosphorPopout/IPopoutService.h
namespace PhosphorPopout {

class IPopoutService : public QObject {
    Q_OBJECT
public:
    enum class Anchor { BarLeft, BarCenter, BarRight, ScreenCenter, AtPointer, Custom };
    Q_ENUM(Anchor)

    enum class ExclusiveMode { Cooperative, Modal, Detached };
    Q_ENUM(ExclusiveMode)

    struct Request {
        QString popoutId;                  // e.g. "control-center", "launcher"
        QQmlComponent* content;
        QScreen* targetScreen;
        Anchor anchor;
        QPointF customAnchor;              // when Anchor == Custom
        ExclusiveMode exclusive;
        bool keyboardFocus;
        bool dismissOnFocusLoss;
        QVariantMap props;                 // forwarded to delegate
    };

    virtual ~IPopoutService() = default;
    virtual QString open(const Request&) = 0;            // returns handle
    virtual void close(const QString& handle) = 0;
    virtual void toggle(const QString& popoutId, const Request&) = 0;
    virtual bool isOpen(const QString& popoutId) const = 0;

Q_SIGNALS:
    void popoutOpened(const QString& handle);
    void popoutClosed(const QString& handle);
};

}
```

Properties:
- Single ownership of `xdg_popup` grabs (today we already have `PanelPopupHost` swap-in-place; promote it).
- Per-screen popout placement via `ScreenManager`.
- Modal popouts (`Lock`, `Polkit`) suppress all `Cooperative` popouts on the same screen.
- Animation hooks via `Motion` tokens, uniform enter/exit transitions.

## Widget / provider registries

One pattern, applied to every UI seam where we want plugins.

```cpp
// libs/phosphor-registry/include/PhosphorRegistry/IBarWidgetFactory.h
namespace PhosphorRegistry {

class IBarWidgetFactory {
public:
    virtual ~IBarWidgetFactory() = default;
    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual QString iconName() const = 0;
    virtual int defaultSlot() const = 0;             // left/center/right
    virtual QQmlComponent* createComponent(QQmlEngine*) = 0;
    virtual QJsonObject defaultConfig() const = 0;
};

// Sibling factories for ControlCenterTile, LauncherProvider, OSD, DesktopWidget
}
```

Registration:

```cpp
// In a built-in widget translation unit:
namespace {
struct ClockWidget : IBarWidgetFactory { ... };
PHOSPHOR_REGISTER_BAR_WIDGET(ClockWidget)
}

// External plugins use a Phosphor-native loader (QPluginLoader + our metadata schema):
{
  "phosphor-plugin": {
    "id": "com.example.weatherbar",
    "version": "1.0.0",
    "abi": "1",
    "extends": ["bar.widget"],
    "capabilities": ["network.read"],
    "qmlEntry": "WeatherBarWidget.qml"
  }
}
```

The five seams to ship at M0:
- `IBarWidgetFactory`
- `IControlCenterTileFactory`
- `ILauncherProviderFactory`
- `IOSDFactory`
- `IDesktopWidgetFactory`

Built-ins register the same way plugins do, proves the seam.

## Typed IPC + `phosphorctl`

QML side:

```qml
// In Phosphor.Launcher.Launcher.qml
IpcTarget {
    target: "launcher"

    function toggle() { /* ... */ }
    function open() { /* ... */ }
    function close() { /* ... */ }
    function search(query: string): var { /* ... */ }
}
```

C++ side (`phosphor-ipc`):

```cpp
class IpcRouter : public QObject {
    Q_OBJECT
public:
    void registerTarget(const QString& name, QObject* qmlObject);
    QVariant invoke(const QString& target, const QString& fn, const QVariantList& args, QString* error);
    QStringList listTargets() const;
    QStringList listFunctions(const QString& target) const;
    QJsonObject schemaFor(const QString& target) const;
};
```

CLI (`bin/phosphorctl/`):

```
phosphorctl call launcher.toggle
phosphorctl call control-center.open --screen DP-1
phosphorctl call theme.setMode light
phosphorctl call wallpaper.set --path ~/Pictures/forest.jpg
phosphorctl list
phosphorctl schema launcher
```

The CLI talks to a single UNIX socket (`$XDG_RUNTIME_DIR/phosphor.sock`); the router is a thin wrapper around our existing D-Bus adaptors, we don't lose the D-Bus surface, we add a typed one on top.

## Compositor adapter pattern

```cpp
// libs/phosphor-compositor/include/PhosphorCompositor/ICompositorBackend.h
namespace PhosphorCompositor {

class ICompositorBackend : public QObject {
    Q_OBJECT
public:
    virtual QString name() const = 0;
    virtual QAbstractItemModel* workspaces() const = 0;
    virtual QAbstractItemModel* monitors() const = 0;
    virtual QAbstractItemModel* toplevels() const = 0;
    virtual void switchToWorkspace(const QString& id) = 0;
    virtual void registerGlobalShortcut(...) = 0;
Q_SIGNALS:
    void workspaceChanged(const QString& id);
    void activeToplevelChanged();
};

class PhosphorBackend : public ICompositorBackend { ... };   // canonical, in-process IPC to phosphor-compositor

// Optional, plugin-distributed, for the "run Phosphor shell as guest on another compositor" mode:
class HyprlandBackend  : public ICompositorBackend { ... };  // hyprland-ipc
class NiriBackend      : public ICompositorBackend { ... };  // niri-ipc
class SwayBackend      : public ICompositorBackend { ... };  // i3-ipc
class WlrBackend       : public ICompositorBackend { ... };  // wlr-foreign-toplevel fallback

class CompositorService : public ICompositorBackend {
    // Façade; auto-detects at startup, prefers PhosphorBackend if available
};

}
```

`PhosphorBackend` is canonical and gets the deepest integration (we own the compositor, workspaces, toplevels, shortcuts route through in-process types, not external IPC). The guest-mode backends are optional plugins; they don't drive P0 architecture decisions, but the abstraction keeps the door open. Aligns with `project_plugin_based_compositor`.

## Matugen + template fan-out

```
phosphor-theme:
  PaletteStore ──┬── reads ──► ~/.local/share/phosphor/palettes/current.json
                 └── writes via TemplateEngine ──► generated files
TemplateEngine:
  templates/        (mirror of DMS's matugen/templates/, scoped to formats Phosphor itself ships against)
    qt6ct-colors.conf.tmpl
    gtk-3.0-colors.css.tmpl
    gtk-4.0-colors.css.tmpl
    kitty-colors.conf.tmpl
    foot-colors.ini.tmpl
    ghostty-colors.conf.tmpl
    alacritty-colors.toml.tmpl
    wezterm-colors.lua.tmpl
    firefox-userchrome.css.tmpl
    vesktop.css.tmpl
    neovim-colors.lua.tmpl
    vscode-dark.json.tmpl
    vscode-light.json.tmpl
    ...
  outputs/          (rendered to XDG dirs per template)
    ~/.config/qt6ct/colors/phosphor.conf
    ~/.config/gtk-3.0/colors.css
    ~/.config/gtk-4.0/colors.css
    ...

MatugenRunner: QProcess, fires on wallpaper change, takes (image, mode) → emits JSON
```

Template selection is opt-in per user; the built-in template set targets formats commonly used by tools Phosphor users are likely to run (terminals, editors, browsers, GTK/Qt theming). Additional templates land as plugins in Phase 5.

## Hot-reload model

Every consumer of theme / settings binds to a singleton; the singleton's properties update from a JSON file watched by `FileView`. No widget knows about JSON. No restart. Same pattern Noctalia and DMS use, but the JSON parsing happens C++-side, not QML.

```
FileView ──► JsonAdapter ──► Theme singleton ──► QML binding fan-out
              (C++)             (QML)
```

For the *editor* / settings app, layer `PersistentProperties` (already in `phosphor-shell`) on top so live edits don't lose in-progress state across reload.

## Bar canvas, the connected-corner shape

The differentiator. Sketch:

```qml
// Phosphor/Bar/BarCanvas.qml
Shape {
    id: canvas

    property var sockets: []   // each = { x, width, depth }, a downward pocket for an attached popout
    property real cornerRadius: Tokens.radius.xl

    ShapePath {
        strokeColor: "transparent"
        fillColor: Theme.surfaceContainer

        // Walk the bar's top edge → right side → bottom edge weaving in/out of sockets → left side
        startX: 0; startY: 0
        PathLine { x: canvas.width; y: 0 }
        PathLine { x: canvas.width; y: canvas.barHeight - cornerRadius }
        PathArc {
            x: canvas.width - cornerRadius; y: canvas.barHeight
            radiusX: cornerRadius; radiusY: cornerRadius
            direction: PathArc.Counterclockwise
        }
        // For each socket: PathLine → PathArc → PathLine down → PathLine across → PathLine up → PathArc
        // (Geometry computed in PhosphorBar/JS/ConnectorGeometry.js)
        ...
    }
}
```

`sockets` is driven by the `PopoutService`: when a popout opens anchored to the bar, it pushes a socket entry; `Behavior on sockets` runs the geometry animation. The popout itself is a sibling `PanelWindow` at the same layer level with a matching fill, the visual illusion is that the bar grew downward.

We have the shader stack to round-shadow the resulting shape uniformly via `corners.glsl`.

## Mapping today's files → the new structure

| Today                                                     | Becomes                                                                              |
|-----------------------------------------------------------|--------------------------------------------------------------------------------------|
| `examples/phosphor-shell/shell.qml`                       | `src/phosphor-shell/qml/main.qml` (slimmer; mostly composition)                      |
| `examples/phosphor-shell/TopPanel.qml`                    | `qml/Phosphor/Bar/BarHost.qml` + `BarCanvas.qml` + per-widget split                  |
| `examples/phosphor-shell/Taskbar.qml`                     | A `IBarWidgetFactory` ("tasks") or its own dock surface                              |
| `examples/phosphor-shell/PanelPopupHost.qml`              | `qml/Phosphor/Helpers/PopoutHost.qml` driven by `IPopoutService`                     |
| `examples/phosphor-shell/CalendarContent.qml`             | A `Cards/CalendarCard.qml` reusable in Control Center + Dashboard                    |
| `examples/phosphor-shell/MenuContent.qml`                 | `qml/Phosphor/Power/PowerMenu.qml` (via PopoutService)                               |
| `examples/phosphor-shell/MprisWidget.qml` + `MprisContent.qml` + `MprisPlayerState.qml` | `qml/Phosphor/Bar/Widgets/Media.qml` + `Cards/MediaCard.qml`           |
| `examples/phosphor-shell/TrayMenuPopup.qml`               | `qml/Phosphor/Bar/Widgets/Tray.qml` + popout via SNI host                            |
| `examples/phosphor-shell/SettingsWindow.qml`              | `src/phosphor-settings/` (separate process, like the editor, keep it out of shell)  |
| `examples/phosphor-shell/shaders/corners.glsl` …          | Move to `libs/phosphor-shaders/shaders/` so other surfaces share them                |

Examples directory keeps a *minimal* working composition that imports the production modules, so we can dogfood the same code reviewers will read.

## Open questions (worth surfacing for the next planning round)

1. **Settings app: stays separate or folds into shell?** DMS and Noctalia bundle. Our editor / settings separation has served us. Recommended: keep separate, give it `phosphorctl call settings.openSection <id>`.
2. **C++ plugin ABI vs. QML-only plugins.** C++ buys us capabilities + sandboxing via a Phosphor-native loader (`QPluginLoader` + our metadata schema, or a stricter custom loader). QML-only is faster to write but the capability model has to be enforced at QML import time. Likely both, with QML-only plugins running in a restricted import context.
3. **Greeter scope.** DMS ships one. We probably *shouldn't*, SDDM theming is a separate art form and the lockscreen reuse story is weaker than it sounds.
4. **Dashboard vs. expanded Control Center.** DMS ships both surfaces (DankDash + ControlCenterPopout) and they overlap. Recommend one expandable Control Center as primary, with the dash as a deferred decision.
5. **Wallpaper transition shaders, built-in catalog or plugin?** Noctalia's 6 shaders are tiny; ship as built-in, let plugins register more.
6. **End-4-style novelty widgets (AI chat, OCR, anime sidebar).** Plugin-only.

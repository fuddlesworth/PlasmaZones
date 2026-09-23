// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorRegistry/IControlCenterTileFactory.h>
#include <PhosphorRegistry/Registry.h>

#include <QObject>
// A full include, NOT a forward declaration, and load-bearing: screenOf()
// returns QScreen* to QML. moc registers the return metatype from this
// header, and Qt's pointer-to-QObject detection needs the COMPLETE type to
// set the PointerToQObject flag. With only `class QScreen;` here the flag
// was missing, QML did not wrap the return as an object, and
// QV4::ExecutionEngine::fromData took an unknown-pointer path and
// segfaulted the shell on the third IPC toggle (the first two survived
// by returning undefined, silently falling back to the primary output).
#include <QHash>
#include <QRect>
#include <QScreen>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QQuickItem;
QT_END_NAMESPACE

namespace PhosphorServiceIdle {
class IdleService;
}

namespace PhosphorShellApp {

// QML-exposed provider for ControlCenter. Owns a
// Registry<IControlCenterTileFactory>, registers the built-in tiles at
// construction, and exposes createTile(id, parent) so ControlCenter can be
// wired as `provider: ControlCenterRegistry`. This is the registry-backed
// provider the framework's README describes; the host stays
// registry-agnostic.
//
// The control-center counterpart to BarController, and bound onto every
// engine the same way (a context property re-installed by an engine hook
// on each hot reload). Like BarController it is process-global and
// outlives every engine rebuild, so createTile resolves the live engine
// from each tile's parent rather than holding one.
class ControlCenterController : public QObject
{
    Q_OBJECT
    // Ids of the registered tiles, in registration order, so QML can feed
    // ControlCenter.tileIds without hard-coding the catalog in two places.
    // CONSTANT because the catalogue is fixed at construction: this
    // controller registers its built-ins in the constructor and nothing
    // adds to the registry afterwards. BarController exposes an accessor
    // and a refresh wired to the registry's notifier because widgets can
    // arrive from plugins. The day a tile can, this has to grow a NOTIFY
    // and the same wiring, or every consumer keeps the startup list.
    Q_PROPERTY(QStringList tileIds READ tileIds CONSTANT)
    /// Name of the screen whose bar currently shows the control center, or
    /// empty when it is closed everywhere.
    ///
    /// The open state lives HERE, in a context property, rather than in
    /// shell.qml, because the thing that has to read it is a BarHost built
    /// by PerScreenPanels — and PerScreenPanels gives each delegate a fresh
    /// QQmlContext, so an id from shell.qml's scope does not resolve inside
    /// one. A context property does. (The same constraint bit the power
    /// menu's `sessionCoordinator` binding.)
    ///
    /// Keyed by screen NAME rather than a bool so a multi-head setup opens
    /// the panel on the bar the user actually clicked, instead of every bar
    /// at once.
    Q_PROPERTY(QString openScreen READ openScreen NOTIFY openScreenChanged)
    /// True while the open control center is an ENGINE-PLACED PANE (a
    /// toplevel the daemon positions, A2 §4) rather than the bar's inline
    /// pane. The bar on `openScreen` then draws only the tether to it.
    /// Written by PanePopoutTransport alongside openScreen.
    Q_PROPERTY(bool paneExternal READ isPaneExternal NOTIFY paneExternalChanged)
    /// The engine-placed pane's frame in its screen's pixels (origin at the
    /// screen's top-left), or an empty rect when unknown. A Wayland client
    /// is not told where the compositor put its toplevel, so the bar locates
    /// the pane on the placement map and reports it here (reportPaneRect);
    /// the pane's own top band reads it back to sample the rail over its
    /// x-range.
    Q_PROPERTY(QRect paneRect READ paneRect NOTIFY paneRectChanged)

public:
    // `idleService` is handed to IdleTile as an initial property. Passing
    // the shell's own service rather than letting the tile construct one
    // matters: a tile-owned IdleService would arm a second, independent
    // idle ladder, and inhibiting one would not inhibit the other.
    explicit ControlCenterController(PhosphorServiceIdle::IdleService* idleService, QObject* parent = nullptr);
    ~ControlCenterController() override;

    [[nodiscard]] QStringList tileIds() const;

    // ControlCenter provider contract: build the tile for `id`, parented
    // into `parent`. Returns null for an unknown id, or when the factory
    // reports the tile unavailable in this environment. The engine is
    // resolved from `parent` so QML need not pass it.
    [[nodiscard]] Q_INVOKABLE QQuickItem* createTile(const QString& id, QQuickItem* parent);

    [[nodiscard]] QString openScreen() const;

    /// Set which output's bar shows the control center; empty closes it.
    ///
    /// NOT Q_INVOKABLE on purpose. The only writer is SocketPopoutTransport,
    /// acting on PopoutController's behalf, so that open/close is arbitrated
    /// like every other popout: the Modal power menu closes it, a
    /// Cooperative open is refused while a modal is up, closeAll() on
    /// reload drains it. QML opens it through `Popouts.toggle(...)` with
    /// popoutId "control-center"; a direct setter here would let shell.qml
    /// bypass all of that again.
    void setOpenScreen(const QString& screenName);

    /// The output `item` is displayed on, or the primary screen when it
    /// cannot be resolved (no window yet, or a null item). Never null while
    /// a screen exists at all.
    ///
    /// Used to turn the bar widget that fired `BarRegistry.widgetActivated`
    /// into the PopoutRequest.targetScreen whose capsule should grow. Done
    /// in C++ rather than by chaining `item.Window.window.screen` in QML
    /// because that chain is invisible to qmllint (QQuickWindow's `screen`
    /// is not in its declarative type info), so a typo there would only
    /// surface at runtime, as an undefined that quietly opens nothing.
    [[nodiscard]] Q_INVOKABLE QScreen* screenOf(QQuickItem* item) const;

    [[nodiscard]] bool isPaneExternal() const;
    /// Not Q_INVOKABLE, for the same reason as setOpenScreen: only the pane
    /// transport says whether the open pane is a toplevel.
    void setPaneExternal(bool external);

    [[nodiscard]] QRect paneRect() const;
    /// The bar reports where the placement map says the pane is. An empty
    /// rect clears it (the pane closed, or it could not be located).
    Q_INVOKABLE void reportPaneRect(const QRect& rect);

    /// Each bar reports where its control-center chip sits, in its screen's
    /// pixels, so the pane transport can ask the engine for the zone nearest
    /// the chip (A2 §4.2). An empty rect clears it.
    Q_INVOKABLE void reportChipRect(const QString& screenName, const QRect& rect);
    [[nodiscard]] QRect chipRectFor(const QString& screenName) const;

    /// Each bar reports its screen's placement mode as the map changes
    /// (0 snapping, 1 tiling, 2 scrolling, -1 none). The pane transport
    /// asks before opening: no engine on the output means the floating
    /// fallback, since a toplevel nobody places is not a pane.
    Q_INVOKABLE void reportScreenMode(const QString& screenName, int mode);
    [[nodiscard]] int modeForScreen(const QString& screenName) const;

Q_SIGNALS:
    void openScreenChanged();
    void paneExternalChanged();
    void paneRectChanged();

private:
    PhosphorRegistry::Registry<PhosphorRegistry::IControlCenterTileFactory> m_registry;
    QStringList m_tileIds;
    QString m_openScreen;
    QRect m_paneRect;
    QHash<QString, QRect> m_chipRects;
    QHash<QString, int> m_screenModes;
    bool m_paneExternal = false;
};

} // namespace PhosphorShellApp

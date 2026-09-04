// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorRegistry/IControlCenterTileFactory.h>
#include <PhosphorRegistry/Registry.h>

#include <QObject>
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

    /// Open the control center on `screenName`, close it if that screen
    /// already has it, and move it if another screen does. Moving rather
    /// than opening a second copy keeps the "one control center" rule the
    /// popout controller enforces for surface-backed popouts, which a
    /// bar-painted socket does not go through.
    ///
    /// An empty `screenName` closes it.
    Q_INVOKABLE void toggleOnScreen(const QString& screenName);
    Q_INVOKABLE void close();

    /// Name of the output `item` is displayed on, or the primary screen's
    /// name when it cannot be resolved (no window yet, or a null item).
    ///
    /// Used to turn the bar widget that fired `BarRegistry.widgetActivated`
    /// into the output whose capsule should grow. Done in C++ rather than
    /// by chaining `item.Window.window.screen.name` in QML because that
    /// chain is invisible to qmllint (QQuickWindow's `screen` is not in its
    /// declarative type info), so a typo there would only surface at
    /// runtime, as an undefined that quietly opens nothing.
    [[nodiscard]] Q_INVOKABLE QString screenNameOf(QQuickItem* item) const;

Q_SIGNALS:
    void openScreenChanged();

private:
    PhosphorRegistry::Registry<PhosphorRegistry::IControlCenterTileFactory> m_registry;
    QStringList m_tileIds;
    QString m_openScreen;
};

} // namespace PhosphorShellApp

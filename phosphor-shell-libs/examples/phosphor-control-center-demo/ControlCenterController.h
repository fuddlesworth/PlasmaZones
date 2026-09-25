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

namespace PhosphorControlCenterDemo {

// QML-exposed provider for ControlCenter. Owns a
// Registry<IControlCenterTileFactory>, registers the built-in tiles at
// construction, and exposes createTile(id, parent) so ControlCenter can be
// wired as `provider: controlCenterController`. This is the registry-backed
// provider the framework's README describes; the host stays
// registry-agnostic. Mirrors the OSD demo's OSDController.
class ControlCenterController : public QObject
{
    Q_OBJECT
    // Ids of the registered tiles, in registration order, so QML can feed
    // ControlCenter.tileIds without hard-coding the catalog in two places.
    Q_PROPERTY(QStringList tileIds READ tileIds CONSTANT)

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

private:
    PhosphorRegistry::Registry<PhosphorRegistry::IControlCenterTileFactory> m_registry;
    QStringList m_tileIds;
};

} // namespace PhosphorControlCenterDemo

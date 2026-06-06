// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/IFactoryBase.h>
#include <PhosphorRegistry/phosphorregistry_export.h>

#include <QtCore/qtclasshelpermacros.h>

QT_BEGIN_NAMESPACE
class QObject;
class QQmlEngine;
class QQuickItem;
QT_END_NAMESPACE

namespace PhosphorRegistry {

// Factory for one tile inside the Control Center popout (network,
// bluetooth, audio sliders, brightness, night-mode, etc.). Mirrors
// IBarWidgetFactory's shape — the host (Control Center QML) iterates
// the registry and instantiates each tile via createTile().
//
// Phase 1.3 ships this interface as a documented header; the
// consuming Control Center surface lands in Phase 4.4. Tile factories
// can be authored against this contract today but they will not be
// driven by a real surface until Phase 4.
class PHOSPHORREGISTRY_EXPORT IControlCenterTileFactory : public IFactoryBase
{
public:
    IControlCenterTileFactory() = default;
    ~IControlCenterTileFactory() override = default;
    Q_DISABLE_COPY_MOVE(IControlCenterTileFactory)

    // Construct a tile QQuickItem rooted at parent. engine MUST NOT
    // be null (same contract as IBarWidgetFactory::createWidget).
    // Same lifetime contract: parent owns; factory does not retain.
    // Returning nullptr is allowed and means "this tile is
    // unavailable in the current environment" (no underlying
    // service, missing hardware, etc.).
    [[nodiscard]] virtual QQuickItem* createTile(QQmlEngine* engine, QObject* parent) = 0;
};

} // namespace PhosphorRegistry

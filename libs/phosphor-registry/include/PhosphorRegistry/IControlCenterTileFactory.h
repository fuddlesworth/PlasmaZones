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
// Phase 1.3 shipped this interface as a documented header, and the
// consuming Control Center surface has since landed, so tile factories
// authored against this contract are driven by a real host: libs/phosphor-shell-
// control-center's ControlCenter.qml, over the registry that
// src/shell/ControlCenterController populates.
class PHOSPHORREGISTRY_EXPORT IControlCenterTileFactory : public IFactoryBase
{
public:
    IControlCenterTileFactory() = default;
    ~IControlCenterTileFactory() override = default;
    Q_DISABLE_COPY_MOVE(IControlCenterTileFactory)

    // Construct a tile QQuickItem rooted at parent. engine MUST NOT
    // be null (same contract as IBarWidgetFactory::createWidget).
    // The factory does not retain the item.
    // Returning nullptr is allowed and means "this tile is
    // unavailable in the current environment" (no underlying
    // service, missing hardware, etc.).
    //
    // OWNERSHIP, and note this is the OPPOSITE of IBarWidgetFactory. The
    // control-center surface destroys the tiles it materialised when it
    // rebuilds, from QML. A QObject-parented item defaults to
    // CppOwnership, and calling destroy() on one of those throws
    // "indestructible object" and leaks the old tile behind the new one.
    // An implementation MUST therefore hand the returned item to the JS
    // engine before returning it:
    //
    //     QQmlEngine::setObjectOwnership(item, QQmlEngine::JavaScriptOwnership);
    //
    // The bar host never destroys its widgets, so IBarWidgetFactory
    // deliberately keeps CppOwnership. The two contracts differ on
    // purpose; do not unify them.
    [[nodiscard]] virtual QQuickItem* createTile(QQmlEngine* engine, QObject* parent) = 0;
};

} // namespace PhosphorRegistry

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/phosphorregistry_export.h>

#include <QString>
#include <QStringList>
#include <QtCore/qtclasshelpermacros.h> // Q_DISABLE_COPY_MOVE

namespace PhosphorRegistry {

// Common base for every factory interface registered with a Registry<T>.
//
// id() is the stable lookup key callers pass to Registry::factory().
// displayName() is the human-readable label a plugin browser or
// settings UI shows. capabilities() is the metadata slot for the
// Phase-5 sandbox; today it is advisory only (no enforcement), but
// every factory ships with the list so the enforcement layer can be
// added without an ABI break.
//
// All five UI-seam interfaces (IBarWidgetFactory,
// IControlCenterTileFactory, ILauncherProviderFactory, IOSDFactory,
// IDesktopWidgetFactory) inherit this base and add a single
// surface-specific create*() method.
class PHOSPHORREGISTRY_EXPORT IFactoryBase
{
public:
    IFactoryBase() = default;
    virtual ~IFactoryBase() = default;
    Q_DISABLE_COPY_MOVE(IFactoryBase)

    // Stable identifier for this factory. Used by the registry as the
    // lookup key. Examples are "clock", "control-center.network",
    // "launcher.calculator". Convention: lowercase ASCII + dot
    // namespace separators. Two factories registering the same id is
    // rejected by Registry::registerFactory.
    [[nodiscard]] virtual QString id() const = 0;

    // Human-readable label for plugin browsers / settings UIs. May be
    // translated by the factory implementation.
    [[nodiscard]] virtual QString displayName() const = 0;

    // Declared capabilities, drawn from the manifest for plugin
    // factories or hardcoded for built-ins. Standard names follow the
    // dot-namespaced convention documented in the design docs
    // (`bar.widget`, `control-center.tile`, `network.read`,
    // `notify.send`, etc.). Enforcement is deferred to Phase 5; today
    // the list is informational.
    //
    // Defaulted to an empty list rather than pure-virtual: the five
    // UI-seam plugin factories override it with their manifest
    // capabilities, but the domain registries unified onto Registry<T>
    // (shader packs, animation effects, easing curves, tiling
    // algorithms, layout sources) carry no capability metadata and
    // should not have to fabricate an override returning `{}`. The
    // advisory contract is unchanged — an entry that declares nothing
    // simply declares nothing.
    [[nodiscard]] virtual QStringList capabilities() const
    {
        return {};
    }
};

} // namespace PhosphorRegistry

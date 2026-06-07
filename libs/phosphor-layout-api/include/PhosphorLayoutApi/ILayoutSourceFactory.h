// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorlayoutapi_export.h>

#include <PhosphorRegistry/IFactoryBase.h>

#include <QString>

#include <memory>

namespace PhosphorLayout {

class ILayoutSource;

/// Abstract factory for an @c ILayoutSource implementation.
///
/// Each provider library (phosphor-zones, phosphor-tiles, future
/// phosphor-scrolling, third-party plugins) ships a concrete factory
/// alongside its @c ILayoutSource implementation. The factory carries
/// whatever provider-specific construction arguments the source
/// requires (e.g. a borrowed registry pointer). Calling @c create
/// hands the caller a fresh source instance bound to those arguments.
///
/// @c LayoutSourceBundle keeps its factories in a
/// @c PhosphorRegistry::Registry<ILayoutSourceFactory> (the shared id-keyed
/// store) and assembles a composite of every produced source. Composition
/// roots register factories — adding a new layout-source family (the upcoming
/// scrolling engine) is then one @c addFactory() line at each root, with no
/// edits to @c phosphor-layout-api or to the bundle.
///
/// Derives @c IFactoryBase so the factory is a first-class registry entry:
/// @c id() / @c displayName() both delegate to @c name() (a layout-source
/// provider has one identity), and @c capabilities() uses the default empty
/// list. Concrete providers therefore still implement only @c name() and
/// @c create() — nothing changes for them.
///
/// Factory contract: the same @c create() call must be safe to invoke
/// repeatedly and from any thread the caller chooses; concrete
/// factories are responsible for any thread-safety in their stored
/// arguments. Returned sources are heap-owned by the caller (typically
/// via @c std::unique_ptr).
class PHOSPHORLAYOUTAPI_EXPORT ILayoutSourceFactory : public PhosphorRegistry::IFactoryBase
{
public:
    ~ILayoutSourceFactory() override;

    /// Stable identifier for diagnostics / logging. Should match the
    /// provider library name (e.g. @c "zones", @c "autotile",
    /// @c "scrolling"). This is also the registry key — id() returns it.
    virtual QString name() const = 0;

    /// IFactoryBase: both the registry key and the UI label are the
    /// provider name (a layout source has a single identity).
    [[nodiscard]] QString id() const override
    {
        return name();
    }
    [[nodiscard]] QString displayName() const override
    {
        return name();
    }

    /// Build a fresh source instance. Caller takes ownership.
    virtual std::unique_ptr<ILayoutSource> create() = 0;
};

} // namespace PhosphorLayout

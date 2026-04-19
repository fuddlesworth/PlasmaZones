// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorlayoutapi_export.h>

#include <PhosphorLayoutApi/CompositeLayoutSource.h>
#include <PhosphorLayoutApi/ILayoutSource.h>
#include <PhosphorLayoutApi/ILayoutSourceFactory.h>
#include <PhosphorLayoutApi/LayoutSourceProviderRegistry.h>

#include <memory>
#include <vector>

namespace PhosphorLayout {

/// Owning bundle that assembles a list of @c ILayoutSourceFactory
/// instances into a single @c CompositeLayoutSource.
///
/// Composition roots (daemon, editor, settings, future plugin hosts)
/// register one factory per layout-source family they want surfaced,
/// then call @c build() to materialise sources and wire the composite.
/// Adding a new family — e.g. the planned scrolling engine — is one
/// @c addFactory() line per composition root; no edits to this bundle
/// or to @c phosphor-layout-api.
///
/// Lifetime contract for consumers: the bundle owns every source and
/// the composite. Callers may hold a borrowed pointer to
/// @c composite() (or to any source via @c source()) for as long as
/// the bundle is alive. Resetting or destroying the bundle while
/// observers still hold raw pointers is a use-after-free — disconnect
/// signals, drop QML bindings, and cancel pending D-Bus calls before
/// teardown.
class PHOSPHORLAYOUTAPI_EXPORT LayoutSourceBundle
{
public:
    LayoutSourceBundle();
    ~LayoutSourceBundle();

    LayoutSourceBundle(const LayoutSourceBundle&) = delete;
    LayoutSourceBundle& operator=(const LayoutSourceBundle&) = delete;
    LayoutSourceBundle(LayoutSourceBundle&&) noexcept;
    LayoutSourceBundle& operator=(LayoutSourceBundle&&) noexcept;

    /// Register a factory. Order is significant — the composite
    /// iterates sources in registration order, which determines
    /// id-namespace precedence for collisions (in practice each
    /// provider library uses a distinct id prefix, so collisions are
    /// only theoretical).
    ///
    /// May only be called before @c build(); registering after build
    /// is a programmer error and triggers a Q_ASSERT in debug builds.
    void addFactory(std::unique_ptr<ILayoutSourceFactory> factory);

    /// Materialise sources from every registered factory and wire the
    /// composite. Idempotent — calling @c build() after a successful
    /// build is a no-op. Each factory's @c create() is invoked exactly
    /// once.
    void build();

    /// Auto-discovery convenience: drain every provider registered
    /// via @c LayoutSourceProviderRegistrar (in priority order, ties
    /// broken by registration order), invoke each builder with @p ctx,
    /// add any non-null factories returned, and call @c build().
    ///
    /// Production composition roots (daemon, editor, settings) call
    /// this once after populating @p ctx with the registries they own.
    /// Builders that return nullptr (because @p ctx doesn't surface
    /// the engine's required service) are silently skipped — that's
    /// the "this composition root doesn't host this engine" signal.
    ///
    /// Coexists with @c addFactory: callers may pre-register custom
    /// factories (typically tests with a fixture-specific source)
    /// before calling @c buildFromRegistered, and both sets will be
    /// stitched into the same composite.
    void buildFromRegistered(const FactoryContext& ctx);

    /// The unified composite over every registered source. Null until
    /// @c build() has been called.
    CompositeLayoutSource* composite() const
    {
        return m_composite.get();
    }

    /// Borrowed access to the source produced by the factory whose
    /// @c name() matches @p name. Returns null when no such factory
    /// has been registered or when @c build() has not run yet. Mainly
    /// used by composition roots that need to wire engine-specific
    /// signals through to the source.
    ILayoutSource* source(const QString& name) const;

private:
    std::vector<std::unique_ptr<ILayoutSourceFactory>> m_factories;
    std::vector<std::unique_ptr<ILayoutSource>> m_sources;
    /// Source-name → m_sources index. Populated during @c build();
    /// the underlying source pointer is borrowed from m_sources.
    std::vector<QString> m_sourceNames;
    /// composite holds raw pointers into m_sources, so it must be
    /// destroyed first. Declaration order is load-bearing — never
    /// reorder. The destructor + move-assign clear composite's
    /// borrowed pointers before m_sources tears down to make the
    /// invariant survive future field reorders too.
    std::unique_ptr<CompositeLayoutSource> m_composite;
};

} // namespace PhosphorLayout

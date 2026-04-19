// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorlayoutapi_export.h>

#include <PhosphorLayoutApi/ILayoutSourceFactory.h>

#include <QDebug>
#include <QList>
#include <QString>

#include <functional>
#include <memory>
#include <type_traits>
#include <typeindex>
#include <unordered_map>

namespace PhosphorLayout {

/// Service registry passed to a layout-source-provider builder so it
/// can pull whatever registries / dependencies its factory needs.
///
/// Composition roots populate one of these with every state object
/// they own (typically the per-library `I*Registry` instances) keyed
/// by the abstract interface type. Provider builders pull what they
/// need by interface and ignore everything else; the type-erased
/// @c get<T>() look-up is keyed on @c std::type_index so interfaces
/// that the builder doesn't recognise are simply absent.
///
/// The context stores **borrowed pointers** — the composition root
/// owns the lifetime. Pointers must outlive every produced
/// @c ILayoutSourceFactory (which in practice means outlive the
/// owning @c LayoutSourceBundle).
///
/// Type-safety of the void* round-trip: @c set<T> stores
/// @c static_cast<void*>(T*) under key @c type_index(typeid(T)). The
/// matching @c get<T> returns @c static_cast<T*>(void*) under the
/// same key. Because @c std::type_index is unique per T and set/get
/// always use the same T, the pointer round-trips without multi-
/// inheritance offset drift — every get<T> returns the same adjusted
/// T* that set<T> stored. Callers who set under one interface and
/// get under a different one (e.g. the base class) simply miss (the
/// keys differ → nullptr), avoiding UB.
///
/// Constraints on @c T (must hold for every interface used as a
/// service key):
///   - **Non-virtual inheritance only.** The
///     @c static_cast<void*>(T*) → @c static_cast<T*>(void*) round-trip
///     assumes a fixed, vtable-independent offset between the derived
///     object and its @c T subobject. Virtual inheritance makes that
///     offset dynamic, and the void* detour silently discards it.
///     Every interface in the PhosphorLayout provider chain
///     (@c ILayoutSourceRegistry, @c IZoneLayoutRegistry,
///     @c ITileAlgorithmRegistry, future @c IScrollingRegistry) is
///     non-virtually inherited; preserve that when adding new services.
///   - **Top-level cv is stripped.** @c std::type_index(typeid(T))
///     collapses @c const-qualification (and volatile), so
///     @c set<const IFoo>(...) and @c get<IFoo>() resolve to the same
///     slot. This is intentional — const doesn't change service
///     identity — but it means you cannot register two distinct
///     entries keyed by constness alone.
class PHOSPHORLAYOUTAPI_EXPORT FactoryContext
{
public:
    /// Register a service of type @c T.
    ///
    /// @c T is intentionally non-deducible (wrapped in @c std::type_identity_t)
    /// so callers must spell the interface explicitly:
    /// @code
    /// ctx.set<IZoneLayoutRegistry>(manager.get()); // keyed by the interface
    /// @endcode
    /// Without the non-deducing context, @c ctx.set(manager.get()) would deduce
    /// @c T from the concrete class and the registrar's
    /// @c ctx.get<IZoneLayoutRegistry>() would silently miss.
    ///
    /// Duplicate registrations are a programmer error: asserts in debug,
    /// warns + ignores + returns @c false in release (first registration
    /// wins). Silent last-write-wins would let a typo in one composition
    /// root hand provider builders a different registry than the rest of
    /// the root is wired against, with no diagnostic. First-wins is the
    /// fail-safe release fallback — the original (presumably correct)
    /// registration stays authoritative; matches the single-shot
    /// discipline on @c LayoutSourceBundle::buildFromRegistered.
    ///
    /// @return @c true on successful registration, @c false when a prior
    ///         registration for @c T was already present. Callers that
    ///         want hard-failure behaviour in release can wrap the call
    ///         in their own assert; the default (discard-duplicate) is
    ///         safe for composition roots that don't care.
    template<typename T>
    bool set(std::type_identity_t<T*> service)
    {
        const auto key = std::type_index(typeid(T));
        const bool duplicate = m_services.find(key) != m_services.end();
        Q_ASSERT_X(!duplicate, "FactoryContext::set",
                   "service key already registered; duplicate set<T>() is a programmer error");
        if (duplicate) {
            qWarning("FactoryContext::set: ignoring duplicate registration for type '%s' (first registration kept)",
                     key.name());
            return false;
        }
        m_services[key] = static_cast<void*>(service);
        return true;
    }

    /// Look up a previously-registered service. Returns nullptr when
    /// no matching service has been set — provider builders use this
    /// as the "composition root doesn't surface this engine, skip"
    /// signal by returning nullptr from their builder.
    template<typename T>
    T* get() const
    {
        const auto it = m_services.find(std::type_index(typeid(T)));
        return it == m_services.end() ? nullptr : static_cast<T*>(it->second);
    }

private:
    std::unordered_map<std::type_index, void*> m_services;
};

/// One pending layout-source-provider registration.
///
/// The @c builder lambda runs at @c LayoutSourceBundle::buildFromRegistered
/// time, with the composition root's @c FactoryContext. Returning
/// nullptr means "this engine isn't applicable to this composition
/// root" (e.g. composition root didn't supply a registry the engine
/// needs) — the bundle silently skips that provider.
struct PHOSPHORLAYOUTAPI_EXPORT PendingLayoutSourceProvider
{
    QString name;
    int priority = 100;
    std::function<std::unique_ptr<ILayoutSourceFactory>(const FactoryContext&)> builder;
};

/// Process-global list of pending provider registrations.
///
/// Populated at static-init time by
/// @c LayoutSourceProviderRegistrar instances in each provider
/// library. Snapshotted (copied locally, never mutated) by
/// @c LayoutSourceBundle::buildFromRegistered, so multiple bundles
/// (daemon, editor, settings) each iterate the same list and create
/// their own factory instances. A free function (rather than nested
/// in a templated class) so every translation unit appends to the
/// same QList regardless of template instantiation.
///
/// @note Plugin-loading constraint: this list is intended for
/// static-init-time population. Bundles snapshot it at
/// @c buildFromRegistered time, so providers introduced via a later
/// @c dlopen are not picked up by bundles already built. Symmetrically,
/// @c dlclose on a provider library leaves a dangling @c std::function
/// closure in the list — safe as long as no bundle calls
/// @c buildFromRegistered afterwards.
///
/// @todo(plugin-compositor) When runtime plugin loading lands, this
/// contract must be revisited. Likely shape: mutex-protected list with
/// explicit per-plugin handles, removal on @c dlclose, and a bundle
/// rebuild API for composition roots that want to pick up a newly-
/// loaded provider. The plugin-discovery pattern described above is the
/// static-init-only variant; do not assume it will survive the switch
/// to a dynamic plugin loader without a redesign.
PHOSPHORLAYOUTAPI_EXPORT QList<PendingLayoutSourceProvider>& pendingLayoutSourceProviders();

/// Static-init self-registration helper for provider libraries.
///
/// Each provider library declares an instance in an anonymous
/// namespace inside its factory .cpp file. The constructor appends
/// to @c pendingLayoutSourceProviders() at process startup, before
/// any composition root constructs its @c LayoutSourceBundle.
///
/// Usage (in e.g. zoneslayoutsourcefactory.cpp):
/// @code
/// namespace {
/// PhosphorLayout::LayoutSourceProviderRegistrar registrar(
///     QStringLiteral("zones"), /*priority=*/0,
///     [](const PhosphorLayout::FactoryContext& ctx) -> std::unique_ptr<PhosphorLayout::ILayoutSourceFactory> {
///         auto* registry = ctx.get<PhosphorZones::IZoneLayoutRegistry>();
///         if (!registry) {
///             return nullptr; // composition root didn't surface this engine
///         }
///         return std::make_unique<PhosphorZones::ZonesLayoutSourceFactory>(registry);
///     });
/// }
/// @endcode
///
/// Adding a new layout-source family (the planned scrolling engine)
/// is one new library + one of these registrars inside it. No edits
/// to phosphor-layout-api, no edits to other provider libraries, and
/// — as long as the engine pulls only services the composition root
/// already surfaces — no edits to daemon / editor / settings either.
class PHOSPHORLAYOUTAPI_EXPORT LayoutSourceProviderRegistrar
{
public:
    LayoutSourceProviderRegistrar(QString name, int priority,
                                  std::function<std::unique_ptr<ILayoutSourceFactory>(const FactoryContext&)> builder);
};

/// Standard provider builder: pull @p Registry from @p ctx, return a
/// new @p Factory bound to it, or nullptr if the composition root
/// didn't surface that registry.
///
/// Lets the per-provider factory .cpp boil down to a one-liner:
/// @code
/// PhosphorLayout::LayoutSourceProviderRegistrar registrar(
///     QStringLiteral("autotile"), /*priority=*/100,
///     &PhosphorLayout::makeProviderFactory<ITileAlgorithmRegistry, AutotileLayoutSourceFactory>);
/// @endcode
/// Forces every provider into the same null-bail-out discipline so a
/// future engine can't accidentally crash the bundle by skipping the
/// guard.
template<typename Registry, typename Factory>
inline std::unique_ptr<ILayoutSourceFactory> makeProviderFactory(const FactoryContext& ctx)
{
    auto* registry = ctx.get<Registry>();
    if (!registry) {
        return nullptr;
    }
    return std::make_unique<Factory>(registry);
}

} // namespace PhosphorLayout

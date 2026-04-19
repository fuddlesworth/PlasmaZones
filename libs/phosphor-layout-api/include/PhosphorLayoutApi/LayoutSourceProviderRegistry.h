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
/// by the abstract interface type. Provider builders type-erase pull
/// what they need by interface and ignore everything else.
///
/// The context stores **borrowed pointers** — the composition root
/// owns the lifetime. Pointers must outlive every produced
/// @c ILayoutSourceFactory (which in practice means outlive the
/// owning @c LayoutSourceBundle).
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
    /// warns + overwrites in release. Silent last-write-wins would let
    /// a typo in one composition root hand provider builders a different
    /// registry than the rest of the root is wired against, with no
    /// diagnostic — matches the single-shot discipline on
    /// @c LayoutSourceBundle::buildFromRegistered.
    template<typename T>
    void set(std::type_identity_t<T*> service)
    {
        const auto key = std::type_index(typeid(T));
        const bool duplicate = m_services.find(key) != m_services.end();
        Q_ASSERT_X(!duplicate, "FactoryContext::set",
                   "service key already registered; duplicate set<T>() is a programmer error");
        if (duplicate) {
            qWarning("FactoryContext::set: overwriting existing service for type '%s'", key.name());
        }
        m_services[key] = static_cast<void*>(service);
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
/// library. Drained-but-not-cleared by
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

} // namespace PhosphorLayout

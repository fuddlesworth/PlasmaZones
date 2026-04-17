// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/IQmlEngineProvider.h>
#include <PhosphorLayer/IScreenProvider.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/SurfaceFactory.h>

#include "internal.h"

#include <QScreen>

namespace PhosphorLayer {

class SurfaceFactory::Impl
{
public:
    explicit Impl(Deps deps)
        : m_deps(std::move(deps))
    {
    }

    Deps m_deps;
};

SurfaceFactory::SurfaceFactory(Deps deps, QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(std::move(deps)))
{
    if (!m_impl->m_deps.transport) {
        qCWarning(lcPhosphorLayer) << "SurfaceFactory: no ILayerShellTransport injected — create() will always fail";
    }
    if (!m_impl->m_deps.screens) {
        qCWarning(lcPhosphorLayer) << "SurfaceFactory: no IScreenProvider injected — create() will always fail";
    }
}

SurfaceFactory::~SurfaceFactory() = default;

const SurfaceFactory::Deps& SurfaceFactory::deps() const noexcept
{
    return m_impl->m_deps;
}

Surface* SurfaceFactory::create(SurfaceConfig cfg, QObject* parent)
{
    const auto debug = cfg.effectiveDebugName();

    if (!m_impl->m_deps.transport || !m_impl->m_deps.transport->isSupported()) {
        qCWarning(lcPhosphorLayer) << "SurfaceFactory::create: transport not available — refusing to create"
                                   << "debugName=" << debug;
        return nullptr;
    }
    if (!m_impl->m_deps.screens) {
        qCWarning(lcPhosphorLayer) << "SurfaceFactory::create: no screen provider — refusing to create"
                                   << "debugName=" << debug;
        return nullptr;
    }

    const bool hasUrl = !cfg.contentUrl.isEmpty();
    const bool hasItem = cfg.contentItem != nullptr;
    if (hasUrl == hasItem) {
        qCWarning(lcPhosphorLayer) << "SurfaceFactory::create: exactly one of contentUrl / contentItem must be set"
                                   << "hasUrl=" << hasUrl << "hasItem=" << hasItem << "debugName=" << debug;
        return nullptr;
    }

    // Engine ownership is tri-state (None / Self / Provider). sharedEngine
    // and engineProvider are documented as mutually exclusive (the former is
    // "caller retains ownership"; the latter is "provider owns"). Mixing both
    // produced the surface-uses-sharedEngine-but-~Impl-calls-releaseEngine
    // footgun. Reject at the factory boundary so no Surface ever reaches
    // that inconsistent state.
    if (cfg.sharedEngine && m_impl->m_deps.engineProvider) {
        qCWarning(lcPhosphorLayer)
            << "SurfaceFactory::create: SurfaceConfig::sharedEngine and SurfaceFactory::Deps::engineProvider"
            << "are mutually exclusive. Pick one: pass sharedEngine for caller-owned sharing, or an engineProvider"
            << "for provider-managed lifecycle — not both. debugName=" << debug;
        return nullptr;
    }

    if (!cfg.role.isValid()) {
        qCWarning(lcPhosphorLayer) << "SurfaceFactory::create: Role is invalid (empty scopePrefix or"
                                   << "semantically-conflicting layer/exclusiveZone) — refusing to create"
                                   << "debugName=" << debug;
        return nullptr;
    }

    // If the caller did not pin a specific screen, fall back to the screen
    // provider's primary. Giving nullptr to the transport lets the compositor
    // pick (fine in principle) but makes the Surface's target non-deterministic
    // and masks "no screens connected" at the wrong layer. Resolving here keeps
    // the failure mode documented at the factory boundary.
    if (!cfg.screen) {
        cfg.screen = m_impl->m_deps.screens->primary();
        if (!cfg.screen) {
            qCWarning(lcPhosphorLayer) << "SurfaceFactory::create: cfg.screen is null and provider has no primary"
                                       << "debugName=" << debug;
            return nullptr;
        }
    } else if (!m_impl->m_deps.screens->screens().contains(cfg.screen)) {
        // Stale QScreen* (disconnected monitor, or mock from a different
        // provider). Fail loudly rather than let the transport attach to
        // an orphan.
        qCWarning(lcPhosphorLayer) << "SurfaceFactory::create: cfg.screen is not in the provider's screen list"
                                   << "— disconnected or foreign screen. debugName=" << debug;
        return nullptr;
    }

    SurfaceDeps sdeps;
    sdeps.transport = m_impl->m_deps.transport;
    sdeps.engineProvider = m_impl->m_deps.engineProvider;
    sdeps.screenProvider = m_impl->m_deps.screens;
    sdeps.loggingCategory =
        m_impl->m_deps.loggingCategory.isEmpty() ? QStringLiteral("phosphorlayer") : m_impl->m_deps.loggingCategory;
    return new Surface(std::move(cfg), std::move(sdeps), parent ? parent : this);
}

} // namespace PhosphorLayer

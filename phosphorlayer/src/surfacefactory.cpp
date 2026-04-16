// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/IQmlEngineProvider.h>
#include <PhosphorLayer/IScreenProvider.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/SurfaceFactory.h>

#include "internal.h"

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

    SurfaceDeps sdeps{
        m_impl->m_deps.transport,
        m_impl->m_deps.engineProvider,
        m_impl->m_deps.loggingCategory.isEmpty() ? QStringLiteral("phosphorlayer") : m_impl->m_deps.loggingCategory,
    };
    return new Surface(std::move(cfg), std::move(sdeps), parent ? parent : this);
}

} // namespace PhosphorLayer

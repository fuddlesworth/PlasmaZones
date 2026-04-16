// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/IScreenProvider.h>
#include <PhosphorLayer/Surface.h>

#include "internal.h"

#include <QQuickItem>

// Out-of-line definitions grouped here so QQuickItem is complete for
// ~unique_ptr<QQuickItem> and ScreenProviderNotifier picks up its vtable
// anchor. Will split into per-class TUs if the file grows.
namespace PhosphorLayer {

// ── SurfaceConfig special members (header forward-declares QQuickItem) ─

SurfaceConfig::SurfaceConfig() = default;
SurfaceConfig::~SurfaceConfig() = default;
SurfaceConfig::SurfaceConfig(SurfaceConfig&&) noexcept = default;
SurfaceConfig& SurfaceConfig::operator=(SurfaceConfig&&) noexcept = default;

// ── ScreenProviderNotifier ────────────────────────────────────────────

ScreenProviderNotifier::ScreenProviderNotifier(QObject* parent)
    : QObject(parent)
{
}
ScreenProviderNotifier::~ScreenProviderNotifier() = default;

} // namespace PhosphorLayer

namespace PhosphorLayer {

// ── Pimpl ──────────────────────────────────────────────────────────────

class Surface::Impl
{
public:
    Impl(SurfaceConfig cfg, SurfaceDeps deps)
        : m_config(std::move(cfg))
        , m_deps(std::move(deps))
    {
    }

    const SurfaceConfig m_config;
    const SurfaceDeps m_deps;

    State m_state = State::Constructed;
    // Window / engine / transport handle members will be populated in
    // later phases once the state machine is wired up. Skeleton is
    // intentionally non-functional so the state-machine commit is isolated.
};

// ── Surface ────────────────────────────────────────────────────────────

Surface::Surface(SurfaceConfig cfg, SurfaceDeps deps, QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(std::move(cfg), std::move(deps)))
{
}

Surface::~Surface() = default;

Surface::State Surface::state() const noexcept
{
    return m_impl->m_state;
}

const SurfaceConfig& Surface::config() const noexcept
{
    return m_impl->m_config;
}

QQuickWindow* Surface::window() const noexcept
{
    return nullptr; // TODO(phosphorlayer): implemented with state machine
}

ITransportHandle* Surface::transport() const noexcept
{
    return nullptr; // TODO(phosphorlayer): implemented with state machine
}

void Surface::show()
{
    qCWarning(lcPhosphorLayer) << "Surface::show() stub — state machine not yet implemented"
                               << "debugName=" << m_impl->m_config.effectiveDebugName();
}

void Surface::hide()
{
    qCWarning(lcPhosphorLayer) << "Surface::hide() stub — state machine not yet implemented"
                               << "debugName=" << m_impl->m_config.effectiveDebugName();
}

void Surface::warmUp()
{
    qCWarning(lcPhosphorLayer) << "Surface::warmUp() stub — state machine not yet implemented"
                               << "debugName=" << m_impl->m_config.effectiveDebugName();
}

} // namespace PhosphorLayer

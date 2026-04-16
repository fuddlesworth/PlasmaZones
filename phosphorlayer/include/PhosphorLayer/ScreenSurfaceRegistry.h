// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/SurfaceConfig.h>
#include <PhosphorLayer/SurfaceFactory.h>
#include <PhosphorLayer/phosphorlayer_export.h>

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QScreen;
QT_END_NAMESPACE

namespace PhosphorLayer {

class IScreenProvider;
class Surface;

/**
 * @brief Per-screen bookkeeping for surfaces with AllScreens affinity.
 *
 * Template parameter lets consumers subclass Surface and keep their type
 * through the API:
 * @code
 *     class PzOverlaySurface : public Surface { ... };
 *     ScreenSurfaceRegistry<PzOverlaySurface> reg(factory, screens);
 *     auto* s = reg.surfaceForScreen(myScreen);  // typed as PzOverlaySurface*
 * @endcode
 *
 * The registry does NOT listen to screensChanged itself — that is the
 * TopologyCoordinator's job. Consumers that want topology response wire
 * a coordinator to one or more registries.
 *
 * Header-only: the template instantiation is trivially small and keeping
 * it inline lets consumer subclasses bind their type without generating
 * library exports for every combination.
 */
template<typename SurfaceT = Surface>
class ScreenSurfaceRegistry
{
    static_assert(std::is_base_of_v<Surface, SurfaceT>, "SurfaceT must derive from PhosphorLayer::Surface");

public:
    ScreenSurfaceRegistry(SurfaceFactory* factory, IScreenProvider* screens)
        : m_factory(factory)
        , m_screens(screens)
    {
    }
    ~ScreenSurfaceRegistry()
    {
        clear();
    }

    ScreenSurfaceRegistry(const ScreenSurfaceRegistry&) = delete;
    ScreenSurfaceRegistry& operator=(const ScreenSurfaceRegistry&) = delete;

    /// Drop all tracked surfaces. QObject parent semantics handle deletion;
    /// this method only un-registers them from our map.
    void clear();

    /// Register an externally-created Surface under @p screen.
    /// Useful when a consumer subclasses Surface and constructs it itself
    /// (until create() learns to return subclass instances).
    void adoptSurface(QScreen* screen, SurfaceT* surface);

    SurfaceT* surfaceForScreen(QScreen* screen) const;
    QList<SurfaceT*> surfaces() const;

    SurfaceFactory* factory() const noexcept
    {
        return m_factory;
    }
    IScreenProvider* screenProvider() const noexcept
    {
        return m_screens;
    }

private:
    SurfaceFactory* m_factory;
    IScreenProvider* m_screens;
    QHash<QScreen*, QPointer<SurfaceT>> m_entries;
};

// ── Template implementations ───────────────────────────────────────────

template<typename SurfaceT>
void ScreenSurfaceRegistry<SurfaceT>::clear()
{
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (SurfaceT* s = it.value().data()) {
            s->deleteLater();
        }
    }
    m_entries.clear();
}

template<typename SurfaceT>
void ScreenSurfaceRegistry<SurfaceT>::adoptSurface(QScreen* screen, SurfaceT* surface)
{
    m_entries.insert(screen, QPointer<SurfaceT>(surface));
}

template<typename SurfaceT>
SurfaceT* ScreenSurfaceRegistry<SurfaceT>::surfaceForScreen(QScreen* screen) const
{
    const auto it = m_entries.constFind(screen);
    return (it != m_entries.constEnd()) ? it.value().data() : nullptr;
}

template<typename SurfaceT>
QList<SurfaceT*> ScreenSurfaceRegistry<SurfaceT>::surfaces() const
{
    QList<SurfaceT*> out;
    out.reserve(m_entries.size());
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        if (SurfaceT* s = it.value().data()) {
            out.append(s);
        }
    }
    return out;
}

} // namespace PhosphorLayer

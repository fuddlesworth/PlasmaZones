// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/IScreenProvider.h>
#include <PhosphorLayer/SurfaceConfig.h>
#include <PhosphorLayer/SurfaceFactory.h>
#include <PhosphorLayer/phosphorlayer_export.h>

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QSet>

#include <functional>

namespace PhosphorLayer {

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
 *
 * @note **Status (v0.1):** not yet used by the reference consumer
 * (PlasmaZones OverlayService, which tracks per-screen state in its own
 * QHash keyed by virtual-screen id). The registry is a public primitive
 * for consumers whose per-screen state maps cleanly to physical QScreen*.
 * Its API is covered by unit tests but has no production integration yet;
 * treat behavioural details (QHash iteration order, pointer identity
 * across createForAllScreens calls) as subject to change until the first
 * real consumer lands.
 */
template<typename SurfaceT = Surface>
class ScreenSurfaceRegistry
{
    static_assert(std::is_base_of_v<Surface, SurfaceT>, "SurfaceT must derive from PhosphorLayer::Surface");

public:
    /// Per-screen builder. Receives the target screen; returns a new Surface
    /// (or subclass) bound to it. Nullptr return is permitted — the registry
    /// skips that screen without logging.
    using Builder = std::function<SurfaceT*(QScreen*)>;

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

    /**
     * @brief Create one Surface per screen reported by the provider.
     *
     * Takes a Builder because SurfaceConfig is move-only (contentItem) and
     * per-screen context often differs (screen name, geometry, etc.).
     * Passing a builder is strictly more flexible than cloning a single
     * config, and gives the consumer a natural place to inject per-screen
     * context properties.
     *
     * Idempotent: re-running with the same screen set is a no-op.
     * Screens that already have an entry keep their existing surface; new
     * screens get a freshly-built one.
     */
    QList<SurfaceT*> createForAllScreens(Builder builder);

    /// Diff-sync against the provider's current screen list. Destroys
    /// surfaces for removed screens; calls @p builder for newly-added ones.
    /// Emits no signals — consumers listen to their surfaces directly.
    void syncToScreens(Builder builder);

    /// Drop all tracked surfaces (deleteLater on each, clear map).
    void clear();

    /// Register an externally-created Surface under @p screen.
    /// Useful when a consumer subclasses Surface and constructs it itself.
    ///
    /// If @p screen already has a registered surface, the prior surface is
    /// scheduled for deletion via `deleteLater()` unless it is the same
    /// object as @p surface (re-adopt is an idempotent no-op).
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
QList<SurfaceT*> ScreenSurfaceRegistry<SurfaceT>::createForAllScreens(Builder builder)
{
    QList<SurfaceT*> out;
    if (!m_screens) {
        return out;
    }
    const auto list = m_screens->screens();
    out.reserve(list.size());
    for (QScreen* s : list) {
        if (m_entries.contains(s) && m_entries.value(s).data()) {
            out.append(m_entries.value(s).data());
            continue;
        }
        SurfaceT* surface = builder ? builder(s) : nullptr;
        if (surface) {
            m_entries.insert(s, QPointer<SurfaceT>(surface));
            out.append(surface);
        }
    }
    return out;
}

template<typename SurfaceT>
void ScreenSurfaceRegistry<SurfaceT>::syncToScreens(Builder builder)
{
    if (!m_screens) {
        return;
    }
    const auto current = m_screens->screens();
    const QSet<QScreen*> currentSet(current.begin(), current.end());

    // Single pass: remove entries for (a) removed screens and (b) screens
    // whose QPointer auto-nulled because the surface was destroyed
    // externally. Without the null-sweep the map accumulates dangling
    // entries under pathological consumer behaviour (consumer deletes the
    // surface without notifying the registry) and surfaceForScreen() keeps
    // reporting null for a key that looks like it's present.
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (!currentSet.contains(it.key())) {
            if (SurfaceT* s = it.value().data()) {
                s->deleteLater();
            }
            it = m_entries.erase(it);
        } else if (!it.value().data()) {
            it = m_entries.erase(it);
        } else {
            ++it;
        }
    }

    for (QScreen* s : current) {
        if (m_entries.contains(s) && m_entries.value(s).data()) {
            continue;
        }
        SurfaceT* surface = builder ? builder(s) : nullptr;
        if (surface) {
            m_entries.insert(s, QPointer<SurfaceT>(surface));
        }
    }
}

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
    // Replace any prior entry — the registry is the owner-of-record, so
    // dropping the QPointer without deleteLater() would orphan the prior
    // surface (clear() only iterates current entries). Re-adopting the
    // same pointer is idempotent.
    const auto existing = m_entries.constFind(screen);
    if (existing != m_entries.constEnd()) {
        if (SurfaceT* prior = existing.value().data()) {
            if (prior != surface) {
                prior->deleteLater();
            }
        }
    }
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

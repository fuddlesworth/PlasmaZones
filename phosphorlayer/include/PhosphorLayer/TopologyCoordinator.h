// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/phosphorlayer_export.h>

#include <QObject>

#include <memory>

namespace PhosphorLayer {

class ILayerShellTransport;
class IScreenProvider;

/**
 * @brief Tunables for TopologyCoordinator.
 *
 * Declared at namespace scope (rather than nested inside the class) so
 * `cfg = {}` works as a default argument on the coordinator's constructor —
 * a nested struct with NSDMIs cannot be default-constructed in its own
 * enclosing class's method signatures.
 */
struct TopologyConfig
{
    int debounceMs = 200; ///< Coalesce screensChanged bursts
    bool debugLogDiffs = false;
};

/**
 * @brief Reacts to screen hot-plug / virtual-screen reconfiguration /
 * compositor restart; drives surface recreations via the registries.
 *
 * Qt's QGuiApplication emits `screensChanged` repeatedly during hot-plug
 * (one per wl_output event, plus scaling updates). The coordinator
 * debounces these into a single recreation cycle.
 *
 * Multiple registries may be attached — e.g. a notification daemon that
 * keeps both a per-screen OSD registry and a singleton modal, where only
 * the former should respond to topology changes.
 */
class PHOSPHORLAYER_EXPORT TopologyCoordinator : public QObject
{
    Q_OBJECT
public:
    /// Convenience alias — existing call sites that spell out the full name
    /// still compile, but `TopologyCoordinator::Config` reads fine too.
    using Config = TopologyConfig;

    TopologyCoordinator(IScreenProvider* screens, ILayerShellTransport* transport, TopologyConfig cfg = {},
                        QObject* parent = nullptr);
    ~TopologyCoordinator() override;

    /**
     * @brief Register a callback invoked whenever the coordinator decides
     * the screen set has materially changed.
     *
     * The callback receives no arguments; the consumer queries the
     * IScreenProvider to diff the old/new sets. Runs on the GUI thread.
     *
     * Returns a cookie that can be passed to detachSyncCallback() to
     * unregister. Unregistering during a callback invocation is safe.
     *
     * Note: v1 exposes callbacks (not typed ScreenSurfaceRegistry handles)
     * so the coordinator can fan out to any consumer-defined state
     * container without knowing the registry's surface type parameter.
     */
    using SyncCallback = std::function<void()>;
    using CallbackId = quint64;
    CallbackId attachSyncCallback(SyncCallback cb);
    void detachSyncCallback(CallbackId id);

Q_SIGNALS:
    /// Debounce fired; the consumer should snapshot any state that depends
    /// on the current screen set before sync callbacks start rebuilding it.
    void screensChanging();

    /// All sync callbacks have run; the screen set is stable again.
    void screensChanged();

    /// Transport signalled compositor loss (wlr-layer-shell global removed).
    /// Consumers must assume every surface has been torn down and will be
    /// respawned once the transport is re-bound.
    void compositorRestarted();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PhosphorLayer

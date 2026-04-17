// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/SurfaceConfig.h>
#include <PhosphorLayer/phosphorlayer_export.h>

#include <QObject>
#include <QString>

#include <memory>

QT_BEGIN_NAMESPACE
class QQuickWindow;
QT_END_NAMESPACE

namespace PhosphorLayer {

class ILayerShellTransport;
class IQmlEngineProvider;
class IScreenProvider;
class ITransportHandle;
class SurfaceFactory;

/**
 * @brief Bundle of dependencies the Surface inherits from the factory.
 *
 * Kept in the public header because SurfaceFactory::create() returns a
 * Surface* and the dependency pointers need to travel with it; private
 * because only SurfaceFactory constructs Surfaces. No stable ABI guarantee
 * between library versions — revisit if Surface becomes user-constructible.
 *
 * @note **Pre-1.0 ABI.** SurfaceDeps is a plain aggregate exposed across
 * the DSO boundary. Adding or reordering fields is a binary-incompatible
 * change until the library reaches 1.0; consumers that aggregate-init by
 * position must rebuild. Use named-member init
 * (`SurfaceDeps{.transport = ...}`) for forward-compatibility.
 */
struct SurfaceDeps
{
    ILayerShellTransport* transport = nullptr;
    IQmlEngineProvider* engineProvider = nullptr; // Optional — nullptr = per-surface engine
    /// Screen provider the Surface subscribes to for hot-unplug detection.
    /// Optional: if nullptr the Surface cannot re-validate its bound QScreen*
    /// after screensChanged and will hand dangling pointers to the transport
    /// on reattach. The factory always sets it.
    IScreenProvider* screenProvider = nullptr;
    QString loggingCategory;
};

/**
 * @brief One layer-shell surface with a managed lifecycle.
 *
 * Owns its QQuickWindow and (by default) its QQmlEngine. Constructed only
 * via SurfaceFactory so injected dependencies can travel with it.
 *
 * ## Lifecycle (state machine)
 *
 * | From           | Event                    | To            |
 * |----------------|--------------------------|---------------|
 * | Constructed    | warmUp()                 | Warming       |
 * | Constructed    | show()                   | Warming       |
 * | Warming        | (content ready, intent=warm) | Hidden     |
 * | Warming        | (content ready, intent=show) | Shown      |
 * | Warming        | (content error)          | Failed        |
 * | Hidden         | show()                   | Shown         |
 * | Shown          | hide()                   | Hidden        |
 * | any            | (transport rejected)     | Failed        |
 *
 * Invalid transitions (e.g. `show()` from Failed) log `qCWarning` with the
 * debugName and no-op. They never throw.
 *
 * Topology-driven respawn (screen removal / compositor restart) is the
 * consumer's responsibility, not the library's: subscribe to
 * TopologyCoordinator callbacks or use ScreenSurfaceRegistry::syncToScreens
 * to decide when to destroy and rebuild surfaces. The library deliberately
 * keeps Surface oblivious to the screen lifecycle so consumers retain full
 * control over the save-state-before-destroy sequencing.
 *
 * `hide()` is synchronous from the caller's perspective — the window is
 * unmapped immediately. We do not expose a transient "Hiding" state because
 * the compositor's acknowledgement is invisible to application code and the
 * surface is unusable until it's either Shown again or recreated.
 */
class PHOSPHORLAYER_EXPORT Surface : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged FINAL)

public:
    enum class State {
        Constructed, ///< Config accepted; no window yet
        Warming, ///< Engine / window / content initialising
        Shown, ///< Layer surface attached; window visible
        Hidden, ///< Attached but window hidden — show() is cheap
        Failed, ///< Unrecoverable (content error, transport rejected, …)
    };
    /// Q_ENUM so QMetaEnum can stringify State values in log messages
    /// (State::Failed → "Failed") and so Q_PROPERTY / QSignalSpy introspection
    /// works. Not currently registered with QML — consumers that want to bind
    /// surface state from QML should register the type themselves via
    /// qmlRegisterUncreatableType<Surface>.
    Q_ENUM(State)

    ~Surface() override;

    State state() const noexcept;
    const SurfaceConfig& config() const noexcept;

    /// @name Lifecycle
    /// Idempotent and safe to call in any state. Invalid transitions warn
    /// and no-op.
    /// @{
    void show();
    void hide();
    /// Create engine + window + content, leave hidden. Pre-compiles QML so
    /// the first show() is latency-free.
    void warmUp();
    /// @}

    /// @name Escape hatches
    /// Prefer the declarative API. These are for consumers that need to
    /// reach into Qt machinery (installing event filters, connecting to
    /// window-specific signals, etc.).
    /// @{
    QQuickWindow* window() const noexcept;
    ITransportHandle* transport() const noexcept;
    /// @}

Q_SIGNALS:
    /// NOTIFY signal for the state Q_PROPERTY. Carries the new state so
    /// slots don't need a round-trip getter call. The previous state is
    /// not exposed — track it in your slot if you need it.
    void stateChanged(State newState);

    /// Emitted once when transitioning to State::Failed. `reason` is a
    /// human-readable diagnostic (QML compile error, transport rejection,
    /// etc.). Surface remains in Failed thereafter.
    ///
    /// ## Safe to delete the Surface from this slot
    /// The library defers this signal via `Qt::QueuedConnection` so consumer
    /// slots that call `delete surface` do not re-enter library code on a
    /// dead `this`. For non-failure `stateChanged` emissions the consumer
    /// must prefer `deleteLater()` over `delete`.
    void failed(const QString& reason);

    /// Emitted when the QScreen the Surface was attached to is removed from
    /// the IScreenProvider's list. The Surface stays in its current state
    /// (Shown/Hidden); consumers decide whether to destroy+rebuild or wait
    /// for the screen to come back. `m_config.screen` is nulled inside the
    /// library so the next attach falls back to the provider's primary.
    void screenLost();

public:
    /**
     * @brief Opaque construction token — only SurfaceFactory can create
     * instances of it.
     *
     * Required as the first parameter of Surface's constructor so direct
     * construction (`new Surface(...)`) is impossible without going through
     * `SurfaceFactory::create` / `createAs<T>`, which are the only paths
     * that run the factory-wide validation (role well-formedness, screen
     * fallback, `sharedEngine`+`engineProvider` mutual exclusion).
     *
     * Subclasses wishing to be constructible via `SurfaceFactory::createAs<T>`
     * accept a `CtorToken` as the first argument of their own constructor
     * and forward it to the base. Consumer code cannot instantiate a
     * CtorToken directly (private ctor, only friended to SurfaceFactory),
     * so subclasses cannot bypass the factory even by exposing a public
     * constructor of their own.
     */
    class CtorToken
    {
        friend class SurfaceFactory;
        CtorToken() = default;
    };

protected:
    /// Protected so subclasses can forward a CtorToken through their own
    /// constructor. Still unreachable to consumers — CtorToken is
    /// SurfaceFactory-only-constructible.
    Surface(CtorToken, SurfaceConfig cfg, SurfaceDeps deps, QObject* parent);

private:
    Q_DISABLE_COPY_MOVE(Surface)

    friend class SurfaceFactory;

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PhosphorLayer

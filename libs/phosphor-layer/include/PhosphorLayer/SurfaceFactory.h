// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/SurfaceConfig.h>
#include <PhosphorLayer/phosphorlayer_export.h>

#include <QObject>
#include <QString>

#include <optional>
#include <type_traits>

namespace PhosphorLayer {

class ILayerShellTransport;
class IQmlEngineProvider;
class IScreenProvider;
class ISurfaceAnimator;

/**
 * @brief Stateless constructor for Surfaces.
 *
 * Holds references to injected dependencies; each create() call produces
 * one Surface that inherits those dependencies.
 *
 * Single responsibility: turn a @ref SurfaceConfig into a live @ref Surface.
 * For multi-screen patterns use @ref ScreenSurfaceRegistry; for topology
 * response use @ref TopologyCoordinator. Those compose the factory but
 * do not replace it.
 */
class PHOSPHORLAYER_EXPORT SurfaceFactory : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Dependency bundle for SurfaceFactory.
     *
     * @note **Pre-1.0 ABI.** Deps is a plain aggregate exposed across the
     * DSO boundary. Adding or reordering fields between releases is a
     * binary-incompatible change until the library reaches 1.0 (SOVERSION
     * 0 signals this). Consumers using positional aggregate-init must
     * rebuild against each release; prefer named-member init
     * (`Deps{.transport = ...}`) for forward compatibility.
     */
    struct Deps
    {
        /// Required. Concrete transport implementation (e.g. PhosphorShellTransport).
        ILayerShellTransport* transport = nullptr;

        /// Required. Source of truth for the QScreen set.
        IScreenProvider* screens = nullptr;

        /// Optional. Nullptr → each Surface owns its own QQmlEngine (default
        /// isolation). Non-null → provider decides (e.g. return the same
        /// engine for every call to share).
        IQmlEngineProvider* engineProvider = nullptr;

        /// Optional. Nullptr → Surface uses the no-op default animator
        /// (synchronous beginShow/beginHide; identical to the pre-Phase-5
        /// lifecycle). Non-null → Surface dispatches show/hide transitions
        /// through this animator.
        ///
        /// **Lifetime contract**: when non-null, the animator MUST outlive
        /// every Surface produced by this factory AND the factory itself.
        /// The factory propagates the raw pointer into each `SurfaceDeps`,
        /// and Surfaces dispatch through it on every state transition
        /// (including their own destruction, which calls `cancel()` to
        /// drop in-flight tracking). A common owner pattern: the consumer
        /// holds the animator and the SurfaceFactory together with the
        /// animator declared first so reverse-declaration-order destruction
        /// destroys the factory (and any Surfaces it parented) before the
        /// animator. See `OverlayService` member ordering for an example.
        ISurfaceAnimator* animator = nullptr;

        /// Logging category name for internal diagnostics. Empty → "phosphorlayer".
        QString loggingCategory;
    };

    explicit SurfaceFactory(Deps deps, QObject* parent = nullptr);
    ~SurfaceFactory() override;

    /**
     * @brief Create a Surface from a config.
     *
     * The returned Surface is owned by @p parent (or by the factory if
     * @p parent is nullptr). Failure modes all yield nullptr + a logged
     * reason:
     * - `deps.transport->isSupported()` is false
     * - Both `cfg.contentUrl` and `cfg.contentItem` set (or both empty)
     * - `cfg.screen == nullptr` and the screen provider has no primary
     * - `cfg.sharedEngine` and `engineProvider` both set (mutually exclusive)
     * - `cfg.role.isValid()` returns false
     */
    [[nodiscard]] Surface* create(SurfaceConfig cfg, QObject* parent = nullptr);

    /**
     * @brief Create a Surface subclass from a config.
     *
     * Same validation as @ref create() but instantiates @p T instead of
     * Surface itself. @p T must derive from Surface and expose a
     * constructor with the signature
     * `T(Surface::CtorToken, SurfaceConfig, SurfaceDeps, QObject*)`
     * — forwarding its `CtorToken` argument to the Surface base is the
     * entire required pattern. The CtorToken is factory-only-constructible,
     * so consumers cannot bypass this validation via direct `new T(...)`
     * even when T exposes a public constructor.
     *
     * Example:
     * @code
     *     class PzOverlaySurface : public PhosphorLayer::Surface {
     *     public:
     *         PzOverlaySurface(Surface::CtorToken tok,
     *                          SurfaceConfig cfg, SurfaceDeps deps,
     *                          QObject* parent)
     *             : Surface(tok, std::move(cfg), std::move(deps), parent) {}
     *     };
     *
     *     auto* s = factory.createAs<PzOverlaySurface>(std::move(cfg));
     * @endcode
     */
    template<typename T>
    [[nodiscard]] T* createAs(SurfaceConfig cfg, QObject* parent = nullptr)
    {
        static_assert(std::is_base_of_v<Surface, T>, "T must derive from PhosphorLayer::Surface");
        auto sdeps = validateAndPrepareDeps(cfg);
        if (!sdeps) {
            return nullptr;
        }
        return new T(Surface::CtorToken{}, std::move(cfg), std::move(*sdeps), parent ? parent : this);
    }

    const Deps& deps() const noexcept;

    /**
     * @brief Run the factory's validation and produce a SurfaceDeps ready
     * for Surface construction. Returns `std::nullopt` on any failure (all
     * failures logged via qCWarning).
     *
     * Exposed publicly because the `createAs<T>` template above must call
     * it at the instantiation site. Consumers should prefer `create()` /
     * `createAs<T>()` over this method directly — they handle the
     * new-expression and parent-ownership wiring for you.
     */
    [[nodiscard]] std::optional<SurfaceDeps> validateAndPrepareDeps(SurfaceConfig& cfg);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PhosphorLayer

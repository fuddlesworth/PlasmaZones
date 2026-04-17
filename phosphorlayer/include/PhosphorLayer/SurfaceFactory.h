// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/SurfaceConfig.h>
#include <PhosphorLayer/phosphorlayer_export.h>

#include <QObject>
#include <QString>

namespace PhosphorLayer {

class ILayerShellTransport;
class IQmlEngineProvider;
class IScreenProvider;
class Surface;

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
     */
    [[nodiscard]] Surface* create(SurfaceConfig cfg, QObject* parent = nullptr);

    const Deps& deps() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PhosphorLayer

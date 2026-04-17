// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/phosphorlayer_export.h>

#include <QMargins>
#include <QObject>
#include <QSize>
#include <QString>

#include <functional>
#include <memory>

QT_BEGIN_NAMESPACE
class QQuickWindow;
class QScreen;
QT_END_NAMESPACE

namespace PhosphorLayer {

/**
 * @brief Arguments passed to ILayerShellTransport::attach().
 *
 * All fields are taken by value; the transport may copy or store them.
 * No references to Surface/SurfaceConfig leak into the transport — the
 * transport is deliberately content-agnostic.
 */
struct TransportAttachArgs
{
    QScreen* screen = nullptr;
    Layer layer = Layer::Overlay;
    Anchors anchors = AnchorNone;
    int exclusiveZone = -1;
    KeyboardInteractivity keyboard = KeyboardInteractivity::None;
    QMargins margins;
    QString scope; ///< Fully-qualified scope (prefix + instance disambiguator)
};

/**
 * @brief Handle returned by attach(); lets the caller mutate post-show
 * properties that the wlr-layer-shell protocol permits (margins, layer,
 * exclusive zone, keyboard interactivity) without re-attaching.
 *
 * Ownership: the caller of attach() owns the handle. Destroying the handle
 * detaches the layer-shell role from the window and releases protocol
 * resources.
 */
class PHOSPHORLAYER_EXPORT ITransportHandle
{
public:
    virtual ~ITransportHandle() = default;

    /// Window this handle controls.
    virtual QQuickWindow* window() const = 0;

    /// True once the compositor has sent its initial configure event.
    /// Consumers should gate geometry queries on this to avoid reading
    /// pre-configure values.
    virtual bool isConfigured() const = 0;

    /// @name Post-show mutable properties
    /// Per wlr-layer-shell v4+. Values are committed immediately.
    ///
    /// `setAnchors` is permitted on compositors implementing wlr-layer-shell
    /// v2+ (the surface stays attached and reconfigures in place). On
    /// transports lacking this capability (xdg_toplevel fallback, older
    /// compositors) the call is silently ignored — consumers that rely on
    /// dynamic re-anchoring should check `isConfigured()` after the next
    /// commit and fall back to destroy-and-recreate if needed.
    /// @{
    virtual void setMargins(QMargins m) = 0;
    virtual void setLayer(Layer l) = 0;
    virtual void setExclusiveZone(int z) = 0;
    virtual void setKeyboardInteractivity(KeyboardInteractivity k) = 0;
    virtual void setAnchors(Anchors a) = 0;
    /// @}

    /// Cached last-known pixel size from the compositor's configure event.
    /// Zero before isConfigured() returns true.
    virtual QSize configuredSize() const = 0;
};

/**
 * @brief Abstracts the layer-shell protocol binding.
 *
 * Default implementation: PhosphorShellTransport (provided in a later
 * phase, wraps PhosphorShell::LayerSurface). Tests inject a MockTransport
 * and exercise the full lifecycle without Wayland.
 */
class PHOSPHORLAYER_EXPORT ILayerShellTransport
{
public:
    virtual ~ILayerShellTransport() = default;

    /// True if the compositor advertises wlr-layer-shell. Surfaces fail
    /// construction with a logged reason when this returns false.
    virtual bool isSupported() const = 0;

    /**
     * @brief Mark @p win as a layer-shell surface with the given initial
     * configuration.
     *
     * MUST be called before @p win's first show(). Post-show properties
     * that the protocol forbids mutating (layer output, scope, anchors in
     * v1–3) are fixed at this point; later changes require destroy +
     * re-attach.
     *
     * Returns nullptr on failure; callers propagate as Surface::Failed.
     */
    virtual std::unique_ptr<ITransportHandle> attach(QQuickWindow* win, const TransportAttachArgs& args) = 0;

    /**
     * @brief Register a callback fired when the compositor's layer-shell
     * global is removed (compositor crash / restart).
     *
     * The library's TopologyCoordinator uses this to tear down all
     * attached handles; consumer code generally does not need to subscribe.
     * Callbacks run on the Wayland event-dispatch context — consumers
     * must marshal to the GUI thread themselves if needed.
     */
    using CompositorLostCallback = std::function<void()>;
    virtual void addCompositorLostCallback(CompositorLostCallback cb) = 0;
};

} // namespace PhosphorLayer

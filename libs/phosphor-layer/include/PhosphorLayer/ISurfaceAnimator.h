// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/phosphorlayer_export.h>

#include <QtGlobal>

#include <functional>

QT_BEGIN_NAMESPACE
class QQuickItem;
class QQuickWindow;
QT_END_NAMESPACE

namespace PhosphorLayer {

class Surface;

/**
 * @brief Hook point for show/hide transitions on Surfaces.
 *
 * Primary animation authoring happens in the consumer's QML — `Behavior
 * on opacity { NumberAnimation { duration: 150 } }` is almost always the
 * right answer for fade-in/out. This interface exists for cases the QML
 * side cannot cover:
 *
 * - Pre-show measurement (e.g. a panel that needs its content sized
 *   before the compositor places it).
 * - Post-hide cleanup that must wait for an animation to finish before
 *   the library tears down the transport handle.
 * - Orchestrated transitions across multiple surfaces (fade out modal
 *   as primary fades in).
 *
 * Most consumers pass nullptr and animate entirely within QML. The
 * default implementation (@ref NoOpSurfaceAnimator) does exactly that —
 * it exists so code paths can always call through the interface without
 * null-checking.
 *
 * ## Threading
 * All hooks are called on the GUI thread.
 *
 * ## Contract
 * Implementations must call `onComplete` exactly once per hook
 * invocation. Failing to do so will strand the Surface in a transient
 * visibility state and eventually trigger a warning + forced transition.
 */
class PHOSPHORLAYER_EXPORT ISurfaceAnimator
{
public:
    /// Invoked when a show/hide animation completes. The animator must
    /// call this exactly once per beginShow/beginHide call. Safe to
    /// invoke synchronously for no-op animators.
    using CompletionCallback = std::function<void()>;

    ISurfaceAnimator() = default;
    virtual ~ISurfaceAnimator() = default;
    Q_DISABLE_COPY_MOVE(ISurfaceAnimator)

    /**
     * @brief Begin a show transition for @p surface.
     *
     * The Surface has just become visible (Hidden → Shown). The animator
     * can mutate @p rootItem (opacity, scale, y-offset) and drive an
     * animation. Invoke @p onComplete when the transition finishes; the
     * library uses this as a no-op today but a future version may defer
     * compositor sync until the completion signal arrives.
     */
    virtual void beginShow(Surface* surface, QQuickItem* rootItem, CompletionCallback onComplete) = 0;

    /**
     * @brief Begin a hide transition for @p surface.
     *
     * The Surface is transitioning Shown → Hidden. The animator should
     * fade/slide the content, then invoke @p onComplete. The library does
     * not delay `window->hide()` waiting for this — the current contract
     * is "animate for visual polish, but the surface is considered hidden
     * the moment hide() is called".
     */
    virtual void beginHide(Surface* surface, QQuickItem* rootItem, CompletionCallback onComplete) = 0;

    /// Interrupt any in-flight animation for @p surface. Called when the
    /// Surface is destroyed or the animation is superseded by an opposite
    /// transition (show-while-hiding).
    virtual void cancel(Surface* surface) = 0;
};

} // namespace PhosphorLayer

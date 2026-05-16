// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/ISurfaceAnimator.h>
#include <PhosphorLayer/phosphorlayer_export.h>

namespace PhosphorLayer {

/**
 * @brief Pass-through ISurfaceAnimator — calls onComplete() synchronously.
 *
 * This is the implicit default when a consumer doesn't provide an
 * animator. Handing it to @ref SurfaceFactory::Deps is equivalent to
 * leaving the animator field nullptr; the class exists so consumers
 * that want an explicit "no animation" contract can opt in.
 *
 * Rationale: 99% of layer-shell consumers animate in QML
 * (`Behavior on opacity { … }`), which happens independently of this
 * interface. The ISurfaceAnimator hook exists for cases QML can't
 * cover (pre-show measurement, orchestrated multi-surface transitions).
 */
class PHOSPHORLAYER_EXPORT NoOpSurfaceAnimator : public ISurfaceAnimator
{
public:
    NoOpSurfaceAnimator() = default;
    ~NoOpSurfaceAnimator() override = default;

    void beginShow(Surface*, QQuickItem*, CompletionCallback onComplete) override
    {
        if (onComplete) {
            onComplete();
        }
    }
    void beginHide(Surface*, QQuickItem*, CompletionCallback onComplete) override
    {
        if (onComplete) {
            onComplete();
        }
    }
    void cancel(Surface*) override
    {
    }
};

} // namespace PhosphorLayer

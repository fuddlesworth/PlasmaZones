// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/phosphorlayer_export.h>

#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QQmlEngine;
QT_END_NAMESPACE

namespace PhosphorLayer {

struct SurfaceConfig;

/**
 * @brief Hook point for consumer-controlled QQmlEngine ownership policy.
 *
 * Default (when @ref SurfaceFactory::Deps::engineProvider is nullptr):
 * every Surface constructs its own QQmlEngine at warmUp() and destroys it
 * in its own dtor. Full isolation — no cross-surface QML state leakage.
 *
 * Opt-in sharing: implement this interface, return the same QQmlEngine
 * pointer from every @ref engineForSurface() call, and release it (or
 * not) as you see fit. Singletons and type registrations become visible
 * to all surfaces sharing the engine; that is the trade-off you make
 * when you opt in.
 */
class PHOSPHORLAYER_EXPORT IQmlEngineProvider
{
public:
    IQmlEngineProvider() = default;
    virtual ~IQmlEngineProvider() = default;
    Q_DISABLE_COPY_MOVE(IQmlEngineProvider)

    /**
     * @brief Return the engine for a surface being constructed.
     *
     * The provider may return:
     * - a per-surface engine (construct a fresh one each call — mirrors the
     *   default behaviour but lets the consumer customise engine setup);
     * - a shared engine (return the same pointer every time);
     * - a pool-selected engine (e.g. one per Role, one per content URL, …).
     *
     * The returned engine must outlive the Surface that receives it.
     */
    virtual QQmlEngine* engineForSurface(const SurfaceConfig& cfg) = 0;

    /**
     * @brief Called when a Surface that previously received an engine is
     * destroyed.
     *
     * Per-surface-engine providers typically `delete engine`. Shared-engine
     * providers no-op. Pool providers may decrement a refcount.
     */
    virtual void releaseEngine(QQmlEngine* engine) = 0;
};

} // namespace PhosphorLayer

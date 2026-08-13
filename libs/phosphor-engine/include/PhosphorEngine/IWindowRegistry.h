// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorengine_export.h>
#include <QString>

#include <optional>

namespace PhosphorEngine {

class PHOSPHORENGINE_EXPORT IWindowRegistry
{
public:
    virtual ~IWindowRegistry() = default;

    /**
     * @brief Freeze (on first contact) and return the canonical id for
     * @p rawWindowId's instance.
     *
     * The mapping is seeded once and never re-seeded, so a mid-session class
     * mutation keeps resolving to the identity the stores were keyed with.
     * An id whose appId part is EMPTY is exempt from the freeze and returned
     * verbatim: a blank window class is a not-yet-mapped surface, and a
     * permanent appId-less canonical would strip the identity every
     * appId-keyed store looks a window up by.
     */
    virtual QString canonicalizeWindowId(const QString& rawWindowId) = 0;
    virtual QString canonicalizeForLookup(const QString& rawWindowId) const = 0;
    virtual QString appIdFor(const QString& instanceId) const = 0;

    /**
     * @brief Tri-state compositor minimize state for @p windowId.
     *
     * Engaged true/false when the compositor has reported the window's
     * minimize state; std::nullopt when it is UNKNOWN (no record for the
     * window, or the metadata push never carried the field). SEEDING and
     * TILING consumers — anything that would place, move, or count a window
     * in a layout — must treat nullopt conservatively rather than as false:
     * collapsing unknown into "not minimized" is the fail-open that seeds
     * hidden windows into tiling layouts and persists minimize-suspension
     * floats. CAPTURE-side consumers may deliberately require engaged-true
     * (`.value_or(false)`) when their traffic is causally ordered after the
     * minimize edge's metadata push (recordFreeGeometry / recordFloatingClose
     * document this per site) — refusing a capture on unknown would lose
     * genuine free positions, the opposite failure.
     *
     * Default implementation reports unknown so registry-less engines and
     * test fakes stay honest instead of claiming visibility.
     */
    virtual std::optional<bool> minimizedState(const QString& windowId) const
    {
        Q_UNUSED(windowId)
        return std::nullopt;
    }
};

} // namespace PhosphorEngine

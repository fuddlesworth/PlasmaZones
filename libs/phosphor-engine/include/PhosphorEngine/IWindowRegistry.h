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
     * That single stable key per window, for the window's whole life, is the
     * invariant every keyed store depends on: engines key their membership
     * maps at adopt time and have no re-key path, so an implementation that
     * let the canonical CHANGE mid-life would strand live state rather than
     * correct it.
     *
     * The freeze is deliberately unconditional, including for an id whose
     * appId part is empty (KWin reports a blank class for a surface it has
     * not finished mapping). Callers needing a window's app identity must ask
     * appIdFor / currentAppIdFor, which read the metadata record and
     * self-correct when the class arrives, rather than parsing it back out of
     * the frozen id.
     */
    virtual QString canonicalizeWindowId(const QString& rawWindowId) = 0;
    virtual QString canonicalizeForLookup(const QString& rawWindowId) const = 0;
    /// The app identity recorded for @p instanceId, or empty when the instance
    /// is unknown. Takes a BARE instance id: unlike minimizedState, which
    /// accepts either form, this does not split a composite `appId|instanceId`
    /// and answers empty for one. Callers holding a window id should extract
    /// the instance part first (or use a service-level currentAppIdFor, which
    /// does it for them).
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

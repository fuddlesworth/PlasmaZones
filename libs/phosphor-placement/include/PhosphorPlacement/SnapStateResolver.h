// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QList>
#include <QString>

#include <functional>

namespace PhosphorSnapEngine {
class SnapState;
}

namespace PhosphorPlacement {

/// Seam onto the snap engine's per-(screen,desktop,activity) SnapState stores.
/// The service no longer holds a single SnapState; instead the daemon injects
/// resolvers so each windowId-keyed call reaches the state that owns the window
/// (via the engine's reverse map) and each screen-carrying write reaches — and
/// registers — the state for that screen. `globals` is the single holder of the
/// still-global last-used-zone / user-snapped scalars, and `allStates`
/// enumerates every store (including the global holder) for whole-store views.
/// When unset (unit tests / early init) every resolver is empty and the facade
/// degrades to the historical "no SnapState wired" no-op behaviour.
struct SnapStateResolver
{
    /// Owning state for a window (reverse-map lookup). Non-null by
    /// construction in every wiring, by one of two mechanisms: the
    /// forwarding resolvers because SnapEngine::stateForWindow falls back
    /// to the global holder rather than returning null, and setSnapState's
    /// convenience resolver because it rejects a null state outright and
    /// captures a non-null one. Two genuine null cases remain, and callers
    /// must still guard: an UNSET resolver (see snapForWindow), and the
    /// production resolver's QPointer arm once the engine is destroyed
    /// while this service still holds the resolver (a real teardown order).
    std::function<PhosphorSnapEngine::SnapState*(const QString& windowId)> forWindow;
    /// Owning state for a window placed/acting on a screen: resolves the state
    /// (creating it on first placement) AND records the reverse-map entry.
    std::function<PhosphorSnapEngine::SnapState*(const QString& windowId, const QString& screenId)> forWindowOnScreen;
    /// Owning state for a SCREEN's current (screen, desktop, activity) context,
    /// independent of any window — resolves (creating on first use) the per-key
    /// store a screen-scoped write (last-used-zone) targets. An empty screenId
    /// resolves to the global holder.
    std::function<PhosphorSnapEngine::SnapState*(const QString& screenId)> forScreen;
    /// The global-scalar holder. Non-null by construction in every wiring
    /// (SnapEngine::globalState() returns the ctor-constructed holder); the
    /// same two null cases as @ref forWindow apply — an unset resolver, and
    /// the production resolver's QPointer arm after engine destruction.
    std::function<PhosphorSnapEngine::SnapState*()> globals;
    /// Every store, including the global holder, for aggregate iteration.
    /// CONTRACT: the returned list never contains a null element. An unset
    /// resolver yields an empty list rather than a list of nulls, and both
    /// wirings filter (SnapEngine::allSnapStates() skips nulls; the
    /// single-store convenience wiring rejects a null state outright). Every
    /// caller dereferences the elements unguarded, so a resolver that
    /// breaks this crashes them all.
    std::function<QList<PhosphorSnapEngine::SnapState*>()> allStates;
    /// Drop a window's reverse-map entry (window closed / fully removed).
    std::function<void(const QString& windowId)> forgetWindow;
};

} // namespace PhosphorPlacement

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorengine_export.h>

#include <QString>
#include <QStringList>

#include <functional>

namespace PhosphorEngine {

/// One side (tiled or floating) of a layer focus switch, as the owning
/// engine sees it. The resolver never touches engine state — the engine
/// snapshots what it knows into this struct and interprets the result.
struct LayerSwitchSide
{
    /// Preferred target on this side: the side's remembered last focus, or,
    /// for an engine whose live selection IS the side, its current active
    /// window (the scroll strip's tiled side). May be empty or stale — an
    /// ineligible candidate falls through to the scan.
    QString candidate;

    /// Ordered fallback pool scanned when the candidate is not eligible.
    /// May be empty for engines whose candidate is authoritative (the
    /// scroll strip's active window IS the tiled side, no scan exists).
    QStringList fallbacks;

    /// Eligibility filter applied to the candidate and every fallback.
    /// Null means always eligible. Typical filter: "still on this side
    /// and not minimized" — the daemon models minimize as a float, so a
    /// remembered focus can name a hidden window that must not be
    /// "activated" with success reported.
    std::function<bool(const QString&)> isEligible;

    /// The window reported as the feedback SOURCE when this side holds
    /// focus (i.e. when the switch departs from this side). Passed through
    /// UNVALIDATED — no eligibility filter runs on it — so supply the live
    /// focus, not a memory that may name a closed or minimized window. The
    /// resolver only ever reads the source side's value, so callers that
    /// know the live focus can assign the same value to both sides.
    QString focusForFeedback;
};

/// Outcome of resolving a layer focus switch. Pure data — the engine emits
/// activation/feedback from it (PlacementEngineBase::announceLayerSwitch)
/// and applies its own activation policy first (e.g. the scroll engine's
/// self-activation echo queue on the float-to-tiling leg).
struct LayerSwitchResult
{
    bool success = false;
    /// Which leg ran: true = float layer held focus and the switch targets
    /// the tiled side. Valid on failure too (the leg that found no target).
    bool toTiled = false;
    /// Feedback reason token: "tiled" / "floating" on success, "no_target"
    /// on failure. Engines may remap the success token before emitting
    /// (the snap engine says "snapped" where scroll says "tiled").
    QString reason;
    QString source;
    QString target;
};

/// Resolve niri's switch-focus-between-floating-and-tiling for any engine:
/// pick the leg from @p floatingHasFocus, then the target on the far side —
/// its candidate if eligible, else the first eligible fallback. Encodes the
/// verb contract once so all three engines refuse and report identically;
/// what stays per-engine is HOW "floating has focus" is known and what
/// bookkeeping the activation needs (see the scroll engine's echo
/// asymmetry: the float-to-tiling activation is the engine's own doing and
/// is echo-filtered, so that engine must clear its flag eagerly, while the
/// tiling-to-float leg deliberately lets the genuine focus report arm the
/// float-side memory).
PHOSPHORENGINE_EXPORT LayerSwitchResult resolveLayerFocusSwitch(bool floatingHasFocus, const LayerSwitchSide& tiledSide,
                                                                const LayerSwitchSide& floatingSide);

} // namespace PhosphorEngine

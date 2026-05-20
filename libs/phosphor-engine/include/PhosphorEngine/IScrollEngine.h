// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/IPlacementState.h>
#include <phosphorengine_export.h>

#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <QtGlobal>

namespace PhosphorScrollEngine {
// Forward-declared so the IScrollEngine surface can return the engine's typed
// viewport-mode enum without dragging ScrollLayout.h (and its transitive
// ScrollScreenState.h chain) into this header. Scoped enums implicitly use
// `int` as their underlying type, which makes this opaque forward declaration
// well-formed and ABI-stable across the cross-library boundary.
enum class ScrollViewportMode : int;
} // namespace PhosphorScrollEngine

namespace PhosphorEngine {

/// Scroll-engine concrete API surfaced to the daemon as a pure interface.
///
/// The daemon holds every placement engine through `PlacementEngineBase*`,
/// which only exposes the IPlacementEngine/Q_OBJECT base contract. The
/// scroll engine, however, has a handful of operations the daemon needs but
/// the generic placement contract does NOT cover — per-screen effective
/// resolvers, persistent-state probing, restored-window reconciliation, and
/// the scroll-only desktop/screen prune (`pruneStatesForScreen`). Before
/// this interface those calls were reached by storing an extra concrete-typed
/// cache pointer (`m_scrollEngineCached`) populated via `dynamic_cast` on
/// engine construction. Cache + cast are now replaced by a cross-cast to
/// `IScrollEngine*` — same hot-path RTTI cost as the existing
/// `IScrollNavigation` cross-cast, with the daemon no longer compiling
/// against the concrete `PhosphorScrollEngine::ScrollEngine` type.
///
/// Cross-cast pattern (mirrors IScrollNavigation):
/// `auto* scroll = dynamic_cast<IScrollEngine*>(engineBase);` — yields
/// nullptr when the engine is not a scroll engine, so the daemon can safely
/// route scroll-only paths through one branch.
///
/// The class is exported so its typeinfo has default visibility and the
/// cross-cast works across the libphosphor-scroll-engine ↔ daemon shared
/// library boundary; losing the export silently turns every scroll-only
/// path in the daemon into a no-op in release builds with no diagnostic.
///
/// IScrollEngine intentionally does NOT inherit from IPlacementEngine: that
/// would force virtual inheritance through PlacementEngineBase to dodge the
/// diamond. Instead the interface re-declares (with identical signatures)
/// the IPlacementEngine methods the daemon reaches through its IScrollEngine
/// pointer. A single `override` in the concrete `ScrollEngine` derived class
/// satisfies both base virtuals in one slot — well-defined behaviour for
/// independent base classes that declare matching virtual signatures — so
/// callers via either pointer dispatch to the same implementation.
class PHOSPHORENGINE_EXPORT IScrollEngine
{
public:
    virtual ~IScrollEngine() = default;

    // ── Effective per-screen config (override → IScrollSettings global) ─
    /// Effective inner column / inter-tile gap (logical px) for @p screenId.
    virtual int effectiveInnerGap(const QString& screenId) const = 0;
    /// Effective working-area outer margin (logical px) for @p screenId.
    virtual int effectiveOuterGap(const QString& screenId) const = 0;
    /// Effective preset window-height fractions [0..1] for @p screenId.
    virtual QVector<qreal> effectivePresetWindowHeights(const QString& screenId) const = 0;
    /// Effective viewport scroll-on-focus mode for @p screenId.
    virtual PhosphorScrollEngine::ScrollViewportMode effectiveViewportMode(const QString& screenId) const = 0;

    // ── Persistence ─────────────────────────────────────────────────────
    /// Serialize every persistable strip to a JSON snapshot.
    virtual QJsonObject serializeEngineState() const = 0;
    /// Restore strip state from a snapshot produced by serializeEngineState.
    virtual void deserializeEngineState(const QJsonObject& state) = 0;
    /// True when at least one strip holds state worth persisting.
    virtual bool hasPersistableState() const = 0;
    /// Reconcile a freshly restored strip against the live window set.
    virtual void reconcileRestoredWindows(const QSet<QString>& liveWindowIds) = 0;

    // ── Per-screen config overrides ─────────────────────────────────────
    /// Apply a per-screen override map for @p screenId.
    virtual void applyPerScreenConfig(const QString& screenId, const QVariantMap& overrides) = 0;
    /// Drop every override for @p screenId.
    virtual void clearPerScreenConfig(const QString& screenId) = 0;
    /// Currently-applied override map for @p screenId (empty if none).
    virtual QVariantMap perScreenOverrides(const QString& screenId) const = 0;

    // ── Lifecycle pruning ───────────────────────────────────────────────
    /// Drop strip state, window mappings, and per-screen overrides tied to
    /// @p screenId — the daemon calls this on monitor disconnect.
    virtual void pruneStatesForScreen(const QString& screenId) = 0;

    // ── State access ────────────────────────────────────────────────────
    /// Per-screen placement state (typed view via IPlacementState).
    virtual IPlacementState* stateForScreen(const QString& screenId) = 0;
    /// Const overload — same contract.
    virtual const IPlacementState* stateForScreen(const QString& screenId) const = 0;
};

} // namespace PhosphorEngine

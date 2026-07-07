// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutSettingsStore.h>

#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorRules/RuleStore.h>

#include <QHash>
#include <QPair>
#include <QString>
#include <QUuid>
#include <functional>
#include <memory>
#include <optional>

namespace PhosphorZones {

/**
 * @brief Triple-axis key for the Combined-context batch API.
 *
 * Combined rules pin all three dimensions (screen + desktop + activity)
 * — the QPair-based keys used by the Activity / Desktop batch APIs can't
 * represent them. This struct + qHash overload below let
 * `combinedAssignments()` / `setAllCombinedAssignments()` round-trip
 * Combined rules without dropping any dimension. Pure-Activity, pure-
 * Desktop, and Monitor-only rules continue to use their dedicated
 * batches and stay invisible to this API.
 */
struct CombinedAssignmentKey
{
    QString screenId;
    int virtualDesktop = 0;
    QString activity;

    bool operator==(const CombinedAssignmentKey& other) const
    {
        return screenId == other.screenId && virtualDesktop == other.virtualDesktop && activity == other.activity;
    }
};

inline size_t qHash(const CombinedAssignmentKey& key, size_t seed = 0) noexcept
{
    return qHashMulti(seed, key.screenId, key.virtualDesktop, key.activity);
}

/**
 * @brief Manual zone-layout registry + per-context assignment store.
 *
 * Concrete counterpart to @ref IZoneLayoutRegistry - mirrors the
 * PhosphorTiles @c AlgorithmRegistry shape (interface for the provider
 * contract, one concrete class for everything else). Composition roots
 * construct one instance per process and inject it into every consumer;
 * there is no process-global singleton.
 *
 * Responsibilities:
 *   - In-memory catalog of @ref Layout instances (enumeration, add,
 *     remove, duplicate, active-layout selection - inherited from
 *     @ref IZoneLayoutRegistry).
 *   - Per-context @ref AssignmentEntry resolution keyed by
 *     (screenId, virtualDesktop, activity), with cascading fallback
 *     (narrower keys beat wider keys; base screen is the widest). The
 *     assignment authority is the injected
 *     @ref PhosphorRules::RuleStore — each per-context
 *     assignment is a context-only @c Rule and the cascade is the
 *     evaluator's descending-priority walk.
 *   - Numbered quick-layout shortcut slots (1..9), persisted to a
 *     @c quicklayouts.json sidecar beside the rule store's file.
 *   - Built-in layout templates (columns, rows, grid, priority, focus).
 *   - JSON-file persistence for layouts.
 */
class PHOSPHORZONES_EXPORT LayoutRegistry : public IZoneLayoutRegistry
{
    Q_OBJECT

    Q_PROPERTY(int layoutCount READ layoutCount NOTIFY layoutsChanged)
    // `activeLayout` Q_PROPERTY moved to IZoneLayoutRegistry alongside its
    // NOTIFY signal so the contract is visible to interface consumers.
    Q_PROPERTY(QString layoutDirectory READ layoutDirectory WRITE setLayoutDirectory NOTIFY layoutDirectoryChanged)

public:
    /**
     * @param ruleStore Borrowed unified Rule store. Required - asserted
     *                non-null. The store's @ref PhosphorRules::RuleSet
     *                is the single source of truth for every per-context
     *                assignment; the registry resolves @ref layoutForScreen /
     *                @ref assignmentEntryForScreen and the per-mode/snapping/
     *                tiling derivatives by building a windowless
     *                @ref PhosphorRules::WindowQuery and evaluating it
     *                through @ref PhosphorRules::RuleEvaluator. Mutators
     *                (@ref assignLayout etc.) translate to context-rule upserts
     *                via @c ContextRuleBridge and persist through the store.
     *                The caller owns the store and must outlive the registry.
     * @param layoutSubdirectory XDG-relative path used for layout JSON
     *                discovery (e.g. @c "plasmazones/layouts"). The registry
     *                writes to
     *                @c QStandardPaths::writableLocation(GenericDataLocation)/\<subdir\>
     *                and reads the union of every @c GenericDataLocation
     *                entry containing that subdirectory, so system copies
     *                (in @c /usr/share/<subdir>) provide built-ins while
     *                the user-writable copy overrides them. Required -
     *                asserted non-empty. Quick-layout slots persist to a
     *                @c quicklayouts.json sibling file in this directory.
     * @param parent Qt parent.
     *
     * @note Required post-construction call order. The constructor does NOT
     *       read the quick-layout sidecar — @ref loadAssignments must be
     *       called once after construction (and after @ref loadLayouts) for
     *       the numbered quick-layout slots to be populated; without it,
     *       @ref layoutForShortcut / @ref applyQuickLayout silently see empty
     *       slots. The per-context assignment cascade itself needs no such
     *       call: the injected @c ruleStore loads its rule set in its own
     *       constructor, so assignment resolution is live immediately.
     *       @ref loadAssignments additionally re-reads the shared rule store
     *       from disk, so composition roots also call it to pick up
     *       cross-process rule edits. The daemon composition root calls
     *       @ref loadLayouts then @ref loadAssignments; the editor / settings
     *       roots, which drive quick slots over D-Bus rather than locally,
     *       deliberately skip it.
     */
    LayoutRegistry(PhosphorRules::RuleStore* ruleStore, QString layoutSubdirectory, QObject* parent = nullptr);
    ~LayoutRegistry() override;

    // ─── IZoneLayoutRegistry ──────────────────────────────────────────────

    int layoutCount() const override
    {
        return m_layouts.size();
    }
    QVector<Layout*> layouts() const override
    {
        return m_layouts;
    }
    Layout* layout(int index) const override;
    Layout* layoutById(const QUuid& id) const override;
    Layout* layoutByName(const QString& name) const override;

    Q_INVOKABLE void addLayout(Layout* layout) override;
    Q_INVOKABLE void removeLayout(Layout* layout) override;
    Q_INVOKABLE void removeLayoutById(const QUuid& id) override;
    Q_INVOKABLE Layout* duplicateLayout(Layout* source) override;

    Layout* activeLayout() const override
    {
        return m_activeLayout;
    }
    Q_INVOKABLE void setActiveLayout(Layout* layout) override;
    Q_INVOKABLE void setActiveLayoutById(const QUuid& id) override;

    // ─── Layout persistence (disk JSON) ───────────────────────────────────

    QString layoutDirectory() const
    {
        return m_layoutDirectory;
    }
    void setLayoutDirectory(const QString& directory);

    Q_INVOKABLE void loadLayouts();
    Q_INVOKABLE void saveLayouts();
    void saveLayout(Layout* layout);

    Q_INVOKABLE void importLayout(const QString& filePath);
    Q_INVOKABLE void exportLayout(Layout* layout, const QString& filePath);

    // ─── Assignment persistence (via config backend) ──────────────────────

    Q_INVOKABLE void loadAssignments();
    Q_INVOKABLE void saveAssignments();

    // ─── Default-layout resolution ────────────────────────────────────────

    /// Resolve the effective default layout. Returns, in order:
    ///   1. The layout whose id matches the default-id provider (if set
    ///      and non-empty).
    ///   2. The first registered layout (by @c defaultOrder).
    ///   3. nullptr if no layouts are registered.
    ///
    /// @note Snap-only fallback. Unlike @ref assignmentIdForScreen and
    /// @ref assignmentEntryForScreen - which on cascade-miss consult
    /// both the snap and autotile level-1 providers - this method only
    /// consults the snap provider, because @ref Layout has no autotile
    /// counterpart. Autotile-mode resolution is the autotile engine's
    /// job, driven by @ref assignmentIdForScreen returning an
    /// @c "autotile:<algo>" id from the level-1 cascade.
    Q_INVOKABLE Layout* defaultLayout() const override;

    /**
     * @brief Inject a callback that returns the user-configured default
     * layout id (or empty if unset).
     *
     * Used by composition roots that own a settings object the lib
     * doesn't know about. The registry invokes the callback on every
     * @ref defaultLayout call (no caching) so the callback can re-read
     * settings on each call. Pass an empty function to disable.
     */
    void setDefaultLayoutIdProvider(std::function<QString()> provider);

    /**
     * @brief Inject a callback that returns the user-configured default
     * autotile algorithm id (or empty if autotile is not the user's
     * active default).
     *
     * Symmetric to @ref setDefaultLayoutIdProvider, completing the
     * level-1 (global) tier of the assignment hierarchy:
     *   1. global default - snap layout id AND autotile algorithm id
     *   2. monitor-level assignment
     *   3. virtual-desktop-level assignment
     *
     * On cascade-miss, @ref assignmentIdForScreen and
     * @ref assignmentEntryForScreen consult the snap provider first,
     * then the autotile provider; the first non-empty return wins.
     * Providers are pass-throughs from the composition root's settings
     * layer - each is expected to return empty when its mode is
     * disabled in settings, which means "autotile-only" users see
     * autotile as the natural cascade fallback (snap provider returns
     * empty → autotile wins) without any mode-priority logic in the
     * composition root. @ref layoutForScreen ignores the autotile
     * provider (it returns a snap @ref Layout*, which has no autotile
     * counterpart) and falls back to @ref defaultLayout as before.
     *
     * Invoked on every cascade-miss with no caching, so providers can
     * re-read settings cheaply per call. Pass an empty function to
     * disable.
     *
     * Thread-safety: the provider is read on every cascade query and
     * swapped via this setter without synchronization; both must run
     * on the same thread (the LayoutRegistry's owner thread, typically
     * the main Qt thread). The same applies to
     * @ref setDefaultLayoutIdProvider.
     */
    void setDefaultAutotileAlgorithmProvider(std::function<QString()> provider);

    /**
     * @brief Inject a callback that returns the tiled-window count for a screen,
     * or std::nullopt when the screen is not actively tiling (so a count
     * predicate stays inert there).
     *
     * The count is fed into the windowless WindowQuery built during
     * @ref resolveAssignmentEntry, letting a SetTilingAlgorithm rule match on
     * @c Field::TiledWindowCount, for example to switch algorithm once a second
     * window opens. The value also participates in that resolver's cache key, so
     * a count change yields a distinct entry rather than a stale hit; the caller
     * (the daemon) re-resolves and re-applies the per-screen algorithm when the
     * count changes (on the engine's placementChanged).
     *
     * The (virtualDesktop, activity) parameters identify the resolution context,
     * but a provider may return the screen's CURRENT-context count when its
     * backing engine only tracks the visible desktop. That is sound because the
     * tiling-algorithm slot is only ever resolved for the screen's current
     * context; a count predicate on a non-current (desktop, activity) is not a
     * supported configuration. Returning nullopt for an unknown context is also
     * valid (the predicate then stays inert).
     *
     * Same threading contract as @ref setDefaultAutotileAlgorithmProvider.
     */
    void setTiledWindowCountProvider(
        std::function<std::optional<int>(const QString& screenId, int virtualDesktop, const QString& activity)>
            provider);

    /**
     * @brief Inject a callback that returns a screen's orientation token
     * ("portrait" / "landscape"), or std::nullopt when the geometry is unknown
     * (so an orientation predicate stays inert there).
     *
     * The token is stamped onto every windowless context WindowQuery this
     * registry builds (assignment, gap, lock, overlay, default-assignment,
     * tiling-params), so an orientation rule can drive any context slot — for example a different
     * tiling algorithm on a rotated (portrait) monitor. Orientation derives from
     * screen geometry alone, independent of the resolved layout, so it carries no
     * recursion risk (unlike an active-layout query). The daemon wires this to
     * @c ScreenManager::screenGeometry. Same threading contract as
     * @ref setTiledWindowCountProvider.
     */
    void setScreenOrientationProvider(std::function<std::optional<QString>(const QString& screenId)> provider);

    /**
     * @brief Inject a callback that returns true when Snapping is the
     * user's preferred default mode (regardless of whether a default
     * snapping layout id is configured).
     *
     * Without this provider, @ref resolveDefaultAssignmentEntry can only
     * tell whether snap has a non-empty default *layout id*. When a user
     * has snapping enabled but never configured a global default layout
     * (a common, valid state - the user expects per-screen assignments
     * to drive everything), the @ref m_defaultLayoutIdProvider returns
     * empty and the cascade silently falls through to the autotile
     * branch - surfacing autotile content (e.g. "Tiling: Binary Split")
     * to a user who never wanted autotile.
     *
     * This provider lets the composition root express "snap mode is
     * preferred" independently of "snap has a default layout". When it
     * returns true, the resolver returns a Snapping entry with the
     * (possibly empty) snappingLayout from
     * @ref m_defaultLayoutIdProvider - `activeLayoutId()` then yields
     * empty, callers see "no assignment", and the OSD path correctly
     * suppresses rather than falling back to autotile.
     *
     * Optional. When unset, the resolver behaves as before
     * (snap-id-non-empty → snap; else autotile if available; else empty).
     * Same threading rules as the other two providers.
     */
    void setSnappingPreferredProvider(std::function<bool()> provider);

    /**
     * @brief Inject a callback that returns true when the user has opted to
     * suppress the synthesized level-1 default layout assignment globally.
     *
     * When the callback returns true, @ref resolveDefaultAssignmentEntry yields
     * a default-constructed (invalid) entry on cascade-miss instead of
     * synthesizing one from the snap / autotile / snapping-preferred providers —
     * i.e. a context with no explicit assignment gets NO active layout and no
     * engine activates until the user assigns one. This is the same effective
     * state as a system with every provider returning empty, so the daemon's
     * existing "empty entry ⇒ no default" handling covers it unchanged.
     *
     * The global baseline is overridable PER CONTEXT by a
     * `DefaultLayoutAssignment` rule (see @ref resolveContextDefaultAssignment):
     * a `false` rule suppresses a single context even when this provider is off,
     * a `true` rule forces the default through even when this provider is on.
     *
     * Optional. When unset, the resolver behaves as before (never suppresses).
     * Same threading rules as the other providers.
     */
    void setDefaultAssignmentSuppressedProvider(std::function<bool()> provider);

    /// True when the snapping-preferred provider is wired AND reports true — i.e.
    /// snapping is globally enabled (the daemon wires the provider to
    /// `ISettings::snappingEnabled`). Mirrors the internal default-assignment
    /// branch's `m_snappingPreferredProvider && m_snappingPreferredProvider()`
    /// test, exposed so other engines can gate cross-engine coordination on the
    /// global snap toggle. When unset, returns false (no provider ⇒ not preferred).
    bool snappingPreferred() const;

    // ─── Assignments (per-context routing) ────────────────────────────────

    /// Get the previous active layout (before the most recent
    /// @ref setActiveLayout). On first call, equals @ref activeLayout.
    /// Used by resnap-to-new-layout.
    Layout* previousLayout() const
    {
        return m_previousLayout;
    }

    Q_INVOKABLE void assignLayout(const QString& screenId, int virtualDesktop, const QString& activity, Layout* layout);
    Q_INVOKABLE void assignLayoutById(const QString& screenId, int virtualDesktop, const QString& activity,
                                      const QString& layoutId);

    /// Store a full entry directly (from KCM via D-Bus). Stores
    /// regardless of @c isValid() - mode-only entries are valid when
    /// explicitly set.
    void setAssignmentEntryDirect(const QString& screenId, int virtualDesktop, const QString& activity,
                                  const AssignmentEntry& entry);

    Q_INVOKABLE Layout* layoutForScreen(const QString& screenId, int virtualDesktop = 0,
                                        const QString& activity = QString()) const override;

    /// Resolve layout for @p screenId using the current desktop/activity
    /// context. @ref layoutForScreen already falls back to
    /// @ref defaultLayout internally when no explicit assignment matches,
    /// so this helper is a thin context-filling forwarder.
    Q_INVOKABLE Layout* resolveLayoutForScreen(const QString& screenId) const override
    {
        // Resolve against THIS screen's current virtual desktop (Plasma 6.7
        // per-output virtual desktops, #648). Falls back to the global desktop
        // when the screen has no per-output value, so every caller (all of which
        // already pass a screenId) gets per-screen resolution with no edits.
        return layoutForScreen(screenId, currentVirtualDesktopForScreen(screenId), m_currentActivity);
    }

    /// Resolve the per-context gap override for (screen, desktop, activity) by
    /// evaluating a windowless WindowQuery through the RuleEvaluator and
    /// reading the gap action slots (ZonePadding / OuterGap /
    /// UsePerSideOuterGap / per-side). Unlike @ref resolveAssignmentEntry this
    /// is a PER-SLOT read across all matching context rules (not a single
    /// winning rule), so independent gap rules compose and there is no
    /// engine-mode gate. Returns an all-unset @ref ContextGapOverride when no
    /// matching rule fills a gap slot. Same owner-thread affinity as the rest
    /// of the registry.
    ContextGapOverride resolveContextGaps(const QString& screenId, int virtualDesktop, const QString& activity,
                                          const QString& mode = QString()) const override;

    /// Resolve whether a context rule locks the active layout for the
    /// (screen, desktop, activity) context by evaluating a windowless
    /// WindowQuery through the RuleEvaluator and reading the
    /// `ActionSlot::Locked` slot. Mode-agnostic per-slot read (mirrors @ref
    /// resolveContextGaps); returns true iff the winning Locked-slot action's
    /// `value` is true. Same owner-thread affinity as the rest of the registry.
    bool resolveContextLocked(const QString& screenId, int virtualDesktop, const QString& activity) const override;

    /// Resolve a per-context override of the global default-layout-assignment
    /// baseline for the (screen, desktop, activity) context by evaluating a
    /// windowless WindowQuery and reading the `ActionSlot::DefaultAssignment`
    /// slot. Per-slot read (mirrors @ref resolveContextLocked): returns the
    /// winning `DefaultLayoutAssignment` action's boolean `value`
    /// (true = allow / force the default through, false = suppress), or
    /// @c std::nullopt when no matching rule fills the slot (the context then
    /// follows the global setting). Same owner-thread affinity as the rest of
    /// the registry.
    std::optional<bool> resolveContextDefaultAssignment(const QString& screenId, int virtualDesktop,
                                                        const QString& activity) const;

    /// Resolve the per-context overlay-property override (shader / style)
    /// for (screen, desktop, activity) by evaluating a windowless WindowQuery and
    /// reading the OverlayShader / OverlayStyle slots. Per-slot
    /// read (mirrors @ref resolveContextGaps), so independent overlay rules
    /// compose; returns an all-unset @ref ContextOverlayOverride when no matching
    /// rule fills an overlay slot. Same owner-thread affinity as the rest of the
    /// registry.
    ContextOverlayOverride resolveContextOverlay(const QString& screenId, int virtualDesktop,
                                                 const QString& activity) const override;

    /// Resolve the per-context autotile parameter overrides (max windows / split
    /// ratio / master count) for (screen, desktop, activity) — a per-slot read
    /// like @ref resolveContextGaps. The daemon layers the returned values onto
    /// the per-screen autotile override map (config stays the base; the rule wins
    /// where present). NOT cached: called on screen / layout changes, not the hot
    /// per-cursor path — which also lets it stamp the active layout onto the query
    /// without a cache-key fold (there is no cache entry to go stale). Concrete
    /// (not on the interface): the daemon holds a concrete LayoutRegistry.
    ContextTilingParams resolveContextTilingParams(const QString& screenId, int virtualDesktop,
                                                   const QString& activity) const;

    /// Stamp the screen-orientation token onto @p query from
    /// @ref m_screenOrientationProvider (a no-op when the provider is unset or
    /// returns nullopt). Called at every windowless-context query build site so a
    /// @c Field::ScreenOrientation predicate can match regardless of which
    /// context slot (assignment / gap / lock / overlay) is being resolved.
    /// Orientation is geometry-derived and layout-independent, so this is safe to
    /// call from the assignment cascade (no recursion, unlike an active-layout read).
    /// The screen-orientation token from @ref m_screenOrientationProvider ("portrait"
    /// / "landscape"), or an empty string when the provider is unset or returns
    /// nullopt. Shared by @ref stampScreenOrientation (the query value) and the
    /// cache-key fold (see @ref contextCacheKeyToken) so both read the same source.
    QString screenOrientationToken(const QString& screenId) const
    {
        if (m_screenOrientationProvider) {
            if (const auto token = m_screenOrientationProvider(screenId)) {
                return *token;
            }
        }
        return QString();
    }

    void stampScreenOrientation(PhosphorRules::WindowQuery& query, const QString& screenId) const
    {
        query.screenOrientation = screenOrientationToken(screenId);
    }

    /// Compose the extra cache-key token a daemon-facing context resolver (gap /
    /// lock / overlay / default-assignment) passes to @ref resolveCachedContext,
    /// folding in the placement @p modeToken (empty for the non-gap resolvers), the
    /// @p activeLayoutId, and the @p orientationToken. Both the active layout AND
    /// the screen orientation are NON-rule-set inputs — each can change without a
    /// rule-set revision bump (the layout via the external global-default provider,
    /// the orientation via a live monitor rotation) — so, exactly like the
    /// tiledWindowCount "twc:" token, they must ride the cache KEY rather than the
    /// value, or the change would return a stale hit. See resolveCachedContext.
    static QString contextCacheKeyToken(const QString& modeToken, const QString& activeLayoutId,
                                        const QString& orientationToken)
    {
        return modeToken + QLatin1String("|al:") + activeLayoutId + QLatin1String("|or:") + orientationToken;
    }

    Q_INVOKABLE void clearAssignment(const QString& screenId, int virtualDesktop = 0,
                                     const QString& activity = QString());
    /// True iff a context-assignment rule whose match is exactly this
    /// (screen, desktop, activity) tuple's shape exists in the rule set —
    /// regardless of the rule's @c enabled state. A DISABLED explicit
    /// assignment is still an explicit assignment: this reports stored
    /// intent, not the effective cascade result. It therefore intentionally
    /// diverges from @ref assignmentEntryForScreen / the resolvers, which
    /// skip disabled rules and fall through to the gated default for a
    /// context whose only rule is disabled.
    Q_INVOKABLE bool hasExplicitAssignment(const QString& screenId, int virtualDesktop = 0,
                                           const QString& activity = QString()) const;

    /// Raw assignment id for a (screen, desktop, activity) context.
    /// Returns the stored string (manual-layout UUID or
    /// @c "autotile:<algorithmId>") without resolving to a @ref Layout*.
    /// On cascade-miss, falls through to the level-1 global defaults
    /// (snap provider first, then autotile provider; see
    /// @ref setDefaultLayoutIdProvider /
    /// @ref setDefaultAutotileAlgorithmProvider). Empty when every
    /// cascade level misses AND both providers return empty. Callers
    /// that need to distinguish "stored" from "synthesized fallback"
    /// must pair this with @ref hasExplicitAssignment.
    Q_INVOKABLE QString assignmentIdForScreen(const QString& screenId, int virtualDesktop = 0,
                                              const QString& activity = QString()) const override;

    /// Full entry for a (screen, desktop, activity) context. Shares
    /// the per-context cascade with @ref layoutForScreen up through
    /// level-2 (per-screen base entry), but the two diverge at level-1
    /// (global defaults): on cascade-miss this method synthesizes from
    /// BOTH providers - snap provider first, then autotile provider -
    /// using the same precedence as @ref assignmentIdForScreen, while
    /// @ref layoutForScreen consults only the snap provider via
    /// @ref defaultLayout. This means a caller mixing both APIs may
    /// see @c entry.mode == @c Autotile with @ref layoutForScreen
    /// returning a snap @ref Layout* (the historical pre-368 fallback
    /// shape, preserved so the autotile engine's
    /// @ref assignmentIdForScreen-driven activation path remains
    /// mode-aware while the snap engine's
    /// @ref layoutForScreen-driven path stays @ref Layout*-typed).
    /// Returns a default-constructed entry when neither provider
    /// returns a value. Callers that need raw stored state without
    /// the synth fallback must gate with @ref hasExplicitAssignment.
    AssignmentEntry assignmentEntryForScreen(const QString& screenId, int virtualDesktop = 0,
                                             const QString& activity = QString()) const;

    AssignmentEntry::Mode modeForScreen(const QString& screenId, int virtualDesktop = 0,
                                        const QString& activity = QString()) const;

    /// True iff the context has NO active layout specifically because the default
    /// assignment is suppressed — globally (see
    /// @ref setDefaultAssignmentSuppressedProvider) or by a per-context
    /// @c DefaultLayoutAssignment rule. Returns false when an active layout exists
    /// (an explicit assignment, or a default forced through by an "allow" rule),
    /// AND false for OTHER empty-assignment states (e.g. snapping enabled with no
    /// global default layout id, where callers still fall back to
    /// @ref defaultLayout). This lets daemon overlay / display paths — which
    /// otherwise fall back to @ref defaultLayout on a missing assignment — treat a
    /// suppressed context as "no layout, engine inactive" without regressing the
    /// no-global-default case. Mode-agnostic: a suppressed context has no layout
    /// for either engine.
    bool isContextActiveLayoutSuppressed(const QString& screenId, int virtualDesktop = 0,
                                         const QString& activity = QString()) const override;

    /// True iff the SYNTHESIZED default (the level-1 provider layout / autotile
    /// algorithm) is suppressed for this context — a per-context
    /// @c DefaultLayoutAssignment override decides locally (suppress → true,
    /// allow → false), otherwise the global suppress setting. Unlike
    /// @ref isContextActiveLayoutSuppressed this does NOT consider whether a
    /// mode-only assignment rule covers the context: such a rule sets the mode
    /// but still draws its layout / algorithm from the default. The autotile
    /// activation path uses this to refuse to tile a bare @c "autotile:" context
    /// (mode set, no concrete algorithm) with the global default algorithm when
    /// the default is suppressed — a concrete assigned algorithm is explicit and
    /// always tiles.
    bool isDefaultAssignmentSuppressedForContext(const QString& screenId, int virtualDesktop = 0,
                                                 const QString& activity = QString()) const;

    /// Per-field cascade readers — return the named field from the first
    /// entry in the cascade where it is non-empty. Crucially, these do
    /// NOT route through the @c activeLayoutId-based reject filter that
    /// @ref assignmentEntryForScreen uses, so a "stored-but-inactive"
    /// preference (e.g. an entry shaped like
    /// {mode=Snapping, snap="", tile="cluster"} that the partial-update
    /// path can produce) IS visible to the field-getter that targets
    /// the populated field. On total cascade miss they synthesize from
    /// the global default (snap provider for snap, autotile provider
    /// for tile) and may still return empty if no provider has a value.
    QString snappingLayoutForScreen(const QString& screenId, int virtualDesktop = 0,
                                    const QString& activity = QString()) const;
    QString tilingAlgorithmForScreen(const QString& screenId, int virtualDesktop = 0,
                                     const QString& activity = QString()) const override;

    /// Flip mode to @c Snapping for every entry currently in @c Autotile
    /// (preserves @c snappingLayout + @c tilingAlgorithm). Emits
    /// @c layoutAssigned per affected screen; one save at end.
    void clearAutotileAssignments();

    /// Batch setters - clear existing, set new, save once at end.
    void setAllScreenAssignments(const QHash<QString, QString>& assignments);
    void setAllDesktopAssignments(const QHash<QPair<QString, int>, QString>& assignments);
    void setAllActivityAssignments(const QHash<QPair<QString, QString>, QString>& assignments);
    /// Combined-context (screen + desktop + activity) batch setter — the
    /// triple-axis sibling of the Activity / Desktop batches. Combined
    /// rules cannot round-trip through `setAllActivityAssignments` (the
    /// (screen, activity) key drops the desktop dimension); this API
    /// keeps them as first-class. Pure-Activity, pure-Desktop, and
    /// Monitor-only rules are NOT touched — they remain in their own
    /// batches.
    void setAllCombinedAssignments(const QHash<CombinedAssignmentKey, QString>& assignments);

    QHash<QPair<QString, int>, QString> desktopAssignments() const;
    QHash<QPair<QString, QString>, QString> activityAssignments() const;
    /// Strict Combined-context reader. See @ref setAllCombinedAssignments
    /// for the round-trip contract. Returns ONLY rules with all three
    /// dimensions pinned (screen + desktop + activity).
    QHash<CombinedAssignmentKey, QString> combinedAssignments() const;

    // ─── Quick-layout slots (1..9) ────────────────────────────────────────
    //
    // Quick slots are keyed by tiling mode: Snapping slots hold manual-layout
    // UUIDs, Autotile slots hold autotile algorithm IDs. The two sets are
    // independent so the same Meta+Alt+N can map to a zone layout in snapping
    // mode and an autotile algorithm in autotile mode.

    /// quicklayouts.json top-level keys: one nested slot object per tiling
    /// mode. This is the ONLY on-disk shape — there is no flat legacy variant.
    /// Shared with the v3→v4 schema migration (configmigration.cpp), which
    /// writes the same nested format, so reader and migration cannot drift.
    static constexpr QLatin1String QuickSlotsSnappingKey{"snapping"};
    static constexpr QLatin1String QuickSlotsAutotileKey{"autotile"};

    Q_INVOKABLE Layout* layoutForShortcut(AssignmentEntry::Mode mode, int number) const;
    Q_INVOKABLE void applyQuickLayout(AssignmentEntry::Mode mode, int number, const QString& screenId);
    void setQuickLayoutSlot(AssignmentEntry::Mode mode, int number, const QString& layoutId);
    void setAllQuickLayoutSlots(AssignmentEntry::Mode mode, const QHash<int, QString>& slots);
    QHash<int, QString> quickLayoutSlots(AssignmentEntry::Mode mode) const
    {
        return m_quickLayoutSlots[modeIndex(mode)];
    }

    // ─── Layout cycling ───────────────────────────────────────────────────

    Q_INVOKABLE void cycleToPreviousLayout(const QString& screenId);
    Q_INVOKABLE void cycleToNextLayout(const QString& screenId);

    // ─── Built-in layouts ─────────────────────────────────────────────────

    Q_INVOKABLE void createBuiltInLayouts();
    QVector<Layout*> builtInLayouts() const;

    // ─── Context (desktop / activity) ─────────────────────────────────────

    Q_INVOKABLE int currentVirtualDesktop() const override
    {
        return m_currentVirtualDesktop;
    }
    Q_INVOKABLE QString currentActivity() const override
    {
        return m_currentActivity;
    }
    void setCurrentVirtualDesktop(int desktop)
    {
        m_currentVirtualDesktop = desktop;
    }
    /// This screen's current virtual desktop, falling back to the global
    /// m_currentVirtualDesktop when no per-output value is set (#648).
    int currentVirtualDesktopForScreen(const QString& screenId) const override
    {
        const auto it = m_screenVirtualDesktop.constFind(screenId);
        return it != m_screenVirtualDesktop.constEnd() ? it.value() : m_currentVirtualDesktop;
    }
    /// Record a single screen's current virtual desktop (per-output virtual
    /// desktops). Pushed by the daemon's per-screen desktop handler.
    void setCurrentVirtualDesktopForScreen(const QString& screenId, int desktop)
    {
        if (!screenId.isEmpty() && desktop >= 1) {
            m_screenVirtualDesktop.insert(screenId, desktop);
        }
    }
    /// Drop a screen's per-output desktop, reverting it to the global value.
    void clearCurrentVirtualDesktopForScreen(const QString& screenId)
    {
        m_screenVirtualDesktop.remove(screenId);
    }
    void setCurrentActivity(const QString& activity)
    {
        m_currentActivity = activity;
    }

    // ─── Autotile layout overrides (per-algorithm user overrides) ─────────

    void saveAutotileOverrides(const QString& algorithmId, const QJsonObject& overrides);
    QJsonObject loadAutotileOverrides(const QString& algorithmId) const override;

    /// Seed curated default picker visibility into the sidecar, but ONLY on a
    /// fresh install — when neither layout-settings.json nor the legacy
    /// autotile-overrides.json exists. @p defaults is keyed exactly as the
    /// sidecar (manual layouts by UUID, algorithms by "autotile:<id>"). Existing
    /// installs are never reseeded. Call before @ref loadLayouts so the merge
    /// picks up the seeded entries.
    void seedDefaultLayoutSettingsIfFresh(const QJsonObject& defaults);

    /// Current entry count in the @ref resolveAssignmentEntry hot-path cache.
    /// Used by tests to verify the cache populates and invalidates against
    /// rule-set revision bumps. Not for production callers — the value
    /// drifts as cursor-move resolves come in. Return type is `int` because
    /// the cache is bounded at 256 entries (`kMaxEntries` in the shared
    /// @ref resolveCachedContext template below), well within `int` range —
    /// keeps test assertions free of the `qsizetype` `int` widening dance.
    [[nodiscard]] int contextResolveCacheSize() const
    {
        return static_cast<int>(m_contextResolveCache.size());
    }

Q_SIGNALS:
    // layoutAdded / layoutRemoved / activeLayoutChanged / layoutAssigned are
    // inherited from IZoneLayoutRegistry - declared on the interface so
    // consumers that target the contract can connect without depending on
    // the concrete class. Emit sites still live in this TU and reach the
    // inherited signal via Q_OBJECT's metaobject chain.
    void layoutsChanged();
    void layoutDirectoryChanged();
    void layoutsLoaded();
    void layoutsSaved();

private:
    /// Post-construction setup: path validation, evaluator binding, signal
    /// wiring.
    void initCommon();

    void ensureLayoutDirectory();
    void loadLayoutsFromDirectory(const QString& directory);
    Layout* restoreSystemLayout(const QUuid& id, const QString& systemPath);
    QString layoutFilePath(const QUuid& id) const;
    QString quickLayoutsFilePath() const;
    QString layoutSettingsFilePath() const;
    void readQuickLayouts();
    void writeQuickLayouts();
    /// Map a tiling mode to its @ref m_quickLayoutSlots array index.
    /// Only Snapping and Autotile carry quick slots; any other value
    /// clamps to Snapping.
    static constexpr int modeIndex(AssignmentEntry::Mode mode)
    {
        return mode == AssignmentEntry::Autotile ? 1 : 0;
    }
    Layout* cycleLayoutImpl(const QString& screenId, int direction);
    bool shouldSkipLayoutAssignment(const QString& layoutId, const QString& context) const;
    void emitLayoutAssigned(const QString& screenId, int virtualDesktop, const QString& layoutId);
    /**
     * @brief Commit a targeted per-screen layout switch (from a quick-slot
     * shortcut, layout cycle, or any caller that wants to change ONLY the
     * named screen - not fan activeLayoutChanged across every screen).
     *
     * Writes the per-desktop assignment for @p screenId with empty activity
     * (so D-Bus/KCM lookups using empty activity see the entry), clears any
     * stale activity-keyed assignment that would shadow it in the cascade,
     * then updates the global active-layout pointer under a QSignalBlocker
     * so @ref activeLayoutChanged does NOT fire - preventing resnap on the
     * other screens. Equivalent to the write+update pattern both
     * applyQuickLayout and cycleLayoutImpl were open-coding before the
     * extraction.
     */
    void applyLayoutToScreen(const QString& screenId, Layout* layout);
    /// One-time idempotent fold of the retired autotile-overrides.json into the
    /// unified layout-settings.json sidecar; deletes the legacy file when done.
    void migrateLegacyAutotileOverrides();

    // ─── Rule-backed assignment resolution ────────────────────────────────
    // The unified RuleStore is the single source of truth for every
    // per-context assignment. These helpers translate the legacy
    // AssignmentEntry API onto the rule store + evaluator.

    /// Resolve the AssignmentEntry for a context by evaluating a windowless
    /// WindowQuery through the RuleEvaluator and reading the engine-mode /
    /// layout / tiling action slots of the resolved action set. The winner of
    /// each slot is the highest-priority matching rule (priority wins, ties by
    /// list order). Returns nullopt when no rule of any shape fills any of the
    /// three slots, so a genuine miss stays distinguishable and routes to the
    /// gated default. @p screenId is taken verbatim — connector / VS fallback is
    /// the caller's (layoutForScreen) retry loop.
    ///
    /// Hot-path cache: the result is memoized in @c m_contextResolveCache keyed
    /// by (screenId, virtualDesktop, activity) plus a "twc:N|or:<token>"
    /// composite of the tiled-window-count and screen-orientation tokens (each
    /// empty when unknown), so a count or rotation change yields a fresh entry
    /// rather than a stale hit while steady callers keep hitting the
    /// cache. The cache is invalidated
    /// lazily by comparing the bound rule set's monotonic
    /// @c RuleSet::revision() against the snapshot taken on the last
    /// insert — a mismatch clears the whole map before falling through to the
    /// linear walk. No explicit signal-time clear is required: a real edit
    /// bumps the revision (see @c RuleSet::setRules), so the next
    /// resolve sees the bump and re-populates. A soft cap (256 entries — the
    /// @c kMaxEntries constant in the shared @ref resolveCachedContext template,
    /// applied uniformly to every context cache) guards against pathological
    /// growth from clients probing unique
    /// non-existent tuples; on overflow the cache is cleared entirely (the
    /// next walk re-seeds it cleanly). 256 sits comfortably above any
    /// realistic live (screens × desktops × activities) footprint and far
    /// below any heap-pressure concern.
    ///
    /// Thread-affinity: like the rest of @c LayoutRegistry, this method
    /// (and the cache it mutates through @c mutable members) must only be
    /// called on the registry's owner thread (typically the main Qt thread).
    /// The cache itself takes no lock; concurrent calls are not supported.
    std::optional<AssignmentEntry> resolveAssignmentEntry(const QString& screenId, int virtualDesktop,
                                                          const QString& activity) const;

    /// True if a rule whose match is exactly the context shape
    /// (context-only All{ScreenId==,VirtualDesktop==,Activity==} for the
    /// pinned dims) exists in the rule set and carries an engine-mode action.
    /// A disabled rule still counts — see @ref findExactContextRule.
    bool hasExactContextRule(const QString& screenId, int virtualDesktop, const QString& activity) const;

    /// Find the exact-shape context rule for a (screen, desktop, activity)
    /// tuple, or nullptr if none exists. The returned pointer aliases into the
    /// borrowed rule set and is valid only until the next rule-set mutation.
    /// This is the single exact-shape scan — @ref exactContextRuleId and
    /// @ref hasExactContextRule are thin wrappers, and the assignment mutators
    /// read the winning rule's actions through it directly (so a wider cascade
    /// entry never bleeds its tilingAlgorithm into a new narrower rule).
    /// The scan ignores the rule's @c enabled state (so an upsert updates a
    /// disabled rule in place rather than appending a duplicate) but rejects
    /// any match carrying a window-property leaf — only a pure context-only
    /// match is an exact context rule.
    const PhosphorRules::Rule* findExactContextRule(const QString& screenId, int virtualDesktop,
                                                    const QString& activity) const;

    /// Find the id of the exact-shape context rule for a (screen, desktop,
    /// activity) tuple, or a null QUuid if none exists.
    QUuid exactContextRuleId(const QString& screenId, int virtualDesktop, const QString& activity) const;

    /// Upsert a context assignment rule: replace the exact-shape rule if one
    /// exists, else add a new one. Persists through the store.
    void upsertAssignmentRule(const QString& screenId, int virtualDesktop, const QString& activity,
                              const AssignmentEntry& entry);

    /// Remove the exact-shape context assignment rule for a tuple, if any.
    /// Returns true if a rule was removed.
    bool removeAssignmentRule(const QString& screenId, int virtualDesktop, const QString& activity);

    /// Shared driver behind the three @c setAll*Assignments batch setters:
    /// snapshot existing exact-context entries, drop the addressed rule
    /// family, rebuild it from @p assignments, commit once, and emit one
    /// @c layoutAssigned per stored screen. @p decode maps a hash key to its
    /// cascade context, @p valid rejects an ill-formed context for the
    /// family, @p familyMatches selects which existing rules to drop,
    /// @p emitDesktop / @p emitActivity are the context the closing
    /// @c layoutAssigned signal is computed under, and @p label names the
    /// family in log output. Only ever instantiated from
    /// layoutregistry_batch.cpp, where it is defined alongside its callers.
    template<typename KeyT, typename DecodeFn, typename ValidFn, typename FamilyFn>
    void applyBatchAssignments(const QHash<KeyT, QString>& assignments, DecodeFn decode, ValidFn valid,
                               FamilyFn familyMatches, int emitDesktop, const QString& emitActivity, const char* label);

    /// Drop @p layoutId from every assignment rule's @c SetSnappingLayout
    /// action when a snap layout is deleted. A rule that still carries
    /// meaningful intent (an Autotile engine-mode, or a preserved
    /// tilingAlgorithm) is rebuilt with only the snapping layout cleared —
    /// the mode + tiling intent survives, preserving mode-toggle
    /// losslessness. A rule left with nothing but a default (Snapping)
    /// engine-mode is dropped entirely. Returns true if the rule set changed.
    bool purgeSnappingLayoutFromAssignments(const QString& layoutId);

    /// Synthesize the level-1 global default into an AssignmentEntry,
    /// IGNORING the global suppress setting. Three-tier precedence:
    ///   1. snapping-preferred provider (true → return Snapping with
    ///      possibly-empty layout, suppressing the autotile fallthrough
    ///      so users in snapping mode don't see autotile OSD content);
    ///   2. snap-id provider (non-empty → return Snapping with that id);
    ///   3. autotile provider (non-empty → return AutoTile with that
    ///      algorithm).
    /// Returns a default-constructed (invalid) entry when no provider
    /// has a value. The "raw" sibling of @ref resolveDefaultAssignmentEntry —
    /// used directly only by the per-context "allow" override, which must
    /// force the default through even when the global suppress gate is on.
    AssignmentEntry resolveDefaultAssignmentEntryRaw() const;

    /// Resolve the level-1 global default into an AssignmentEntry on
    /// cascade-miss, HONORING the global suppress setting. When the
    /// default-assignment-suppressed provider (see
    /// @ref setDefaultAssignmentSuppressedProvider) returns true, yields a
    /// default-constructed (invalid) entry — no synthesized default. Otherwise
    /// delegates to @ref resolveDefaultAssignmentEntryRaw.
    AssignmentEntry resolveDefaultAssignmentEntry() const;

    /// Resolve the level-1 default for a specific context, applying the
    /// per-context @c DefaultLayoutAssignment override (if any) over the global
    /// suppress baseline. A `false` override returns an invalid entry (suppress
    /// this context), a `true` override returns @ref resolveDefaultAssignmentEntryRaw
    /// (force the default through), and no override defers to
    /// @ref resolveDefaultAssignmentEntry (follow the global setting). This is
    /// the single cascade-miss tail every per-context resolver routes through so
    /// the override and the global gate can never drift between call sites.
    AssignmentEntry resolveDefaultAssignmentEntryForContext(const QString& screenId, int virtualDesktop,
                                                            const QString& activity) const;

    /// True iff an enabled engine-mode assignment rule matches the (screen,
    /// desktop, activity) context — i.e. the user authored an explicit
    /// per-context assignment, even one that sets only the mode with
    /// no layout. Such a rule overrides the global suppress setting (the
    /// context is managed, never suppressed). Mirrors the connector /
    /// virtual-screen fallback chain of @ref assignmentIdForScreen so a rule
    /// keyed by the physical/connector id still matches a virtual-screen query.
    bool hasMatchingAssignmentRule(const QString& screenId, int virtualDesktop, const QString& activity) const;

    /// Lookup key for @c m_contextResolveCache. Mirrors the parameters of
    /// @ref resolveAssignmentEntry — three independent context dimensions
    /// the windowless cascade walks (screen id, virtual desktop, activity).
    /// Field-equal + per-field hash composition is enough: every dimension is
    /// part of the cascade identity.
    struct ContextResolveKey
    {
        QString screenId;
        int virtualDesktop = 0;
        QString activity;
        // A free-form fourth key dimension, used by the context resolvers that
        // share the ContextResolveKey type but never the same cache container. It
        // folds in every non-rule-set input the resolved value depends on, so a
        // change in one yields a fresh entry rather than a stale hit:
        //   - gap cascade: contextCacheKeyToken(mode, activeLayout, orientation) —
        //     the SAME (screen, desktop, activity) resolves DIFFERENT gaps per
        //     placement mode, active layout, and screen orientation.
        //   - lock / default-assignment / overlay: contextCacheKeyToken with an
        //     empty mode (they are mode-agnostic) plus activeLayout (except
        //     default-assignment, which omits it to avoid recursion) and orientation.
        //   - assignment resolver: "twc:N|or:<token>" — the tiled-window-count and
        //     the screen orientation (it does not read the active layout).
        // Each resolver owns its own cache hash, so the token vocabularies never
        // collide.
        QString mode;
        bool operator==(const ContextResolveKey& other) const noexcept
        {
            return virtualDesktop == other.virtualDesktop && screenId == other.screenId && activity == other.activity
                && mode == other.mode;
        }
    };
    friend size_t qHash(const LayoutRegistry::ContextResolveKey& key, size_t seed) noexcept
    {
        // ::qHash routes to the global Qt qHash overloads — without the leading
        // qualifier ADL would pick up @c qHash(LayoutAssignmentKey&) (declared
        // in @c AssignmentEntry.h alongside this header) and fail to convert
        // each field. Mirrors the same pattern @c LayoutAssignmentKey itself uses.
        size_t h = seed;
        h = ::qHash(key.screenId, h);
        h = ::qHash(key.virtualDesktop, h);
        h = ::qHash(key.activity, h);
        h = ::qHash(key.mode, h);
        return h;
    }

    /// Shared revision-invalidated memoization for the five context resolvers
    /// (@ref resolveAssignmentEntry, @ref resolveContextGaps,
    /// @ref resolveContextLocked, @ref resolveContextDefaultAssignment,
    /// @ref resolveContextOverlay). Drops @p cache wholesale whenever the rule
    /// set's monotonic revision moves past @p cacheRevision, returns a cached
    /// hit, else runs @p compute, then soft-caps the cache at 256 entries —
    /// dropping the whole cache on overflow rather than evicting one key, which
    /// keeps the structure simple and lets the next walk re-seed cleanly — and
    /// stores the result (a @c nullopt / false value is cached too, so a genuine
    /// miss pays the walk once per revision, not on every cursor frame). Callers
    /// that must short-circuit on a null evaluator do so BEFORE calling this.
    template<typename V, typename ComputeFn>
    V resolveCachedContext(QHash<ContextResolveKey, V>& cache, quint64& cacheRevision, const QString& screenId,
                           int virtualDesktop, const QString& activity, const QString& mode, ComputeFn&& compute) const
    {
        const quint64 revision = m_ruleStore->ruleSet().revision();
        if (revision != cacheRevision) {
            cache.clear();
            cacheRevision = revision;
        }
        const ContextResolveKey key{screenId, virtualDesktop, activity, mode};
        const auto cached = cache.constFind(key);
        if (cached != cache.constEnd()) {
            return cached.value();
        }
        V value = compute();
        constexpr qsizetype kMaxEntries = 256;
        if (cache.size() >= kMaxEntries) {
            cache.clear();
        }
        cache.insert(key, value);
        return value;
    }

    /// The rule-derived slot resolution cached by @ref resolveAssignmentEntry.
    /// Holds ONLY what the rule set produced for each of the three independent
    /// slots. Given a fixed cache key the value is a pure function of the rule
    /// set (the cache's revision-invalidation contract). The live tiled-window
    /// count and the screen orientation are the non-rule-set inputs that affect
    /// the result; rather than break that contract they participate in the cache
    /// KEY (the "twc:N" and "|or:<token>" components), so each combination resolves
    /// its own entry. The global default — an external
    /// provider, not part of the rule set and not revision-tracked — is folded
    /// in AFTER the cache returns, so a default-setting change is reflected
    /// immediately without a rule-set revision bump (a settings edit produces
    /// none). @c modeEntry is engaged when an engine-mode rule won (it carries
    /// that rule's mode plus its own layout tokens); when disengaged the caller
    /// bases the entry on the live global default for the context.
    /// @c snappingLayout / @c tilingAlgorithm are engaged when a layout rule
    /// filled that slot, and override the base's field.
    struct RuleSlotResolution
    {
        std::optional<AssignmentEntry> modeEntry;
        std::optional<QString> snappingLayout;
        std::optional<QString> tilingAlgorithm;
    };

    /// Cache of @ref resolveAssignmentEntry's rule-derived resolution keyed by
    /// context tuple. A @c nullopt value caches a genuine cascade miss (no rule
    /// filled any slot) — so a missed lookup pays the linear walk exactly once
    /// per rule-set revision, not three times per cursor-move (the
    /// connector / virtual-screen fallback chain in
    /// @ref assignmentEntryForScreen drives the same miss into three lookups).
    mutable QHash<ContextResolveKey, std::optional<RuleSlotResolution>> m_contextResolveCache;
    /// The rule-set revision the cache contents are valid for. The cache is
    /// dropped wholesale whenever this no longer matches
    /// @c m_ruleStore->ruleSet().revision() — see @ref resolveAssignmentEntry.
    mutable quint64 m_contextResolveCacheRevision = 0;

    /// Hot-path cache for @ref resolveContextGaps, keyed and revision-invalidated
    /// exactly like @c m_contextResolveCache. Separate cache because gaps read a
    /// per-slot ResolvedActions walk (not the single-winner assignment walk),
    /// and the geometry path resolves the same (screen, desktop, activity) tuple
    /// twice per op (padding + outer gaps) — and N× inside a multi-zone snap —
    /// so memoizing collapses those repeats to one walk per rule-set revision.
    mutable QHash<ContextResolveKey, ContextGapOverride> m_contextGapCache;
    mutable quint64 m_contextGapCacheRevision = 0;

    /// Hot-path cache for @ref resolveContextLocked, keyed and
    /// revision-invalidated exactly like @c m_contextGapCache. The lock check
    /// runs on overlay/selector updates (per cursor-move while a selector is
    /// open) as well as every layout-switch attempt, so memoizing the per-slot
    /// walk collapses repeats to one walk per rule-set revision.
    mutable QHash<ContextResolveKey, bool> m_contextLockCache;
    mutable quint64 m_contextLockCacheRevision = 0;

    /// Hot-path cache for @ref resolveContextDefaultAssignment, keyed and
    /// revision-invalidated exactly like @c m_contextLockCache. The override is
    /// read on every cascade-miss in @ref assignmentEntryForScreen /
    /// @ref assignmentIdForScreen (and their connector / virtual-screen retries),
    /// so memoizing the per-slot walk collapses repeats to one walk per rule-set
    /// revision. A @c std::nullopt value (no override rule) is cached too.
    mutable QHash<ContextResolveKey, std::optional<bool>> m_contextDefaultAssignmentCache;
    mutable quint64 m_contextDefaultAssignmentCacheRevision = 0;

    /// Hot-path cache for @ref resolveContextOverlay, keyed and
    /// revision-invalidated exactly like @c m_contextGapCache. The overlay
    /// build path resolves the same (screen, desktop, activity) tuple on every
    /// overlay show / screen-change, so memoizing the per-slot walk collapses
    /// repeats to one walk per rule-set revision.
    mutable QHash<ContextResolveKey, ContextOverlayOverride> m_contextOverlayCache;
    mutable quint64 m_contextOverlayCacheRevision = 0;

    std::function<QString()> m_defaultLayoutIdProvider; ///< Empty = provider disabled; falls back to first layout
    /// Empty = provider disabled. Returns the user's default autotile
    /// algorithm id when autotile is the active mode; returns empty
    /// when autotile is disabled (composition root's responsibility).
    /// Symmetric to @c m_defaultLayoutIdProvider; together they form
    /// the level-1 cascade tier.
    std::function<QString()> m_defaultAutotileAlgorithmProvider;
    /// Empty = provider unset. Returns the tiled-window count for a context
    /// (or nullopt when it is not actively tiling), fed into the windowless
    /// query during @ref resolveAssignmentEntry so a SetTilingAlgorithm rule
    /// can match @c Field::TiledWindowCount. See @ref setTiledWindowCountProvider.
    std::function<std::optional<int>(const QString& screenId, int virtualDesktop, const QString& activity)>
        m_tiledWindowCountProvider;
    /// Empty = provider unset. Returns a screen's orientation token
    /// ("portrait" / "landscape"), or nullopt when geometry is unknown. Stamped
    /// onto every windowless context query so a Field::ScreenOrientation predicate
    /// can match. See @ref setScreenOrientationProvider.
    std::function<std::optional<QString>(const QString& screenId)> m_screenOrientationProvider;
    /// Empty = provider unset (legacy behaviour). Returns true when
    /// the user has snapping mode enabled in settings, regardless of
    /// whether a global default snap layout id is configured. See
    /// @ref setSnappingPreferredProvider for the rationale.
    std::function<bool()> m_snappingPreferredProvider;
    /// Empty = provider unset (never suppresses). Returns true when the user has
    /// globally opted to suppress the synthesized level-1 default assignment, so
    /// a context with no explicit assignment gets no active layout until the
    /// user assigns one. See @ref setDefaultAssignmentSuppressedProvider.
    std::function<bool()> m_defaultAssignmentSuppressedProvider;
    /// Borrowed unified rule store — the single assignment authority. The
    /// caller owns the store and must outlive the registry; always non-null
    /// (the ctor asserts it).
    PhosphorRules::RuleStore* m_ruleStore = nullptr;
    /// Evaluator bound to m_ruleStore->ruleSet(); the one resolution model.
    std::unique_ptr<PhosphorRules::RuleEvaluator> m_evaluator;
    QString m_layoutSubdirectory; ///< XDG-relative (e.g. "plasmazones/layouts") - drives locateAll discovery
    QString m_layoutDirectory; ///< Absolute user-writable path derived from m_layoutSubdirectory
    QVector<Layout*> m_layouts;
    Layout* m_activeLayout = nullptr;
    Layout* m_previousLayout = nullptr; ///< Active layout before last setActiveLayout (for resnap)
    /// Quick-layout slots keyed by mode: index 0 = Snapping (zone-layout
    /// UUIDs), index 1 = Autotile (autotile algorithm IDs). Each maps slot
    /// number (1..9) → layout/algorithm ID. See @ref modeIndex.
    QHash<int, QString> m_quickLayoutSlots[2];
    /// Per-layout settings sidecar (layout-settings.json), keyed by layout UUID.
    /// Settings are split out of the structural layout file on save and merged
    /// back in on load — see layoutregistry_persistence.cpp.
    LayoutSettingsStore m_layoutSettings;
    int m_currentVirtualDesktop = 1;
    /// Per-screen current virtual desktop (screenId → 1-based) under Plasma 6.7
    /// per-output virtual desktops (#648). Empty unless the daemon pushes
    /// per-screen values, so resolveLayoutForScreen falls back to the global.
    QHash<QString, int> m_screenVirtualDesktop;
    QString m_currentActivity;
};

} // namespace PhosphorZones

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorScreens/ScreenIdentity.h>

#include "enginelimits.h"
#include "scrollenginelogging.h"

#include <QTimer>
#include <QVariant>

#include <algorithm>
#include <utility>

namespace PhosphorScrollEngine {

void ScrollEngine::seedFloatRestoreForOpen(const QString& windowId, int minWidth, int minHeight)
{
    // windowMinimumSize reads the clamp out of this entry for every window
    // that is floated rather than tiled, and the cross-engine handoff queries
    // it whatever state the window is in: with no entry the answer is the
    // "unknown" one, and the receiving engine gets an unclamped window. That
    // bites hardest on exactly the windows these paths float — the oversized
    // ones, whose clamp is why they could not take a column in the first
    // place.
    const auto existing = m_floatRestore.find(windowId);
    if (existing != m_floatRestore.end()) {
        // A real remembered slot is worth more than a slotless seed; only
        // the clamp is refreshed.
        existing->minWidth = qMax(0, minWidth);
        existing->minHeight = qMax(0, minHeight);
        return;
    }
    FloatRestore restore;
    restore.column = -1; // no slot to go back to; unfloat opens a fresh column
    restore.minWidth = qMax(0, minWidth);
    restore.minHeight = qMax(0, minHeight);
    m_floatRestore.insert(windowId, restore);
}

void ScrollEngine::emitGatedFloatGeometryRestore(const QString& windowId, const PhosphorEngine::WindowPlacement& record,
                                                 const QString& screenId)
{
    // SCREEN-LOCAL recorded position only, for autotile's documented reason: a
    // rect captured on a different screen would teleport the window while the
    // float tracking points elsewhere. The move itself is gated (daemon-wired
    // scrollingRestoreFloatedWindowsOnLogin setting + per-window
    // RestorePosition rule) while the floating MARK is not — the callers mark
    // unconditionally and only the geometry comes through here.
    //
    // Shared by the two restore paths (insertOpenedWindow's record-float branch
    // and restoreFloatRecordForOpen) because they are one rule with two entry
    // points, and a change to the gate that reached only one of them would
    // restore the position on one open path and not the other.
    const QString restoreScreen = record.screenId.isEmpty() ? screenId : record.screenId;
    const QRect freeGeo = record.freeGeometryFor(restoreScreen);
    const bool restorePosition = !m_restorePositionPredicate || m_restorePositionPredicate(windowId);
    if (freeGeo.isValid() && restorePosition) {
        Q_EMIT geometryRestoreRequested(windowId, freeGeo, restoreScreen);
    }
}

void ScrollEngine::restoreFloatRecordForOpen(const QString& windowId, const QString& screenId)
{
    // Registry answer, not a parse: a canonical id frozen before KWin resolved
    // the class has no appId to parse, and this gate would then silently skip
    // the float restore for the window's whole life.
    const QString appId = currentAppIdFor(windowId);
    if (!m_windowTracker || !PhosphorEngine::hasStableAppIdFor(appId, windowId)) {
        return;
    }
    // The window floats regardless (the caller already decided that), so only
    // a FLOATING record is consumed — takeForReopen's contract. The accept
    // itself is the store's shared predicate.
    const PhosphorEngine::PlacementStateKey key = currentKeyForScreen(screenId);
    const auto record = m_windowTracker->placementStore().takeForReopen(engineId(), windowId, appId, key.screenId);
    if (!record) {
        return;
    }
    emitGatedFloatGeometryRestore(windowId, *record, screenId);
}

bool ScrollEngine::insertOpenedWindow(ScrollState* state, const QString& windowId, const QString& screenId,
                                      int minWidthIn, int minHeightIn, ScrollOpenParams* outOpenParams)
{
    // Public-API belt at the one boundary the update path already guards:
    // windowMinSizeUpdated clamps because "a negative floor flows into
    // Tile::minWidth/minHeight, and the relayout slack math is not written
    // for one" — the open path feeds the same five sinks (both insert
    // shapes, the float-restore seed and the stash-restore inserts) and let
    // raw values through. The in-tree daemon forwards KWin's minimum size
    // and never sends a negative, so this is a belt for embedders.
    const int minWidth = qMax(0, minWidthIn);
    const int minHeight = qMax(0, minHeightIn);
    const ScrollLayoutParams params = layoutParamsForScreen(screenId);
    // ONE fetch for the whole open path. Five effective* values are resolved
    // out of this map below (sticky handling, default display, the two
    // client-decides verdicts and the insert position) plus the template
    // blueprint, and the screenId-taking wrappers would each rebuild it.
    const QVariantMap screenOverrides = overridesForScreen(screenId);

    // Fixed-size / oversized windows cannot honour a column slot: float them
    // at their native size instead of forcing a tile (the min size is the
    // only constraint the compositor reports; a rule `float` action covers
    // the rest).
    const bool oversized =
        params.workArea.isValid() && (minWidth > params.workArea.width() || minHeight > params.workArea.height());
    const bool ruleFloated = m_floatPredicate && m_floatPredicate(windowId, screenId);
    // Sticky handling gates insertion only: RestoreOnly and IgnoreAll both
    // keep sticky windows out of the strip, because insertion is active
    // management (autotile's shouldTileWindow makes the same collapse). The
    // desktop-pin logic in updateStickyScreenPins stays unconditional — with
    // sticky windows floated, the all-sticky managed set never forms and the
    // pin degrades correctly on its own.
    const bool stickyExcluded =
        effectiveStickyWindowHandling(screenOverrides) != PhosphorEngine::StickyWindowHandling::TreatAsNormal
        && m_windowTracker && m_windowTracker->isWindowSticky(windowId);
    if (oversized || ruleFloated || stickyExcluded) {
        state->addFloating(windowId);
        seedFloatRestoreForOpen(windowId, minWidth, minHeight);
        // Engine-decided float, so it carries the mode marker like every
        // other float this engine makes: isModeSpecificFloated has to answer
        // true or the daemon captures the scroll-mode float into the snap
        // slot at the next mode transition (presaveSnapFloats skips exactly
        // the marked windows).
        m_scrollFloatedWindows.insert(windowId);
        // A floated arrival consumes its seed entry too, or the screen's
        // list never empties and the stale entry survives every later mode
        // transition.
        consumePendingInitialOrder(screenId, windowId);
        // An engine-decided float still consumes its FLOATING placement
        // record and restores the remembered float-back — autotile reaches
        // the same outcome through its record branch (record first, rule
        // float layered on top); this engine floats before ever consulting
        // the store, so the consumption happens here or never.
        restoreFloatRecordForOpen(windowId, screenId);
        Q_EMIT windowFloatingStateSynced(windowId, true, screenId);
        return true;
    }

    // Unified-placement FLOAT restore: a window whose record's scroll slot
    // says floating reopens floating, with the recorded float-back applied
    // through this engine's own gated geometryRestoreRequested emit below
    // (the daemon's passive float-sync arm deliberately restores no
    // geometry).
    // Resolved via the store's takeForReopen so a close/reopen — fresh KWin
    // uuid, appId-FIFO match — restores exactly like a daemon restart's
    // uuid-exact match. peekExact alone covered only the restart case: a
    // reopened floated window missed its record and fell through to a tile
    // insert. FLOATING records only, by the store's contract: a TILED record
    // is not consumed and restores no position — it stays in the store as the
    // exact-final evidence that the window closed tiled (so the reopen must
    // not float it), and the window takes a normal insert below. Column-order
    // restore across close/reopen was removed deliberately: reconstructing
    // strip order from per-window records needed close-burst ledgers and rank
    // anchors that every structural mutation had to invalidate, and the
    // strip stash already restores structure where it matters.
    const QString appId = currentAppIdFor(windowId);
    if (m_windowTracker && PhosphorEngine::hasStableAppIdFor(appId, windowId)) {
        const PhosphorEngine::PlacementStateKey currentKey = currentKeyForScreen(screenId);
        if (const auto record =
                m_windowTracker->placementStore().takeForReopen(engineId(), windowId, appId, currentKey.screenId)) {
            const PhosphorEngine::EngineSlot slot = record->slotFor(engineId());
            if (slot.state == PhosphorEngine::WindowPlacement::stateFloating()) {
                state->addFloating(windowId);
                seedFloatRestoreForOpen(windowId, minWidth, minHeight);
                // The record's SCROLL slot says floating, so this float is
                // this engine's own — marked like the rule-float exit above.
                m_scrollFloatedWindows.insert(windowId);
                consumePendingInitialOrder(screenId, windowId); // same rationale as the rule-float exit
                // The window is marked floating unconditionally above; only
                // the geometry MOVE onto the recorded free spot is gated. That
                // gate and its screen-local rule live in the shared helper.
                emitGatedFloatGeometryRestore(windowId, *record, screenId);
                Q_EMIT windowFloatingStateSynced(windowId, true, screenId);
                return true;
            }
        }
    }

    // Taken off the params rather than re-resolved: layoutParamsForScreen
    // already resolved this exact value against this screen's override map and
    // preset vocabulary, and the screenId overload would parse both again.
    ColumnWidth width = params.defaultColumnWidth;
    ColumnDisplay display = effectiveDefaultColumnDisplay(screenOverrides);
    // "Client decides" is the CONFIG default, so a per-screen rule override
    // outranks it — the header documents these overrides as layering over the
    // config defaults. Overwriting unconditionally meant a
    // SetScrollDefaultColumnWidth rule pinned to a screen never took effect
    // while the global kind was ClientDecides, with no diagnostic. (The
    // per-WINDOW open rule below is applied after this block and wins over
    // both, which is the intended precedence.)
    // The rule channel's bare fraction pins a width outright. The settings
    // channel's kind trio answers BY VALUE through
    // effectiveWidthClientDecides: a per-screen kind of Fixed/Preset/
    // Proportion pins a width (params.defaultColumnWidth above already
    // carries it resolved), while a per-screen kind of ClientDecides means exactly
    // that — testing the kind key's mere PRESENCE here inverted the setting,
    // gating the client-size branch off on precisely the monitors scoped to
    // it.
    // The rule gate asks the RESOLVER's question, not "is the key there":
    // effectiveDefaultColumnWidth validates the fraction and falls through on
    // an out-of-range one, so a presence test let a rule that contributed no
    // width suppress the client-sized open anyway — the column then took the
    // configured default, which is neither what the rule asked for nor what
    // the ClientDecides setting asked for.
    const bool rulePinsWidth = ruleColumnWidthFraction(screenOverrides).has_value();
    if (effectiveWidthClientDecides(screenOverrides) && m_windowTracker && !rulePinsWidth) {
        // Open at the client's own size when one is on record; the first
        // client resize reconciles it afterwards.
        // exactOnly: a column width is a PER-WINDOW contract, and the
        // non-exact default admits a same-app SIBLING's record (the interface
        // documents that sharing as being for free POSITIONS). Minting one
        // window's sizing intent out of another instance's remembered rect
        // opens the column at a size this window never asked for.
        if (const auto geo = m_windowTracker->validatedUnmanagedGeometry(windowId, screenId)) {
            // The tracked geometry is a PHYSICAL rect from the compositor, so
            // it has to be decoded by role. Reading .width() unconditionally
            // would, on a vertical strip, feed the client's cross extent into
            // the column's MAIN intent and open every client-sized window at
            // the wrong length along the strip.
            //
            // Bounded like every other Fixed intent this engine mints (the
            // settings and override arms both qBound before the round). The
            // rect comes from IWindowTrackingService, which an embedder
            // implements, and validatedUnmanagedGeometry validates only that a
            // geometry EXISTS for the window on that screen — its extents are
            // whatever the compositor reported, so a degenerate or absurd one
            // would become the column's standing width intent.
            const int clientMain = params.axis.mainSize(geo->size());
            width = ColumnWidth::makeFixed(qBound(1, clientMain, static_cast<int>(kMaxFixedExtentPx)));
        }
    }

    // Per-window open rules layer over the context/config defaults.
    ScrollOpenParams openParams;
    if (m_openParamsResolver) {
        openParams = m_openParamsResolver(windowId, screenId);
    }
    if (outOpenParams) {
        *outOpenParams = openParams;
    }
    if (openParams.widthFraction) {
        width = ColumnWidth::makeProportion(qBound<qreal>(MinColumnWidthFraction, *openParams.widthFraction, 1.0));
    }
    // A maximized open is the stronger width verdict: it outranks the
    // fraction arm above and (via ruleMaximized) the template blueprint's
    // width below. It leaves the column at the same WIDTH the manual
    // maximize verb would, but not in the same state: the verb's
    // pre-maximize memory is a single slot keyed to the column it was
    // pressed on, and this open seeds nothing into it (there is no earlier
    // width to remember, and the arriving column is not necessarily the
    // active one). The first un-maximize toggle on such a column therefore
    // takes toggleMaximizeActiveColumn's no-stored-intent arm and lands on
    // the configured default width — the same place a column that was
    // already full width at session restore lands.
    const bool ruleMaximized = openParams.maximized.value_or(false);
    if (ruleMaximized) {
        width = ColumnWidth::makeProportion(1.0);
    }
    if (openParams.tabbed) {
        display = *openParams.tabbed ? ColumnDisplay::Tabbed : ColumnDisplay::Normal;
    }
    // Falls THROUGH on success rather than returning: the height commit at
    // the tail of this function is the one the openWindowHeight rule lands
    // on, and an early exit here dropped it silently for
    // openColumnPlacement=consume while its config-driven twin (the
    // IntoActiveColumn arm below) applied it.
    bool inserted = false;
    // Which arm below actually placed the tile. Reported at the tail together
    // with the resulting active window: the arms differ in whether they take
    // focus and re-anchor the view (insertWindow does both, insertWindowAt and
    // insertWindowIntoActiveColumn do neither), so an arrival that lands
    // off-screen is diagnosable only by naming the arm that placed it.
    // Whether the insert APPENDED to a column that already existed rather
    // than creating one. Derived from the column count, not from which arm
    // ran: insertWindowIntoActiveColumn delegates to insertWindow when there
    // is no active column and still returns true, so an arm-keyed flag would
    // call that a join. The client-decides height gate below excludes joins —
    // a joining window shares its host column's shape, which is why the width
    // twin never reaches these arms either (insertWindowIntoActiveColumn
    // honours @p width only on its empty-strip fallback).
    const int columnsBeforeInsert = state->strip().columnCount();
    const char* insertArm = "none";
    // Whether the tile came back out of the mode-round-trip stash. The height
    // commit at the tail reads it: a stash restore rebuilds the remembered
    // SHAPE, and the open rules' width verdicts are already dropped on that
    // arm (the restore inserts with the stashed width, never `width`), so the
    // height must not be the one axis that overrides it. See the height
    // commit for the other half of this contract.
    bool restoredFromStash = false;
    // Mode-round-trip structure restore FIRST — before the consume rule too:
    // a strip stashed at the last reassignment away from Scrolling rebuilds
    // exactly (stacks, widths, display, heights), which is strictly stronger
    // than the order seed's position-only verdict AND than an
    // openColumnPlacement=consume rule (letting the rule outrank the stash
    // left the window's stash tile unconsumed forever: it re-walked on every
    // later open, persisted across sessions, and could hand its slot to an
    // unrelated same-app window through the cross-session claim). The seed
    // entry is still consumed so it cannot linger past the adoption.
    if (restoreFromStripStash(state, currentKeyForScreen(screenId), windowId, params, minWidth, minHeight)) {
        inserted = true;
        restoredFromStash = true;
        insertArm = "stash";
        consumePendingInitialOrder(screenId, windowId);
    }
    if (!inserted && openParams.consume && *openParams.consume && !state->strip().isEmpty()) {
        const std::optional<ColumnDisplay> displayOverride =
            openParams.tabbed ? std::optional<ColumnDisplay>(display) : std::nullopt;
        if (state->strip().insertWindowIntoActiveColumn(windowId, width, displayOverride, params, minWidth,
                                                        minHeight)) {
            // Consume this id from the mode-transition seed too — leaving
            // it would let a stale entry re-position an unrelated later
            // open (the block below documents exactly that hazard).
            consumePendingInitialOrder(screenId, windowId);
            inserted = true;
            insertArm = "consume-rule";
        }
    }

    // Deterministic mode-transition seeding: when the previous engine's
    // window order was captured for this screen, insert each arriving window
    // at its recorded relative position instead of next-to-focus. Each id is
    // CONSUMED on use and the entry is dropped once empty — the header's
    // "consumed as windows arrive" contract. Without consumption a stale
    // seed would re-position an unrelated later open that happens to share
    // an id with the captured list.
    const auto pendingIt = m_pendingInitialOrder.constFind(screenId);
    if (!inserted && pendingIt != m_pendingInitialOrder.constEnd()) {
        const int orderIdx = pendingIt->indexOf(windowId);
        // A consumed id must not re-enter the seed branch: a later unrelated
        // open reusing the id would otherwise be re-positioned by the stale
        // entry (the list keeps consumed ids to preserve positions).
        if (orderIdx >= 0 && !m_consumedInitialOrder.value(screenId).contains(windowId)) {
            int columnIdx = 0;
            const QStringList present = state->strip().windowsInOrder();
            for (const QString& earlier : pendingIt->mid(0, orderIdx)) {
                if (present.contains(earlier)) {
                    ++columnIdx;
                }
            }
            inserted = state->strip().insertWindowAt(columnIdx, windowId, width, display, params);
            if (inserted) {
                insertArm = "order-seed";
                state->strip().setWindowMinimumSize(windowId, minWidth, minHeight);
            }
            // Through the shared consume helper — it drops the screen's entry
            // once the list empties. pendingIt is dangling from here.
            consumePendingInitialOrder(screenId, windowId);
        }
    }
    if (!inserted) {
        // Fresh open with no remembered position: the ONLY site the
        // insert-position setting governs. Restore/seed/unfloat paths above
        // and the re-homing call sites elsewhere keep right-of-active — a
        // "first/last" default teleporting a restored window would read as
        // a lost slot. IntoActiveColumn routes through the consume verb
        // (same shape as the openColumnPlacement rule) and falls through to
        // a positional insert on an empty strip.
        //
        // TEMPLATE blueprint: a materializing column takes the next UNSPENT
        // blueprint entry's width and display. Applied ONLY here (a
        // genuinely new column on the fresh-open path) so restore, seed and
        // stash adoptions keep their remembered shapes, and never
        // retroactively — a template change reshapes nothing that already
        // exists. Precedence: per-window open rules above outrank the
        // blueprint; the blueprint outranks every default, including a
        // client-decides width already resolved into `width`.
        const auto blueprintIt = screenOverrides.constFind(ScrollPerScreenKeys::templateColumns());
        // Blueprint-swap detection, here rather than on the override map's
        // writes. The map is dropped and rebuilt on ordinary context changes
        // that leave the template alone (a desktop switch does it twice), so
        // reacting to the write restarted the seed for events the user never
        // made. Comparing the VALUE the cursor is counting against restarts
        // it exactly when the template really changed.
        //
        // The reset needs an ESTABLISHED identity to compare against, not
        // merely a stored one. A state rebuilt by a mode round trip or staged
        // from the persisted blob arrives holding a real cursor and no
        // identity at all, and treating that absence as a mismatch reset the
        // cursor the carry had just restored — the refill all over again, one
        // arrival later. Not established means STAMP and keep the cursor;
        // only an established value that differs is a genuine swap. Null is
        // no help as the unset marker: it is the ordinary identity of a
        // context with no template. See ScrollState::hasBlueprintIdentity.
        const QVariant blueprintNow = blueprintIt != screenOverrides.constEnd() ? *blueprintIt : QVariant();
        if (!state->hasBlueprintIdentity()) {
            state->setBlueprintIdentity(blueprintNow);
        } else if (state->blueprintIdentity() != blueprintNow) {
            state->setBlueprintIdentity(blueprintNow);
            state->resetBlueprintCursor();
        }
        // The entry this column would take: the strip's consumption cursor,
        // floored at the live column count. ScrollState::blueprintCursor
        // carries the full contract for both halves — the cursor makes an
        // entry SPENT once used (so closing a column no longer hands its
        // prescription back to the next open, which is what made a manual
        // untab look like it reverted), and the floor keeps a strip that grew
        // through a non-consuming path (stash restore, seed, unfloat) from
        // re-taking entries its columns already stand for.
        const int blueprintIdx = qMax(state->blueprintCursor(), int(state->strip().columns().size()));
        // Set once an entry is actually READ below, and turned into a cursor
        // advance only after a new column materialized. Consuming at the read
        // would spend the entry on an insert that never happened (a duplicate
        // id refusal) or on one that joined an EXISTING column.
        int consumedBlueprintIdx = -1;
        // Bounded at kMaxTemplateEntries, the same library-boundary cap the
        // preset vocabularies get: applyPerScreenConfig is exported LGPL
        // surface and an embedder-supplied blueprint must not be read past
        // it. Tested BEFORE the list conversion so a strip that is already
        // longer than any consumable entry pays nothing on the open path.
        if (blueprintIt != screenOverrides.constEnd() && blueprintIdx < kMaxTemplateEntries) {
            const QVariantList blueprint = blueprintIt->toList();
            if (blueprintIdx < blueprint.size()) {
                consumedBlueprintIdx = blueprintIdx;
                const QVariantMap entry = blueprint.at(blueprintIdx).toMap();
                const qreal fraction = entry.value(ScrollPerScreenKeys::templateColumnWidth()).toDouble();
                if (!openParams.widthFraction && !ruleMaximized && fraction >= MinColumnWidthFraction
                    && fraction <= 1.0) {
                    width = ColumnWidth::makeProportion(fraction);
                }
                // Guarded on the VALUE, not merely on the key's presence, and
                // for the same reason effectiveDefaultColumnDisplay is:
                // QVariant::toInt() answers 0 for anything unconvertible, and
                // 0 is a legal ColumnDisplay (Normal). Reading "any value that
                // is not 1" as Normal let a garbage override — or a display
                // kind a future build knows and this one does not — silently
                // replace a Tabbed default (from the settings channel, or a
                // SetScrollDefaultColumnDisplay rule) for exactly the first N
                // columns. An entry that carries no usable display leaves
                // `display` on the effective default resolved above, which is
                // the same fall-through the width arm's range test gives.
                const auto displayIt = entry.constFind(ScrollPerScreenKeys::templateColumnDisplay());
                if (!openParams.tabbed && displayIt != entry.constEnd()) {
                    bool displayOk = false;
                    const int displayValue = displayIt->toInt(&displayOk);
                    if (displayOk
                        && (displayValue == static_cast<int>(ColumnDisplay::Normal)
                            || displayValue == static_cast<int>(ColumnDisplay::Tabbed))) {
                        display = static_cast<ColumnDisplay>(displayValue);
                    }
                }
            }
        }
        const ScrollInsertPosition insertPos = effectiveInsertPosition(screenOverrides);
        if (insertPos == ScrollInsertPosition::IntoActiveColumn && !state->strip().isEmpty()) {
            // The same ENGAGED-ONLY optional the openColumnPlacement=consume
            // arm above builds, not a bare nullopt. The two arms are the rule
            // and config spellings of one intent, and this one silently
            // dropped an openWindowTabbed rule's verdict on the host column
            // while its twin applied it. Engaged only when the rule actually
            // spoke: nullopt leaves the host column's own display alone, which
            // is what an arriving TILE should do by default.
            const std::optional<ColumnDisplay> displayOverride =
                openParams.tabbed ? std::optional<ColumnDisplay>(display) : std::nullopt;
            inserted = state->strip().insertWindowIntoActiveColumn(windowId, width, displayOverride, params, minWidth,
                                                                   minHeight);
            if (inserted) {
                insertArm = "into-active-column";
            }
        }
        if (!inserted) {
            inserted = state->strip().insertWindow(
                windowId, width, display, params, minWidth, minHeight,
                insertPos == ScrollInsertPosition::IntoActiveColumn ? ScrollInsertPosition::RightOfActive : insertPos);
            // The entry is spent HERE and nowhere else: this is the only arm
            // that creates a column out of the blueprint. The
            // IntoActiveColumn arm just above appends to a column that
            // already exists — it spends nothing, and the tile it adds
            // carries no column shape of its own.
            if (inserted && consumedBlueprintIdx >= 0) {
                state->setBlueprintCursor(consumedBlueprintIdx + 1);
            }
        }
    }
    if (inserted && !restoredFromStash && openParams.heightFraction) {
        // Per-window open rule wins over every DEFAULT height, matching the
        // width/tabbed precedence above. Committed as Fixed pixels against
        // the live work area, the same resolution the adjust verbs use.
        //
        // A stash restore is excluded, which is the same precedence the width
        // arm already has: the stash carries the shape the user left the
        // strip in at the last reassignment away from Scrolling, and that
        // remembered shape outranks an open-time default on BOTH axes. Height
        // overriding it while width did not made the round trip return a
        // window at its old width and its rule height, which is a shape the
        // user never had.
        //
        // Re-resolved AFTER the insert rather than reusing the params from
        // the top of this function: with smart gaps the work area depends on
        // the strip's column count, and an insert that took the strip from
        // one column to two makes the pre-insert area stale — the committed
        // pixels are PERSISTED intent, so the error would not self-heal on
        // the next relayout the way a transient anchor does. The override
        // pins the resolve to the post-insert count.
        //
        // Resolved against the work area's CROSS extent, which is what a
        // window height divides — the within-column stack. Physical height on
        // a vertical strip is the extent the STRIP runs along, so committing
        // a fraction of it hands the tile an intent that can be larger than
        // the column it lives in.
        const ScrollLayoutParams postParams = layoutParamsForScreen(screenId, state->strip().columnCount());
        const int crossExtent = postParams.axis.crossSize(postParams.workArea);
        if (crossExtent > 0) {
            const qreal fraction = qBound<qreal>(MinWindowHeightFraction, *openParams.heightFraction, 1.0);
            state->strip().setWindowHeightIntent(windowId,
                                                 WindowHeight::makeFixed(qMax(1, qRound(fraction * crossExtent))));
        }
    }
    // "The client decides" default HEIGHT, the twin of the width gate above
    // the insert. Every term of that gate applies here for its own reason:
    // a stash restore's remembered shape outranks an open-time default (the
    // rule arm just above documents that precedence), a per-window
    // openWindowHeight rule outranks the kind and is handled there, and a
    // per-screen rule height means the resolver already pinned one.
    //
    // A window that JOINED an existing column is excluded, which is where the
    // width twin's reach ends too: that arm passes the width to
    // insertWindowIntoActiveColumn, which honours it only on the empty-strip
    // fallback, so a joining window keeps its host column's shape. Committing
    // a height there is not merely redundant, it is disruptive — the intent
    // is Fixed, so on a TABBED host setWindowHeightIntent hands the arrival
    // the column's extent and resizes every tab to a window that just showed
    // up, and on a Normal host the relayout renormalizes the siblings down to
    // fit the newcomer. A joining tile takes the column's even split, as
    // before.
    //
    // Placed AFTER the insert rather than folded into params.defaultWindowHeight
    // because the insert is what puts the tile in the strip: unlike the
    // width, which the insert takes as an argument, a tile's height is
    // written by the insert from params and can only be re-stated through
    // setWindowHeightIntent afterwards.
    const bool joinedExistingColumn = inserted && state->strip().columnCount() == columnsBeforeInsert;
    if (inserted && !restoredFromStash && !joinedExistingColumn && !openParams.heightFraction) {
        // Re-resolved after the insert for the rule arm's reason: with smart
        // gaps the work area depends on the strip's column count, and the
        // committed pixels are PERSISTED intent that would not self-heal.
        commitClientDecidedHeight(state->strip(), windowId, screenId, screenOverrides,
                                  layoutParamsForScreen(screenId, state->strip().columnCount()));
    }
    if (!inserted) {
        qCWarning(lcScrollEngine) << "insertOpenedWindow: duplicate window" << windowId;
        // A refusal is still an OUTCOME, and the seed's "consumed on every
        // outcome" contract has no exception for it: the strip already holds
        // this window, so the seed entry can never place it, and leaving the
        // id behind pins the screen's seed list for the whole session (it can
        // never reach the all-consumed drop) where it waits to re-position an
        // unrelated later open that reuses the id.
        consumePendingInitialOrder(screenId, windowId);
        // Do not leave a reverse-map key for a window no structure holds —
        // that is the exact inconsistency floatWindowInternal warns about.
        // (Keyed-but-present is fine: the insert also fails when the strip
        // already contains the window, and unkeying a live tile would just
        // create the mirror inconsistency.)
        if (!state->strip().containsWindow(windowId) && !state->isFloating(windowId)) {
            m_states.removeWindow(windowId);
        }
    }
    // Which arm placed the arrival, and whether it ended up as the strip's
    // active window. The arms differ in whether they take focus and re-anchor
    // (insertWindow does both; insertWindowAt and insertWindowIntoActiveColumn
    // do neither), and an arrival that is not active cannot pull the view onto
    // itself — which is how a freshly-opened window ends up parked off-screen.
    qCDebug(lcScrollEngine) << "insertOpenedWindow:" << windowId << "arm=" << insertArm << "inserted=" << inserted
                            << "active=" << state->strip().activeWindowId()
                            << "viewDetached=" << state->strip().viewDetached() << "burstDepth=" << m_arrivalBurstDepth;
    return inserted;
}

void ScrollEngine::windowOpened(const QString& rawWindowId, const QString& screenId, int minWidth, int minHeight)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    if (windowId.isEmpty() || !m_scrollingScreens.contains(screenId)) {
        return;
    }

    PhosphorEngine::PlacementStateKey oldKey;
    ScrollState* oldState = stateForWindow(windowId, &oldKey);
    const PhosphorEngine::PlacementStateKey key = currentKeyForScreen(screenId);
    if (oldState && oldKey == key) {
        // Raw reverse-map key gate, deliberately NOT the containsWindow
        // membership test the restore paths use: the one same-key producer
        // whose strip does not contain the window is a drag-preview detach
        // (tracked against the target key while the tile is held out), and
        // early-returning there is exactly right — the commit/cancel owns
        // the re-insert.
        // Re-announce of a window we already track here. Still an arrival as
        // far as the mode-transition seed is concerned: the header's
        // "consumed on EVERY outcome" invariant has no exception for this
        // one, and skipping it leaves the screen's seed list unable to empty,
        // so it survives to re-position an unrelated later open. No-op when
        // the screen carries no seed, which is the usual case here.
        consumePendingInitialOrder(screenId, windowId);
        return;
    }

    // Cross-screen restore defer, the reciprocal of SnapEngine's
    // recorded-screen gate and autotile's claimCrossScreenReopen — every
    // engine runs PhosphorEngine::pendingCrossScreenManagedRestore over the
    // same record fields, so a session window snapped on a snapping-mode
    // monitor (or tiled on an autotile-mode monitor) that KWin drops on a
    // scrolling screen at login is claimed by its OWN engine cross-screen
    // and NOT double-claimed into the strip here. Gated on
    // !oldState: a window this engine already tracks anywhere is scroll's
    // own (in-session migration), never a session restore.
    // Membership, not the raw reverse-map key (autotile's gate term for
    // term): a refused earlier open can leave a phantom key, and gating on
    // it would skip the defer while this engine manages nothing.
    const bool trackedHere = oldState && oldState->containsWindow(windowId);
    if (!trackedHere && m_windowTracker && (m_snappingModeResolver || m_autotileModeResolver)) {
        // Registry-aware appId and the reclaim-grade lookup, matching what
        // the CLAIMING side asks. Both halves are load-bearing for the N-way
        // agreement: parsing the frozen canonical string would read a
        // different bucket after an Electron/CEF class mutation (so this gate
        // could miss a record the claim finds — both-claimed), and a plain
        // peek would see a LIVE sibling's record the claim's exclusion
        // rejects (so this gate could defer to an engine that then declines —
        // both-skipped).
        const QString appId = m_windowTracker->currentAppIdFor(windowId);
        if (PhosphorEngine::hasStableAppIdFor(appId, windowId)) {
            const auto crossRestorePending = [&](const PhosphorEngine::WindowPlacement& p) {
                if (m_snappingModeResolver
                    && PhosphorEngine::pendingCrossScreenSnapRestore(
                        p, screenId, [this](const QString& rec, int desktop, const QString& activity) {
                            return m_snappingModeResolver(rec, desktop, activity);
                        })) {
                    return true;
                }
                return m_autotileModeResolver
                    && PhosphorEngine::pendingCrossScreenManagedRestore(
                           p, PhosphorEngine::WindowPlacement::autotileEngineId(),
                           PhosphorEngine::WindowPlacement::stateTiled(), screenId,
                           [this](const QString& rec, int desktop, const QString& activity) {
                               return m_autotileModeResolver(rec, desktop, activity);
                           });
            };
            if (m_windowTracker->placementStore().peekForReclaim(windowId, appId, crossRestorePending).has_value()) {
                qCInfo(lcScrollEngine) << "windowOpened:" << windowId << "on scrolling screen" << screenId
                                       << "defers — carries a cross-screen restore for another engine";
                // A deferred arrival is still an arrival: without the
                // consume, this id never reaches insertOpenedWindow and its
                // seed entry lingers on the screen forever.
                consumePendingInitialOrder(screenId, windowId);
                // A refused-first-open phantom key must not survive the defer
                // (autotile's twin does the same): isWindowTracked would keep
                // answering true for a window another engine is about to own,
                // misrouting the daemon's float and handoff dispatch. The
                // enclosing block is already !trackedHere, so a key here is
                // by definition a phantom.
                if (stateForWindow(windowId)) {
                    m_states.removeWindow(windowId);
                }
                return;
            }
        }
    }
    bool migratedWindowedFs = false;
    bool migratedMaximizedToEdges = false;
    std::optional<WindowHeight> migratedHeight;
    if (oldState) {
        // The window moved context (screen or desktop) — migrate. The old
        // context's per-window bookkeeping goes with it: a stale
        // FloatRestore could re-slot an unfloat on the NEW screen against
        // the OLD strip's geometry, and lastAppliedRect would keep
        // answering for a context that no longer holds the window.
        const ScrollLayoutParams oldParams = layoutParamsForScreen(oldKey.screenId);
        const bool wasFloating = oldState->isFloating(windowId);
        // Windowed fullscreen is per-tile state the fresh insert below would
        // silently default false; read it off the old tile before takeWindow
        // destroys it (the boundary-crossing verb carries it the same way).
        migratedWindowedFs = oldState->strip().isWindowedFullscreen(windowId);
        // Maximize-to-edges is declared COLUMN state nothing re-derives, carried
        // the same way but only off a LONE tile (floatWindowInternal's rule): a
        // shared column survives the migration and keeps its own flag.
        const int oldColIdx = oldState->strip().columnOfWindow(windowId);
        migratedMaximizedToEdges = oldColIdx >= 0 && oldState->strip().columns().at(oldColIdx).tiles.size() == 1
            && oldState->strip().columns().at(oldColIdx).maximizedToEdges;
        // The tile's HEIGHT intent is carried the same way and for a sharper
        // reason: the insert below re-runs the whole open path, so a
        // ClientDecides screen would re-stamp the window with its recorded
        // client extent and discard whatever height the user had given it on
        // the old context. A migration is a move, not an open — the height
        // the window already had outranks the open-time default. Carried
        // verbatim, Auto included, so a window the user never resized keeps
        // sharing its column rather than acquiring an intent on every hop.
        // Only from a real TILE. A window that was FLOATING on the old
        // context is not in its strip, and windowHeightIntent answers a
        // default-constructed Auto for a window it does not hold — an engaged
        // optional carrying a height that never belonged to a tile, which the
        // re-apply below would then stamp over the client-decided height the
        // new screen's open path just committed. The windowed-fullscreen twin
        // gets away with the unconditional read because a false flag is a
        // no-op write; an Auto height is not.
        if (oldState->strip().containsWindow(windowId)) {
            migratedHeight = oldState->strip().windowHeightIntent(windowId);
        }
        oldState->strip().takeWindow(windowId, oldParams);
        oldState->removeFloating(windowId);
        m_floatRestore.remove(windowId);
        // The mode-float marker goes with the old context too: the window
        // re-enters (usually tiled) on the new screen, and a stale marker
        // would re-float it at the next mode transition. The
        // windowed-fullscreen apply memory follows for the same eviction
        // symmetry the handoff paths keep (a stale true only costs one
        // redundant emit, but the exit sets should stay identical).
        m_scrollFloatedWindows.remove(windowId);
        m_lastAppliedWindowedFs.remove(windowId);
        m_lastAppliedMaximizedToEdges.remove(windowId);
        if (wasFloating) {
            // Announce the dropped float bit: signal-driven subscribers
            // (the effect's FloatingCache) would otherwise keep believing
            // the window floats while insertOpenedWindow tiles it below,
            // and resolve the divergence as a float-back. A float RECORD
            // re-float re-announces true immediately afterwards.
            Q_EMIT windowFloatingStateSynced(windowId, false, oldKey.screenId);
        }
        // Background-context guard, the same one windowClosed and the float
        // paths carry: a scheduled retile resolves the screen's CURRENT
        // context, so a migration out of another desktop's state must not
        // drive one. The switch back retiles the mutated strip.
        if (oldKey == currentKeyForScreen(oldKey.screenId)) {
            scheduleRetileForScreen(oldKey.screenId);
        }
        Q_EMIT placementChanged(oldKey.screenId);
    }

    ScrollState* state = stateForKey(key, true);
    if (!state) {
        return;
    }
    // Track BEFORE inserting: insertOpenedWindow's oversized/rule-float
    // paths emit windowFloatingStateSynced, and a synchronous query-back from
    // a subscriber must already see the window as this engine's.
    m_states.setKeyForWindow(windowId, key);
    // Capture the pre-insert focus: with focus-new-windows OFF the
    // compositor keeps focus on the previous window, so the strip must not
    // adopt the arrival as its active column either — a diverged strip
    // makes every later focus-direction verb navigate from the wrong
    // origin, and a consume-open into a tabbed column would park the
    // window the user is actually looking at.
    const QString priorActive = state->strip().activeWindowId();
    // Re-adoption starts from a blank rect memory, unconditionally. The close
    // and release paths deliberately RETAIN m_lastAppliedRect (the daemon's
    // close capture reads it as the float-back poison guard), and this is the
    // second of its two reclaimers — pruneStaleWindows is the other, and it
    // only fires on aliveness. Left standing, a retained rect that happens to
    // equal the one the strip resolves defeats applyLayout's emit-on-change
    // gate, so no windowsTiled batch ever fires for the re-adopted window and
    // a single-window screen sits at the geometry the OTHER mode left it in.
    const QRect priorAppliedRect = m_lastAppliedRect.value(windowId);
    m_lastAppliedRect.remove(windowId);
    // The parked-edge memory follows the rect memory: a re-adopted window
    // never parked here, and a stale side would mis-anchor its first arrival
    // slide. Taken (not dropped) so the refuse branch can put it back — a
    // refused insert means the window is a live tile that may genuinely be
    // parked right now.
    const QString priorParkedEdge = m_parkedScrollEdge.take(windowId);
    ScrollOpenParams openParams;
    if (!insertOpenedWindow(state, windowId, screenId, minWidth, minHeight, &openParams)) {
        // Every insert refused (the strip already holds the window). On a
        // fresh open nothing moved; on the MIGRATION path above the old
        // context already released the window and announced its own retile,
        // and the only reachable refusal here is "this strip already holds
        // it", so the window is not stranded either way. Neither the
        // geometry batch nor the dirty mark may fire for THIS strip — and
        // the rect memory goes back, because
        // the window is still a live tile: dropped, lastManagedRect answers
        // null and a later float-back captures the COLUMN rect as free
        // geometry, which is the poison this map exists to prevent.
        if (priorAppliedRect.isValid()) {
            m_lastAppliedRect.insert(windowId, priorAppliedRect);
        }
        if (!priorParkedEdge.isEmpty()) {
            m_parkedScrollEdge.insert(windowId, priorParkedEdge);
        }
        if (migratedWindowedFs) {
            qCWarning(lcScrollEngine) << "windowOpened: insert refused for" << windowId
                                      << "— migrated windowed-fullscreen state dropped";
        }
        return;
    }
    // Re-state the migrated height intent on the fresh tile, overwriting
    // whatever the open path just seeded (a ClientDecides screen re-stamps
    // the client extent there, which on a migration would discard the user's
    // height). Guarded on the window actually being a tile: insertOpenedWindow
    // returns true on its float exits too, and a float has no tile to carry.
    if (migratedHeight && state->strip().containsWindow(windowId)) {
        state->strip().setWindowHeightIntent(windowId, *migratedHeight);
    }
    // Hand the migrated windowed-fullscreen flag to the fresh tile (captured
    // above, before takeWindow destroyed the old one). insertOpenedWindow
    // returns true on its FLOAT exits too, and a float has no tile to carry
    // the flag (float and windowed fullscreen are exclusive by design) —
    // warn about the drop instead of relying on the strip write's silent
    // no-op, matching the refusal arm's diagnostic above.
    if (migratedWindowedFs) {
        if (state->strip().containsWindow(windowId)) {
            state->strip().setWindowedFullscreen(windowId, true);
        } else {
            qCWarning(lcScrollEngine) << "windowOpened:" << windowId
                                      << "arrived floated — migrated windowed-fullscreen state dropped";
        }
    }
    // The same hand-over for the maximize-to-edges flag, onto a column the
    // arrival has to ITSELF: a consume-open lands it in an existing column
    // whose flag is that column's own, and a float exit leaves no tile at all.
    // Both of those drop the carried flag, and quietly, unlike the
    // windowed-fullscreen arm above: that state is the client's and losing it
    // is worth a warning, while this one describes how a column the arrival
    // no longer owns alone was presenting, which the new host answers for.
    const int newColIdx = migratedMaximizedToEdges ? state->strip().columnOfWindow(windowId) : -1;
    if (newColIdx >= 0 && state->strip().columns().at(newColIdx).tiles.size() == 1) {
        state->strip().setMaximizedToEdgesForWindow(windowId, true);
    }

    // Three tiers, narrowest first: the per-window openFocused rule outranks
    // the per-context SetScrollFocusNewWindows rule, which outranks the
    // global setting — the same precedence OpenColumnWidth has over
    // SetScrollDefaultColumnWidth. Whichever wins carries BOTH of the
    // setting's effects: false also rewinds the strip's active column to the
    // pre-insert focus below, true adopts the arrival even when the global
    // default would not.
    // Lazy on purpose: value_or would evaluate the settings read even when the
    // rule already decided, and that read walks the override map and does a
    // qobject_cast on the settings object.
    const bool focusNew = openParams.focused ? *openParams.focused : effectiveFocusNewWindows(screenId);
    const bool arrivalTookFocus = focusNew && state->strip().activeWindowId() == windowId;
    // Paired with the insertOpenedWindow report above: arrivalTookFocus is what
    // decides whether the deferred/immediate applyLayout re-centres the view on
    // the arrival, so a false here with focusNew true is the signal that the
    // insert arm silently declined focus.
    qCDebug(lcScrollEngine) << "windowOpened focus:" << windowId << "focusNew=" << focusNew
                            << "arrivalTookFocus=" << arrivalTookFocus << "burstDepth=" << m_arrivalBurstDepth;
    if (!focusNew && !priorActive.isEmpty() && state->strip().activeWindowId() == windowId
        && state->strip().containsWindow(priorActive)) {
        const ScrollLayoutParams params = layoutParamsForScreen(screenId);
        state->strip().focusWindow(priorActive, params);
        // Rewinding the strip is only half of declining focus. The compositor
        // focuses the arriving window on its own, and reports that focus back
        // independently of anything this engine asked for — so without the two
        // steps below the report lands in windowFocused, adopts the arrival,
        // and undoes the rewind. That was confirmed live before this was
        // added: the arrival came up active with the rule in force.
        //
        // So ALSO put the compositor's focus back where the strip now points,
        // and mark the arrival's own report for a single consume. The mark
        // alone would leave the strip disagreeing with real focus (the window
        // would be focused while the strip highlighted its neighbour); the
        // activation alone would race the arrival's report. Together they
        // agree.
        //
        // Skipped inside an arrival burst (daemon-restart re-announce, mode
        // flip): those re-announce EXISTING windows, the user is not opening
        // anything, and firing an activation per arrival would fight the
        // burst's own deferred focus restore.
        if (m_arrivalBurstDepth == 0) {
            m_declinedOpenFocus.insert(windowId);
            queueSelfActivation(priorActive);
            Q_EMIT activateWindowRequested(priorActive);
        }
    }
    // Only an arrival that actually TAKES focus re-targets the screen-hintless
    // shortcut paths. Writing this on ANY arrival pointed them at whatever
    // monitor a background app last opened a window on — including floated
    // opens and opens under focus-new-windows OFF, neither of which the user
    // is looking at. Focus events own the value the rest of the time.
    if (arrivalTookFocus) {
        m_activeScreen = screenId;
    }
    // Inside an arrival burst (daemon-restart re-announce, mode flip) the
    // apply is deferred to the outermost endArrivalBurst: each arrival here
    // splices into a PARTIAL strip, and applying per arrival marches every
    // already-placed window through N intermediate layouts the user can see
    // even when the restored strip resolves to exactly the pre-restart rects.
    if (m_arrivalBurstDepth > 0) {
        // Keyed by CONTEXT, not bare screen: a desktop/activity switch landing
        // mid-burst must not replay the deferred apply against the new
        // context's strip (the drain below skips a key the screen no longer
        // resolves to).
        auto it = m_burstPendingApplies.find(key);
        if (it == m_burstPendingApplies.end()) {
            m_burstPendingApplies.insert(key, arrivalTookFocus);
        } else {
            it.value() = it.value() || arrivalTookFocus;
        }
    } else {
        applyLayout(screenId, arrivalTookFocus);
    }
    Q_EMIT placementChanged(screenId);
}

void ScrollEngine::beginArrivalBurst()
{
    ++m_arrivalBurstDepth;
}

void ScrollEngine::endArrivalBurst()
{
    if (m_arrivalBurstDepth == 0 || --m_arrivalBurstDepth > 0) {
        return;
    }
    const QHash<PhosphorEngine::PlacementStateKey, bool> pending = std::move(m_burstPendingApplies);
    // Sorted, not hash order: with focus-taking arrivals on two screens the
    // LAST activation request wins the compositor's focus, and hash order
    // would make that winner vary run to run.
    QList<PhosphorEngine::PlacementStateKey> keys = pending.keys();
    std::sort(keys.begin(), keys.end(),
              [](const PhosphorEngine::PlacementStateKey& a, const PhosphorEngine::PlacementStateKey& b) {
                  return std::tie(a.screenId, a.desktop, a.activity) < std::tie(b.screenId, b.desktop, b.activity);
              });
    // Which screens have a key in THIS burst that still matches their current
    // context. The focus seed is keyed by bare screen id while these keys carry
    // the whole context, so one screen can appear twice — arrivals straddling a
    // mid-burst context switch insert under both the old key and the new one.
    // The skip arm below drops the seed, and without this the stale key (which
    // sorts first whenever its desktop is lower) would drop the seed out from
    // under the matching key that is about to consume it.
    QSet<QString> screensWithLiveKey;
    for (const PhosphorEngine::PlacementStateKey& key : std::as_const(keys)) {
        if (key == currentKeyForScreen(key.screenId)) {
            screensWithLiveKey.insert(key.screenId);
        }
    }
    for (const PhosphorEngine::PlacementStateKey& key : std::as_const(keys)) {
        // The screen may have left the scrolling set mid-burst (mode flip
        // races — applyLayout's own guards no-op that), and a context switch
        // mid-burst means the deferred apply's strip is no longer the one on
        // screen: skip it, the switch-back retile covers the mutated strip.
        if (key != currentKeyForScreen(key.screenId)) {
            // The seed dies here, but ONLY when no live key for this screen is
            // going to consume it. It was captured for the transition this
            // burst belongs to, and that transition is over the moment the
            // context moves — applying it to whatever arrives next would
            // re-anchor a view the user has since moved. Dropped rather than
            // left, because nothing downstream of this `continue` revisits the
            // entry: the switch-back retile does not consume seeds, so a
            // skipped drain leaves it armed for some later, unrelated burst.
            if (!screensWithLiveKey.contains(key.screenId)) {
                m_pendingInitialFocus.remove(key.screenId);
            }
            continue;
        }
        // Mode-transition focus restore, consumed here and nowhere else.
        //
        // The order seed positions each arrival with insertWindowAt, which
        // takes focus only for the first column of an empty strip and otherwise
        // re-clamps the anchor without re-anchoring on the arrival — right for
        // seeding a whole strip, but it leaves the strip pointed at whichever
        // column happened to be adopted first. The window the user was
        // actually on then has no way to pull the view onto itself, and a
        // window that opened just before the flip lands parked off-screen.
        //
        // Applied BEFORE applyLayout so the same pass's updateViewForFocus
        // re-anchors on the restored focus, and forced through the focus
        // verb (not a bare active-index write) so the latch clears with it —
        // "a focus change re-attaches" is the strip's own contract.
        //
        // The seed is dropped whether or not it landed: a window that closed
        // between the capture and the re-announce is gone for good, and
        // leaving the entry armed would re-anchor some later, unrelated
        // transition onto a view the user has since moved away from.
        const QString seededFocus = m_pendingInitialFocus.take(key.screenId);
        bool restoredFocus = false;
        if (!seededFocus.isEmpty()) {
            ScrollState* state = stateForKey(key, false);
            if (state && state->strip().containsWindow(seededFocus)) {
                state->strip().focusWindow(seededFocus, layoutParamsForScreen(key.screenId));
                restoredFocus = true;
            }
        }
        // A restored focus is a focus move in its own right, so the apply has
        // to treat it as one — otherwise the arm that re-centres the view is
        // skipped for exactly the case this restore exists to fix.
        applyLayout(key.screenId, pending.value(key) || restoredFocus);
    }
}

void ScrollEngine::windowClosed(const QString& rawWindowId)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    // Before any state mutation: a preview whose dragged window just closed
    // must not survive to restore a dead id (autotile's
    // dropClosedWindowFromDragPreview twin).
    dropClosedWindowFromDragPreview(windowId);
    // A pending self-activation for a window that closes before its echo
    // lands can never be answered; without this a later genuine focus of a
    // reused id would be eaten as that echo.
    m_pendingSelfActivations.removeAll(windowId);
    m_pendingSelfActivationQueuedAt.remove(windowId);
    // Same reasoning for the declined-open mark: the arrival that was denied
    // focus can close before its one report arrives, and a stale mark would
    // then eat the first genuine focus of a reused id.
    m_declinedOpenFocus.remove(windowId);
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state) {
        return;
    }
    const bool wasActive = state->strip().activeWindowId() == windowId;
    const ScrollLayoutParams params = layoutParamsForScreen(key.screenId);
    const bool inStrip = state->strip().removeWindow(windowId, params);
    // Unconditional, not gated on the strip removal failing: the two sets are
    // meant to be disjoint, but a window that somehow sits in BOTH would keep
    // its floating entry forever under the gated form — nothing else ever
    // revisits a closed window's floating membership. A no-op for the normal
    // case, which is the whole point.
    state->removeFloating(windowId);
    // Deliberately NOT consumePendingInitialOrder: the seed's "consumed on
    // every outcome" contract is about OPEN outcomes (the window arrived and
    // was placed, floated or refused), and a close is not one. Marking the id
    // consumed here would defeat a RE-SEED — setInitialWindowOrder resets the
    // consumed set precisely so a closed window reopens at its seeded
    // position, and a close-time consume would send it to the ordinary
    // next-to-focus path instead (partiallyConsumedSeedGuardsReopens covers
    // exactly that sequence).
    m_states.removeWindow(windowId);
    // m_lastAppliedRect is deliberately RETAINED through the close: the
    // daemon's close capture consults lastManagedRect DURING windowClosed
    // (the engines hear the close before WindowTracking does), and the
    // live frame is still the strip rect at that moment — without the
    // memory, the column rect becomes the reopen float-back geometry (the
    // float-back tile-rect poison, autotile's twin retains for the same
    // reason). pruneStaleWindows reclaims the entry independently.
    m_floatRestore.remove(windowId);
    m_scrollFloatedWindows.remove(windowId);
    // The parked-edge and windowed-fullscreen apply memories die with the
    // window: a closed window has no tile left to consume the edge or
    // compare the flag, and pruneStaleWindows fires only once per session
    // (bring-up), so without these drops both maps grow by one entry per
    // qualifying close for the session's lifetime. (m_lastAppliedRect above
    // is different — it is retained on purpose for the close capture.)
    m_parkedScrollEdge.remove(windowId);
    m_lastAppliedWindowedFs.remove(windowId);
    m_lastAppliedMaximizedToEdges.remove(windowId);

    if (inStrip && key == currentKeyForScreen(key.screenId)) {
        // Background-context guard: applyLayout resolves the screen's
        // CURRENT context, so a close on another desktop's state would
        // relayout the wrong strip (the mutated one must stay silent
        // until its desktop returns).
        //
        // Close-settle hold: with a configured delay, the reflow waits out
        // the closing window's disappear animation instead of moving the
        // neighbours over the still-painting corpse (the two animations
        // fighting for the vacated slot is exactly the visual mess the hold
        // exists to prevent). The deferred flush passes focusAfter=false on
        // purpose: the compositor activates its own successor immediately
        // and windowFocused adopts it long before the flush fires, so the
        // engine re-asserting its pick a delay later would only re-open the
        // dueling-activations bounce the pending-self-activation fix closed.
        if (m_closeReflowDelayMs > 0) {
            startCloseReflowHold(key.screenId);
        } else {
            applyLayout(key.screenId, wasActive && !state->strip().activeWindowId().isEmpty());
        }
    }
    Q_EMIT placementChanged(key.screenId);
}

// startCloseReflowHold / deferForCloseReflowHold / scheduleCloseReflowFlush
// live in engine_closehold.cpp (this TU is over the file-size ceiling).

void ScrollEngine::windowFocused(const QString& rawWindowId, const QString& screenId)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    if (!screenId.isEmpty() && m_scrollingScreens.contains(screenId)) {
        m_activeScreen = screenId;
    }
    // Self-activation echo filter, m_pendingSelfActivations' consume side
    // and the home of its contract: the effect reports EVERY activation
    // back through notifyWindowFocused, including ones this engine
    // initiated, and the round trip is asynchronous — on a rapid focus
    // scroll the strip has already advanced past the echoed window by the
    // time the report lands, and treating the stale echo as user focus
    // would rewind the active column below (the next scroll step then
    // advances from the rewound column and skips one). Entries ahead of the
    // match go with it: the effect's calls share one ordered D-Bus
    // connection, so their echoes were dropped (show desktop, window gone)
    // and can never arrive after this one. The tab-click path never queues
    // here — its activation goes out via the adaptor's focusWindowRequested,
    // never this engine's emit, so its echo still drives the strip
    // (signals.cpp documents that contract).
    if (const int selfIdx = m_pendingSelfActivations.indexOf(windowId); selfIdx >= 0) {
        // Expiry check BEFORE swallowing: an entry whose echo the compositor
        // dropped (show desktop, focus-stealing prevention) has no reclaim
        // path any more (see below), and without this it ate the FIRST real
        // click on its window. Echo round trips are milliseconds; a stamp
        // this old means the echo is dead and this report is the user.
        const qint64 queuedAt = m_pendingSelfActivationQueuedAt.value(windowId, -1);
        const bool expired = queuedAt < 0 || !m_selfActivationClock.isValid()
            || m_selfActivationClock.elapsed() - queuedAt > kSelfActivationEchoExpiryMs;
        m_pendingSelfActivations.erase(m_pendingSelfActivations.begin(),
                                       m_pendingSelfActivations.begin() + selfIdx + 1);
        for (auto stampIt = m_pendingSelfActivationQueuedAt.begin();
             stampIt != m_pendingSelfActivationQueuedAt.end();) {
            stampIt = m_pendingSelfActivations.contains(stampIt.key()) ? std::next(stampIt)
                                                                       : m_pendingSelfActivationQueuedAt.erase(stampIt);
        }
        if (!expired) {
            // The swallow is silent to every other observer; without this
            // line a report eaten here is indistinguishable in the journal
            // from one that never arrived.
            qCDebug(lcScrollEngine) << "windowFocused: swallowed self-activation echo for" << windowId;
            return;
        }
        // Fall through: adopt as genuine focus.
    }
    // Declined-open consume, m_declinedOpenFocus' read side. An
    // `openFocused = false` arrival was focused by the compositor anyway, and
    // the rewind that declined it has already asked for the prior window back;
    // adopting this report would undo that. Consumed exactly once, so the next
    // report naming the same window is a real user click and adopts below.
    //
    // Placed ahead of the reclaim on the next line ON PURPOSE: this report is
    // not the "genuine focus" the reclaim reasons about, and clearing the queue
    // here would drop the prior window's activation echo that the rewind just
    // queued, letting that echo rewind the strip a second time when it lands.
    if (m_declinedOpenFocus.remove(windowId)) {
        return;
    }
    // Deliberately NO reclaim of m_pendingSelfActivations here. An earlier
    // form cleared the queue on every genuine report, reasoning that a
    // genuine focus implies every previously-sent echo already landed. That
    // inference is false ACROSS DIRECTIONS: on a window close the compositor
    // activates its own successor pick and that genuine report is in flight
    // BEFORE this engine queues its competing self-activation (the close
    // handler's focusWindowAfter), so the queued entry's echo is still
    // legitimately in flight when the genuine report lands. The clear wiped
    // it, the echo then arrived against an empty queue, was mis-read as user
    // focus, and the two picks re-anchored the strip against each other —
    // the post-close anchor ping-pong (0↔1916 several times in two seconds,
    // re-parking live windows on every bounce). The queue stays bounded
    // without the reclaim: the prefix-drop at match above, the close-time
    // removeAll, and the kMaxPendingSelfActivations cap all still run — and
    // the dropped-echo case the reclaim used to (over-)serve is now handled
    // precisely by the per-entry expiry at the match above.
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state) {
        return;
    }
    if (state->isFloating(windowId)) {
        // Focus-side memory for switchFocusBetweenFloatingAndTiling: a
        // genuine report naming a float is the only place the engine learns
        // the float layer holds focus, and which member holds it.
        state->setLastFloatingFocus(windowId);
        state->setFloatingHasFocus(true);
        return;
    }
    // A genuine report naming a tile means the float layer lost focus,
    // whether or not the strip's own focus slot moves below.
    state->setFloatingHasFocus(false);
    const ScrollLayoutParams params = layoutParamsForScreen(key.screenId);
    // DETACH-ONCE (drag_preview.cpp): a live drag-insert preview on this
    // screen owns the view for the rest of the hold, and picking the dragged
    // window up activates it. Handing the view back here would slide the
    // layout under a stationary cursor, the hazard applyLayout's own
    // dragPreviewSteersView guard exists for. screensMatch, not ==, for that
    // guard's reason. Read before the focus move so both outcomes below share
    // one answer.
    const bool dragPreviewSteersView = m_dragInsertPreview
        && PhosphorScreens::ScreenIdentity::screensMatch(m_dragInsertPreview->targetScreenId, key.screenId);
    const bool focusMoved = state->strip().focusWindow(windowId, params);
    // A view still detached AFTER the focus move is one no re-anchor took
    // back, and there are two ways to arrive here holding one. focusWindow
    // REFUSED the report, because it names the window the strip already calls
    // active (scrollstrip_navigation.cpp's same-column, same-tile bail); or it
    // accepted a same-COLUMN tile move, which re-anchors nothing because no
    // strip geometry moved. Both are right about the focus SLOT and wrong
    // about the VIEW. A pan detaches the view from the centering policy, and
    // the re-anchor that re-attaches it (reanchorAfterFocusChange) is reached
    // from focusWindow on a COLUMN change and on nothing else, so neither of
    // these two outcomes gets there. No later pass revisits the question
    // either: updateViewForFocus returns early while detached, so even the
    // applyLayout a desktop return runs cannot re-derive the anchor. The
    // result was that clicking the focused window, or switching away from its
    // desktop and back, did nothing at all — the whole report was dropped,
    // latch and all.
    //
    // Re-attach and let the POLICY answer, rather than re-anchoring outright.
    // Under Never/OnOverflow updateViewForFocus leaves a fully-visible column
    // alone, so a pan that kept the focused column on screen survives an
    // incidental activation — KWin re-fires windowActivated on restacking,
    // fullscreen exit and desktop switches, not only on real focus moves.
    // Under Always it re-centres, which is what that setting asks for.
    //
    // The report must NAME the active window: a minimized tile in the active
    // column is refused by focusWindow without becoming the column's active
    // tile, and that report has no claim on the view.
    const bool activeReport = state->strip().activeWindowId() == windowId && !dragPreviewSteersView;
    const bool handBackView = state->strip().viewDetached() && activeReport;
    // ATTACHED-view twin of the hand-back: the report names the active window,
    // focusWindow refused it (same column), and there is no detach latch to
    // clear — but the active column sits entirely OFF the viewport, so the
    // user just activated a window they cannot see. That state is reachable
    // without any pan: a desktop return renders from the stored per-context
    // anchor, and when that anchor and the active column disagree the column
    // renders parked off-screen while the view stays attached. Dropping the
    // report here left every click on the parked window dead (the tester's
    // "missing from the strip" windows). Let it through: applyLayout's own
    // updateViewForFocus re-anchors an attached view whose active column is
    // off-screen, under every anchor policy. Viewport INTERSECTION on
    // purpose, not full visibility — a partially visible column stays put, so
    // KWin's incidental re-activations (restacking, fullscreen exit, desktop
    // switch) still cannot nudge a view the user can see their window in.
    const int activeIdx = state->strip().activeColumnIndex();
    const bool activeOffViewport = activeReport && !state->strip().viewDetached() && activeIdx >= 0
        && !state->strip().visibleColumnIndices(params).contains(activeIdx);
    if (!focusMoved && !handBackView && !activeOffViewport) {
        // A refused report for a BACKGROUND context is not the no-op it is
        // for the current one. All three refusal tests above measured the
        // STORED strip state, but the compositor's own scroll view is keyed
        // per output, not per desktop, so what it is actually showing for
        // this strip can disagree with everything the tests trusted. Arm the
        // pending emit anyway: the desktop return then re-asserts this
        // strip's geometry unconditionally, which is exactly the repair a
        // report the model could not classify still deserves.
        //
        // Membership-gated, unlike the mutate arm below which by construction
        // has one: a refused open can leave a reverse-map key whose state holds
        // the window in neither the strip nor the float set, and a focus report
        // naming such a phantom would otherwise arm a forced full-place batch
        // for a context that never held it.
        if (key != currentKeyForScreen(key.screenId) && state->containsWindow(windowId)) {
            m_pendingFocusEmitContexts.insert(key);
        }
        return;
    }
    if (handBackView) {
        state->strip().setViewDetached(false);
    }
    // The focus change may scroll the viewport; never re-activate here (the
    // compositor initiated this focus). Background-context guard: see
    // windowClosed. The latch is cleared for a background context too (it is
    // persisted state), but only the on-screen context re-derives the anchor
    // now — a background one re-derives on the applyLayout its own desktop
    // return runs, which is the pass that used to return early.
    if (key == currentKeyForScreen(key.screenId)) {
        // Close-settle hold, second arm: the compositor's successor pick
        // lands here milliseconds after a close, and its reanchor reflow
        // moving the neighbours would defeat the hold windowClosed just
        // started. The anchor/focus state above is already updated — only
        // the geometry emission waits; the scheduled flush replays it.
        if (!deferForCloseReflowHold(key.screenId)) {
            applyLayout(key.screenId, false);
        }
    } else {
        // Background context: the strip's focus and anchor moved above, but
        // no geometry batch carries it — and the desktop return that brings
        // this context on screen cannot be trusted to emit one on its own.
        // Its retile's rects can all match the stored baseline (the strip is
        // returning to where it was), and the context switch's own force arm
        // is a screen-keyed flag any interleaved pass can spend. Record the
        // KEY this report belongs to; applyLayout promotes it to a forced
        // emit only when it runs with this context current, so the centering
        // from this activation survives whatever ordering the desktop switch
        // and the focus report arrive in.
        m_pendingFocusEmitContexts.insert(key);
    }
    // Focus and view anchor are persisted (serializeStripState), and
    // placementChanged is the only thing that marks DirtyScrollStrips.
    // Emitted for a background context too: the strip that changed is
    // serialized whether or not it is the one on screen right now.
    Q_EMIT placementChanged(key.screenId);
}

// Minimum-size bookkeeping (windowMinimumSize / windowMinSizeUpdated) and the
// client resize echo (onWindowResized) live in engine_minsize.cpp — split out
// when this file crossed the size ceiling a FOURTH time.

// The cross-engine handoff section (handoffRelease / handoffReceive and the
// unified placement capture) lives in engine_handoff.cpp — split out when
// this file crossed the size ceiling a second time, on the seam
// engine_float.cpp's first split established. claimCrossScreenReopen went to
// engine_reopen.cpp on the third crossing.

} // namespace PhosphorScrollEngine

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Durable strip-structure snapshots. serializeStripState captures every live
// strip plus the un-consumed mode-round-trip stash as one JSON blob the
// daemon persists through the WTA KConfig layer; restoreStripState loads a
// blob back INTO THE STASH so the existing arrival-restore path
// (restoreFromStripStash) rebuilds each strip as its windows are announced —
// there is deliberately no second restore mechanism.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include "enginelimits.h"
#include "scrollenginelogging.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <utility> // std::as_const

namespace PhosphorScrollEngine {

namespace {

// JSON keys — QLatin1String per the Qt6 string rules. Schema is
// version-free by the no-ad-hoc-migration policy: absent key = empty,
// malformed entry = skipped.
inline QLatin1String kColumns()
{
    return QLatin1String("columns");
}
inline QLatin1String kFocused()
{
    return QLatin1String("focusedWindow");
}
/// The AXIS the blob was written under. ADDITIVE, and absent reads Horizontal
/// — which is correct by construction for every blob written before strips
/// could run vertically.
///
/// No schema version bump: this file's format is deliberately version-free
/// and additive (see the header note), and three keys have already landed
/// exactly this way — presetFraction, windowedFullscreen and
/// unclaimedSessions. Absent-key-means-default IS the migration mechanism
/// here, and it is not the ad-hoc per-key migration the project rule forbids:
/// that rule governs Settings/ConfigDefaults keys, which this is not.
inline QLatin1String kAxis()
{
    return QLatin1String("axis");
}

inline QLatin1String kViewAnchor()
{
    return QLatin1String("viewAnchor");
}
/// Whether that anchor was an explicit pan (ScrollStrip's View detachment
/// section). ADDITIVE on the same terms as kAxis above, and absent reads
/// false — which is what every pre-key blob means, since no session that
/// wrote one could detach the view in the first place.
inline QLatin1String kViewDetached()
{
    return QLatin1String("viewDetached");
}
/// How far the strip had worked through its template blueprint. ADDITIVE on
/// the same terms as kAxis above, and absent reads 0 — which is exactly what a
/// blob written before the key existed means, since the reader's floor at the
/// live column count then recovers the same lower bound it always did.
///
/// The blueprint IDENTITY the cursor counts against is deliberately NOT
/// written beside it: a state staged from this blob has no established
/// identity, so the consumption site stamps whatever blueprint is in force and
/// keeps the cursor. Persisting the value would buy the same answer and add a
/// staleness class (a template edited between sessions) for nothing. See
/// StashedStrip::blueprintIdentity.
inline QLatin1String kBlueprintCursor()
{
    return QLatin1String("blueprintCursor");
}
/// Sanity ceiling for the restored cursor, the same shape and purpose as the
/// viewAnchor bound below: wide enough for any real session, narrow enough
/// that a hand-edited INT_MAX cannot overflow the arithmetic it feeds. NOT
/// kMaxTemplateEntries — the cursor counts every column the strip has ever
/// opened, not just the ones a blueprint described, so it legitimately runs
/// past the blueprint's length and the consumption site bounds the INDEX
/// separately.
constexpr int kMaxRestoredBlueprintCursor = 100000;
inline QLatin1String kUnclaimedSessions()
{
    return QLatin1String("unclaimedSessions");
}
/// Logins an entry may be staged without a single tile being claimed before it
/// is dropped. Small on purpose: the entry only survives at all because its app
/// might come back, and three cold starts is generous evidence that it will not.
constexpr int kMaxUnclaimedSessions = 3;
inline QLatin1String kTiles()
{
    return QLatin1String("tiles");
}
inline QLatin1String kDisplay()
{
    return QLatin1String("display");
}
inline QLatin1String kActiveWindow()
{
    return QLatin1String("activeWindow");
}
inline QLatin1String kWindowId()
{
    return QLatin1String("windowId");
}
inline QLatin1String kMinimized()
{
    return QLatin1String("minimized");
}
inline QLatin1String kWindowedFullscreen()
{
    return QLatin1String("windowedFullscreen");
}
inline QLatin1String kKind()
{
    return QLatin1String("kind");
}
inline QLatin1String kProportion()
{
    return QLatin1String("proportion");
}
inline QLatin1String kWeight()
{
    return QLatin1String("weight");
}
inline QLatin1String kFixedPx()
{
    return QLatin1String("fixedPx");
}
inline QLatin1String kPresetIdx()
{
    // LEGACY read-only key: pre-value-anchor blobs stored the preset INDEX.
    // New blobs write kPresetFraction; the readers resolve a legacy index
    // against the restoring screen's effective list (claim-site fixup).
    return QLatin1String("presetIdx");
}
inline QLatin1String kPresetFraction()
{
    return QLatin1String("presetFraction");
}
inline QLatin1String kWidth()
{
    return QLatin1String("width");
}
inline QLatin1String kHeight()
{
    return QLatin1String("height");
}

QJsonObject widthToJson(const ColumnWidth& w)
{
    QJsonObject obj;
    obj.insert(kKind(), static_cast<int>(w.kind));
    obj.insert(kProportion(), w.proportion);
    obj.insert(kFixedPx(), w.fixedPx);
    obj.insert(kPresetFraction(), w.presetFraction);
    return obj;
}

/// @p legacyVocab: the restoring screen's effective preset-width list, used
/// ONLY to resolve a pre-value-anchor blob's stored index into a fraction
/// (fromJson stays pure — the vocabulary is a parameter, threaded in by
/// restoreStripState which knows the screen). Version-free additive per the
/// policy above; a blob written by this build is unreadable-as-preset by
/// older builds (stash entries expire after three sessions regardless).
ColumnWidth widthFromJson(const QJsonObject& obj, const QList<qreal>& legacyVocab)
{
    // Persisted config is user-writable, so this is a system boundary: every
    // numeric field is bounded to the range its producers already enforce
    // (refreshConfigFromSettings, the open-rule override and
    // effectiveDefaultColumnWidth all clamp proportion to [0.05, 1.0]).
    // Unbounded, a hand-edited or corrupt value reaches proportionalPx, where
    // qRound() of a huge double to int is undefined, and a zero or negative
    // proportion pins the column at 1px for the rest of the session.
    ColumnWidth w;
    const int kind = obj.value(kKind()).toInt(static_cast<int>(ColumnWidth::Proportion));
    w.kind = (kind == ColumnWidth::Fixed || kind == ColumnWidth::Preset) ? static_cast<ColumnWidth::Kind>(kind)
                                                                         : ColumnWidth::Proportion;
    w.proportion = qBound<qreal>(MinColumnWidthFraction, obj.value(kProportion()).toDouble(0.5), 1.0);
    w.fixedPx = qMax(0, obj.value(kFixedPx()).toInt(0));
    if (obj.contains(kPresetFraction())) {
        w.presetFraction = qBound<qreal>(MinColumnWidthFraction, obj.value(kPresetFraction()).toDouble(0.5), 1.0);
    } else {
        // Legacy blob: resolve the stored index against the current
        // vocabulary. If the list changed since the write the anchor lands on
        // a slightly different value — accepted degradation, smoothed by
        // snap-at-resolve.
        const int legacyIdx = qMax(0, obj.value(kPresetIdx()).toInt(0));
        w.presetFraction =
            legacyVocab.isEmpty() ? 0.5 : legacyVocab.at(qBound(0, legacyIdx, int(legacyVocab.size()) - 1));
    }
    return w;
}

QJsonObject heightToJson(const WindowHeight& h)
{
    QJsonObject obj;
    obj.insert(kKind(), static_cast<int>(h.kind));
    obj.insert(kWeight(), h.weight);
    obj.insert(kFixedPx(), h.fixedPx);
    obj.insert(kPresetFraction(), h.presetFraction);
    return obj;
}

/// Same legacy-vocabulary contract as widthFromJson.
WindowHeight heightFromJson(const QJsonObject& obj, const QList<qreal>& legacyVocab)
{
    WindowHeight h;
    const int kind = obj.value(kKind()).toInt(static_cast<int>(WindowHeight::Auto));
    h.kind = (kind == WindowHeight::Fixed || kind == WindowHeight::Preset) ? static_cast<WindowHeight::Kind>(kind)
                                                                           : WindowHeight::Auto;
    // Same boundary hardening as widthFromJson: a non-positive weight would
    // divide the auto-height share by zero-or-negative, and a negative
    // fixedPx/presetIdx indexes out of range.
    h.weight = qBound<qreal>(0.01, obj.value(kWeight()).toDouble(1.0), 100.0);
    h.fixedPx = qMax(0, obj.value(kFixedPx()).toInt(0));
    if (obj.contains(kPresetFraction())) {
        h.presetFraction = qBound<qreal>(MinWindowHeightFraction, obj.value(kPresetFraction()).toDouble(0.5), 1.0);
    } else {
        const int legacyIdx = qMax(0, obj.value(kPresetIdx()).toInt(0));
        h.presetFraction =
            legacyVocab.isEmpty() ? 0.5 : legacyVocab.at(qBound(0, legacyIdx, int(legacyVocab.size()) - 1));
    }
    return h;
}

QString keyToString(const PhosphorEngine::PlacementStateKey& key)
{
    return key.screenId + QLatin1Char('|') + QString::number(key.desktop) + QLatin1Char('|') + key.activity;
}

// Right-anchored parse: activity is the last '|' segment, desktop the one
// before it, the rest is the screen id (screen ids never contain '|', but
// anchoring from the right keeps this true even if one ever did). The
// ACTIVITY segment shares the assumption from the other side: an activity id
// containing '|' would shift the desktop segment and fail the numeric
// parse, dropping that key's stash. KDE activity ids are UUIDs, so none
// carries one in practice; if that ever changes the parse must left-anchor
// (screen, then desktop, then activity = remainder) instead.
bool keyFromString(const QString& s, PhosphorEngine::PlacementStateKey* out)
{
    const int actSep = s.lastIndexOf(QLatin1Char('|'));
    if (actSep < 0) {
        return false;
    }
    const int deskSep = s.lastIndexOf(QLatin1Char('|'), actSep - 1);
    if (deskSep < 0) {
        return false;
    }
    bool ok = false;
    const int desktop = s.mid(deskSep + 1, actSep - deskSep - 1).toInt(&ok);
    // Persisted config is user-writable, so this is a system boundary and the
    // desktop is bounded like every other numeric read in this file. Virtual
    // desktops are 1-based (0 means "unset" in the context tracker); a
    // negative one names a key nothing can ever resolve to, so the entry
    // would sit in the stash forever.
    if (!ok || desktop < 0 || s.left(deskSep).isEmpty()) {
        return false;
    }
    out->screenId = s.left(deskSep);
    out->desktop = desktop;
    out->activity = s.mid(actSep + 1);
    return true;
}

} // namespace

QJsonObject ScrollEngine::serializeStripState() const
{
    QJsonObject out;
    const auto stashToJson = [](const StashedStrip& stash) {
        QJsonArray columns;
        for (const StashedColumn& col : stash.columns) {
            QJsonArray tiles;
            for (const StashedTile& tile : col.tiles) {
                QJsonObject t;
                t.insert(kWindowId(), tile.windowId);
                t.insert(kHeight(), heightToJson(tile.height));
                t.insert(kMinimized(), tile.minimized);
                t.insert(kWindowedFullscreen(), tile.windowedFullscreen);
                // PER-TILE lease. A tile still flagged staged-from-persistence
                // has not been claimed this session, so it ages by one; a
                // claimed tile (flag cleared, count zeroed) and a fresh
                // mode-exit stash both write 0. Per tile so a returning
                // co-tenant app cannot keep a dead sibling's tile alive.
                t.insert(kUnclaimedSessions(), tile.stagedFromPersistence ? tile.unclaimedSessions + 1 : 0);
                tiles.append(t);
            }
            QJsonObject c;
            c.insert(kTiles(), tiles);
            c.insert(kWidth(), widthToJson(col.width));
            c.insert(kDisplay(), static_cast<int>(col.display));
            c.insert(kActiveWindow(), col.activeWindowId);
            columns.append(c);
        }
        QJsonObject obj;
        obj.insert(kColumns(), columns);
        obj.insert(kFocused(), stash.focusedWindowId);
        obj.insert(kViewAnchor(), stash.viewAnchor);
        obj.insert(kViewDetached(), stash.viewDetached);
        obj.insert(kAxis(), static_cast<int>(stash.axis));
        obj.insert(kBlueprintCursor(), stash.blueprintCursor);
        return obj;
    };
    // Every window id that is LIVE on some strip right now. Writing the stash
    // first and the live strips second makes a live strip win its own KEY, but
    // a window that migrated screen or desktop after a mode exit is duplicated
    // across two DIFFERENT keys, which is not a collision and so is not
    // resolved by write order at all. restoreStripState's cross-key dedup then
    // keeps whichever key it meets first — alphabetical by "screen|desktop|
    // activity", not newest — so a stale stash tile could displace the
    // authoritative live one and restore the window to its old screen's slot.
    // Dropping the stale copy at WRITE time removes the ambiguity entirely.
    // One buildStashFromState walk per state: the snapshots feed both the
    // live-id sweep below and the output loop at the end.
    const auto& states = m_states.states();
    // The fallback axis for every screen with a state, resolved BEFORE the
    // walk below and handed in — the precondition buildStashFromState
    // documents. Its nullopt path resolves live through stripAxisForScreen,
    // which invokes the daemon-injected geometry and gap providers, and this
    // walk iterates the state map those providers must not be re-entered
    // during. setActiveScreens pre-resolves for exactly this reason.
    //
    // Two passes rather than one: the id collection touches nothing but the
    // keys, and only then does the resolve run, so no provider is called while
    // the map is being iterated. The resolve itself only LOOKS UP states (the
    // smart-gaps arm), which is safe. Per screen, not per state — the axis is
    // a screen-level verdict and several contexts share one.
    QSet<QString> screensWithState;
    for (auto it = states.cbegin(); it != states.cend(); ++it) {
        screensWithState.insert(it.key().screenId);
    }
    QHash<QString, PhosphorProtocol::ScrollAxis> fallbackAxisByScreen;
    fallbackAxisByScreen.reserve(screensWithState.size());
    for (const QString& screenId : std::as_const(screensWithState)) {
        fallbackAxisByScreen.insert(screenId, stripAxisForScreen(screenId).axis());
    }
    QHash<PhosphorEngine::PlacementStateKey, StashedStrip> liveStrips;
    liveStrips.reserve(states.size());
    QSet<QString> liveWindowIds;
    for (auto it = states.cbegin(); it != states.cend(); ++it) {
        // Hold the iterator in a named local rather than binding the
        // reference straight off insert(...).value(): the returned iterator
        // is a temporary, and while the value it names lives in the hash (so
        // the reference is sound), the compiler cannot prove that and warns.
        // Every screen holding a state was resolved above, so the lookup
        // always hits; the default is unreachable and named only because
        // QHash::value demands one.
        const PhosphorProtocol::ScrollAxis fallbackAxis =
            fallbackAxisByScreen.value(it.key().screenId, PhosphorProtocol::ScrollAxis::Horizontal);
        const auto inserted = liveStrips.insert(it.key(), buildStashFromState(it.value(), fallbackAxis));
        const StashedStrip& live = inserted.value();
        for (const StashedColumn& col : live.columns) {
            for (const StashedTile& tile : col.tiles) {
                liveWindowIds.insert(tile.windowId);
            }
        }
        // FLOATING windows are live too, but buildStashFromState walks only
        // strip columns: without this a window stashed on mode exit that came
        // back FLOATING kept its stale stash tile alive across saves, and the
        // cross-session claim could hand that dead slot to an unrelated
        // same-app window at the next login.
        const QStringList floating = it.value()->floatingWindows();
        for (const QString& windowId : floating) {
            liveWindowIds.insert(windowId);
        }
    }
    // Reverse-map residue too: a window tracked in neither the strip nor
    // the floating set (the state the drag/float heal arms exist to repair)
    // is still LIVE, and a save landing while one exists would let a stale
    // stash tile naming it survive the prune and be handed to a
    // cross-session claim — the same hazard the two structural walks above
    // guard against.
    const auto& trackedKeys = m_states.windowKeys();
    for (auto it = trackedKeys.cbegin(); it != trackedKeys.cend(); ++it) {
        liveWindowIds.insert(it.key());
    }
    // A drag-insert preview's dragged window is DETACHED — in no strip and
    // in no stash — but it is emphatically live, and without this a save
    // landing mid-hold lets a stale stash tile naming it win the write and
    // hand its slot to a cross-session claim. (Its own slot is still absent
    // from this save; a drop that never lands because the session ends
    // mid-drag is inherently unrecoverable aim.)
    if (m_dragInsertPreview) {
        liveWindowIds.insert(m_dragInsertPreview->windowId);
    }
    // Stash entries first (mode-round-trip structure not yet re-adopted),
    // live strips second so a live strip also wins its own key.
    //
    // The live sweep above only resolves stash-vs-LIVE. Two STASH entries can
    // name the same window as well — a window that left mode A on one screen
    // and later left mode A on another — and restoreStripState's reader-side
    // dedup is first-wins in QJsonObject key order, i.e. alphabetical by
    // "screen|desktop|activity", which has nothing to do with recency. So the
    // stashes are walked NEWEST FIRST and a window is written at most once:
    // the entry that saw it last is the one whose structure it belongs to.
    QList<PhosphorEngine::PlacementStateKey> stashKeys = m_stripStash.keys();
    std::sort(stashKeys.begin(), stashKeys.end(),
              [this](const PhosphorEngine::PlacementStateKey& a, const PhosphorEngine::PlacementStateKey& b) {
                  const quint64 seqA = m_stripStash.value(a).sequence;
                  const quint64 seqB = m_stripStash.value(b).sequence;
                  if (seqA != seqB) {
                      return seqA > seqB;
                  }
                  // Persisted entries all carry stamp 0 — break the tie by key
                  // so the written order is deterministic across runs.
                  return keyToString(a) < keyToString(b);
              });
    QSet<QString> writtenWindowIds;
    // Staged rather than written straight to `out`: a key can hold BOTH an
    // unconsumed stash and a live strip. Three windows leave scrolling and are
    // stashed at K, two of them re-announce, restoreFromStripStash holds the
    // entry back waiting on the third, and now K has a two-window live strip
    // beside a three-tile stash. The prune above drops the two live tiles, so
    // the stash still carries the third — and the live loop below inserts at
    // the same key, which in a QJsonObject REPLACES rather than merges. That
    // window's structure would vanish from the save entirely. So the surviving
    // stash columns are folded into the live entry instead.
    QHash<PhosphorEngine::PlacementStateKey, StashedStrip> prunedStashes;
    for (const PhosphorEngine::PlacementStateKey& key : std::as_const(stashKeys)) {
        StashedStrip pruned = m_stripStash.value(key);
        for (auto colIt = pruned.columns.begin(); colIt != pruned.columns.end();) {
            colIt->tiles.removeIf([&liveWindowIds, &writtenWindowIds](const StashedTile& tile) {
                return liveWindowIds.contains(tile.windowId) || writtenWindowIds.contains(tile.windowId);
            });
            colIt = colIt->tiles.isEmpty() ? pruned.columns.erase(colIt) : std::next(colIt);
        }
        if (pruned.isEmpty()) {
            continue;
        }
        for (const StashedColumn& col : std::as_const(pruned.columns)) {
            for (const StashedTile& tile : col.tiles) {
                writtenWindowIds.insert(tile.windowId);
            }
        }
        prunedStashes.insert(key, pruned);
    }
    for (auto it = liveStrips.cbegin(); it != liveStrips.cend(); ++it) {
        StashedStrip merged = it.value();
        // Live columns first: they are the strip as it stands, and the stash
        // holds only windows that have not come back to it. focusedWindowId,
        // the viewAnchor/viewDetached pair and the captured axis stay the LIVE
        // ones for the same reason.
        const StashedStrip stash = prunedStashes.take(it.key());
        merged.columns += stash.columns;
        // The blueprint cursor is the one field where the STASH can outrank
        // the live strip: it counts entries this context spent, and the stash
        // side holds the windows that have not come back yet, so its count can
        // be the higher of the two. Raised, never replaced — the same qMax the
        // arrival-time restore applies, for the same reason.
        merged.blueprintCursor = qMax(merged.blueprintCursor, stash.blueprintCursor);
        // Structure-less entries are not written. A context whose windows are
        // all floated has a cursor and no columns, and the reader drops a
        // column-less entry anyway; carrying one through the blob would add a
        // row per such context for a value the in-session stash already
        // preserves across the round trip that actually loses it (mode exit).
        // So a cursor survives a RESTART only alongside structure.
        if (!merged.isEmpty()) {
            out.insert(keyToString(it.key()), stashToJson(merged));
        }
    }
    // Whatever was left keyed at a context with no live strip.
    for (auto it = prunedStashes.cbegin(); it != prunedStashes.cend(); ++it) {
        out.insert(keyToString(it.key()), stashToJson(it.value()));
    }
    return out;
}

void ScrollEngine::restoreStripState(const QJsonObject& state)
{
    int restored = 0;
    // A window id may appear in exactly ONE staged tile, across every key.
    // restoreFromStripStash completes an entry when the count of distinct
    // consumed ids reaches its tile count, so a repeat inside one key makes
    // that entry permanently unconsumable (it is then re-consulted on every
    // later open of the context). A repeat ACROSS keys is not hypothetical —
    // serializeStripState legitimately writes a live strip for one key beside
    // an un-consumed stash for another that still lists the same window — and
    // would let two contexts stage it, so whichever announces second splices
    // it into a second strip.
    QSet<QString> claimedWindowIds;
    for (auto it = state.constBegin(); it != state.constEnd(); ++it) {
        // Count cap (see enginelimits.h): the numerics below are all
        // bounded at this boundary; the counts must be too, or a corrupt
        // blob stages unbounded structure the per-open stash walk then
        // pays for until the entries age out.
        if (restored >= kMaxRestoredKeys) {
            qCWarning(lcScrollEngine) << "restoreStripState: key cap reached (" << kMaxRestoredKeys
                                      << ") — dropping remaining persisted keys";
            break;
        }
        PhosphorEngine::PlacementStateKey key;
        if (!keyFromString(it.key(), &key) || !it.value().isObject()) {
            continue;
        }
        // Additive and conservative: an existing stash entry is newer
        // in-session state, and a live populated strip means the context
        // already adopted its windows — re-staging stale structure over
        // either would re-position later unrelated opens.
        if (m_stripStash.contains(key)) {
            continue;
        }
        if (const ScrollState* live = m_states.stateForKey(key); live && live->windowCount() > 0) {
            continue;
        }
        const QJsonObject obj = it.value().toObject();
        // Vocabularies for the LEGACY presetIdx fixup in the two fromJson
        // calls below — resolved once per key; the screen is in scope here,
        // which is why the fixup lives at this claim site.
        //
        // "Effective" means effective AT RESTORE TIME, which on the login
        // path is the settings list: the daemon hands this blob over from
        // initEnginesAndWiring, and a screen's template overrides are not
        // pushed until updateScrollingScreens runs later, out of start().
        // A legacy index therefore usually resolves against the settings
        // vocabulary even for a screen that ends up on a template. The
        // result is a value anchor either way, and relayout snaps it into
        // whatever vocabulary the screen finally has, so the reload path
        // (which does see the overrides) differs only in which entry the
        // anchor starts from.
        const QList<qreal> widthVocab = effectivePresetColumnWidths(key.screenId);
        const QList<qreal> heightVocab = effectivePresetWindowHeights(key.screenId);
        StashedStrip stash;
        stash.focusedWindowId = obj.value(kFocused()).toString();
        // Same boundary hardening as widthFromJson. The anchor is deliberately
        // NOT clamped to the strip (restoreViewAnchor: centered anchors imply
        // out-of-range viewOffset by design), so the bound is only a sanity range
        // wide enough for any real strip — it stops a hand-edited INT_MIN/MAX
        // from overflowing the viewOffset arithmetic it later feeds.
        stash.viewAnchor = qBound(-1000000, obj.value(kViewAnchor()).toInt(0), 1000000);
        stash.viewDetached = obj.value(kViewDetached()).toBool(false);
        // Absent reads 0, which is what every pre-key blob means. Bounded for
        // the same boundary-hardening reason as the anchor: the cursor feeds
        // qMax against the live column count and then indexes a blueprint.
        stash.blueprintCursor = qBound(0, obj.value(kBlueprintCursor()).toInt(0), kMaxRestoredBlueprintCursor);
        // Absent reads Horizontal, and an out-of-range int degrades the same
        // way rather than inventing an axis.
        stash.axis = PhosphorProtocol::scrollAxisFromInt(obj.value(kAxis()).toInt(0));
        const QJsonArray columns = obj.value(kColumns()).toArray();
        for (const QJsonValue& colVal : columns) {
            if (stash.columns.size() >= kMaxRestoredColumnsPerKey) {
                qCWarning(lcScrollEngine) << "restoreStripState: column cap reached for" << it.key() << "("
                                          << kMaxRestoredColumnsPerKey << ") — dropping the rest";
                break;
            }
            if (!colVal.isObject()) {
                continue;
            }
            const QJsonObject colObj = colVal.toObject();
            StashedColumn col;
            col.width = widthFromJson(colObj.value(kWidth()).toObject(), widthVocab);
            col.display = colObj.value(kDisplay()).toInt(0) == static_cast<int>(ColumnDisplay::Tabbed)
                ? ColumnDisplay::Tabbed
                : ColumnDisplay::Normal;
            const QJsonArray tiles = colObj.value(kTiles()).toArray();
            for (const QJsonValue& tileVal : tiles) {
                if (col.tiles.size() >= kMaxRestoredTilesPerColumn) {
                    qCWarning(lcScrollEngine) << "restoreStripState: tile cap reached in a column of" << it.key() << "("
                                              << kMaxRestoredTilesPerColumn << ") — dropping the rest";
                    break;
                }
                if (!tileVal.isObject()) {
                    continue;
                }
                const QJsonObject tileObj = tileVal.toObject();
                StashedTile tile;
                tile.windowId = tileObj.value(kWindowId()).toString();
                tile.height = heightFromJson(tileObj.value(kHeight()).toObject(), heightVocab);
                tile.minimized = tileObj.value(kMinimized()).toBool(false);
                // Additive key: an older blob reads false (version-free
                // policy above).
                tile.windowedFullscreen = tileObj.value(kWindowedFullscreen()).toBool(false);
                // PER-TILE lease, bounded at the system boundary like every
                // other numeric in this file (persisted config is
                // user-writable). Absent key reads 0, so an older blob gets a
                // full fresh lease. A tile at the cap has gone that many
                // logins without any claim; its app is not coming back, and
                // the aliveness sweep can never reach it (pruneStaleWindows
                // fires once per session, at bring-up, while the entry is
                // still sweep-exempt), so dropping it here is what stops it
                // living forever and eventually handing an unrelated same-app
                // window a long-dead slot. Per tile so a returning co-tenant
                // in the same key cannot renew a dead sibling's lease.
                tile.unclaimedSessions = qBound(0, tileObj.value(kUnclaimedSessions()).toInt(0), kMaxUnclaimedSessions);
                if (tile.unclaimedSessions >= kMaxUnclaimedSessions) {
                    qCInfo(lcScrollEngine) << "restoreStripState: dropping stashed tile" << tile.windowId << "for"
                                           << it.key() << "after" << tile.unclaimedSessions << "sessions with no claim";
                    continue;
                }
                tile.stagedFromPersistence = true;
                if (!tile.windowId.isEmpty() && !claimedWindowIds.contains(tile.windowId)) {
                    claimedWindowIds.insert(tile.windowId);
                    col.tiles.append(tile);
                }
            }
            if (!col.tiles.isEmpty()) {
                // The column's active tile (a tabbed column's shown tab) can
                // be dropped above by the per-tile lease or the cross-key
                // duplicate filter. A dangling id would point the tab
                // re-assert at a window this key never stages, so clear it
                // and let the arrival order decide, as it did before.
                col.activeWindowId = colObj.value(kActiveWindow()).toString();
                bool activeStaged = false;
                for (const StashedTile& t : std::as_const(col.tiles)) {
                    activeStaged = activeStaged || t.windowId == col.activeWindowId;
                }
                if (!activeStaged) {
                    col.activeWindowId.clear();
                }
                stash.columns.append(col);
            }
        }
        if (stash.isEmpty()) {
            continue;
        }
        // The focus is read from JSON before the tiles are filtered, and the
        // cross-key duplicate claim above can drop the very tile it names. A
        // stash whose focus belongs to no surviving tile never hands the focus
        // over on restore, so the round trip silently re-anchors on whichever
        // window arrives first — the regression the focus carry exists to fix.
        // Fall back to the first surviving tile rather than leaving it dangling.
        if (!stash.focusedWindowId.isEmpty()) {
            bool focusSurvives = false;
            for (const StashedColumn& col : std::as_const(stash.columns)) {
                for (const StashedTile& tile : col.tiles) {
                    if (tile.windowId == stash.focusedWindowId) {
                        focusSurvives = true;
                        break;
                    }
                }
                if (focusSurvives) {
                    break;
                }
            }
            if (!focusSurvives) {
                // Every surviving column has at least one tile (empty ones are
                // dropped above), and stash.isEmpty() was just checked.
                stash.focusedWindowId = stash.columns.first().tiles.first().windowId;
            }
        }
        // Recency stamp 0, NOT ++m_stashSequence: persisted entries must rank
        // below every in-session stash, and restoreStripState can run again
        // after in-session stashes exist (the persistence delegate calls it
        // from every loadState()). A counter stamp taken then would be HIGHER
        // than a genuinely newer mode-exit stash and win the newest-first
        // cross-key dedup — the exact inversion the stamp exists to prevent.
        // serializeStripState treats equal stamps as key-ordered, which is
        // fine here: the write side already deduped windows across the keys
        // of one snapshot.
        stash.sequence = 0;
        m_stripStash.insert(key, stash);
        m_stripStashConsumed.remove(key);
        ++restored;
    }
    if (restored > 0) {
        qCInfo(lcScrollEngine) << "restoreStripState: staged" << restored << "strip snapshot(s) for arrival restore";
    }
}

} // namespace PhosphorScrollEngine

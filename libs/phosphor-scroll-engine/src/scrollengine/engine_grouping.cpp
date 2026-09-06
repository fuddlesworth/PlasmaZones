// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Tab grouping on the open path: the live GroupSameAppAsTabs read, the host
// column scan and the grouped insert verb insertOpenedWindow delegates to.
// Split out of engine_lifecycle.cpp, which sits at the file-size ceiling; the
// open path's ORDERING (stash, consume rule, order seed, then grouping, then
// the fresh-open block) stays there, this file only answers "which column,
// by which key" and performs the join.

#include <PhosphorScrollEngine/ScrollEngine.h>

// Complete type needed for the qobject_cast in groupSameAppAsTabs.
#include <PhosphorScrollEngine/IScrollSettings.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollStrip.h>

#include <QHash>
#include <QString>
#include <QVector>

namespace PhosphorScrollEngine {

bool ScrollEngine::groupSameAppAsTabs() const
{
    if (auto* settings = qobject_cast<PhosphorEngine::IScrollSettings*>(engineSettings())) {
        return settings->scrollingGroupSameAppAsTabs();
    }
    return false;
}

int ScrollEngine::tabGroupColumnIndex(const ScrollStrip& strip,
                                      const std::function<bool(const QString&)>& inGroup) const
{
    const auto columnInGroup = [&](const Column& col) {
        // A column under a plain interactive move keeps its dragged tile in
        // the strip with a frozen rect (see setInteractiveDragWindow). It is
        // not a host: the join would make the dragged window a hidden tab
        // and flip the column tabbed under the user's cursor.
        if (!m_interactiveDragWindow.isEmpty()) {
            for (const Tile& tile : col.tiles) {
                if (tile.windowId == m_interactiveDragWindow) {
                    return false;
                }
            }
        }
        for (const Tile& tile : col.tiles) {
            // A minimized tile is not evidence: applyColumnDisplay would name
            // it the tabbed extent owner and relayout could never resolve it.
            if (tile.minimized) {
                continue;
            }
            if (inGroup(tile.windowId)) {
                return true;
            }
        }
        return false;
    };
    const QVector<Column>& columns = strip.columns();
    // The active column first: when several columns hold the group, the one
    // the user is working in is the one a new window of it belongs with.
    const int activeIdx = strip.activeColumnIndex();
    if (activeIdx >= 0 && activeIdx < columns.size() && columnInGroup(columns.at(activeIdx))) {
        return activeIdx;
    }
    for (int i = 0; i < columns.size(); ++i) {
        // Already probed above; the predicate is not free (a rule walk per
        // tile behind the memo), so do not ask twice.
        if (i == activeIdx) {
            continue;
        }
        if (columnInGroup(columns.at(i))) {
            return i;
        }
    }
    return -1;
}

ScrollEngine::GroupedOpenHost ScrollEngine::groupedOpenHost(const ScrollStrip& strip, const QString& windowId,
                                                            const QString& appId, const QString& screenId,
                                                            const ScrollOpenParams& openParams) const
{
    GroupedOpenHost host;
    // One resolve per sibling tile per open. The daemon's resolver is
    // deliberately uncached (it re-stamps screen context and walks the rule
    // set on every call), and the active-first probe plus the strip scan
    // would otherwise ask for the same tile twice.
    QHash<QString, ScrollOpenParams> memo;
    const auto resolveTile = [&](const QString& tileId) -> const ScrollOpenParams& {
        auto it = memo.find(tileId);
        if (it == memo.end()) {
            it =
                memo.insert(tileId, m_openParamsResolver ? m_openParamsResolver(tileId, screenId) : ScrollOpenParams{});
        }
        return it.value();
    };
    // Trimmed on both sides of the compare: the daemon trims before it hands
    // the name over, but an embedder's resolver may not, and a group named
    // by stray spaces is the same group. The compare is otherwise exact,
    // case included.
    const QString group = openParams.tabGroup ? openParams.tabGroup->trimmed() : QString();
    if (!group.isEmpty()) {
        host.named = true;
        host.columnIdx = tabGroupColumnIndex(strip, [&](const QString& tileId) {
            const ScrollOpenParams& tileParams = resolveTile(tileId);
            return tileParams.tabGroup && tileParams.tabGroup->trimmed() == group;
        });
        return host;
    }
    // An explicit openTabbed=false rule is the window opting OUT of tabs, and
    // the per-window rule outranks the global default (the card promises
    // exactly that). The named arm above is not gated on it: a rule that
    // names a group is asking for a tab.
    const bool optedOutOfTabs = openParams.tabbed && !*openParams.tabbed;
    // hasStableAppIdFor is defensive parity with the placement-record paths
    // rather than something this arm depends on: through currentAppIdFor a
    // bare id (no separator) is its own appId, so it can never equal another
    // window's, and the gate only saves the strip scan. Lazy on the settings
    // read, which qobject_casts the settings object: the appId gate is the
    // cheap half.
    if (optedOutOfTabs || !PhosphorEngine::hasStableAppIdFor(appId, windowId) || !groupSameAppAsTabs()) {
        return host;
    }
    // A tile a rule has NAMED into a group belongs to that group only: it is
    // not an app-group sibling, or an unnamed window of the app would be
    // pulled into a "work" column because one of its kind happens to be
    // tabbed there. The two keys are disjoint in both directions (the named
    // arm above never reads the app).
    host.columnIdx = tabGroupColumnIndex(strip, [&](const QString& tileId) {
        if (currentAppIdFor(tileId) != appId) {
            return false;
        }
        const ScrollOpenParams& tileParams = resolveTile(tileId);
        return !tileParams.tabGroup || tileParams.tabGroup->trimmed().isEmpty();
    });
    return host;
}

bool ScrollEngine::insertGroupedOpen(ScrollState* state, const QString& windowId, const QString& appId,
                                     const QString& screenId, const ScrollLayoutParams& params, int minWidth,
                                     int minHeight, const ScrollOpenParams& openParams, QString* outDisplacedTab,
                                     bool* outNamed)
{
    // Two outranking paths the caller's ordering cannot show: the stash's
    // cross-session fuzzy app claim (restoreFromStripStash, inside its grace
    // window) lands a same-app open in a DEAD sibling's stashed column before
    // this verb can offer the live one, an accepted cost of that claim; and a
    // daemon-restart re-announce with no persisted stash DOES group, on
    // purpose, because the strip is being rebuilt from nothing and the
    // setting describes the strip the user asked for.
    //
    // The host column is turned tabbed through the engaged override, so a
    // Normal stack becomes tabs on the first grouped arrival and a column
    // that is already tabbed keeps its owner (applyColumnDisplay no-ops on a
    // same-display write). The arriving window becomes the tab on show and
    // takes the strip focus, which the focus arm in windowOpened rewinds when
    // focus-new-windows says no, exactly as it does for insertWindow's focus;
    // the tab the host was showing is reported through @p outDisplacedTab so
    // that rewind can put it back on show too.
    const GroupedOpenHost host = groupedOpenHost(state->strip(), windowId, appId, screenId, openParams);
    if (host.columnIdx < 0) {
        return false;
    }
    const Column& hostCol = state->strip().columns().at(host.columnIdx);
    const int tileIdx = hostCol.tiles.size();
    const QString shownTab = hostCol.activeTileIdx >= 0 && hostCol.activeTileIdx < hostCol.tiles.size()
        ? hostCol.tiles.at(hostCol.activeTileIdx).windowId
        : QString();
    if (!state->strip().insertWindowIntoColumnAt(host.columnIdx, tileIdx, windowId, params, minWidth, minHeight,
                                                 ColumnDisplay::Tabbed)) {
        return false;
    }
    if (outDisplacedTab) {
        *outDisplacedTab = shownTab;
    }
    if (outNamed) {
        *outNamed = host.named;
    }
    return true;
}

} // namespace PhosphorScrollEngine

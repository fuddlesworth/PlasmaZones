// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overviewmodelbuilder.h"

#include <QJsonArray>
#include <QSet>

#include <algorithm>

namespace PlasmaZones {

namespace {

QJsonObject rectJson(const QRect& r)
{
    QJsonObject o;
    o.insert(QLatin1String("x"), r.x());
    o.insert(QLatin1String("y"), r.y());
    o.insert(QLatin1String("w"), r.width());
    o.insert(QLatin1String("h"), r.height());
    return o;
}

struct WindowRow
{
    QString id;
    QRect rect;
    bool floating = false;
    bool minimized = false;
    bool sticky = false;
    int column = -1;
    int tile = -1;
};

QJsonObject rowJson(const WindowRow& w)
{
    QJsonObject o;
    o.insert(QLatin1String("id"), w.id);
    o.insert(QLatin1String("rect"), rectJson(w.rect));
    o.insert(QLatin1String("floating"), w.floating);
    o.insert(QLatin1String("minimized"), w.minimized);
    o.insert(QLatin1String("sticky"), w.sticky);
    o.insert(QLatin1String("column"), w.column);
    o.insert(QLatin1String("tile"), w.tile);
    return o;
}

QJsonObject stripJson(const PhosphorEngine::OverviewStripEntry& strip, const QRect& output)
{
    QJsonObject o;
    o.insert(QLatin1String("viewOffset"), strip.viewOffset);
    QJsonArray columns;
    for (const PhosphorEngine::OverviewStripColumn& c : strip.columns) {
        QJsonObject col;
        col.insert(QLatin1String("rect"), rectJson(OverviewModelBuilder::toWorkspaceLocal(c.rect, output)));
        col.insert(QLatin1String("tabbed"), c.tabbed);
        col.insert(QLatin1String("activeTab"), c.activeTab);
        QJsonArray tiles;
        for (const PhosphorEngine::OverviewStripTile& t : c.tiles) {
            QJsonObject tile;
            tile.insert(QLatin1String("id"), t.windowId);
            tile.insert(QLatin1String("rect"), rectJson(OverviewModelBuilder::toWorkspaceLocal(t.rect, output)));
            tiles.append(tile);
        }
        col.insert(QLatin1String("tiles"), tiles);
        columns.append(col);
    }
    o.insert(QLatin1String("columns"), columns);
    return o;
}

PhosphorEngine::IOverviewModelSource* sourceFor(const OverviewModelBuilder::Inputs& in, QLatin1String mode)
{
    if (mode == OverviewMode::Snapping) {
        return in.snapping;
    }
    if (mode == OverviewMode::Tiling) {
        return in.tiling;
    }
    if (mode == OverviewMode::Scrolling) {
        return in.scrolling;
    }
    return nullptr;
}

} // namespace

QRect OverviewModelBuilder::toWorkspaceLocal(const QRect& engineRect, const QRect& outputGeometry)
{
    if (engineRect.isNull()) {
        return engineRect;
    }
    return engineRect.translated(-outputGeometry.topLeft());
}

QJsonObject OverviewModelBuilder::build(const Inputs& in)
{
    QJsonObject root;
    root.insert(QLatin1String("v"), 1);
    root.insert(QLatin1String("activity"), in.activity);
    QJsonObject screens;
    if (!in.map) {
        root.insert(QLatin1String("screens"), screens);
        return root;
    }

    // Tracked windows bucketed by screen, then by desktop. Sticky windows
    // (and windows with no resolvable desktop) bucket under 0 and are
    // attached to their screen's current workspace below.
    QHash<QString, QHash<int, QList<const OverviewTrackedWindow*>>> byScreen;
    for (const OverviewTrackedWindow& w : in.windows) {
        if (w.omitted || w.id.isEmpty() || w.screenId.isEmpty()) {
            continue;
        }
        const int bucket = w.sticky ? 0 : w.desktop;
        byScreen[w.screenId][bucket].append(&w);
    }

    for (const QString& screenId : in.map->screenOrder()) {
        const QRect output = in.screenGeometry ? in.screenGeometry(screenId) : QRect();
        QJsonObject screen;
        QJsonObject size;
        size.insert(QLatin1String("w"), output.width());
        size.insert(QLatin1String("h"), output.height());
        screen.insert(QLatin1String("logicalSize"), size);
        const int current = in.currentDesktopByScreen.value(screenId, 0);
        const QHash<int, QList<const OverviewTrackedWindow*>> tracked = byScreen.value(screenId);
        QStringList keys = in.virtualScreensFor ? in.virtualScreensFor(screenId) : QStringList();
        if (keys.isEmpty()) {
            keys = QStringList{screenId};
        }

        QJsonObject workspaces;
        const QList<PhosphorWorkspaces::WorkspaceEntry> slice = in.map->slice(screenId);
        for (int sliceIndex = 0; sliceIndex < slice.size(); ++sliceIndex) {
            const PhosphorWorkspaces::WorkspaceEntry& entry = slice.at(sliceIndex);
            const int desktop = in.desktopIndexOf ? in.desktopIndexOf(entry.desktopId) : 0;
            const QLatin1String mode = (desktop > 0 && in.modeFor) ? in.modeFor(screenId, desktop) : OverviewMode::None;
            PhosphorEngine::IOverviewModelSource* source = sourceFor(in, mode);

            QList<WindowRow> rows;
            QSet<QString> listed;
            QHash<QString, const OverviewTrackedWindow*> trackedById;
            for (const OverviewTrackedWindow* w : tracked.value(desktop)) {
                trackedById.insert(w->id, w);
            }
            bool stripEmitted = false;
            QJsonObject strip;
            if (source && desktop > 0) {
                for (const QString& key : keys) {
                    const PhosphorEngine::PlacementStateKey stateKey{key, desktop, in.activity};
                    const auto windows = source->overviewWindowsFor(stateKey);
                    if (windows) {
                        for (const PhosphorEngine::OverviewWindowEntry& e : *windows) {
                            if (e.windowId.isEmpty() || listed.contains(e.windowId)) {
                                continue;
                            }
                            // The daemon's tracking is the census: a window an
                            // engine still names but the daemon no longer tracks
                            // is a closed window the engine has not reaped yet.
                            const OverviewTrackedWindow* t = trackedById.value(e.windowId, nullptr);
                            if (!t) {
                                continue;
                            }
                            WindowRow row;
                            row.id = e.windowId;
                            row.rect = e.rect.isValid() ? toWorkspaceLocal(e.rect, output)
                                                        : toWorkspaceLocal(t->frame, output);
                            row.floating = e.floating;
                            row.minimized = e.minimized || t->minimized;
                            row.column = e.column;
                            row.tile = e.tile;
                            rows.append(row);
                            listed.insert(e.windowId);
                        }
                    }
                    // One strip per workspace on the wire: a subdivided output
                    // reports the first virtual screen's strip, its siblings'
                    // windows are still listed with their rects.
                    if (!stripEmitted && mode == OverviewMode::Scrolling) {
                        if (const auto stripEntry = source->overviewStripFor(stateKey)) {
                            strip = stripJson(*stripEntry, output);
                            stripEmitted = true;
                        }
                    }
                }
            }
            // Tracked windows the engine did not name: never visited by the
            // engine, stashed, or a disabled mode. Reported at their tracked
            // frame, non-floating (the overview never carries a float bit it
            // did not read from an engine).
            for (const OverviewTrackedWindow* t : tracked.value(desktop)) {
                if (listed.contains(t->id)) {
                    continue;
                }
                WindowRow row;
                row.id = t->id;
                row.rect = toWorkspaceLocal(t->frame, output);
                row.minimized = t->minimized;
                rows.append(row);
                listed.insert(t->id);
            }
            // Sticky windows appear once, on the screen's current workspace.
            if (desktop > 0 && desktop == current) {
                for (const OverviewTrackedWindow* t : tracked.value(0)) {
                    if (listed.contains(t->id)) {
                        continue;
                    }
                    WindowRow row;
                    row.id = t->id;
                    row.rect = toWorkspaceLocal(t->frame, output);
                    row.minimized = t->minimized;
                    row.sticky = true;
                    rows.append(row);
                    listed.insert(t->id);
                }
            }
            // Deterministic order so the change gate compares payloads, not
            // hash-iteration luck. The effect sorts by stacking order anyway.
            std::sort(rows.begin(), rows.end(), [](const WindowRow& a, const WindowRow& b) {
                return a.id < b.id;
            });
            QJsonArray windowsJson;
            for (const WindowRow& row : rows) {
                windowsJson.append(rowJson(row));
            }

            QJsonObject workspace;
            workspace.insert(QLatin1String("sliceIndex"), sliceIndex);
            workspace.insert(QLatin1String("index"), desktop);
            workspace.insert(QLatin1String("name"), entry.name);
            workspace.insert(QLatin1String("current"), desktop > 0 && desktop == current);
            workspace.insert(QLatin1String("mode"), QString(mode));
            workspace.insert(QLatin1String("windows"), windowsJson);
            if (stripEmitted) {
                workspace.insert(QLatin1String("strip"), strip);
            }
            workspaces.insert(entry.desktopId, workspace);
        }
        screen.insert(QLatin1String("workspaces"), workspaces);
        screens.insert(screenId, screen);
    }
    root.insert(QLatin1String("screens"), screens);
    return root;
}

} // namespace PlasmaZones

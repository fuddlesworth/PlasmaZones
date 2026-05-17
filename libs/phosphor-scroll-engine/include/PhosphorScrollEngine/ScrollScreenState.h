// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorScrollEngine/Column.h>
#include <phosphorscrollengine_export.h>

#include <PhosphorEngine/IPlacementState.h>

#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <utility>

namespace PhosphorScrollEngine {

/// Per-screen (per desktop/activity) scrollable-tiling state: the niri-style
/// horizontal strip of columns, the focused column/tile, and the viewport
/// offset.
///
/// This is the pure-logic data model — no KWin, no daemon, no geometry. It
/// implements PhosphorEngine::IPlacementState so the daemon's persistence and
/// D-Bus layers can read it uniformly alongside snap/autotile state.
///
/// Invariants: the strip never exposes an empty column; `activeColumnIndex`
/// is -1 when the strip is empty and otherwise a valid index; the focused
/// window survives mutations that do not remove it.
class PHOSPHORSCROLLENGINE_EXPORT ScrollScreenState final : public PhosphorEngine::IPlacementState
{
public:
    ScrollScreenState() = default;
    explicit ScrollScreenState(QString screenId);

    // ── Strip ───────────────────────────────────────────────────────────
    const QVector<Column>& columns() const
    {
        return m_columns;
    }
    int columnCount() const
    {
        return static_cast<int>(m_columns.size());
    }
    bool isEmpty() const
    {
        return m_columns.isEmpty();
    }

    /// Index of the focused column, or -1 when the strip is empty.
    int activeColumnIndex() const
    {
        return m_activeColumnIndex;
    }
    const Column* activeColumn() const;
    QString focusedWindowId() const;

    /// Viewport offset relative to the active column (logical pixels).
    /// Stored intent only; geometry resolution interprets it.
    qreal viewOffset() const
    {
        return m_viewOffset;
    }
    void setViewOffset(qreal offset)
    {
        m_viewOffset = offset;
    }

    // ── Window placement ────────────────────────────────────────────────
    /// Open @p windowId as a new column immediately right of the focused
    /// column, and focus it — niri's default new-window behaviour. No-op if
    /// the window is already managed.
    void addColumnForWindow(const QString& windowId);
    /// Add @p windowId as a new tile in the focused column, and focus it.
    /// Falls back to addColumnForWindow when the strip is empty.
    void addWindowToActiveColumn(const QString& windowId);
    /// Remove @p windowId from the strip or the floating set. Drops the
    /// column if it becomes empty. Returns true if the window was found.
    bool removeWindow(const QString& windowId);

    /// Mark @p windowId minimized or restored. A minimized tiled window keeps
    /// its slot in the strip but is excluded from the resolved layout —
    /// Karousel's TiledMinimized state — so its column collapses out of view
    /// (and reappears in place) without losing column/tile order. Returns
    /// true if the state changed. Minimizing the focused window hands focus
    /// to the first still-visible window in strip order; if every tiled
    /// window is minimized, focus is left unchanged.
    bool setWindowMinimized(const QString& windowId, bool minimized);
    /// Whether @p windowId is a minimized tiled window.
    bool isWindowMinimized(const QString& windowId) const;

    // ── niri column / tile operations ───────────────────────────────────
    /// Pull the focused tile of the next column into the focused column.
    bool consumeIntoColumn();
    /// Push the focused tile out into its own new column, right of the
    /// current one, and focus it. No-op if the column has only one tile.
    bool expelFromColumn();
    /// Move column focus by @p delta (clamped). Returns true if it moved.
    bool focusColumn(int delta);
    /// Move tile focus within the focused column by @p delta (clamped).
    bool focusTile(int delta);
    /// Focus the column and tile holding @p windowId. Returns false if the
    /// window is not tiled.
    bool focusWindow(const QString& windowId);
    /// Reorder the focused column by @p delta; focus follows it.
    bool moveColumn(int delta);
    /// Reorder the focused tile within its column by @p delta; focus follows.
    bool moveTile(int delta);

    // ── Width / height intent ───────────────────────────────────────────
    /// Set the focused column's width intent (no-op when the strip is empty).
    /// @p presetIndex tags which width preset the value corresponds to, or
    /// -1 when the width is not one of the presets.
    void setActiveColumnWidth(const ColumnWidth& width, int presetIndex = -1);
    /// Set the focused tile's height intent (no-op when the strip is empty).
    void setActiveTileHeight(const WindowHeight& height);

    // ── Floating set ────────────────────────────────────────────────────
    /// Mark @p windowId floating — removes it from the strip if tiled.
    void markFloating(const QString& windowId);
    /// Drop @p windowId from the floating set (does not re-tile it).
    void clearFloating(const QString& windowId);

    // ── IPlacementState ─────────────────────────────────────────────────
    QString screenId() const override;
    int windowCount() const override;
    QStringList managedWindows() const override;
    bool containsWindow(const QString& windowId) const override;
    bool isFloating(const QString& windowId) const override;
    QStringList floatingWindows() const override;
    QString placementIdForWindow(const QString& windowId) const override;
    int tiledWindowCount() const override;
    QJsonObject toJson() const override;

    static ScrollScreenState fromJson(const QJsonObject& obj);

private:
    /// Locate a window in the strip. Returns {columnIndex, tileIndex}, or
    /// {-1, -1} when the window is not tiled.
    std::pair<int, int> locateWindow(const QString& windowId) const;
    void clampActiveColumnIndex();
    /// Re-point the focus at @p preferredWindowId if it is still tiled,
    /// otherwise clamp the active index into range.
    void refocus(const QString& preferredWindowId);
    /// First non-minimized window in strip order, or empty when every tiled
    /// window is minimized. Used to move focus off a window being minimized.
    QString firstVisibleWindowId() const;

    QString m_screenId;
    QVector<Column> m_columns;
    int m_activeColumnIndex = -1;
    qreal m_viewOffset = 0.0;
    QSet<QString> m_floatingWindows;
};

} // namespace PhosphorScrollEngine

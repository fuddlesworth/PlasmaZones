// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace PlasmaZones {

/**
 * @brief Tracks tiling state for a single screen
 *
 * TilingState maintains all the mutable state needed for autotiling:
 * - Window order (insertion order determines tiling position)
 * - Master window count (how many windows in master area)
 * - Split ratio (master vs stack area ratio)
 * - Per-window floating state (excluded from tiling)
 *
 * This class is used by AutotileEngine to track state and by
 * TilingAlgorithm implementations to calculate zone geometries.
 *
 * Note: Window IDs are KWin's internal resource names (QString).
 */
class PLASMAZONES_EXPORT TilingState : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString screenName READ screenName CONSTANT)
    Q_PROPERTY(int windowCount READ windowCount NOTIFY windowCountChanged)
    Q_PROPERTY(int tiledWindowCount READ tiledWindowCount NOTIFY windowCountChanged)
    Q_PROPERTY(int masterCount READ masterCount WRITE setMasterCount NOTIFY masterCountChanged)
    Q_PROPERTY(qreal splitRatio READ splitRatio WRITE setSplitRatio NOTIFY splitRatioChanged)

public:
    /**
     * @brief Construct a TilingState for a specific screen
     * @param screenName Unique identifier for the screen
     * @param parent Parent QObject
     */
    explicit TilingState(const QString &screenName, QObject *parent = nullptr);
    ~TilingState() override = default;

    // Prevent copying (QObject rule)
    TilingState(const TilingState &) = delete;
    TilingState &operator=(const TilingState &) = delete;

    /**
     * @brief Get the screen name this state belongs to
     */
    QString screenName() const;

    // ═══════════════════════════════════════════════════════════════════════
    // Window Order Management
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Get total number of tracked windows (including floating)
     */
    int windowCount() const;

    /**
     * @brief Get number of tiled windows (excluding floating)
     */
    int tiledWindowCount() const;

    /**
     * @brief Get the ordered list of window IDs
     * @return Window IDs in tiling order (first = master, rest = stack)
     */
    QStringList windowOrder() const;

    /**
     * @brief Get only tiled (non-floating) windows in order
     */
    QStringList tiledWindows() const;

    /**
     * @brief Add a window to the tiling
     * @param windowId Window identifier
     * @param position Insert position (-1 = end, 0 = beginning/master)
     * @return true if window was added, false if already tracked or invalid
     */
    bool addWindow(const QString &windowId, int position = -1);

    /**
     * @brief Remove a window from the tiling
     * @param windowId Window to remove
     * @return true if window was found and removed
     */
    bool removeWindow(const QString &windowId);

    /**
     * @brief Move a window to a different position
     * @param fromIndex Current position
     * @param toIndex Target position
     * @return true if move was successful
     */
    bool moveWindow(int fromIndex, int toIndex);

    /**
     * @brief Swap two windows' positions
     * @param index1 First window position
     * @param index2 Second window position
     * @return true if swap was successful
     */
    bool swapWindows(int index1, int index2);

    /**
     * @brief Swap two windows by their IDs
     * @param windowId1 First window ID
     * @param windowId2 Second window ID
     * @return true if both windows found and swapped
     */
    bool swapWindowsById(const QString &windowId1, const QString &windowId2);

    /**
     * @brief Get the index of a window
     * @param windowId Window to find
     * @return Index in window order, or -1 if not found
     */
    int windowIndex(const QString &windowId) const;

    /**
     * @brief Check if a window is tracked
     */
    bool containsWindow(const QString &windowId) const;

    // ═══════════════════════════════════════════════════════════════════════
    // Master Management
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Get number of windows in master area
     */
    int masterCount() const;

    /**
     * @brief Set number of windows in master area
     * @param count New master count (clamped to 1..windowCount)
     */
    void setMasterCount(int count);

    /**
     * @brief Check if a window is in the master area
     */
    bool isMaster(const QString &windowId) const;

    /**
     * @brief Get windows currently in master area
     */
    QStringList masterWindows() const;

    /**
     * @brief Get windows currently in stack area
     */
    QStringList stackWindows() const;

    /**
     * @brief Promote a window to master (move to position 0)
     * @param windowId Window to promote
     * @return true if window found and promoted
     */
    bool promoteToMaster(const QString &windowId);

    // ═══════════════════════════════════════════════════════════════════════
    // Split Ratio
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Get the master/stack split ratio
     * @return Ratio where 0.6 = master gets 60% of width
     */
    qreal splitRatio() const;

    /**
     * @brief Set the master/stack split ratio
     * @param ratio New ratio (clamped to 0.1..0.9)
     */
    void setSplitRatio(qreal ratio);

    /**
     * @brief Increase split ratio by delta
     * @param delta Amount to increase (default 0.05)
     */
    void increaseSplitRatio(qreal delta = 0.05);

    /**
     * @brief Decrease split ratio by delta
     * @param delta Amount to decrease (default 0.05)
     */
    void decreaseSplitRatio(qreal delta = 0.05);

    // ═══════════════════════════════════════════════════════════════════════
    // Per-Window Floating State
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Check if a window is floating (excluded from tiling)
     */
    bool isFloating(const QString &windowId) const;

    /**
     * @brief Set a window's floating state
     * @param windowId Window to modify
     * @param floating true to exclude from tiling
     */
    void setFloating(const QString &windowId, bool floating);

    /**
     * @brief Toggle a window's floating state
     * @param windowId Window to toggle
     * @return Current floating state after toggle (unchanged if window not tracked)
     */
    bool toggleFloating(const QString &windowId);

    /**
     * @brief Get list of floating windows
     */
    QStringList floatingWindows() const;

    // ═══════════════════════════════════════════════════════════════════════
    // Focus Tracking
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Get the currently focused window
     */
    QString focusedWindow() const;

    /**
     * @brief Set the focused window
     * @param windowId Window that received focus
     */
    void setFocusedWindow(const QString &windowId);

    /**
     * @brief Get index of focused window in tiled list
     * @return Index or -1 if no focused window
     */
    int focusedTiledIndex() const;

    // ═══════════════════════════════════════════════════════════════════════
    // Serialization
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Serialize state to JSON
     */
    QJsonObject toJson() const;

    /**
     * @brief Deserialize state from JSON
     * @param json Serialized state
     * @param parent Parent QObject
     * @return New TilingState or nullptr on error
     */
    static TilingState *fromJson(const QJsonObject &json, QObject *parent = nullptr);

    /**
     * @brief Clear all state (remove all windows, reset to defaults)
     */
    void clear();

Q_SIGNALS:
    /**
     * @brief Emitted when window count changes (add/remove)
     */
    void windowCountChanged();

    /**
     * @brief Emitted when window order changes (move/swap)
     */
    void windowOrderChanged();

    /**
     * @brief Emitted when master count changes
     */
    void masterCountChanged();

    /**
     * @brief Emitted when split ratio changes
     */
    void splitRatioChanged();

    /**
     * @brief Emitted when a window's floating state changes
     * @param windowId Affected window
     * @param floating New floating state
     */
    void floatingChanged(const QString &windowId, bool floating);

    /**
     * @brief Emitted when focused window changes
     */
    void focusedWindowChanged();

    /**
     * @brief Emitted when any state change requires retiling
     */
    void stateChanged();

private:
    QString m_screenName;
    QStringList m_windowOrder;
    QSet<QString> m_floatingWindows;
    QString m_focusedWindow;
    int m_masterCount = 1;
    qreal m_splitRatio = 0.6;

    // Helper to emit stateChanged after other signals
    void notifyStateChanged();
};

} // namespace PlasmaZones

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QSet>
#include <QString>

namespace PlasmaZones {

/// Per-window qualifiers on TilingHandler's minimize-float records. All
/// answer the same question at the unminimize edge: does the window's frame
/// still equal the tile it is about to get? When it does, the unfloat is
/// deferred past KWin's unminimize animation and the deferral is invisible.
/// When it does not, the unfloat commits immediately so the window is never
/// shown at the stale rect and then hopped into its tile.
class MinimizeFloatMarks
{
public:
    /// The window was claimed at batch-announce time (already minimized when
    /// the screen entered autotile), so its geometry belongs to the prior mode.
    void markUntiled(const QString& windowId)
    {
        m_untiled.insert(windowId);
    }
    bool isUntiled(const QString& windowId) const
    {
        return m_untiled.contains(windowId);
    }

    /// The window crossed onto another autotile screen while minimize-floated,
    /// so its frame is the tile it held on the screen it left. Kept apart
    /// from the untiled mark: that one also tells the pre-tile capture the
    /// frame is genuine free geometry, which a tile rect is not.
    void markDisplaced(const QString& windowId)
    {
        m_displaced.insert(windowId);
    }
    bool isDisplaced(const QString& windowId) const
    {
        return m_displaced.contains(windowId);
    }

    /// Record the OTHER tiled windows on the screen at the moment @p windowId
    /// was minimize-floated.
    void recordPeers(const QString& windowId, QSet<QString> tiledOnScreen)
    {
        tiledOnScreen.remove(windowId);
        m_peers.insert(windowId, tiledOnScreen);
    }
    /// True when the screen's tiled set no longer matches the recorded peers:
    /// the layout moved on while the window was minimized (a sole window
    /// restoring at full area beside windows opened since). No record reads
    /// as unchanged.
    bool peersChanged(const QString& windowId, QSet<QString> tiledOnScreen) const
    {
        const auto recorded = m_peers.constFind(windowId);
        if (recorded == m_peers.constEnd()) {
            return false;
        }
        tiledOnScreen.remove(windowId);
        return tiledOnScreen != recorded.value();
    }

    void remove(const QString& windowId)
    {
        m_untiled.remove(windowId);
        m_displaced.remove(windowId);
        m_peers.remove(windowId);
    }
    void clear()
    {
        m_untiled.clear();
        m_displaced.clear();
        m_peers.clear();
    }

private:
    QSet<QString> m_untiled;
    QSet<QString> m_displaced;
    QHash<QString, QSet<QString>> m_peers;
};

} // namespace PlasmaZones

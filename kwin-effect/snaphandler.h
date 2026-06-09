// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorCompositor/AutotileState.h>

#include <QColor>
#include <QObject>
#include <QString>

namespace PlasmaZones {

using namespace PhosphorCompositor;

class PlasmaZonesEffect;

/**
 * @brief Handles snapping integration for PlasmaZones.
 *
 * The snap-mode counterpart to AutotileHandler. Owns the snap-side managed-
 * window border state (m_border, parallel to AutotileHandler::m_border) and the
 * title-bar / border lifecycle for snap-committed windows. Delegates window
 * lookups, border rendering, and cross-mode borderless queries back to the
 * effect through the m_effect back-pointer.
 *
 * Built on the shared PhosphorCompositor BorderState + AutotileStateHelpers so
 * snap and autotile share one standardized border mechanism. The effect's
 * mode-aware border resolver (resolveBorderStateFor) reads borderState() here
 * alongside AutotileHandler's so each window draws with the settings of the
 * mode that manages it.
 */
class SnapHandler : public QObject
{
    Q_OBJECT

public:
    explicit SnapHandler(PlasmaZonesEffect* effect, QObject* parent = nullptr);

    // ── Snap border-state lifecycle (mirrors AutotileHandler's set) ──

    /// Record @p windowId as snap-committed on @p screenId (idempotent), apply
    /// title-bar hiding if enabled, and (re)draw its border.
    void markWindowSnapped(const QString& windowId, const QString& screenId);
    /// Drop @p windowId from the snap set on every screen, restore its title
    /// bar if we hid it, and remove its border.
    void clearWindowSnapped(const QString& windowId);
    /// Apply/restore title-bar hiding across all currently snap-committed
    /// windows when the snapWindowHideTitleBars setting toggles.
    void updateSnapHideTitleBars(bool hide);
    /// Restore every snap-hidden title bar and drop the snap border set.
    /// Called on daemon loss / effect teardown (symmetric with
    /// AutotileHandler::restoreAllBorderless).
    void restoreAllSnapBorderless();
    /// Drop snap border/title-bar tracking for a window being destroyed. Pure
    /// bookkeeping — no setNoBorder/removeWindowBorder, the window is going away.
    void onWindowClosed(const QString& windowId);

    // ── Border rendering accessors — delegate to shared AutotileStateHelpers ──
    bool isBorderlessWindow(const QString& windowId) const
    {
        return AutotileStateHelpers::isBorderlessWindow(m_border, windowId);
    }
    bool isTiledWindow(const QString& windowId) const
    {
        return AutotileStateHelpers::isTiledWindow(m_border, windowId);
    }
    bool shouldShowBorderForWindow(const QString& windowId) const
    {
        return AutotileStateHelpers::shouldShowBorderForWindow(m_border, windowId);
    }
    /// Read-only view of the snap border state. The effect's mode-aware border
    /// resolution reads this alongside the parallel autotile BorderState so each
    /// window draws with the settings of the mode that manages it.
    const BorderState& borderState() const
    {
        return m_border;
    }
    bool hideTitleBars() const
    {
        return m_border.hideTitleBars;
    }
    bool showBorder() const
    {
        return m_border.showBorder;
    }
    void setShowBorder(bool show)
    {
        m_border.showBorder = show;
    }
    int borderWidth() const
    {
        return m_border.width;
    }
    void setBorderWidth(int w)
    {
        m_border.width = w;
    }
    int borderRadius() const
    {
        return m_border.radius;
    }
    void setBorderRadius(int r)
    {
        m_border.radius = r;
    }
    QColor borderColor() const
    {
        return m_border.color;
    }
    void setBorderColor(const QColor& c)
    {
        m_border.color = c;
    }
    QColor inactiveBorderColor() const
    {
        return m_border.inactiveColor;
    }
    void setInactiveBorderColor(const QColor& c)
    {
        m_border.inactiveColor = c;
    }

private:
    PlasmaZonesEffect* m_effect;
    // Snapping's own managed-window border state, parallel to
    // AutotileHandler::m_border. Populated at snap commit, cleared on
    // float / unsnap / close.
    BorderState m_border;
};

} // namespace PlasmaZones

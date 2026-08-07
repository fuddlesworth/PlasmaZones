// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/IPlacementState.h>
#include <PhosphorScrollEngine/ScrollStrip.h>
#include <phosphorscrollengine_export.h>

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace PhosphorScrollEngine {

/// Per-(screen, desktop, activity) scrolling state: one ScrollStrip plus the
/// windows floated out of it. Owned by ScrollEngine via PerScreenStates
/// (Qt-parent-owned), mirroring SnapState / TilingState.
///
/// Floating windows are NOT in the strip — a float pulls the window out and
/// its column closes up. An unfloat restores the remembered slot (stack
/// anchor, column index, width and display), falling back to a fresh column
/// next to the focused one only when none of that survives. The engine drives
/// both transitions; this object only stores membership.
class PHOSPHORSCROLLENGINE_EXPORT ScrollState : public QObject, public PhosphorEngine::IPlacementState
{
    Q_OBJECT

public:
    explicit ScrollState(const QString& screenId, QObject* parent = nullptr)
        : QObject(parent)
        , m_screenId(screenId)
    {
    }

    ScrollStrip& strip()
    {
        return m_strip;
    }
    const ScrollStrip& strip() const
    {
        return m_strip;
    }

    void addFloating(const QString& windowId)
    {
        m_floating.insert(windowId);
    }
    bool removeFloating(const QString& windowId)
    {
        return m_floating.remove(windowId);
    }

    /// The strip `viewX` carried by the last geometry batch this state
    /// actually emitted, and whether it has emitted one at all.
    ///
    /// The difference between this and the next batch's `viewX` is the view
    /// DELTA: how far the whole strip slid, as opposed to how far any one
    /// window moved. The effect springs that delta once per output and lets
    /// every carried window ride it, instead of starting an independent
    /// per-window spring each (see the `viewDeltaX` field on the tile-request
    /// wire).
    ///
    /// TRANSIENT — deliberately not serialized. A restored or freshly created
    /// state has nothing on screen to slide FROM, so its first batch must
    /// carry a zero delta and place windows outright. Living on the state
    /// rather than in a parallel per-screen hash means it dies with the
    /// context it describes, so no pruning path has to remember it.
    ///
    /// Only an EMITTED batch updates it. A relayout suppressed by the
    /// emit-on-change gate leaves the compositor showing the previous
    /// positions, so the baseline has to keep describing those.
    /// The baseline is only meaningful against the work area it was resolved
    /// in. Column widths are fractions of that area, so a resolution change, a
    /// panel appearing or a gap edit rescales every column's strip position
    /// and therefore the view coordinate itself — by an amount proportional to
    /// how deep the anchor sits on the strip, which on a long strip is
    /// thousands of logical pixels. A delta measured across two different
    /// areas describes a slide that never happened, and the effect would fly
    /// the whole strip in from off-screen to "recover" from it, once per
    /// emitted change while a user drags a gap slider.
    ///
    /// Stamped with the baseline so the next batch can tell whether the two
    /// share a basis. Note it does NOT catch a change to column widths or
    /// presets, which move strip positions without touching the work area.
    bool hasLastAppliedViewX() const
    {
        return m_hasLastAppliedViewX;
    }
    int lastAppliedViewX() const
    {
        return m_lastAppliedViewX;
    }
    QRect lastAppliedWorkArea() const
    {
        return m_lastAppliedWorkArea;
    }
    void setLastAppliedViewX(int viewX, const QRect& workArea)
    {
        m_lastAppliedViewX = viewX;
        m_lastAppliedWorkArea = workArea;
        m_hasLastAppliedViewX = true;
    }

    // ── IPlacementState ─────────────────────────────────────────────────────
    QString screenId() const override
    {
        return m_screenId;
    }
    int windowCount() const override
    {
        return m_strip.windowCount() + m_floating.size();
    }
    QStringList managedWindows() const override
    {
        QStringList all = m_strip.windowsInOrder();
        all += floatingWindows();
        return all;
    }
    bool containsWindow(const QString& windowId) const override
    {
        return m_strip.containsWindow(windowId) || m_floating.contains(windowId);
    }
    bool isFloating(const QString& windowId) const override
    {
        return m_floating.contains(windowId);
    }
    QStringList floatingWindows() const override
    {
        QStringList out(m_floating.cbegin(), m_floating.cend());
        out.sort();
        return out;
    }
    /// Opaque slot id: the window's index in strip order, matching the
    /// autotile convention (order index as string). Empty when floating.
    /// NOTE: this is the WINDOW index; the engine's capturePlacement stores
    /// the COLUMN index in slot.order (its comment explains why). The two
    /// notions coincide only while every column holds one tile — do not
    /// feed this id into a restore path expecting slot.order semantics.
    QString placementIdForWindow(const QString& windowId) const override
    {
        const int idx = m_strip.windowsInOrder().indexOf(windowId);
        return idx >= 0 ? QString::number(idx) : QString();
    }
    int tiledWindowCount() const override
    {
        return m_strip.windowCount();
    }

private:
    QString m_screenId;
    ScrollStrip m_strip;
    QSet<QString> m_floating;
    int m_lastAppliedViewX = 0;
    QRect m_lastAppliedWorkArea;
    bool m_hasLastAppliedViewX = false;
};

} // namespace PhosphorScrollEngine

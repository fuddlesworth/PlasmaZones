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
};

} // namespace PhosphorScrollEngine

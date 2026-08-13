// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/IPlacementState.h>
#include <PhosphorScrollEngine/ScrollStrip.h>
#include <PhosphorScrollEngine/StripAxis.h>
#include <phosphorscrollengine_export.h>

#include <QObject>
#include <QRect>
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
        // The two focus-memory fields move together: a state must never claim
        // "the float layer holds focus" while naming nobody. Every caller that
        // needs the pre-teardown verdict (unfloatWindowInternal) reads
        // floatingHasFocus() BEFORE calling this, per its own comment.
        if (m_lastFloatingFocus == windowId) {
            m_lastFloatingFocus.clear();
            m_floatingHasFocus = false;
        }
        return m_floating.remove(windowId);
    }

    /// Focus-side memory for switch-focus-between-floating-and-tiling: the
    /// float most recently reported focused by the compositor, and whether
    /// the float layer holds focus RIGHT NOW. Writers fall into three
    /// classes: genuine focus reports (windowFocused), the engine's own
    /// activation arms (applyLayout's focus arm and the floating/tiling
    /// switch), and the float/unfloat/adoption/handoff transitions that move
    /// focus with NO compositor round trip (floatWindowInternal's
    /// active-tile arm, unfloat's insert-refused restore, the
    /// boundary-move refusal adoption, handoffReceive's heldFocus seed, and
    /// clearSourceFloatFocusAfterCrossing on the clearing side). Neither
    /// field is serialized — focus history dies with the session.
    ///
    /// PER-STATE, while focus is global: only the state owning the newly
    /// focused window is updated by a focus report, so a sibling screen's
    /// flag can stay stale until the next report names a window that sibling
    /// tracks. Accepted: the flag self-heals on the next genuine focus report
    /// for that state, and clearing across states would couple per-screen
    /// ownership in ways no consumer currently needs.
    QString lastFloatingFocus() const
    {
        return m_lastFloatingFocus;
    }
    void setLastFloatingFocus(const QString& windowId)
    {
        m_lastFloatingFocus = windowId;
    }
    bool floatingHasFocus() const
    {
        return m_floatingHasFocus;
    }
    void setFloatingHasFocus(bool hasFocus)
    {
        m_floatingHasFocus = hasFocus;
    }

    /// The strip `viewOffset` carried by the last geometry batch this state
    /// actually emitted, and whether it has emitted one at all.
    ///
    /// The difference between this and the next batch's `viewOffset` is the view
    /// DELTA: how far the whole strip slid, as opposed to how far any one
    /// window moved. The effect springs that delta once per output and lets
    /// every carried window ride it, instead of starting an independent
    /// per-window spring each (see the `viewDelta` field on the tile-request
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
    bool hasLastAppliedViewOffset() const
    {
        return m_hasLastAppliedViewOffset;
    }
    int lastAppliedViewOffset() const
    {
        return m_lastAppliedViewOffset;
    }
    QRect lastAppliedWorkArea() const
    {
        return m_lastAppliedWorkArea;
    }
    /// The AXIS the baseline was measured along. Part of the basis, not a
    /// decoration: the view offset is a coordinate ALONG the strip, so two
    /// offsets taken on different axes are not comparable at all.
    StripAxis lastAppliedAxis() const
    {
        return m_lastAppliedAxis;
    }

    /// The axis this state was last RESOLVED against, which is a different
    /// question from lastAppliedAxis above.
    ///
    /// The applied basis is stamped only on an EMITTED batch, deliberately,
    /// so it keeps describing what the compositor is actually showing. A flip
    /// sweep cannot key on that: a relayout suppressed by the emit-on-change
    /// gate would leave the old axis standing and the sweep would fire again
    /// later against a basis it already converted. This one advances on every
    /// resolve.
    bool hasResolvedAxis() const
    {
        return m_hasResolvedAxis;
    }
    StripAxis resolvedAxis() const
    {
        return m_resolvedAxis;
    }
    void setResolvedAxis(StripAxis axis)
    {
        m_resolvedAxis = axis;
        m_hasResolvedAxis = true;
    }
    void setLastAppliedViewOffset(int viewOffset, const QRect& workArea, StripAxis axis)
    {
        m_lastAppliedViewOffset = viewOffset;
        m_lastAppliedWorkArea = workArea;
        m_lastAppliedAxis = axis;
        m_hasLastAppliedViewOffset = true;
    }
    /// Invalidate the baseline entirely: the next emitted batch takes the
    /// first-batch path (zero delta, outright placement). Called when the
    /// strip EMPTIES — the state object survives an empty period, and a
    /// baseline captured before it would describe a slide from a coordinate
    /// nothing on screen occupies, flying the repopulating window in from
    /// wherever the old view sat.
    void clearLastAppliedViewOffset()
    {
        m_lastAppliedViewOffset = 0;
        m_lastAppliedWorkArea = QRect();
        m_lastAppliedAxis = StripAxis::horizontal();
        m_hasLastAppliedViewOffset = false;
    }

    /// How many entries this strip has taken from the context template's seed
    /// blueprint since it was last empty.
    ///
    /// The blueprint is a SEED, not a standing rule: entry `i` describes the
    /// i-th column this strip grows, and once consumed it is spent. Deriving
    /// the index from the live column count instead made the blueprint refill
    /// any gap — closing a column handed its prescription straight back to the
    /// next open, so a column the user toggled to Normal came back Tabbed and
    /// the toggle read as broken.
    ///
    /// Readers take qMax(cursor, columnCount), never the cursor alone. Columns
    /// also materialize through paths that consume NOTHING (stash restore,
    /// mode-transition seed, unfloat, re-home), and without that floor a
    /// restored three-column strip would hand entry 0 to its next open. The
    /// floor states the real invariant: entry `i` is never given to a column
    /// when `i` columns already exist. While a strip only grows the two agree
    /// exactly, which is why growing a strip behaves as it always did.
    ///
    /// The floor is a lower bound on spent-ness, not a substitute for it: it
    /// recovers only as many entries as there are live columns, so a strip
    /// that LOST a column before travelling through a non-consuming path
    /// comes back under-counted. That is why the cursor rides the strip stash
    /// (StashedStrip::blueprintCursor) across a mode round trip rather than
    /// being rebuilt from the column count on the far side.
    ///
    /// Reset on exactly two events:
    ///   - the strip genuinely EMPTIES — no columns, nothing floating, no
    ///     detached drag window (applyLayout's empty branch, beside
    ///     clearLastAppliedViewOffset). A screen you cleared out starts its
    ///     next session from the top of the blueprint. The condition is the
    ///     STRIP, not the resolve: an all-minimized or mid-drag strip
    ///     resolves to no columns while still standing for its entries, and
    ///     resetting there handed them straight back out again.
    ///   - the blueprint itself CHANGES, noticed by comparing
    ///     blueprintIdentity at the consumption site. Picking a new template
    ///     is an explicit act, and a cursor describing the old template's
    ///     entries would swallow the new one's opening columns. Comparing the
    ///     VALUE rather than reacting to override-map writes is deliberate:
    ///     the map is dropped and rebuilt on ordinary context changes that
    ///     leave the template untouched.
    int blueprintCursor() const
    {
        return m_blueprintCursor;
    }
    void setBlueprintCursor(int cursor)
    {
        m_blueprintCursor = qMax(0, cursor);
    }
    void resetBlueprintCursor()
    {
        m_blueprintCursor = 0;
    }

    /// The blueprint value the cursor is counting against.
    ///
    /// The cursor indexes into one particular blueprint, so it is only
    /// meaningful beside the blueprint that produced it. Holding the value
    /// here lets the consumption site notice a swap by comparing, which is
    /// what makes invalidation independent of the override map's lifecycle:
    /// dropping and re-pushing the SAME template (every desktop switch away
    /// from scrolling and back does exactly that) leaves the cursor standing,
    /// while picking a different template restarts it. Keying the reset off
    /// the map's writes instead meant a screen that merely left scrolling for
    /// a moment came back with a cursor of 0 and refilled entries its columns
    /// already stood for.
    QVariant blueprintIdentity() const
    {
        return m_blueprintIdentity;
    }
    void setBlueprintIdentity(const QVariant& blueprint)
    {
        m_blueprintIdentity = blueprint;
    }

    // ── IPlacementState ─────────────────────────────────────────────────────
    QString screenId() const override
    {
        return m_screenId;
    }
    int windowCount() const override
    {
        // The two sets are disjoint by intent but not by construction — the
        // engine explicitly tolerates a window transiently in both (see
        // windowClosed's both-sets handling). Count the union, not the sum.
        int overlap = 0;
        for (const QString& id : m_floating) {
            if (m_strip.containsWindow(id)) {
                ++overlap;
            }
        }
        return m_strip.windowCount() + m_floating.size() - overlap;
    }
    QStringList managedWindows() const override
    {
        QStringList all = m_strip.windowsInOrder();
        all += floatingWindows();
        // Same union-not-sum rule as windowCount.
        all.removeDuplicates();
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
    QString m_lastFloatingFocus;
    bool m_floatingHasFocus = false;
    int m_lastAppliedViewOffset = 0;
    QRect m_lastAppliedWorkArea;
    StripAxis m_lastAppliedAxis;
    StripAxis m_resolvedAxis;
    bool m_hasResolvedAxis = false;
    bool m_hasLastAppliedViewOffset = false;
    int m_blueprintCursor = 0;
    QVariant m_blueprintIdentity;
};

} // namespace PhosphorScrollEngine

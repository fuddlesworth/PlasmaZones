// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

// Full definitions, not forward declarations, for both types this
// header's moc surface exposes as pointer properties. Qt6 moc
// auto-registers property metatypes and QMetaType static_asserts that a
// pointed-to type is complete (or declared opaque) — a fwd decl trips
// "Pointer Meta Types must either point to fully-defined types". Same
// rationale as PipeWireHost.h.
#include <QAbstractListModel>
#include <QList>
#include <QPointer>
#include <QQmlComponent>
#include <QQuickItem>

namespace PhosphorShell {

/**
 * @brief Instantiates one panel per screen, parented so ShellEngine can
 *        find it.
 *
 * Usage:
 *
 *     Item {
 *         PerScreenPanels {
 *             model: PhosphorShell.screens
 *             delegate: BarHost { }
 *         }
 *     }
 *
 * ## Why this exists rather than a Repeater or PerScreen
 *
 * `ShellEngine::materializePanels()` finds panels with `findChildren` on
 * the QML root and then DETACHES each one, handing ownership to the
 * `PhosphorLayer::Surface` it creates. Two properties follow from that,
 * and together they rule out every stock instantiator:
 *
 *  - Instances must be QObject CHILDREN of something under the root, or
 *    `findChildren` never sees them and they are never materialized. A
 *    PanelWindow that is not materialized is completely inert — no
 *    window, no wl_surface, no input — so it fails silently. `PerScreen`
 *    creates with `createObject(null)` (it must: its delegates are
 *    Windows, and a Window under a non-Window parent is never committed
 *    by Wayland compositors), so its instances are invisible to
 *    discovery.
 *
 *  - Whatever creates an instance must NOT also claim the right to
 *    destroy it. `Repeater` deletes its delegates on model reset, and
 *    `ScreenModel` resets on every hotplug — about 100 ms before
 *    ShellEngine's debounced reload. The Surface holds its content item
 *    by raw pointer, so a delegate deleted out from under it dangles.
 *    `PerScreen` and `Variants` both `destroy()` / `deleteLater()` what
 *    they built, for the same reason.
 *
 * This type does neither: it parents instances to itself, and it never
 * destroys them. Ownership passes cleanly to the Surface on
 * materialization; anything that failed to materialize is still a child
 * and dies with this item.
 *
 * PanelWindow is a QQuickItem, not a Window, so the Wayland constraint
 * that forces `PerScreen`'s null parent does not apply here. `PerScreen`
 * remains the right tool for per-monitor WINDOWS (wallpaper, OSD); this
 * is the right tool for per-monitor PANELS.
 *
 * ## One-shot by design
 *
 * The screen set is read once, at `componentComplete`. Topology changes
 * are NOT tracked here, because ShellEngine already responds to them by
 * tearing down and rebuilding the whole engine (debounced 100 ms), which
 * re-runs this enumeration against the new screen set. Reacting to model
 * changes as well would create duplicate panels in the window between the
 * model reset and the reload. Tracking a live model would also mean
 * destroying instances, which is exactly what this type must not do.
 *
 * Each instance is created in a context exposing `modelData` — a map of
 * the model's role names to their values for that row (`screen`, `name`,
 * `width`, `height`, `isPrimary` for ScreenModel) — matching `Variants`.
 * A delegate that is a PanelWindow and whose `screen` is still null gets
 * it assigned from the row, so the common case needs no bindings at all;
 * a delegate that binds `screen` to an output keeps that output. Note the
 * condition is "screen is null", not "the delegate never assigned one" —
 * those are indistinguishable after the fact, so a delegate that
 * deliberately binds `screen: null` is filled in anyway. That costs
 * nothing in practice: ShellEngine resolves a null panel screen to the
 * primary output, so binding null was never a way to opt out.
 */
class PHOSPHORSHELL_EXPORT PerScreenPanels : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(QAbstractListModel* model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QQmlComponent* delegate READ delegate WRITE setDelegate NOTIFY delegateChanged)
    /// Number of instances created. Reads 0 until componentComplete.
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_CLASSINFO("DefaultProperty", "delegate")

public:
    explicit PerScreenPanels(QQuickItem* parent = nullptr);
    ~PerScreenPanels() override;

    [[nodiscard]] QAbstractListModel* model() const;
    void setModel(QAbstractListModel* model);

    [[nodiscard]] QQmlComponent* delegate() const;
    void setDelegate(QQmlComponent* delegate);

    [[nodiscard]] int count() const;

Q_SIGNALS:
    void modelChanged();
    void delegateChanged();
    void countChanged();

protected:
    void componentComplete() override;

private:
    void build();
    /// Warn when a property that only matters before build() is written
    /// after it. Enumeration is one-shot, so such a write is silently inert.
    void warnIfAlreadyBuilt(const char* property) const;

    QPointer<QAbstractListModel> m_model;
    QPointer<QQmlComponent> m_delegate;
    // Latches in build(), unconditionally, so the one-shot contract holds
    // even when a build produced nothing.
    bool m_built = false;
    // QPointer rows: an instance leaves this list's control the moment
    // ShellEngine detaches it, and the Surface that adopts it may outlive
    // or predecease us. Nothing here ever deletes an instance — the
    // pointers exist so `count` and tests can observe what was built.
    QList<QPointer<QObject>> m_instances;
};

} // namespace PhosphorShell

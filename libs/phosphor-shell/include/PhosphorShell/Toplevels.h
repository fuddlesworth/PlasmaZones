// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <PhosphorWayland/ForeignToplevel.h>

#include <QAbstractListModel>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

// `create()` below names both of these. Every existing consumer happened
// to pull them in first, so the omission only surfaced once a translation
// unit included this header on its own — the usual way a unity build
// hides a missing declaration.
QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace PhosphorShell {

/**
 * @brief QAbstractListModel view of the live toplevel set.
 *
 * Translates `ForeignToplevelManager`'s `toplevelAdded` / `toplevelRemoved`
 * signals into proper `beginInsertRows` / `beginRemoveRows` model events,
 * so a `Repeater { model: Toplevels.model }` patches one delegate in /
 * out per change instead of rebuilding the whole list (which is what
 * happens when `model:` is bound to a `QList<...>` value property —
 * every change re-evaluates the binding and resets every delegate).
 *
 * Roles:
 *   - `toplevel` — the `ForeignToplevel*` for the row.
 *
 * Usage from QML:
 *
 *     Repeater {
 *         model: Toplevels.model
 *         delegate: Rectangle {
 *             required property var toplevel
 *             Text { text: toplevel.title }
 *         }
 *     }
 */
class PHOSPHORSHELL_EXPORT ToplevelListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        ToplevelRole = Qt::UserRole + 1
    };
    Q_ENUM(Role)

    explicit ToplevelListModel(PhosphorWayland::ForeignToplevelManager* manager, QObject* parent = nullptr);
    ~ToplevelListModel() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

private Q_SLOTS:
    void onToplevelAdded(PhosphorWayland::ForeignToplevel* toplevel);
    void onToplevelRemoved(PhosphorWayland::ForeignToplevel* toplevel);

private:
    QPointer<PhosphorWayland::ForeignToplevelManager> m_manager;
    // QPointer rows — a toplevel can be deleteLater'd between `closed`
    // and the actual delete; storing QPointer means a stale row never
    // returns a dangling pointer to a delegate's `var` property.
    QList<QPointer<PhosphorWayland::ForeignToplevel>> m_rows;
};

/**
 * @brief QML singleton exposing the live list of toplevel windows for taskbars.
 *
 * Wraps `PhosphorWayland::ForeignToplevelManager` and turns the imperative
 * `toplevelAdded`/`toplevelRemoved` signals into a notifying `toplevels`
 * Q_PROPERTY suitable for `Repeater { model: Toplevels.toplevels }`.
 *
 * Usage from QML:
 *
 *     import Phosphor.Shell
 *
 *     Repeater {
 *         model: Toplevels.toplevels
 *         delegate: Rectangle {
 *             required property var modelData  // ForeignToplevel*
 *             Text { text: modelData.title }
 *             MouseArea {
 *                 anchors.fill: parent
 *                 onClicked: modelData.activate()
 *             }
 *         }
 *     }
 *
 * The `supported` property is `false` on compositors that don't advertise
 * `zwlr_foreign_toplevel_manager_v1` (e.g. plain Mutter without extensions);
 * shells should hide their taskbar UI in that case rather than showing an
 * empty placeholder. KWin and the wlroots family (sway, Hyprland, Niri,
 * river, labwc) all support it.
 */
// Registered as a QML singleton imperatively in ShellEngine::load() via
// `qmlRegisterSingletonType<Toplevels>(..., &Toplevels::create)`. The
// QML_NAMED_ELEMENT / QML_SINGLETON macros would only be honoured by
// `qt_add_qml_module`, which the build doesn't use.
class PHOSPHORSHELL_EXPORT Toplevels : public QObject
{
    Q_OBJECT

    /// Snapshot list of currently-live toplevels. Useful for one-shot
    /// reads (`const list = Toplevels.toplevels`). DO NOT bind a
    /// Repeater or ListView's `model:` to this property — every change
    /// re-evaluates the QList and resets every delegate, including
    /// hover state, scroll position, and any per-delegate state. Bind
    /// `model: Toplevels.model` for incremental add/remove updates.
    Q_PROPERTY(QList<PhosphorWayland::ForeignToplevel*> toplevels READ toplevels NOTIFY toplevelsChanged)
    /// Incrementally-updating model — emits begin/endInsertRows and
    /// begin/endRemoveRows so consumers patch the delta instead of
    /// rebuilding. Bind `Repeater { model: Toplevels.model }`.
    Q_PROPERTY(QAbstractListModel* model READ model CONSTANT)
    Q_PROPERTY(bool supported READ isSupported CONSTANT)
    /// The toplevel the compositor currently reports as activated, or null
    /// when nothing is (the bare desktop, or a compositor that does not
    /// advertise the protocol).
    ///
    /// The protocol has no "which one is active" event — activation is a
    /// per-toplevel state flag — so anything that wants the focused window
    /// must watch every toplevel's `stateChanged` and re-scan. Doing that
    /// in QML costs a hidden delegate per window in every consuming
    /// widget; doing it once here costs one connection per toplevel for
    /// the whole process.
    Q_PROPERTY(PhosphorWayland::ForeignToplevel* activeToplevel READ activeToplevel NOTIFY activeToplevelChanged)

public:
    explicit Toplevels(QObject* parent = nullptr);
    ~Toplevels() override;

    /// QML-singleton factory hook.
    [[nodiscard]] static Toplevels* create(QQmlEngine* engine, QJSEngine* scriptEngine);

    [[nodiscard]] QList<PhosphorWayland::ForeignToplevel*> toplevels() const;
    [[nodiscard]] QAbstractListModel* model() const;
    [[nodiscard]] bool isSupported() const;
    [[nodiscard]] PhosphorWayland::ForeignToplevel* activeToplevel() const;

Q_SIGNALS:
    void toplevelsChanged();
    void activeToplevelChanged();

private:
    /// Wire one toplevel's state signals to the active scan. Does NOT
    /// scan — seeding subscribes many and scans once at the end.
    void subscribeToplevel(PhosphorWayland::ForeignToplevel* toplevel);
    /// Subscribe a newly-arrived toplevel and re-scan. Activation is not
    /// known at add time, so both halves matter on that path.
    void watchToplevel(PhosphorWayland::ForeignToplevel* toplevel);
    /// Re-pick the activated toplevel and emit only on a real change.
    void recomputeActive();
    /// Process-wide ForeignToplevelManager. Lazily constructed on first
    /// access and parented to qApp so it dies at process exit. Sharing
    /// across engines avoids two protocol bindings to the same global
    /// (which the wlroots protocol does not support — see
    /// `ForeignToplevelManager` docs: "Construct one per process").
    static PhosphorWayland::ForeignToplevelManager* sharedManager();

    // Per-engine model owned by this Toplevels wrapper. Reads from the
    // shared manager but provides a fresh QAbstractListModel for each
    // QML engine, so engine teardown cleanly tears down its own model
    // without disturbing siblings.
    ToplevelListModel* m_model = nullptr;

    // The last-published active toplevel. QPointer because a toplevel can
    // be destroyed without a preceding state change, and the getter is
    // read from arbitrary binding evaluations; a raw pointer would hand
    // QML a dangling ForeignToplevel* in that window.
    QPointer<PhosphorWayland::ForeignToplevel> m_active;
    // The pointer VALUE last published through activeToplevelChanged. Raw
    // and never dereferenced: it exists purely so the change gate compares
    // published state rather than a handle that can clear itself out from
    // under the comparison.
    PhosphorWayland::ForeignToplevel* m_publishedActive = nullptr;
};

} // namespace PhosphorShell

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

    /// Snapshot list of currently-live toplevels. Suitable for one-shot
    /// reads, but binding `Repeater { model: Toplevels.toplevels }` to
    /// it rebuilds every delegate on every change — use `model` instead
    /// for incremental updates.
    Q_PROPERTY(QList<PhosphorWayland::ForeignToplevel*> toplevels READ toplevels NOTIFY toplevelsChanged)
    /// Incrementally-updating model — emits begin/endInsertRows and
    /// begin/endRemoveRows so consumers patch the delta instead of
    /// rebuilding. Bind `Repeater { model: Toplevels.model }`.
    Q_PROPERTY(QAbstractListModel* model READ model CONSTANT)
    Q_PROPERTY(bool supported READ isSupported CONSTANT)

public:
    explicit Toplevels(QObject* parent = nullptr);
    ~Toplevels() override;

    /// QML-singleton factory hook.
    [[nodiscard]] static Toplevels* create(QQmlEngine* engine, QJSEngine* scriptEngine);

    [[nodiscard]] QList<PhosphorWayland::ForeignToplevel*> toplevels() const;
    [[nodiscard]] QAbstractListModel* model() const;
    [[nodiscard]] bool isSupported() const;

Q_SIGNALS:
    void toplevelsChanged();

private:
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
};

} // namespace PhosphorShell

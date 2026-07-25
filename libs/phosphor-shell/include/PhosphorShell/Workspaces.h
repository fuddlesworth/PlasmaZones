// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QAbstractListModel>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace PhosphorWorkspaces {
class VirtualDesktopManager;
} // namespace PhosphorWorkspaces

namespace PhosphorShell {

/**
 * @brief Incrementally-updating model of the compositor's workspaces.
 *
 * Roles:
 *   - `workspaceId` — the compositor's stable workspace identifier.
 *                    Not `id`: QML's parser rejects `id` as a delegate
 *                    property name, and qmlformat fails outright on it.
 *   - `name`        — the user-visible label.
 *   - `isActive`    — whether this is the current workspace.
 *
 * A workspace switch emits `dataChanged` over `isActive` only, so a pager
 * re-styles two pills instead of rebuilding every delegate. The list is
 * reset only when workspaces are actually added, removed, reordered, or
 * renamed.
 */
class PHOSPHORSHELL_EXPORT WorkspaceListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        IsActiveRole,
    };
    Q_ENUM(Role)

    explicit WorkspaceListModel(PhosphorWorkspaces::VirtualDesktopManager* manager, QObject* parent = nullptr);
    ~WorkspaceListModel() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

private:
    void reload();
    void refreshActive();

    QPointer<PhosphorWorkspaces::VirtualDesktopManager> m_manager;
    QStringList m_ids;
    QStringList m_names;
    // 0-based row of the active workspace, or -1 when none matches.
    int m_activeRow = -1;
};

/**
 * @brief QML singleton exposing the compositor's workspaces.
 *
 * Usage:
 *
 *     import Phosphor.Shell
 *
 *     Repeater {
 *         model: Workspaces.model
 *         delegate: Rectangle {
 *             required property string workspaceId
 *             required property string name
 *             required property bool isActive
 *             TapHandler { onTapped: Workspaces.switchTo(workspaceId) }
 *         }
 *     }
 *
 * ## Identifiers, not indices
 *
 * The model's `workspaceId` role and `switchTo(id)` are the whole interface for
 * addressing a workspace. Positions renumber whenever a workspace is
 * created or removed, so anything holding a workspace across such an event
 * — a pager delegate mid-animation, a rule, a shortcut — must key on the
 * id. This also matches the shape the planned CompositorService façade
 * specifies (`workspaces()` as a model, `switchToWorkspace(id)`), so the
 * QML side will not need to change when a non-KWin backend lands behind
 * it.
 *
 * `supported` is false wherever no workspace source answered — a non-KWin
 * compositor, or a session where KWin's D-Bus interface is absent.
 *
 * It is CONSTANT, and that is a real limitation rather than an oversight
 * to work around: the underlying availability flag is latched by a single
 * D-Bus probe at construction and there is no re-probe. A shell that
 * reaches this singleton before KWin has registered on the session bus
 * therefore reads false for the rest of the process. Gate UI on `count`
 * instead, which is notified and is already 0 whenever no source answered.
 * `supported` is for diagnostics and for telling "no compositor support"
 * apart from "supported, but zero workspaces" — a distinction no current
 * compositor actually produces.
 */
// Registered as a QML singleton imperatively in ShellEngine::load(), same
// as Toplevels; the QML_* macros would need qt_add_qml_module, which this
// library does not use.
class PHOSPHORSHELL_EXPORT Workspaces : public QObject
{
    Q_OBJECT

    /// Bind `Repeater { model: Workspaces.model }`.
    Q_PROPERTY(QAbstractListModel* model READ model CONSTANT)
    /// Identifier of the current workspace, or empty when unsupported.
    Q_PROPERTY(QString activeId READ activeId NOTIFY activeChanged)
    /// 1-based position of the current workspace, for a plain numeric
    /// readout. Do NOT use it to address a workspace — see the class docs.
    Q_PROPERTY(int activeIndex READ activeIndex NOTIFY activeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool supported READ isSupported CONSTANT)

public:
    explicit Workspaces(QObject* parent = nullptr);
    ~Workspaces() override;

    /// QML-singleton factory hook.
    [[nodiscard]] static Workspaces* create(QQmlEngine* engine, QJSEngine* scriptEngine);

    [[nodiscard]] QAbstractListModel* model() const;
    [[nodiscard]] QString activeId() const;
    [[nodiscard]] int activeIndex() const;
    [[nodiscard]] int count() const;
    [[nodiscard]] bool isSupported() const;

    /// Switch to the workspace with this identifier. Unknown or empty ids
    /// are ignored. Asynchronous: the compositor confirms by way of the
    /// change signals, so `activeId` does not update on return.
    Q_INVOKABLE void switchTo(const QString& id);

Q_SIGNALS:
    void activeChanged();
    void countChanged();

private:
    /// Process-wide VirtualDesktopManager, lazily constructed and parented
    /// to qApp. Shared for the same reason Toplevels shares its manager:
    /// one D-Bus subscription per process, and a hot reload rebuilds the
    /// QML engine without re-subscribing.
    static PhosphorWorkspaces::VirtualDesktopManager* sharedManager();

    WorkspaceListModel* m_model = nullptr;
    // Last-published values, so the notifications are change-gated rather
    // than mirroring every refresh the manager performs.
    QString m_activeId;
    int m_activeIndex = 0;
    int m_count = 0;
};

} // namespace PhosphorShell

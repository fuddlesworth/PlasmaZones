// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServices/phosphorservices_export.h>

#include <QAbstractListModel>
#include <QPointer>
#include <QQmlEngine>

namespace PhosphorServices {

class StatusNotifierHost;
class StatusNotifierItem;

/// QAbstractListModel adapter over StatusNotifierHost::items(). Hand
/// this to a QML Repeater / ListView. The roles cover everything a
/// typical tray delegate needs without exposing the raw item QObject
/// (kept available via `ItemObjectRole` for invoking action methods).
class PHOSPHORSERVICES_EXPORT StatusNotifierItemModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(PhosphorServices::StatusNotifierHost* host READ host WRITE setHost NOTIFY hostChanged)
    // `count` mirrors rowCount() and emits countChanged on every
    // insert/remove. QAbstractListModel does NOT expose this by
    // default — QML's `model.count` binding silently evaluates to
    // `undefined` without it, and any `visible: model.count > 0`
    // expression collapses to false, hiding the whole tray.
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        CategoryRole,
        StatusRole,
        // Icon URL forms — bind to QML's Image.source (a QUrl). The
        // underlying QImage is published to the
        // `image://phosphor-services/` provider as a side-effect of
        // model attachment / item icon changes.
        IconUrlRole,
        OverlayIconUrlRole,
        AttentionIconUrlRole,
        // Raw QImage forms — kept for C++ consumers / future ImageItem
        // bindings. Not directly usable as Image.source.
        IconImageRole,
        OverlayIconImageRole,
        AttentionIconImageRole,
        ToolTipTitleRole,
        ToolTipBodyRole,
        MenuPathRole,
        ItemIsMenuRole,
        DBusServiceRole,
        DBusPathRole,
        ItemObjectRole, ///< the raw StatusNotifierItem* for Q_INVOKABLE methods
    };
    Q_ENUM(Roles)

    explicit StatusNotifierItemModel(QObject* parent = nullptr);
    ~StatusNotifierItemModel() override;

    [[nodiscard]] StatusNotifierHost* host() const;
    void setHost(StatusNotifierHost* host);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// QML-friendly action invocations — looks up the item by row and
    /// dispatches. coords are in screen-logical pixels; the item's
    /// process uses them to position any popup it wants to render
    /// (some menus position relative to these).
    Q_INVOKABLE void activate(int row, int x, int y);
    Q_INVOKABLE void secondaryActivate(int row, int x, int y);
    Q_INVOKABLE void contextMenu(int row, int x, int y);
    Q_INVOKABLE void scroll(int row, int delta, const QString& orientation);
    Q_INVOKABLE PhosphorServices::StatusNotifierItem* itemAt(int row) const;

Q_SIGNALS:
    void hostChanged();
    void countChanged();

private:
    QPointer<StatusNotifierHost> m_host;

    void connectItem(StatusNotifierItem* item);
    void onItemAdded(StatusNotifierItem* item);
    void onItemRemoved(StatusNotifierItem* item);
    void onItemDataChanged(StatusNotifierItem* item);
    int rowFor(StatusNotifierItem* item) const;
};

} // namespace PhosphorServices

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceSni/phosphorservicesni_export.h>

// Qt's MOC needs fully-defined pointer types for Q_PROPERTY, signal
// parameters, and Q_INVOKABLE returns (qmetatype.h's
// `checkTypeIsSuitableForMetaType` static-asserts `is_complete<T>`).
// Forward decls compile in older Qt but fail with Qt ≥ 6.10.
#include <PhosphorServiceSni/StatusNotifierHost.h>
#include <PhosphorServiceSni/StatusNotifierItem.h>

#include <QAbstractListModel>

#include <memory>

namespace PhosphorServiceSni {

/// QAbstractListModel adapter over StatusNotifierHost::items(). Hand
/// this to a QML Repeater / ListView. The roles cover everything a
/// typical tray delegate needs without exposing the raw item QObject
/// (kept available via `ItemObjectRole` for invoking action methods).
class PHOSPHORSERVICESNI_EXPORT StatusNotifierItemModel : public QAbstractListModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(StatusNotifierItemModel)
    Q_PROPERTY(PhosphorServiceSni::StatusNotifierHost* host READ host WRITE setHost NOTIFY hostChanged)
    // `count` mirrors rowCount() and emits countChanged on every
    // insert/remove. QAbstractListModel does NOT expose this by
    // default: QML's `model.count` binding silently evaluates to
    // `undefined` without it, and any `visible: model.count > 0`
    // expression collapses to false, hiding the whole tray.
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        CategoryRole,
        StatusRole,
        // Icon URL forms: bind to QML's Image.source (a QUrl). The
        // underlying QImage is published to the
        // `image://phosphor-service-icontheme/` provider as a
        // side-effect of model attachment / item icon changes. The
        // URL host is resolved through
        // PhosphorServiceIconTheme::imageProviderUrlHost() at publish
        // time so any rename there propagates here without a hidden
        // string-match.
        IconUrlRole,
        OverlayIconUrlRole,
        AttentionIconUrlRole,
        // Raw QImage forms: kept for C++ consumers / future ImageItem
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

    /// QML-friendly action invocations: looks up the item by row and
    /// dispatches. coords are in screen-logical pixels; the item's
    /// process uses them to position any popup it wants to render
    /// (some menus position relative to these).
    Q_INVOKABLE void activate(int row, int x, int y);
    Q_INVOKABLE void secondaryActivate(int row, int x, int y);
    Q_INVOKABLE void contextMenu(int row, int x, int y);
    Q_INVOKABLE void scroll(int row, int delta, const QString& orientation);
    Q_INVOKABLE PhosphorServiceSni::StatusNotifierItem* itemAt(int row) const;

Q_SIGNALS:
    void hostChanged();
    void countChanged();

private:
    class Private;
    std::unique_ptr<Private> d;

    void connectItem(StatusNotifierItem* item);
    void onItemAdded(StatusNotifierItem* item);
    void onItemRemoved(StatusNotifierItem* item);
    void onItemDataChanged(StatusNotifierItem* item);
    void onItemIconChanged(StatusNotifierItem* item);
    [[nodiscard]] int rowFor(StatusNotifierItem* item) const;
};

} // namespace PhosphorServiceSni

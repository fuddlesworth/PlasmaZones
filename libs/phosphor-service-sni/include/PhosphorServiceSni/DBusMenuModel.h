// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceSni/phosphorservicesni_export.h>

#include <QAbstractListModel>
#include <QString>

#include <memory>

namespace PhosphorServiceSni {

/// Flat model exposing ONE level of a com.canonical.dbusmenu tree at
/// a time. Tree mode (QAbstractItemModel) is awkward to drive from
/// QML: most shells render submenus as cascading popups instead, so
/// each popup binds a fresh DBusMenuModel pointing at a different
/// `rootId`.
///
/// Construct one with the SNI item's `dbusService` + the menu object
/// path from `StatusNotifierItem::menuPath()`. The model then drives
/// itself: GetLayout on construction, LayoutUpdated/ItemsPropertiesUpdated
/// signals to refresh.
class PHOSPHORSERVICESNI_EXPORT DBusMenuModel : public QAbstractListModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(DBusMenuModel)
    Q_PROPERTY(QString service READ service WRITE setService NOTIFY sourceChanged)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY sourceChanged)
    Q_PROPERTY(int rootId READ rootId WRITE setRootId NOTIFY rootIdChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY validChanged)
    // Mirror of rowCount(): QAbstractListModel doesn't expose count
    // as a property, so QML's `model.count` evaluates to undefined
    // without this. Worth keeping the API symmetric with
    // StatusNotifierItemModel.
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TypeRole, ///< "standard" | "separator"
        LabelRole, ///< menu text, ampersands already stripped for accel
        EnabledRole,
        VisibleRole,
        IconUrlRole, ///< data:image/png;base64,... URL: bind to Image.source
        IconImageRole, ///< raw QImage (kept for C++ use; not usable as Image.source)
        ToggleTypeRole, ///< "checkmark" | "radio" | empty
        ToggleStateRole, ///< 0 = off, 1 = on, -1 = indeterminate
        ChildrenDisplayRole, ///< "submenu" if this entry has children, else empty
        ShortcutRole,
    };
    Q_ENUM(Roles)

    explicit DBusMenuModel(QObject* parent = nullptr);
    ~DBusMenuModel() override;

    [[nodiscard]] QString service() const;
    void setService(const QString& service);

    [[nodiscard]] QString path() const;
    void setPath(const QString& path);

    [[nodiscard]] int rootId() const;
    void setRootId(int id);

    [[nodiscard]] bool valid() const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Fire a click event at the given row. Calls Event(id, "clicked", ...).
    Q_INVOKABLE void triggerItem(int row);
    /// Call AboutToShow on row's submenu (if it has one) and return
    /// its id: the QML layer uses that id as the rootId for the
    /// cascaded child model.
    Q_INVOKABLE int aboutToShowSubmenu(int row);
    /// Tell the item we're about to display this menu: required by
    /// the spec before showing the root. Idempotent on repeated calls.
    Q_INVOKABLE void aboutToShow();
    /// Tell the item we're hiding this menu. Fires `closed` Event in
    /// LIFO order for every level previously opened via aboutToShow /
    /// aboutToShowSubmenu. Safe to call without a paired aboutToShow
    /// (no events fired in that case) so QML can wire it to a
    /// debounced dismiss handler without tracking opened-state itself.
    Q_INVOKABLE void aboutToHide();
    /// Force a fresh GetLayout against the current service/path. Used
    /// by the QML side when re-opening the SAME menu: `setService`/
    /// `setPath` early-return when values are unchanged, so without
    /// this the model never re-fetches and never re-fires `loaded`,
    /// leaving the popup unable to remap.
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void sourceChanged();
    void rootIdChanged();
    void validChanged();
    void countChanged();
    /// Fires on EVERY successful GetLayout: not just transitions
    /// from invalid → valid. Used by QML to remap the popup when the
    /// same menu is re-opened (validChanged stays silent in that case
    /// because the bool didn't change).
    void loaded();
    /// Fired when a GetLayout call returns a DBus error: usually
    /// means the SNI item advertised a Menu path that's stale,
    /// broken, or implemented as something other than canonical
    /// dbusmenu. QML can listen and dismiss the popup so the user
    /// doesn't stare at an empty floating box.
    void loadFailed(const QString& message);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceSni

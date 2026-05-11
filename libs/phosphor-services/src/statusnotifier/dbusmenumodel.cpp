// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/DBusMenuModel.h>

#include <PhosphorServices/IconThemeResolver.h>

#include "dbusmenu_interface.h"
#include "dbustypes.h"

#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QDebug>
#include <QHash>
#include <QImage>
#include <QVariant>

namespace PhosphorServices {

namespace {

/// Visit each level-1 child variant under a layout struct and unpack
/// it into a DBusMenuLayoutItem. The dbus marshalling already gave
/// us flat (id, props, children-variants) — we just need to recurse
/// one level for the rendered list-model row data and lazily for
/// submenus.
DBusMenuLayoutItem unpackLayoutVariant(const QVariant& v)
{
    DBusMenuLayoutItem out;
    auto arg = v.value<QDBusArgument>();
    if (arg.currentType() == QDBusArgument::StructureType) {
        arg >> out;
    } else {
        // Some apps send the layout as a Variant<(ia{sv}av)> instead of
        // a straight struct. Unwrap one level.
        QDBusVariant dv;
        if (v.canConvert<QDBusVariant>()) {
            dv = v.value<QDBusVariant>();
            arg = dv.variant().value<QDBusArgument>();
            arg >> out;
        }
    }
    return out;
}

QString labelFromProps(const QVariantMap& props)
{
    // dbusmenu labels embed accelerator markers as single ampersands —
    // "&File" means "F" is the accel. We strip them for display since
    // we don't render underline-on-Alt; doubled "&&" stays as one
    // literal ampersand.
    auto raw = props.value(QStringLiteral("label")).toString();
    QString out;
    out.reserve(raw.size());
    for (int i = 0; i < raw.size(); ++i) {
        if (raw[i] == QLatin1Char('&')) {
            if (i + 1 < raw.size() && raw[i + 1] == QLatin1Char('&')) {
                out.append(QLatin1Char('&'));
                ++i;
            }
            // Otherwise it's an accelerator marker — skip.
        } else {
            out.append(raw[i]);
        }
    }
    return out;
}

QImage iconFromProps(const QVariantMap& props, int size, const QStringList& themePaths)
{
    // dbusmenu lets items specify icon-name (themed) OR icon-data
    // (PNG bytes). Try icon-name first since it's smaller on the
    // wire; fall back to inline data.
    const auto iconName = props.value(QStringLiteral("icon-name")).toString();
    if (!iconName.isEmpty()) {
        // dbusmenu has its OWN IconThemePath list (plural — different
        // from SNI's singular). Try each.
        for (const auto& path : themePaths) {
            auto img = IconThemeResolver::instance()->iconForName(iconName, size, 1, path);
            if (!img.isNull())
                return img;
        }
        return IconThemeResolver::instance()->iconForName(iconName, size, 1, {});
    }
    const auto iconData = props.value(QStringLiteral("icon-data")).toByteArray();
    if (!iconData.isEmpty()) {
        QImage img;
        img.loadFromData(iconData); // PNG, PNG-with-alpha, etc.
        return img;
    }
    return {};
}

} // namespace

class DBusMenuModel::Private
{
public:
    explicit Private(DBusMenuModel* q)
        : q(q)
    {
    }
    DBusMenuModel* q;

    QString service;
    QString path;
    int rootId = 0;
    bool valid = false;

    std::unique_ptr<ComCanonicalDbusmenuInterface> proxy;
    QStringList themePaths; ///< from Version/IconThemePath property
    uint revision = 0;

    /// Flat child list at rootId. Built from the most recent
    /// GetLayout call.
    struct Row
    {
        int id = 0;
        QVariantMap properties;
        bool hasChildren = false;
    };
    QList<Row> rows;

    void buildProxy();
    void refresh();
    void onLayoutUpdated(uint rev, int parent);
    void onPropertiesUpdated(const DBusMenuItemPropertiesList& updated, const DBusMenuItemKeysList& removed);
    QString rowType(const Row& r) const;
    QString toggleType(const Row& r) const;
};

void DBusMenuModel::Private::buildProxy()
{
    proxy.reset();
    if (service.isEmpty() || path.isEmpty())
        return;
    proxy = std::make_unique<ComCanonicalDbusmenuInterface>(service, path, QDBusConnection::sessionBus(), q);
    QObject::connect(proxy.get(), &ComCanonicalDbusmenuInterface::LayoutUpdated, q, [this](uint r, int p) {
        onLayoutUpdated(r, p);
    });
    QObject::connect(proxy.get(), &ComCanonicalDbusmenuInterface::ItemsPropertiesUpdated, q,
                     [this](const DBusMenuItemPropertiesList& u, const DBusMenuItemKeysList& r) {
                         onPropertiesUpdated(u, r);
                     });
    themePaths = proxy->iconThemePath();
}

void DBusMenuModel::Private::refresh()
{
    if (!proxy) {
        if (valid) {
            valid = false;
            Q_EMIT q->validChanged();
        }
        return;
    }

    // GetLayout(rootId, recursionDepth=1, props=[])
    //   depth=1 returns the requested node + its direct children (id +
    //   metadata, no deeper). That's what we want for a flat list-model
    //   level; deeper submenus get their own DBusMenuModel instance.
    auto pending = proxy->GetLayout(rootId, 1, QStringList());
    auto* watcher = new QDBusPendingCallWatcher(pending, q);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, q, [this, watcher] {
        watcher->deleteLater();
        QDBusPendingReply<uint, DBusMenuLayoutItem> reply = *watcher;
        if (reply.isError()) {
            qWarning() << "DBusMenuModel: GetLayout failed:" << reply.error().message();
            return;
        }
        revision = reply.argumentAt<0>();
        const DBusMenuLayoutItem root = reply.argumentAt<1>();

        QList<Row> nextRows;
        nextRows.reserve(root.children.size());
        for (const auto& childVariant : root.children) {
            const auto child = unpackLayoutVariant(childVariant);
            // hasChildren is signalled by the "children-display"
            // property being "submenu" — the actual children aren't
            // included at depth=1.
            const bool hasChildren =
                child.properties.value(QStringLiteral("children-display")).toString() == QLatin1String("submenu");
            nextRows.append({child.id, child.properties, hasChildren});
        }

        q->beginResetModel();
        rows = nextRows;
        q->endResetModel();

        if (!valid) {
            valid = true;
            Q_EMIT q->validChanged();
        }
    });
}

void DBusMenuModel::Private::onLayoutUpdated(uint /*rev*/, int parent)
{
    // We only care about updates that affect OUR root level.
    // dbusmenu apps re-emit Layout signals for the entire tree when
    // any submenu changes, so filtering is mandatory to avoid
    // thrashing.
    if (parent == rootId || parent == 0) {
        refresh();
    }
}

void DBusMenuModel::Private::onPropertiesUpdated(const DBusMenuItemPropertiesList& updated,
                                                 const DBusMenuItemKeysList& removed)
{
    bool anyChanged = false;
    for (const auto& upd : updated) {
        for (int i = 0; i < rows.size(); ++i) {
            if (rows[i].id != upd.id)
                continue;
            for (auto it = upd.properties.begin(); it != upd.properties.end(); ++it) {
                rows[i].properties.insert(it.key(), it.value());
            }
            anyChanged = true;
            const auto idx = q->index(i);
            Q_EMIT q->dataChanged(idx, idx);
            break;
        }
    }
    for (const auto& rem : removed) {
        for (int i = 0; i < rows.size(); ++i) {
            if (rows[i].id != rem.id)
                continue;
            for (const auto& k : rem.keys) {
                rows[i].properties.remove(k);
            }
            anyChanged = true;
            const auto idx = q->index(i);
            Q_EMIT q->dataChanged(idx, idx);
            break;
        }
    }
    Q_UNUSED(anyChanged);
}

QString DBusMenuModel::Private::rowType(const Row& r) const
{
    const auto type = r.properties.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("separator"))
        return QStringLiteral("separator");
    return QStringLiteral("standard");
}

QString DBusMenuModel::Private::toggleType(const Row& r) const
{
    return r.properties.value(QStringLiteral("toggle-type")).toString();
}

// ─── Public API ────────────────────────────────────────────────────────────

DBusMenuModel::DBusMenuModel(QObject* parent)
    : QAbstractListModel(parent)
    , d(new Private(this))
{
    registerDBusTypes();
}

DBusMenuModel::~DBusMenuModel()
{
    delete d;
}

QString DBusMenuModel::service() const
{
    return d->service;
}
QString DBusMenuModel::path() const
{
    return d->path;
}
int DBusMenuModel::rootId() const
{
    return d->rootId;
}
bool DBusMenuModel::valid() const
{
    return d->valid;
}

void DBusMenuModel::setService(const QString& service)
{
    if (d->service == service)
        return;
    d->service = service;
    d->buildProxy();
    d->refresh();
    Q_EMIT sourceChanged();
}

void DBusMenuModel::setPath(const QString& path)
{
    if (d->path == path)
        return;
    d->path = path;
    d->buildProxy();
    d->refresh();
    Q_EMIT sourceChanged();
}

void DBusMenuModel::setRootId(int id)
{
    if (d->rootId == id)
        return;
    d->rootId = id;
    d->refresh();
    Q_EMIT rootIdChanged();
}

int DBusMenuModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return d->rows.size();
}

QVariant DBusMenuModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= d->rows.size())
        return {};
    const auto& r = d->rows.at(index.row());

    switch (role) {
    case IdRole:
        return r.id;
    case TypeRole:
        return d->rowType(r);
    case LabelRole:
        return labelFromProps(r.properties);
    case EnabledRole:
        return r.properties.value(QStringLiteral("enabled"), true).toBool();
    case VisibleRole:
        return r.properties.value(QStringLiteral("visible"), true).toBool();
    case IconImageRole: {
        const int size = 16; // typical menu icon
        return iconFromProps(r.properties, size, d->themePaths);
    }
    case ToggleTypeRole:
        return d->toggleType(r);
    case ToggleStateRole:
        return r.properties.value(QStringLiteral("toggle-state"), -1).toInt();
    case ChildrenDisplayRole:
        return r.hasChildren ? QStringLiteral("submenu") : QString();
    case ShortcutRole:
        return r.properties.value(QStringLiteral("shortcut")).toStringList();
    default:
        return {};
    }
}

QHash<int, QByteArray> DBusMenuModel::roleNames() const
{
    // `itemEnabled` / `itemVisible` rather than the plain names — the
    // QML side renders these as Item delegates, and Item exposes
    // `enabled` + `visible` as Q_PROPERTYs (visible is FINAL). A
    // matching role name would shadow QQuickItem's property and fail
    // at QML load with "Cannot override FINAL property", which is
    // exactly what bit us the first time around. Rename here so the
    // delegate can bind cleanly via `Item.enabled: model.itemEnabled`.
    return {
        {IdRole, "menuId"},
        {TypeRole, "itemType"},
        {LabelRole, "label"},
        {EnabledRole, "itemEnabled"},
        {VisibleRole, "itemVisible"},
        {IconImageRole, "iconImage"},
        {ToggleTypeRole, "toggleType"},
        {ToggleStateRole, "toggleState"},
        {ChildrenDisplayRole, "childrenDisplay"},
        {ShortcutRole, "shortcut"},
    };
}

void DBusMenuModel::triggerItem(int row)
{
    if (!d->proxy || row < 0 || row >= d->rows.size())
        return;
    const auto& r = d->rows.at(row);
    const uint ts = uint(QDateTime::currentMSecsSinceEpoch() / 1000);
    d->proxy->Event(r.id, QStringLiteral("clicked"), QDBusVariant(0), ts);
}

int DBusMenuModel::aboutToShowSubmenu(int row)
{
    if (!d->proxy || row < 0 || row >= d->rows.size())
        return -1;
    const auto& r = d->rows.at(row);
    if (!r.hasChildren)
        return -1;
    d->proxy->AboutToShow(r.id); // async — submenu model refresh fires on LayoutUpdated
    return r.id;
}

void DBusMenuModel::aboutToShow()
{
    if (!d->proxy)
        return;
    d->proxy->AboutToShow(d->rootId);
}

void DBusMenuModel::aboutToHide()
{
    if (!d->proxy)
        return;
    const uint ts = uint(QDateTime::currentMSecsSinceEpoch() / 1000);
    d->proxy->Event(d->rootId, QStringLiteral("closed"), QDBusVariant(0), ts);
}

} // namespace PhosphorServices

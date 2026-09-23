// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSni/DBusMenuModel.h>

#include "dbusmenu_interface.h"
#include "dbusmenuhelpers.h"
#include "dbustypes.h"

#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>
#include <QImage>
#include <QLoggingCategory>
#include <QSet>
#include <QVariant>

Q_LOGGING_CATEGORY(lcSniMenu, "phosphor.service.sni.menu")

namespace PhosphorServiceSni {

namespace {

// Icon-defining properties on a dbusmenu row. Used both by
// onPropertiesUpdated (to invalidate the per-row icon cache when one
// changes) and by refresh (to decide whether a same-shape row's
// cached icon can be carried into the new Row instance).
const QSet<QString>& iconPropKeys()
{
    static const QSet<QString> keys{QStringLiteral("icon-name"), QStringLiteral("icon-data")};
    return keys;
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
    /// Revision number returned by the most recent successful GetLayout.
    /// LayoutUpdated signals carrying a revision <= this value are stale
    /// (a slow GetLayout reply that landed after the LayoutUpdated that
    /// invalidated it) and would needlessly bump the in-flight queue;
    /// drop them at the slot.
    uint revision = 0;

    // Outstanding GetLayout watcher. Tracking it lets buildProxy()
    // cancel an in-flight call when the menu source changes; without
    // this guard the stale reply lands after the new proxy is set
    // and overwrites the fresh menu's rows with the old menu's data.
    QPointer<QDBusPendingCallWatcher> pendingLayoutWatcher;

    // Coalesce buildProxy + refresh across rapid setService / setPath
    // pairs. A typical QML init binds both properties at once: without
    // coalescing each setter independently runs buildProxy + refresh,
    // producing one stale GetLayout that the second call cancels and
    // a duplicate model-reset. The pending flag is consumed at the
    // next event-loop tick by `runScheduledRebuild`.
    bool rebuildScheduled = false;

    // dbusmenu apps gate dynamic-menu population on `opened` and tear
    // down on `closed`. The shell may navigate a multi-level cascade
    // (root -> submenu -> submenu) before dismissal, firing opened
    // events for each level via aboutToShow / aboutToShowSubmenu. On
    // dismissal we must fire closed for EVERY level that was opened,
    // not just the one that happens to be the current rootId. Without
    // this, the root-level "opened" never gets a matching "closed"
    // and apps leak state. Stored in stack order (push on open) so
    // aboutToHide can close in LIFO order; the spec doesn't require
    // a specific order but apps that maintain a depth counter
    // implicitly expect inverse-of-open. Each id appears at most once.
    QList<int> openedIds;

    /// Flat child list at rootId. Built from the most recent
    /// GetLayout call.
    struct Row
    {
        int id = 0;
        QVariantMap properties;
        bool hasChildren = false;
        // Cached PNG+base64 data URL for the row's icon. Populated on
        // first IconUrlRole / IconImageRole read and reused; without
        // this cache QML's `data()` is called many times per delegate
        // (one per role binding × redraws) and each call re-runs the
        // PNG encode → base64 encode pipeline, which dominates menu
        // open latency for icon-heavy menus. Invalidated when the
        // row's icon-name / icon-data properties change.
        mutable QString cachedIconUrl;
        mutable QImage cachedIconImage;
        mutable bool iconCacheValid = false;
    };
    QList<Row> rows;

    void buildProxy();
    void refresh();
    void scheduleProxyRebuild();
    void runScheduledRebuild();
    [[nodiscard]] bool flushPendingRebuild();
    void onLayoutUpdated(uint rev, int parent);
    void onPropertiesUpdated(const DBusMenuItemPropertiesList& updated, const DBusMenuItemKeysList& removed);
    QString rowType(const Row& r) const;
    QString toggleType(const Row& r) const;
};

void DBusMenuModel::Private::scheduleProxyRebuild()
{
    // Already queued: just let the prior tick handle the latest state.
    if (rebuildScheduled)
        return;
    rebuildScheduled = true;
    // The queued lambda gates its body on `rebuildScheduled`. If a
    // synchronous `flushPendingRebuild` consumed the pending rebuild
    // before the event-loop tick fires, the lambda becomes a no-op;
    // otherwise it runs the rebuild as originally scheduled. Without
    // this guard, a flushed rebuild followed by the queued tick would
    // re-run `buildProxy` a second time (clearing openedIds, resetting
    // rows, emitting validChanged false) and re-issue a GetLayout for
    // the same rootId that just succeeded.
    QMetaObject::invokeMethod(
        q,
        [this]() {
            if (rebuildScheduled)
                runScheduledRebuild();
        },
        Qt::QueuedConnection);
}

void DBusMenuModel::Private::runScheduledRebuild()
{
    rebuildScheduled = false;
    buildProxy();
    refresh();
}

bool DBusMenuModel::Private::flushPendingRebuild()
{
    // Synchronously execute any queued rebuild before a method that
    // depends on `proxy` runs. Required because the public open/close
    // contract is "QML sets service+path then immediately calls
    // aboutToShow()". The queued rebuild would normally fire on the
    // next event-loop tick, but aboutToShow runs in the same tick as
    // the setters; if we don't flush, aboutToShow sees either the OLD
    // app's proxy (first popup with prior content) or null (first
    // popup of a fresh model), neither of which is correct.
    //
    // runScheduledRebuild clears `rebuildScheduled` first, so the
    // later queued tick checks the flag in its lambda body and
    // short-circuits. Returns true when a flush actually ran so
    // callers (setRootId, public refresh) can skip a follow-up
    // refresh that would otherwise duplicate the GetLayout the flushed
    // rebuild already issued.
    if (rebuildScheduled) {
        runScheduledRebuild();
        return true;
    }
    return false;
}

void DBusMenuModel::Private::buildProxy()
{
    proxy.reset();
    // Reset the revision watermark: each menu source maintains its own
    // independent revision sequence, and carrying the prior source's
    // value would let `onLayoutUpdated`'s `rev <= revision` guard
    // silently drop fresh LayoutUpdated signals for the new source
    // until the first GetLayout reply re-establishes a baseline.
    revision = 0;
    // Drop any opened-id tracking from the prior menu. We're losing
    // the proxy that those ids referenced, so there's no longer a way
    // to fire closed for them; clearing here keeps aboutToHide from
    // sending them to the NEW app's proxy (which would be wrong).
    openedIds.clear();
    // Clear out the previous menu's rows + validity immediately
    // (before the new GetLayout completes) so the popup doesn't
    // briefly display the OLD app's menu under the NEW app's icon.
    // Without this, the QML side observes leftover rows while the
    // async GetLayout for the new path is in flight, and a popup
    // that's bound to `model.valid` would still flash the wrong
    // content during the swap.
    if (!rows.isEmpty()) {
        q->beginResetModel();
        rows.clear();
        q->endResetModel();
        Q_EMIT q->countChanged();
    }
    if (valid) {
        valid = false;
        Q_EMIT q->validChanged();
    }
    // Cancel any in-flight GetLayout from the previous proxy. The
    // QDBusPendingCallWatcher is parented to `q` and would fire after
    // the new proxy is set, overwriting fresh rows with stale data
    // from the previous menu source.
    if (pendingLayoutWatcher) {
        pendingLayoutWatcher->disconnect();
        pendingLayoutWatcher->deleteLater();
        pendingLayoutWatcher.clear();
    }
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

    // Cancel any in-flight GetLayout from a prior refresh() before
    // launching a fresh one. Without this, rapid LayoutUpdated
    // signals (or manual refresh() chained off a setService that
    // matched the previous value) leave two watchers alive; if the
    // older reply lands after the newer one, the stale snapshot
    // wins and the model reverts. The disconnect call removes our
    // finished handler so the about-to-be-deleted watcher's reply
    // is dropped on the floor.
    if (pendingLayoutWatcher) {
        pendingLayoutWatcher->disconnect();
        pendingLayoutWatcher->deleteLater();
        pendingLayoutWatcher.clear();
    }

    // GetLayout(rootId, recursionDepth=1, props=[])
    //   depth=1 returns the requested node + its direct children (id +
    //   metadata, no deeper). That's what we want for a flat list-model
    //   level; deeper submenus get their own DBusMenuModel instance.
    auto pending = proxy->GetLayout(rootId, 1, QStringList());
    auto* watcher = new QDBusPendingCallWatcher(pending, q);
    // Track in-flight watcher so buildProxy() (and a subsequent
    // refresh()) can cancel it if the source changes mid-call.
    // Auto-clears via QPointer when the watcher self-destructs on
    // finished.
    pendingLayoutWatcher = watcher;
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, q, [this, watcher] {
        watcher->deleteLater();
        QDBusPendingReply<uint, DBusMenuLayoutItem> reply = *watcher;
        if (reply.isError()) {
            // App misconfigurations are common, SNI items advertise
            // a Menu path that's empty, stale, or implements something
            // OTHER than canonical dbusmenu. Log at info, not warning:
            // these are user-visible only via the popup-auto-close
            // path below (loadFailed → QML dismisses popup), and they
            // happen often enough on a typical desktop that warning-
            // level noise would drown out real bugs.
            qCInfo(lcSniMenu) << "GetLayout failed for" << service << path << ":" << reply.error().message();
            // Flip valid → false so QML bindings (loadFailed +
            // onLoadFailed close handler in TrayMenuPopup) react.
            if (valid) {
                valid = false;
                Q_EMIT q->validChanged();
            }
            Q_EMIT q->loadFailed(reply.error().message());
            return;
        }
        revision = reply.argumentAt<0>();
        const DBusMenuLayoutItem root = reply.argumentAt<1>();

        QList<Row> nextRows;
        nextRows.reserve(root.children.size());
        for (const auto& childVariant : root.children) {
            const auto child = unpackLayoutVariant(childVariant);
            // hasChildren is signalled by the "children-display"
            // property being "submenu", the actual children aren't
            // included at depth=1.
            const bool hasChildren =
                child.properties.value(QStringLiteral("children-display")).toString() == QLatin1String("submenu");
            // Default-construct and assign the populated fields. GCC
            // still emits -Wmissing-field-initializers on partial
            // designated initializers (the cached-icon trio relies on
            // in-class member defaults), and a positional brace-init
            // would trip the same warning.
            Row row;
            row.id = child.id;
            row.properties = child.properties;
            row.hasChildren = hasChildren;
            nextRows.append(std::move(row));
        }

        // Use dataChanged when the row count + id sequence hasn't
        // changed, keeps the QML view's delegate-cache warm across
        // property-only refreshes (status toggling, dynamic labels).
        // beginResetModel destroys every delegate, which is expensive
        // and visually flickers the menu.
        bool sameShape = nextRows.size() == rows.size();
        if (sameShape) {
            for (int i = 0; i < nextRows.size(); ++i) {
                if (nextRows[i].id != rows[i].id || nextRows[i].hasChildren != rows[i].hasChildren) {
                    sameShape = false;
                    break;
                }
            }
        }
        if (sameShape) {
            // Carry the icon cache across the refresh when the icon-
            // defining properties (`icon-name`, `icon-data`) haven't
            // changed. Without this every LayoutUpdated for a property-
            // only change (enabled toggle, dynamic label) re-decodes
            // and re-base64-encodes every icon in the level on the
            // next data() read, defeating the per-row cache.
            // onPropertiesUpdated already invalidates the cache on
            // icon-prop changes; this just preserves the cache for
            // rows that didn't move.
            const QSet<QString>& iconKeys = iconPropKeys();
            for (int i = 0; i < nextRows.size(); ++i) {
                bool iconUnchanged = true;
                for (const auto& key : iconKeys) {
                    if (rows[i].properties.value(key) != nextRows[i].properties.value(key)) {
                        iconUnchanged = false;
                        break;
                    }
                }
                if (iconUnchanged && rows[i].iconCacheValid) {
                    nextRows[i].cachedIconUrl = rows[i].cachedIconUrl;
                    nextRows[i].cachedIconImage = rows[i].cachedIconImage;
                    nextRows[i].iconCacheValid = true;
                }
            }
            rows = nextRows;
            if (!rows.isEmpty()) {
                Q_EMIT q->dataChanged(q->index(0), q->index(rows.size() - 1));
            }
        } else {
            q->beginResetModel();
            rows = nextRows;
            q->endResetModel();
            Q_EMIT q->countChanged();
        }

        if (!valid) {
            valid = true;
            Q_EMIT q->validChanged();
        }
        // Fires on every successful GetLayout. validChanged is
        // silent when valid was already true (re-fetch of an
        // already-loaded menu), and QML needs SOMETHING to listen
        // for so it can remap a popup on re-open.
        qCDebug(lcSniMenu) << "loaded" << service << path << "rows=" << rows.size();
        Q_EMIT q->loaded();
    });
}

void DBusMenuModel::Private::onLayoutUpdated(uint rev, int parent)
{
    // We only care about updates that affect OUR root level.
    // dbusmenu apps re-emit Layout signals for the entire tree when
    // any submenu changes, so filtering is mandatory to avoid
    // thrashing.
    if (parent != rootId && parent != 0) {
        return;
    }
    // Drop signals that pre-date the revision our last GetLayout
    // reply observed: a slow GetLayout in flight can land BEFORE a
    // LayoutUpdated that invalidates the layout the reply describes,
    // and the redundant refresh just thrashes the wire without
    // changing the displayed state.
    if (rev != 0 && rev <= revision) {
        return;
    }
    refresh();
}

void DBusMenuModel::Private::onPropertiesUpdated(const DBusMenuItemPropertiesList& updated,
                                                 const DBusMenuItemKeysList& removed)
{
    // Invalidate icon cache if any of the visual properties change.
    // Anything that affects iconFromProps' return value goes here.
    const QSet<QString>& iconKeys = iconPropKeys();

    for (const auto& upd : updated) {
        for (int i = 0; i < rows.size(); ++i) {
            if (rows[i].id != upd.id)
                continue;
            for (auto it = upd.properties.begin(); it != upd.properties.end(); ++it) {
                rows[i].properties.insert(it.key(), it.value());
                if (iconKeys.contains(it.key())) {
                    rows[i].iconCacheValid = false;
                }
            }
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
                if (iconKeys.contains(k)) {
                    rows[i].iconCacheValid = false;
                }
            }
            const auto idx = q->index(i);
            Q_EMIT q->dataChanged(idx, idx);
            break;
        }
    }
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
    , d(std::make_unique<Private>(this))
{
    registerDBusTypes();
}

DBusMenuModel::~DBusMenuModel() = default;

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
    d->scheduleProxyRebuild();
    Q_EMIT sourceChanged();
}

void DBusMenuModel::setPath(const QString& path)
{
    if (d->path == path)
        return;
    d->path = path;
    d->scheduleProxyRebuild();
    Q_EMIT sourceChanged();
}

void DBusMenuModel::setRootId(int id)
{
    if (d->rootId == id)
        return;
    d->rootId = id;
    // setRootId reaches d->refresh() which dereferences d->proxy via
    // d->proxy->GetLayout(...). Flush any pending rebuild first so the
    // refresh runs against the new proxy rather than the prior app's
    // (or a stale null after the queued tick races behind us). If the
    // flush ran, the rebuild already issued a GetLayout for the new
    // rootId (which was set above before the flush) so we don't need
    // to issue another one here.
    if (!d->flushPendingRebuild())
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
    case IconUrlRole: {
        // Cache the PNG+base64 result per-row. data() is called many
        // times per delegate; without caching, every read re-rasters
        // the icon and re-encodes it. Invalidated on property update.
        if (!r.iconCacheValid) {
            r.cachedIconImage = iconFromProps(r.properties, 16, d->themePaths);
            r.cachedIconUrl = iconToDataUrl(r.cachedIconImage);
            r.iconCacheValid = true;
        }
        return r.cachedIconUrl;
    }
    case IconImageRole: {
        if (!r.iconCacheValid) {
            r.cachedIconImage = iconFromProps(r.properties, 16, d->themePaths);
            r.cachedIconUrl = iconToDataUrl(r.cachedIconImage);
            r.iconCacheValid = true;
        }
        return r.cachedIconImage;
    }
    case ToggleTypeRole:
        return d->toggleType(r);
    case ToggleStateRole:
        return r.properties.value(QStringLiteral("toggle-state"), -1).toInt();
    case ChildrenDisplayRole:
        return r.hasChildren ? QStringLiteral("submenu") : QString();
    case ShortcutRole:
        return shortcutFromProps(r.properties);
    default:
        return {};
    }
}

QHash<int, QByteArray> DBusMenuModel::roleNames() const
{
    // `itemEnabled` / `itemVisible` rather than the plain names, the
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
        {IconUrlRole, "iconUrl"},
        {IconImageRole, "iconImage"},
        {ToggleTypeRole, "toggleType"},
        {ToggleStateRole, "toggleState"},
        {ChildrenDisplayRole, "childrenDisplay"},
        {ShortcutRole, "shortcut"},
    };
}

void DBusMenuModel::triggerItem(int row)
{
    (void)d->flushPendingRebuild();
    if (!d->proxy || row < 0 || row >= d->rows.size())
        return;
    const auto& r = d->rows.at(row);
    // dbusmenu spec forbids sending `clicked` on a disabled row; some
    // apps reject the Event as a protocol violation and log a noisy
    // warning. The QML side already gates click handling on
    // itemEnabled, but a buggy delegate (or a programmatic
    // triggerItem(n) call) could still get here; check defensively.
    if (!r.properties.value(QStringLiteral("enabled"), true).toBool())
        return;
    d->proxy->Event(r.id, QStringLiteral("clicked"), QDBusVariant(0), dbusmenuTimestamp());
}

int DBusMenuModel::aboutToShowSubmenu(int row)
{
    (void)d->flushPendingRebuild();
    if (!d->proxy || row < 0 || row >= d->rows.size())
        return -1;
    const auto& r = d->rows.at(row);
    if (!r.hasChildren)
        return -1;
    // dbusmenu spec: AboutToShow returns bool needUpdate. If true, the
    // app populated this submenu lazily and a GetLayout is required to
    // pick up the new contents. We do NOT refresh here on the reply,
    // because the QML side reassigns `rootId = aboutToShowSubmenu(...)`
    // after this call returns; setRootId already triggers GetLayout
    // synchronously. Refreshing in the watcher would issue a duplicate
    // round-trip.
    auto* watcher = new QDBusPendingCallWatcher(d->proxy->AboutToShow(r.id), this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher]() {
        watcher->deleteLater();
    });
    // Pair with the closed Event aboutToHide will fire. Per the
    // dbusmenu spec, opened/closed pair up; some apps gate dynamic-
    // menu population on the opened event, so without it the first
    // open shows an empty/stale submenu until a later LayoutUpdated
    // arrives. Track the id so aboutToHide can close every level we
    // opened on the way in. Guard both the wire fire AND the bookkeeping
    // by the same predicate so a re-hover into an already-open submenu
    // does not produce an unbalanced "opened" the app would have to
    // depth-count against.
    if (!d->openedIds.contains(r.id)) {
        d->proxy->Event(r.id, QStringLiteral("opened"), QDBusVariant(0), dbusmenuTimestamp());
        d->openedIds.append(r.id);
    }
    return r.id;
}

void DBusMenuModel::aboutToShow()
{
    (void)d->flushPendingRebuild();
    if (!d->proxy)
        return;
    // AboutToShow's needUpdate reply triggers a refresh only when the
    // root id we asked about is still the active root by the time the
    // reply lands (the user can re-navigate during the round-trip).
    // Some apps skip LayoutUpdated when AboutToShow already returned
    // needUpdate=true, so this reply path is load-bearing for the
    // initial-open case where the app populates lazily.
    auto* watcher = new QDBusPendingCallWatcher(d->proxy->AboutToShow(d->rootId), this);
    const int rootIdSnapshot = d->rootId;
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, rootIdSnapshot]() {
        watcher->deleteLater();
        const QDBusPendingReply<bool> reply = *watcher;
        if (reply.isError())
            return;
        if (reply.value() && rootIdSnapshot == d->rootId)
            d->refresh();
    });
    // Mirror aboutToHide's closed event. Some apps only build their
    // submenu items on the opened tick and tear them down on closed.
    // Track the id so aboutToHide can close every level we opened on
    // the way in. Guard the wire fire AND the bookkeeping by the same
    // predicate so a duplicate aboutToShow (animation race, re-show
    // without an intervening dismiss) does not produce an unbalanced
    // "opened" the app would have to depth-count against.
    if (!d->openedIds.contains(d->rootId)) {
        d->proxy->Event(d->rootId, QStringLiteral("opened"), QDBusVariant(0), dbusmenuTimestamp());
        d->openedIds.append(d->rootId);
    }
}

void DBusMenuModel::refresh()
{
    // If a rebuild was queued, the flush already issued a fresh
    // GetLayout via refresh: UNLESS service/path were empty when the
    // flush ran (in which case `buildProxy` left the proxy null and the
    // inner refresh early-returned without issuing GetLayout). We still
    // want a no-op in that case: calling refresh() on an unconfigured
    // model is nonsensical and the early-return mirrors the unflushed
    // d->refresh() check below.
    const bool flushed = d->flushPendingRebuild();
    qCDebug(lcSniMenu) << "refresh() invoked for" << d->service << d->path << "proxy=" << (d->proxy ? "live" : "null");
    if (!flushed)
        d->refresh();
}

void DBusMenuModel::aboutToHide()
{
    (void)d->flushPendingRebuild();
    if (!d->proxy) {
        d->openedIds.clear();
        return;
    }
    // No paired aboutToShow ever ran (animation race, hide-before-show
    // QML toggle, fresh model that the user dismissed). Firing closed
    // here would be unbalanced and a strict app maintaining a depth
    // counter could underflow; skip.
    if (d->openedIds.isEmpty())
        return;
    // Fire closed for every level we opened on the way in, in inverse
    // open order so apps tracking a depth counter unwind LIFO. Append
    // the current rootId if it wasn't already in the list so a
    // programmatic setRootId caller (which doesn't update openedIds)
    // still gets a paired close.
    QList<int> toClose = d->openedIds;
    if (!toClose.contains(d->rootId))
        toClose.append(d->rootId);
    const uint ts = dbusmenuTimestamp();
    for (auto it = toClose.crbegin(); it != toClose.crend(); ++it)
        d->proxy->Event(*it, QStringLiteral("closed"), QDBusVariant(0), ts);
    d->openedIds.clear();
}

} // namespace PhosphorServiceSni

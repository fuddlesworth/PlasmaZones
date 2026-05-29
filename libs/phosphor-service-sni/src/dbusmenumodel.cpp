// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSni/DBusMenuModel.h>

#include <PhosphorServiceIconTheme/IconThemeResolver.h>

#include "dbusmenu_interface.h"
#include "dbustypes.h"

#include <QBuffer>
#include <QByteArray>
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QDebug>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QLoggingCategory>
#include <QSet>
#include <QVariant>

Q_LOGGING_CATEGORY(lcSniMenu, "phosphor.service.sni.menu")

namespace PhosphorServiceSni {

namespace {

// dbusmenu spec timestamps are unix-epoch milliseconds, truncated to
// 32 bits. Centralised so the four Event() call sites can't drift
// (an earlier rev divided by 1000 and produced "stale" rejections on
// apps with focus-stealing prevention because the timestamps didn't
// match the platform's input-event clock).
uint dbusmenuTimestamp()
{
    return uint(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFFu);
}

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
        return out;
    }
    // Some apps send the layout as a Variant<(ia{sv}av)> instead of
    // a straight struct. Unwrap one level — but ONLY after we've
    // verified the unwrapped argument is itself a struct. A malicious
    // (or buggy) sender can deliver a variant wrapping anything else
    // (an integer, an array, an empty argument), and `arg >> out`
    // against a non-struct argument is undefined per QtDBus's
    // assertion semantics. Validate before dereferencing.
    if (!v.canConvert<QDBusVariant>()) {
        return out;
    }
    const QDBusVariant dv = v.value<QDBusVariant>();
    auto inner = dv.variant().value<QDBusArgument>();
    if (inner.currentType() != QDBusArgument::StructureType) {
        return out;
    }
    inner >> out;
    return out;
}

QString labelFromProps(const QVariantMap& props)
{
    // dbusmenu labels embed accelerator markers in two flavours,
    // depending on whether the publishing app uses GTK or Qt
    // conventions:
    //
    //   GTK   — `_` before the accel char, `__` for a literal _
    //   Qt    — `&` before the accel char, `&&` for a literal &
    //
    // The Deskflow app indicator publishes GTK-style ("_Quit",
    // "Rest_art"), KDE's own indicators publish Qt-style. We strip
    // BOTH because we don't render underline-on-Alt either way.
    auto raw = props.value(QStringLiteral("label")).toString();
    QString out;
    out.reserve(raw.size());
    for (int i = 0; i < raw.size(); ++i) {
        const QChar c = raw[i];
        if (c == QLatin1Char('&') || c == QLatin1Char('_')) {
            // Doubled = literal character; keep one copy and skip the
            // duplicate.
            if (i + 1 < raw.size() && raw[i + 1] == c) {
                out.append(c);
                ++i;
                continue;
            }
            // Lone marker at end-of-string (e.g. an app that ships a
            // single "&" as the label) is malformed but should not
            // silently disappear; keep the char so the label survives.
            if (i + 1 >= raw.size()) {
                out.append(c);
                continue;
            }
            // Lone marker mid-string = strip and move on; the NEXT
            // char is the mnemonic but we don't render an underline.
            continue;
        }
        out.append(c);
    }
    return out;
}

/// Parse the `shortcut` property — the dbusmenu spec types it as
/// `aas` (array of array of strings), where each outer entry is an
/// alternative key-press and each inner entry is a modifier list
/// terminating in the key. Example: [["Control","S"], ["Control","Q"]]
/// means "Ctrl+S or Ctrl+Q". We render only the first chord.
///
/// Modifier strings come straight from the spec — "Control", "Shift",
/// "Alt", "Super". We map them to the standard human display form
/// ("Ctrl", "Shift", "Alt", "Super") and join with "+".
QString shortcutFromProps(const QVariantMap& props)
{
    const QVariant raw = props.value(QStringLiteral("shortcut"));
    if (!raw.isValid()) {
        return {};
    }
    // Use qdbus_cast on the variant rather than manual
    // QDBusArgument iteration: hand-rolling beginArray/endArray on
    // a QDBusArgument obtained via `raw.value<QDBusArgument>()`
    // triggers Qt 6's "write from a read-only object" diagnostic —
    // the returned arg's internal state machine flips to read-only
    // and beginArray() is overloaded as a write operation when no
    // metatype id is supplied. qdbus_cast knows about both
    // QList<T> and QStringList natively, so a single cast does the
    // full demarshalling without that wrinkle.
    const QList<QStringList> chords = qdbus_cast<QList<QStringList>>(raw);
    if (chords.isEmpty() || chords.first().isEmpty()) {
        return {};
    }

    static const QHash<QString, QString> modifierDisplay{
        {QStringLiteral("Control"), QStringLiteral("Ctrl")},
        {QStringLiteral("Shift"), QStringLiteral("Shift")},
        {QStringLiteral("Alt"), QStringLiteral("Alt")},
        {QStringLiteral("Super"), QStringLiteral("Super")},
    };

    QStringList rendered;
    for (const auto& part : chords.first()) {
        rendered.append(modifierDisplay.value(part, part));
    }
    return rendered.join(QLatin1Char('+'));
}

/// Encode a QImage as a data:image/png;base64 URL. Used for menu
/// icons because they're per-row, short-lived (only while the menu
/// is open), and small (typically 16×16) — no point routing them
/// through a stateful image provider with cleanup plumbing. Empty
/// input → empty string, so the QML side can `visible: src.length`.
QString iconToDataUrl(const QImage& img)
{
    if (img.isNull()) {
        return {};
    }
    QByteArray buf;
    QBuffer dev(&buf);
    dev.open(QIODevice::WriteOnly);
    img.save(&dev, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(buf.toBase64());
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
            auto img = PhosphorServiceIconTheme::IconThemeResolver::instance()->iconForName(iconName, size, 1, path);
            if (!img.isNull())
                return img;
        }
        return PhosphorServiceIconTheme::IconThemeResolver::instance()->iconForName(iconName, size, 1, {});
    }
    const auto iconData = props.value(QStringLiteral("icon-data")).toByteArray();
    if (!iconData.isEmpty()) {
        // Decode through QImageReader with a hard alloc cap. Qt's
        // default QImageReader::allocationLimit is 128 MiB, which a
        // hostile menu provider could exploit per-row across a long
        // menu (128 MiB * N rows). Tray-menu icons are 16-32 px, so a
        // 4 MiB cap is generous and bounds the damage. Dimension cap
        // mirrors the SNI item path (kMaxIconDim = 4096) for the same
        // reason.
        QBuffer buf;
        buf.setData(iconData);
        buf.open(QIODevice::ReadOnly);
        QImageReader reader(&buf);
        reader.setAllocationLimit(4); // MiB
        constexpr int kMaxIconDim = 4096;
        const QSize wireSize = reader.size();
        if (wireSize.isValid() && (wireSize.width() > kMaxIconDim || wireSize.height() > kMaxIconDim))
            return {};
        QImage img;
        if (!reader.read(&img))
            return {};
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
    void flushPendingRebuild();
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
    QMetaObject::invokeMethod(
        q,
        [this]() {
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

void DBusMenuModel::Private::flushPendingRebuild()
{
    // Synchronously execute any queued rebuild before a method that
    // depends on `proxy` runs. Required because the public open/close
    // contract is "QML sets service+path then immediately calls
    // aboutToShow()". The queued rebuild would normally fire on the
    // next event-loop tick, but aboutToShow runs in the same tick as
    // the setters; if we don't flush, aboutToShow sees either the OLD
    // app's proxy (first popup with prior content) or null (first
    // popup of a fresh model), neither of which is correct. The queued
    // tick is still a no-op when it later fires (rebuildScheduled
    // returns to false here).
    if (rebuildScheduled)
        runScheduledRebuild();
}

void DBusMenuModel::Private::buildProxy()
{
    proxy.reset();
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
            // App misconfigurations are common — SNI items advertise
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
            // property being "submenu" — the actual children aren't
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
        // changed — keeps the QML view's delegate-cache warm across
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
            static const QSet<QString> kIconProps{QStringLiteral("icon-name"), QStringLiteral("icon-data")};
            for (int i = 0; i < nextRows.size(); ++i) {
                bool iconUnchanged = true;
                for (const auto& key : kIconProps) {
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
    // Invalidate icon cache if any of the visual properties change.
    // Anything that affects iconFromProps' return value goes here.
    static const QSet<QString> kIconProps{QStringLiteral("icon-name"), QStringLiteral("icon-data")};

    for (const auto& upd : updated) {
        for (int i = 0; i < rows.size(); ++i) {
            if (rows[i].id != upd.id)
                continue;
            for (auto it = upd.properties.begin(); it != upd.properties.end(); ++it) {
                rows[i].properties.insert(it.key(), it.value());
                if (kIconProps.contains(it.key())) {
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
                if (kIconProps.contains(k)) {
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
    // (or a stale null after the queued tick races behind us).
    d->flushPendingRebuild();
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
    d->flushPendingRebuild();
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
    d->flushPendingRebuild();
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
    // opened on the way in.
    d->proxy->Event(r.id, QStringLiteral("opened"), QDBusVariant(0), dbusmenuTimestamp());
    if (!d->openedIds.contains(r.id))
        d->openedIds.append(r.id);
    return r.id;
}

void DBusMenuModel::aboutToShow()
{
    d->flushPendingRebuild();
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
    // the way in.
    d->proxy->Event(d->rootId, QStringLiteral("opened"), QDBusVariant(0), dbusmenuTimestamp());
    if (!d->openedIds.contains(d->rootId))
        d->openedIds.append(d->rootId);
}

void DBusMenuModel::refresh()
{
    d->flushPendingRebuild();
    qCDebug(lcSniMenu) << "refresh() invoked for" << d->service << d->path << "proxy=" << (d->proxy ? "live" : "null");
    d->refresh();
}

void DBusMenuModel::aboutToHide()
{
    d->flushPendingRebuild();
    if (!d->proxy) {
        d->openedIds.clear();
        return;
    }
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

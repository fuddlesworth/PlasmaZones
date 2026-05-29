// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSni/StatusNotifierItem.h>

#include <PhosphorServiceIconTheme/IconThemeResolver.h>

#include "dbustypes.h"
#include "statusnotifieritem_interface.h"

#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>
#include <QImage>
#include <QtEndian>

namespace PhosphorServiceSni {

namespace {

/// "Active" / "Passive" / "NeedsAttention" → enum. Anything else
/// defaults to Passive: items in the wild occasionally send empty
/// strings during startup.
StatusNotifierItem::Status statusFromString(const QString& s)
{
    if (s == QLatin1String("Active")) {
        return StatusNotifierItem::Status::Active;
    }
    if (s == QLatin1String("NeedsAttention")) {
        return StatusNotifierItem::Status::NeedsAttention;
    }
    return StatusNotifierItem::Status::Passive;
}

/// Convert the wire's ARGB byte data (network byte order) into a
/// platform-native QImage. The SNI spec is explicit about
/// "network-byte-order": most apps that have been tested actually
/// emit native-endian on little-endian platforms, but enough emit
/// big-endian that we have to swap unconditionally on little-endian
/// hosts.
QImage argbBytesToImage(int width, int height, const QByteArray& bytes)
{
    // Adversarial input from another process: validate dimensions,
    // compute size in 64-bit to avoid the signed-int overflow that a
    // malicious sender could weaponise (e.g. width=height=65536 wraps
    // to a small positive int that "passes" the size check, then the
    // copy loop overruns).
    static constexpr int kMaxIconDim = 4096; ///< Generous cap; real icons are ≤512.
    if (width <= 0 || height <= 0 || width > kMaxIconDim || height > kMaxIconDim) {
        return {};
    }
    const qint64 expected = qint64(width) * qint64(height) * 4;
    if (bytes.size() < expected) {
        return {};
    }

    QImage img(width, height, QImage::Format_ARGB32);
    // Source bytes from QByteArray::constData() are NOT guaranteed
    // 4-byte aligned, so a direct `reinterpret_cast<const quint32*>`
    // + dereference can SIGBUS on ARM / RISC-V. Use the unaligned-
    // safe `qFromBigEndian<quint32>(const void*)` overload that does
    // a byte-wise load + swap internally. Destination (QImage::bits())
    // IS guaranteed aligned for ARGB32.
    const char* src = bytes.constData();
    for (int y = 0; y < height; ++y) {
        auto* dstRow = reinterpret_cast<quint32*>(img.scanLine(y));
        for (int x = 0; x < width; ++x) {
            // qFromBigEndian on a pointer: unaligned load + swap on
            // little-endian (the common case); no-op on big-endian.
            dstRow[x] = qFromBigEndian<quint32>(src + (qsizetype(y) * width + x) * 4);
        }
    }
    return img;
}

QImage pixmapListToImage(const DBusImageList& pixmaps, int desiredSize)
{
    if (pixmaps.isEmpty()) {
        return {};
    }
    // Pick the pixmap whose larger dimension is closest to (but not
    // less than) desiredSize. Falls back to the largest available if
    // every pixmap is smaller than requested.
    const DBusImage* best = &pixmaps.first();
    int bestScore = -1;
    for (const auto& p : pixmaps) {
        const int dim = qMax(p.width, p.height);
        int score;
        if (dim >= desiredSize) {
            score = dim - desiredSize; // smaller is better (closer above)
        } else {
            // Penalty for being smaller: we'd have to upscale.
            score = (desiredSize - dim) + 1000;
        }
        if (bestScore < 0 || score < bestScore) {
            bestScore = score;
            best = &p;
        }
    }
    return argbBytesToImage(best->width, best->height, best->data);
}

} // namespace

class StatusNotifierItem::Private
{
public:
    explicit Private(StatusNotifierItem* q)
        : q(q)
    {
    }

    StatusNotifierItem* q;

    QString service;
    QString path;
    std::unique_ptr<OrgKdeStatusNotifierItemInterface> proxy;

    // Property cache. Refreshed on construction + on the NewXxx
    // signals. We hold off emitting Q_PROPERTY change signals until
    // the full property refresh completes so QML bindings don't see
    // a half-updated state.
    QString id;
    QString title;
    QString category;
    Status status = Status::Passive;
    QString iconName;
    QString attentionIconName;
    QString overlayIconName;
    QString iconThemePath;
    DBusImageList iconPixmap;
    DBusImageList attentionIconPixmap;
    DBusImageList overlayIconPixmap;
    DBusToolTip toolTip;
    QString menuPath;
    bool itemIsMenu = false;
    int preferredIconSize = 22;
    bool valid = false;

    // Lazily-rendered icons keyed by the property they came from. Reset
    // on icon-change signals or preferred-size changes.
    mutable QImage iconCache;
    mutable QImage overlayIconCache;
    mutable QImage attentionIconCache;

    void invalidateIconCache()
    {
        iconCache = {};
        overlayIconCache = {};
        attentionIconCache = {};
    }

    QImage renderIcon(const QString& themedName, const DBusImageList& pixmaps, QImage& cache) const
    {
        if (!cache.isNull()) {
            return cache;
        }
        if (!themedName.isEmpty()) {
            cache = PhosphorServiceIconTheme::IconThemeResolver::instance()->iconForName(themedName, preferredIconSize,
                                                                                         1, iconThemePath);
        }
        if (cache.isNull() && !pixmaps.isEmpty()) {
            cache = pixmapListToImage(pixmaps, preferredIconSize);
        }
        return cache;
    }

    void refreshAll();
    void refreshIcons();
    void refreshToolTip();
    void refreshStatus();
    void refreshTitle();
};

void StatusNotifierItem::Private::refreshAll()
{
    if (!proxy) {
        return;
    }
    // Property reads on the proxy are synchronous calls under the
    // hood (via org.freedesktop.DBus.Properties.Get). We do them
    // sync because a tray with 8 items × 10 properties × 30ms
    // round-trip is still under 3 seconds, the cost happens once
    // per item lifetime, and async juggling for property bundles
    // is more complexity than it's worth. The hot path
    // (NewIcon → refreshIcons) does only a couple of props at a time.
    bool changedTitle = false, changedStatus = false, changedIcon = false, changedToolTip = false, changedMenu = false,
         changedCategory = false, changedId = false;

    const auto newId = proxy->id();
    if (newId != id) {
        id = newId;
        changedId = true;
    }

    const auto newTitle = proxy->title();
    if (newTitle != title) {
        title = newTitle;
        changedTitle = true;
    }

    const auto newCategory = proxy->category();
    if (newCategory != category) {
        category = newCategory;
        changedCategory = true;
    }

    const auto newStatus = statusFromString(proxy->status());
    if (newStatus != status) {
        status = newStatus;
        changedStatus = true;
    }

    const auto newThemePath = proxy->iconThemePath();
    const auto newIconName = proxy->iconName();
    const auto newOverlayName = proxy->overlayIconName();
    const auto newAttentionName = proxy->attentionIconName();
    const auto newIconPixmap = proxy->iconPixmap();
    const auto newOverlayPixmap = proxy->overlayIconPixmap();
    const auto newAttentionPixmap = proxy->attentionIconPixmap();

    if (newThemePath != iconThemePath || newIconName != iconName || newOverlayName != overlayIconName
        || newAttentionName != attentionIconName) {
        iconThemePath = newThemePath;
        iconName = newIconName;
        overlayIconName = newOverlayName;
        attentionIconName = newAttentionName;
        changedIcon = true;
    }
    // Byte-level pixmap comparison. Earlier rev only compared
    // list.size() which missed the case of an app re-publishing a
    // pixmap of the same dimensions but different ARGB content
    // (Discord and a few electron apps do this on theme-change).
    // DBusImage::operator== walks width/height/data; QList's
    // operator== walks the list.
    if (newIconPixmap != iconPixmap || newOverlayPixmap != overlayIconPixmap
        || newAttentionPixmap != attentionIconPixmap) {
        iconPixmap = newIconPixmap;
        overlayIconPixmap = newOverlayPixmap;
        attentionIconPixmap = newAttentionPixmap;
        changedIcon = true;
    }
    if (changedIcon) {
        invalidateIconCache();
    }

    const auto newTip = proxy->toolTip();
    if (newTip.title != toolTip.title || newTip.body != toolTip.body || newTip.iconName != toolTip.iconName) {
        toolTip = newTip;
        changedToolTip = true;
    }

    const auto newMenu = proxy->menu().path();
    const auto newIsMenu = proxy->itemIsMenu();
    if (newMenu != menuPath || newIsMenu != itemIsMenu) {
        menuPath = newMenu;
        itemIsMenu = newIsMenu;
        changedMenu = true;
    }

    if (!valid) {
        valid = true;
        Q_EMIT q->validChanged();
    }

    if (changedId)
        Q_EMIT q->idChanged();
    if (changedTitle)
        Q_EMIT q->titleChanged();
    if (changedCategory)
        Q_EMIT q->categoryChanged();
    if (changedStatus)
        Q_EMIT q->statusChanged();
    if (changedIcon)
        Q_EMIT q->iconChanged();
    if (changedToolTip)
        Q_EMIT q->toolTipChanged();
    if (changedMenu)
        Q_EMIT q->menuPathChanged();
}

void StatusNotifierItem::Private::refreshIcons()
{
    if (!proxy)
        return;
    // Fetch first, compare against cached values, only emit if any
    // icon-related property actually changed. CLAUDE.md mandates
    // "only emit signals when value actually changes": without this
    // guard, every NewIcon spam from a chatty app fires iconChanged
    // on the model, triggering full QML delegate refresh per signal.
    const auto newIconName = proxy->iconName();
    const auto newOverlayIconName = proxy->overlayIconName();
    const auto newIconThemePath = proxy->iconThemePath();
    const auto newIconPixmap = proxy->iconPixmap();
    const auto newOverlayIconPixmap = proxy->overlayIconPixmap();
    // See refreshAll for the byte-level comparison rationale.
    const bool changed = newIconName != iconName || newOverlayIconName != overlayIconName
        || newIconThemePath != iconThemePath || newIconPixmap != iconPixmap
        || newOverlayIconPixmap != overlayIconPixmap;
    if (!changed) {
        return;
    }
    iconName = newIconName;
    overlayIconName = newOverlayIconName;
    iconThemePath = newIconThemePath;
    iconPixmap = newIconPixmap;
    overlayIconPixmap = newOverlayIconPixmap;
    invalidateIconCache();
    Q_EMIT q->iconChanged();
}

void StatusNotifierItem::Private::refreshToolTip()
{
    if (!proxy)
        return;
    const auto newTip = proxy->toolTip();
    if (newTip.title == toolTip.title && newTip.body == toolTip.body && newTip.iconName == toolTip.iconName) {
        return;
    }
    toolTip = newTip;
    Q_EMIT q->toolTipChanged();
}

void StatusNotifierItem::Private::refreshStatus()
{
    if (!proxy)
        return;
    const auto newStatus = statusFromString(proxy->status());
    if (newStatus == status)
        return;
    status = newStatus;
    Q_EMIT q->statusChanged();
}

void StatusNotifierItem::Private::refreshTitle()
{
    if (!proxy)
        return;
    const auto newTitle = proxy->title();
    if (newTitle == title)
        return;
    title = newTitle;
    Q_EMIT q->titleChanged();
}

StatusNotifierItem::StatusNotifierItem(const QString& dbusService, const QString& dbusPath, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this))
{
    registerDBusTypes();
    d->service = dbusService;
    d->path = dbusPath;
    d->proxy =
        std::make_unique<OrgKdeStatusNotifierItemInterface>(dbusService, dbusPath, QDBusConnection::sessionBus(), this);

    // Subscribe to per-property change signals. The spec is vague
    // about whether items also fire org.freedesktop.DBus.Properties
    // PropertiesChanged: most don't, so we rely on these instead.
    connect(d->proxy.get(), &OrgKdeStatusNotifierItemInterface::NewIcon, this, [this] {
        d->refreshIcons();
    });
    connect(d->proxy.get(), &OrgKdeStatusNotifierItemInterface::NewAttentionIcon, this, [this] {
        d->refreshIcons();
    });
    connect(d->proxy.get(), &OrgKdeStatusNotifierItemInterface::NewOverlayIcon, this, [this] {
        d->refreshIcons();
    });
    connect(d->proxy.get(), &OrgKdeStatusNotifierItemInterface::NewToolTip, this, [this] {
        d->refreshToolTip();
    });
    connect(d->proxy.get(), &OrgKdeStatusNotifierItemInterface::NewTitle, this, [this] {
        d->refreshTitle();
    });
    connect(d->proxy.get(), &OrgKdeStatusNotifierItemInterface::NewStatus, this, [this](const QString&) {
        d->refreshStatus();
    });

    d->refreshAll();
}

StatusNotifierItem::~StatusNotifierItem() = default;

QString StatusNotifierItem::id() const
{
    return d->id;
}
QString StatusNotifierItem::title() const
{
    return d->title;
}
QString StatusNotifierItem::category() const
{
    return d->category;
}
StatusNotifierItem::Status StatusNotifierItem::status() const
{
    return d->status;
}
QString StatusNotifierItem::toolTipTitle() const
{
    return d->toolTip.title;
}
QString StatusNotifierItem::toolTipBody() const
{
    return d->toolTip.body;
}
QString StatusNotifierItem::menuPath() const
{
    return d->menuPath;
}
bool StatusNotifierItem::itemIsMenu() const
{
    return d->itemIsMenu;
}
QString StatusNotifierItem::dbusService() const
{
    return d->service;
}
QString StatusNotifierItem::dbusPath() const
{
    return d->path;
}
bool StatusNotifierItem::isValid() const
{
    return d->valid;
}

QImage StatusNotifierItem::iconImage() const
{
    return d->renderIcon(d->iconName, d->iconPixmap, d->iconCache);
}

QImage StatusNotifierItem::overlayIconImage() const
{
    return d->renderIcon(d->overlayIconName, d->overlayIconPixmap, d->overlayIconCache);
}

QImage StatusNotifierItem::attentionIconImage() const
{
    return d->renderIcon(d->attentionIconName, d->attentionIconPixmap, d->attentionIconCache);
}

void StatusNotifierItem::setPreferredIconSize(int size)
{
    // Reject implausible sizes at both ends. The lower bound matches
    // the XDG-spec minimum useful render; the upper bound caps the
    // worst-case render allocation a buggy QML delegate could request
    // (256 px is already huge for a tray icon).
    static constexpr int kMinIconSize = 8;
    static constexpr int kMaxIconSize = 256;
    if (size < kMinIconSize || size > kMaxIconSize || size == d->preferredIconSize)
        return;
    d->preferredIconSize = size;
    d->invalidateIconCache();
    Q_EMIT iconChanged();
}

int StatusNotifierItem::preferredIconSize() const
{
    return d->preferredIconSize;
}

void StatusNotifierItem::activate(int x, int y)
{
    if (d->proxy)
        d->proxy->Activate(x, y);
}

void StatusNotifierItem::secondaryActivate(int x, int y)
{
    if (d->proxy)
        d->proxy->SecondaryActivate(x, y);
}

void StatusNotifierItem::contextMenu(int x, int y)
{
    if (d->proxy)
        d->proxy->ContextMenu(x, y);
}

void StatusNotifierItem::scroll(int delta, const QString& orientation)
{
    if (d->proxy)
        d->proxy->Scroll(delta, orientation);
}

} // namespace PhosphorServiceSni

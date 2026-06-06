// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QDBusArgument>
#include <QDBusMetaType>
#include <QImage>
#include <QList>
#include <QString>

namespace PhosphorServiceSni {

// ─── StatusNotifierItem types ──────────────────────────────────────────────

/// One element of the `a(iiay)` icon-pixmap array: (width, height,
/// ARGB32 raw byte data). The bytes are network-byte-order ARGB on the
/// wire, regardless of the host endianness: `decode()` in
/// statusnotifieritem.cpp swaps to platform order before constructing
/// the QImage.
struct DBusImage
{
    int width = 0;
    int height = 0;
    QByteArray data;

    // Byte-level equality so QList<DBusImage>::operator== works. Used
    // by StatusNotifierItem's change detection: a same-count pixmap
    // list with mutated bytes (rare, but seen with players that send
    // a fresh ARGB blob without resizing) must invalidate the icon
    // cache and re-emit iconChanged.
    friend bool operator==(const DBusImage& lhs, const DBusImage& rhs)
    {
        return lhs.width == rhs.width && lhs.height == rhs.height && lhs.data == rhs.data;
    }
    friend bool operator!=(const DBusImage& lhs, const DBusImage& rhs)
    {
        return !(lhs == rhs);
    }
};
using DBusImageList = QList<DBusImage>;

/// `(sa(iiay)ss)`: icon name, pixmaps, title, body. We mostly read
/// title + body for the hover tooltip; the icon is usually empty or
/// duplicates the main item icon.
struct DBusToolTip
{
    QString iconName;
    DBusImageList iconPixmaps;
    QString title;
    QString body;
};

// ─── DBusmenu types ────────────────────────────────────────────────────────

/// `(ia{sv}av)`: the recursive layout struct GetLayout() returns. The
/// generated QtDBus interface unmarshalls one level at a time; we
/// repack the variant children into nested DBusMenuLayoutItem during
/// tree walk in DBusMenuModel.
struct DBusMenuLayoutItem
{
    int id = 0;
    QVariantMap properties;
    QList<QVariant> children;
};

/// `(ia{sv})`: per-item property bundle used by ItemsPropertiesUpdated
/// and GetGroupProperties.
struct DBusMenuItemProperties
{
    int id = 0;
    QVariantMap properties;
};
using DBusMenuItemPropertiesList = QList<DBusMenuItemProperties>;

/// `(ias)`: removed-property entries from ItemsPropertiesUpdated.
struct DBusMenuItemKeys
{
    int id = 0;
    QStringList keys;
};
using DBusMenuItemKeysList = QList<DBusMenuItemKeys>;

/// `(isvu)`: entries of the EventGroup() input array.
struct DBusMenuEvent
{
    int id = 0;
    QString eventId;
    QVariant data;
    uint timestamp = 0;
};
using DBusMenuEventList = QList<DBusMenuEvent>;

/// Register every custom DBus type with QtDBus's metatype system.
/// Must be called once on the main thread before the first proxy /
/// adaptor is instantiated; idempotent under repeated calls.
void registerDBusTypes();

} // namespace PhosphorServiceSni

Q_DECLARE_METATYPE(PhosphorServiceSni::DBusImage)
Q_DECLARE_METATYPE(PhosphorServiceSni::DBusImageList)
Q_DECLARE_METATYPE(PhosphorServiceSni::DBusToolTip)
Q_DECLARE_METATYPE(PhosphorServiceSni::DBusMenuLayoutItem)
Q_DECLARE_METATYPE(PhosphorServiceSni::DBusMenuItemProperties)
Q_DECLARE_METATYPE(PhosphorServiceSni::DBusMenuItemPropertiesList)
Q_DECLARE_METATYPE(PhosphorServiceSni::DBusMenuItemKeys)
Q_DECLARE_METATYPE(PhosphorServiceSni::DBusMenuItemKeysList)
Q_DECLARE_METATYPE(PhosphorServiceSni::DBusMenuEvent)
Q_DECLARE_METATYPE(PhosphorServiceSni::DBusMenuEventList)

QDBusArgument& operator<<(QDBusArgument& argument, const PhosphorServiceSni::DBusImage& image);
const QDBusArgument& operator>>(const QDBusArgument& argument, PhosphorServiceSni::DBusImage& image);

QDBusArgument& operator<<(QDBusArgument& argument, const PhosphorServiceSni::DBusToolTip& tip);
const QDBusArgument& operator>>(const QDBusArgument& argument, PhosphorServiceSni::DBusToolTip& tip);

QDBusArgument& operator<<(QDBusArgument& argument, const PhosphorServiceSni::DBusMenuLayoutItem& item);
const QDBusArgument& operator>>(const QDBusArgument& argument, PhosphorServiceSni::DBusMenuLayoutItem& item);

QDBusArgument& operator<<(QDBusArgument& argument, const PhosphorServiceSni::DBusMenuItemProperties& props);
const QDBusArgument& operator>>(const QDBusArgument& argument, PhosphorServiceSni::DBusMenuItemProperties& props);

QDBusArgument& operator<<(QDBusArgument& argument, const PhosphorServiceSni::DBusMenuItemKeys& keys);
const QDBusArgument& operator>>(const QDBusArgument& argument, PhosphorServiceSni::DBusMenuItemKeys& keys);

QDBusArgument& operator<<(QDBusArgument& argument, const PhosphorServiceSni::DBusMenuEvent& event);
const QDBusArgument& operator>>(const QDBusArgument& argument, PhosphorServiceSni::DBusMenuEvent& event);

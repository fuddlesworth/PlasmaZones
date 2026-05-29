// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "dbustypes.h"

#include <QDBusMetaType>

#include <mutex>

namespace PhosphorServiceSni {

void registerDBusTypes()
{
    static std::once_flag s_flag;
    std::call_once(s_flag, [] {
        qDBusRegisterMetaType<DBusImage>();
        qDBusRegisterMetaType<DBusImageList>();
        qDBusRegisterMetaType<DBusToolTip>();
        qDBusRegisterMetaType<DBusMenuLayoutItem>();
        qDBusRegisterMetaType<DBusMenuItemProperties>();
        qDBusRegisterMetaType<DBusMenuItemPropertiesList>();
        qDBusRegisterMetaType<DBusMenuItemKeys>();
        qDBusRegisterMetaType<DBusMenuItemKeysList>();
        qDBusRegisterMetaType<DBusMenuEvent>();
        qDBusRegisterMetaType<DBusMenuEventList>();
    });
}

} // namespace PhosphorServiceSni

// ─── (iiay) ─── DBusImage ──────────────────────────────────────────────────

QDBusArgument& operator<<(QDBusArgument& argument, const PhosphorServiceSni::DBusImage& image)
{
    argument.beginStructure();
    argument << image.width << image.height << image.data;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, PhosphorServiceSni::DBusImage& image)
{
    argument.beginStructure();
    argument >> image.width >> image.height >> image.data;
    argument.endStructure();
    return argument;
}

// ─── (sa(iiay)ss) ─── DBusToolTip ──────────────────────────────────────────

QDBusArgument& operator<<(QDBusArgument& argument, const PhosphorServiceSni::DBusToolTip& tip)
{
    argument.beginStructure();
    argument << tip.iconName << tip.iconPixmaps << tip.title << tip.body;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, PhosphorServiceSni::DBusToolTip& tip)
{
    argument.beginStructure();
    argument >> tip.iconName >> tip.iconPixmaps >> tip.title >> tip.body;
    argument.endStructure();
    return argument;
}

// ─── (ia{sv}av) ─── DBusMenuLayoutItem ─────────────────────────────────────

QDBusArgument& operator<<(QDBusArgument& argument, const PhosphorServiceSni::DBusMenuLayoutItem& item)
{
    argument.beginStructure();
    argument << item.id << item.properties;
    argument.beginArray(qMetaTypeId<QDBusVariant>());
    for (const QVariant& child : item.children) {
        argument << QDBusVariant(child);
    }
    argument.endArray();
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, PhosphorServiceSni::DBusMenuLayoutItem& item)
{
    argument.beginStructure();
    argument >> item.id >> item.properties;
    item.children.clear();
    argument.beginArray();
    while (!argument.atEnd()) {
        QDBusVariant variant;
        argument >> variant;
        item.children.append(variant.variant());
    }
    argument.endArray();
    argument.endStructure();
    return argument;
}

// ─── (ia{sv}) ─── DBusMenuItemProperties ──────────────────────────────────

QDBusArgument& operator<<(QDBusArgument& argument, const PhosphorServiceSni::DBusMenuItemProperties& props)
{
    argument.beginStructure();
    argument << props.id << props.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, PhosphorServiceSni::DBusMenuItemProperties& props)
{
    argument.beginStructure();
    argument >> props.id >> props.properties;
    argument.endStructure();
    return argument;
}

// ─── (ias) ─── DBusMenuItemKeys ───────────────────────────────────────────

QDBusArgument& operator<<(QDBusArgument& argument, const PhosphorServiceSni::DBusMenuItemKeys& keys)
{
    argument.beginStructure();
    argument << keys.id << keys.keys;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, PhosphorServiceSni::DBusMenuItemKeys& keys)
{
    argument.beginStructure();
    argument >> keys.id >> keys.keys;
    argument.endStructure();
    return argument;
}

// ─── (isvu) ─── DBusMenuEvent ─────────────────────────────────────────────

QDBusArgument& operator<<(QDBusArgument& argument, const PhosphorServiceSni::DBusMenuEvent& event)
{
    argument.beginStructure();
    argument << event.id << event.eventId << QDBusVariant(event.data) << event.timestamp;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, PhosphorServiceSni::DBusMenuEvent& event)
{
    argument.beginStructure();
    QDBusVariant variant;
    argument >> event.id >> event.eventId >> variant >> event.timestamp;
    event.data = variant.variant();
    argument.endStructure();
    return argument;
}

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "dbustypes.h"

#include <QDBusMetaType>

namespace PhosphorServices {

void registerDBusTypes()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

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
}

} // namespace PhosphorServices

// ─── (iiay) ─── DBusImage ──────────────────────────────────────────────────

QDBusArgument& operator<<(QDBusArgument& argument, const PhosphorServices::DBusImage& image)
{
    argument.beginStructure();
    argument << image.width << image.height << image.data;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, PhosphorServices::DBusImage& image)
{
    argument.beginStructure();
    argument >> image.width >> image.height >> image.data;
    argument.endStructure();
    return argument;
}

// ─── (sa(iiay)ss) ─── DBusToolTip ──────────────────────────────────────────

QDBusArgument& operator<<(QDBusArgument& argument, const PhosphorServices::DBusToolTip& tip)
{
    argument.beginStructure();
    argument << tip.iconName << tip.iconPixmaps << tip.title << tip.body;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, PhosphorServices::DBusToolTip& tip)
{
    argument.beginStructure();
    argument >> tip.iconName >> tip.iconPixmaps >> tip.title >> tip.body;
    argument.endStructure();
    return argument;
}

// ─── (ia{sv}av) ─── DBusMenuLayoutItem ─────────────────────────────────────

QDBusArgument& operator<<(QDBusArgument& argument, const PhosphorServices::DBusMenuLayoutItem& item)
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

const QDBusArgument& operator>>(const QDBusArgument& argument, PhosphorServices::DBusMenuLayoutItem& item)
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

QDBusArgument& operator<<(QDBusArgument& argument, const PhosphorServices::DBusMenuItemProperties& props)
{
    argument.beginStructure();
    argument << props.id << props.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, PhosphorServices::DBusMenuItemProperties& props)
{
    argument.beginStructure();
    argument >> props.id >> props.properties;
    argument.endStructure();
    return argument;
}

// ─── (ias) ─── DBusMenuItemKeys ───────────────────────────────────────────

QDBusArgument& operator<<(QDBusArgument& argument, const PhosphorServices::DBusMenuItemKeys& keys)
{
    argument.beginStructure();
    argument << keys.id << keys.keys;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, PhosphorServices::DBusMenuItemKeys& keys)
{
    argument.beginStructure();
    argument >> keys.id >> keys.keys;
    argument.endStructure();
    return argument;
}

// ─── (isvu) ─── DBusMenuEvent ─────────────────────────────────────────────

QDBusArgument& operator<<(QDBusArgument& argument, const PhosphorServices::DBusMenuEvent& event)
{
    argument.beginStructure();
    argument << event.id << event.eventId << QDBusVariant(event.data) << event.timestamp;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, PhosphorServices::DBusMenuEvent& event)
{
    argument.beginStructure();
    QDBusVariant variant;
    argument >> event.id >> event.eventId >> variant >> event.timestamp;
    event.data = variant.variant();
    argument.endStructure();
    return argument;
}

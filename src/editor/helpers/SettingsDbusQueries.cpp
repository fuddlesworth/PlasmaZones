// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SettingsDbusQueries.h"
#include "../../core/dbusvariantutils.h"
#include "../../core/logging.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <QDBusError>
#include <QDBusMessage>
#include <QDBusVariant>

namespace PlasmaZones {
namespace SettingsDbusQueries {

namespace {

/// Unwrap a getSetting() reply into a plain QVariant. Returns an invalid
/// QVariant on D-Bus error or if the reply shape is wrong. The daemon
/// answers with an `sv` (variant-of-variant) so we have to step through
/// QDBusVariant once.
QVariant unwrapGetSetting(const QDBusMessage& reply)
{
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }
    return reply.arguments().constFirst().value<QDBusVariant>().variant();
}

} // namespace

QVariantMap querySettingsBatch(const QStringList& keys)
{
    if (keys.isEmpty()) {
        return QVariantMap();
    }

    const QDBusMessage reply = PhosphorProtocol::ClientHelpers::syncCall(PhosphorProtocol::Service::Interface::Settings,
                                                                         QStringLiteral("getSettings"), {keys});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        // Defensively unwrap any QDBusArgument/QDBusVariant wrappers Qt may
        // leave in the map. For a flat a{sv} of scalar int/bool Qt normally
        // demarshals cleanly into plain types, but nested compound values
        // (or a future extension to this method returning lists/maps) would
        // arrive as QDBusArgument wrappers that toInt()/toBool() can't
        // handle — the result would silently fall through to caller-side
        // defaults. ShaderDbusQueries::queryShaderInfo does the same thing
        // against the same daemon object for the same reason.
        QVariant converted = DBusVariantUtils::convertDbusArgument(reply.arguments().constFirst());
        return converted.toMap();
    }

    // No per-key fallback for a daemon too old to have getSettings(). There used to be
    // one, and it was a trap: the only daemon that can reach it is one that predates this
    // build, which is exactly the daemon that still answers an unknown key with a valid
    // EMPTY STRING instead of an error — so the fallback silently wrote ""/0/false into
    // the result map where the caller's own default belonged. It was written to work
    // around that sentinel and then outlived the reasoning. The editor and the daemon
    // ship together, and CLAUDE.md is explicit that ad-hoc backwards compatibility is
    // write-once and maintain-forever. Callers fall back to their defaults.
    if (reply.type() == QDBusMessage::ErrorMessage) {
        const QDBusError err(reply);
        qCWarning(lcDbus) << "getSettings() failed:" << err.message() << "(type" << err.type() << ")";
    }
    return QVariantMap();
}

bool queryBoolSetting(const QString& settingKey, bool defaultValue)
{
    const QVariant value = unwrapGetSetting(PhosphorProtocol::ClientHelpers::syncCall(
        PhosphorProtocol::Service::Interface::Settings, QStringLiteral("getSetting"), {settingKey}));
    if (!value.isValid()) {
        return defaultValue;
    }
    return value.toBool();
}

} // namespace SettingsDbusQueries
} // namespace PlasmaZones

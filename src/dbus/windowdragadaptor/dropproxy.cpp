// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dropproxy.h"

#include "core/platform/logging.h"
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorScreens/ScreenIdentity.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace PlasmaZones {

namespace {

namespace Key = PhosphorProtocol::Service::DropProxyKey;

/// [x, y, w, h] with a positive size, else an invalid rect.
QRect rectFromJson(const QJsonValue& value)
{
    if (!value.isArray()) {
        return QRect();
    }
    const QJsonArray arr = value.toArray();
    if (arr.size() != 4) {
        return QRect();
    }
    for (const QJsonValue& v : arr) {
        if (!v.isDouble()) {
            return QRect();
        }
    }
    const QRect rect(arr.at(0).toInt(), arr.at(1).toInt(), arr.at(2).toInt(), arr.at(3).toInt());
    return rect.width() > 0 && rect.height() > 0 ? rect : QRect();
}

// The registry is fed over D-Bus by whatever peer claims a screen, and every
// drag tick walks it. Real hardware has a handful of outputs and a placement
// map has tens of cells, so these ceilings sit far above anything legitimate
// while keeping both the memory and the per-tick walk bounded.
constexpr int MaxProxies = 64;
constexpr int MaxCellsPerProxy = 512;

} // namespace

std::optional<DropProxy> DropProxyRegistry::parse(const QString& json)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcDbusWindow) << "drop proxy: rejecting malformed JSON:" << error.errorString();
        return std::nullopt;
    }
    const QJsonObject obj = doc.object();
    DropProxy proxy;
    proxy.rect = rectFromJson(obj.value(Key::Rect));
    if (!proxy.rect.isValid()) {
        qCWarning(lcDbusWindow) << "drop proxy: rejecting proxy without a positive rect";
        return std::nullopt;
    }
    const QJsonValue cellsValue = obj.value(Key::Cells);
    if (!cellsValue.isArray()) {
        qCWarning(lcDbusWindow) << "drop proxy: rejecting proxy without a cells array";
        return std::nullopt;
    }
    const QJsonArray cells = cellsValue.toArray();
    if (cells.size() > MaxCellsPerProxy) {
        qCWarning(lcDbusWindow) << "drop proxy: rejecting" << cells.size() << "cells; the ceiling is"
                                << MaxCellsPerProxy;
        return std::nullopt;
    }
    proxy.cells.reserve(cells.size());
    for (const QJsonValue& v : cells) {
        const QJsonObject cellObj = v.toObject();
        DropProxyCell cell;
        cell.zoneId = cellObj.value(Key::Id).toString();
        cell.rect = rectFromJson(cellObj.value(Key::Rect));
        if (cell.zoneId.isEmpty() || !cell.rect.isValid()) {
            qCWarning(lcDbusWindow) << "drop proxy: rejecting cell with empty id or non-positive rect";
            return std::nullopt;
        }
        proxy.cells.append(cell);
    }
    return proxy;
}

QHash<QString, DropProxy>::const_iterator DropProxyRegistry::find(const QString& screenId) const
{
    if (screenId.isEmpty()) {
        return m_proxies.constEnd();
    }
    // A handful of screens at most, so a screensMatch walk costs less than
    // a normalized key would and keeps the equivalence rule in one place.
    for (auto it = m_proxies.constBegin(); it != m_proxies.constEnd(); ++it) {
        if (PhosphorScreens::ScreenIdentity::screensMatch(it.key(), screenId)) {
            return it;
        }
    }
    return m_proxies.constEnd();
}

bool DropProxyRegistry::registerProxy(const QString& screenId, const QString& json, const QString& owner)
{
    if (screenId.isEmpty()) {
        qCWarning(lcDbusWindow) << "drop proxy: rejecting registration with an empty screen id";
        return false;
    }
    std::optional<DropProxy> proxy = parse(json);
    if (!proxy) {
        return false;
    }
    proxy->owner = owner;
    // Replace under whichever spelling the earlier registration used, so an
    // alternate id for the same output does not leave two proxies standing.
    if (const auto it = find(screenId); it != m_proxies.constEnd()) {
        m_proxies.erase(it);
    }
    // Only a screen id that MATCHES an existing one replaces it, so a peer
    // handing over a fresh spelling each time would otherwise grow this
    // without bound and lengthen every drag tick's walk with it.
    if (m_proxies.size() >= MaxProxies) {
        qCWarning(lcDbusWindow) << "drop proxy: refusing" << screenId << "— already holding" << m_proxies.size()
                                << "proxies, which is the ceiling";
        return false;
    }
    m_proxies.insert(screenId, std::move(*proxy));
    return true;
}

QStringList DropProxyRegistry::unregisterProxiesOwnedBy(const QString& owner)
{
    QStringList dropped;
    if (owner.isEmpty()) {
        return dropped;
    }
    for (auto it = m_proxies.begin(); it != m_proxies.end();) {
        if (it.value().owner == owner) {
            dropped.append(it.key());
            it = m_proxies.erase(it);
        } else {
            ++it;
        }
    }
    return dropped;
}

QString DropProxyRegistry::ownerOf(const QString& screenId) const
{
    const auto it = find(screenId);
    return it == m_proxies.constEnd() ? QString() : it->owner;
}

void DropProxyRegistry::unregisterProxy(const QString& screenId)
{
    if (const auto it = find(screenId); it != m_proxies.constEnd()) {
        m_proxies.erase(it);
    }
}

bool DropProxyRegistry::hasProxy(const QString& screenId) const
{
    return find(screenId) != m_proxies.constEnd();
}

DropProxyRegistry::Resolution DropProxyRegistry::resolve(const QString& screenId, const QPoint& screenLocal) const
{
    Resolution result;
    const auto it = find(screenId);
    if (it == m_proxies.constEnd() || !it->rect.contains(screenLocal)) {
        return result;
    }
    result.hit = Hit::InsideNoCell;
    for (const DropProxyCell& cell : it->cells) {
        if (cell.rect.contains(screenLocal)) {
            result.hit = Hit::Cell;
            result.zoneId = cell.zoneId;
            break;
        }
    }
    return result;
}

} // namespace PlasmaZones

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "perscreenresolver.h"

#include <QJsonObject>
#include <QLatin1String>

namespace PlasmaZones {

namespace {
struct PerScreenMapping
{
    const char* prefix; // e.g. "AutotileScreen" — external group-name prefix
    const char* category; // e.g. "Autotile"       — JSON container key
};

constexpr PerScreenMapping kPerScreenMappings[] = {
    {"ZoneSelector", "ZoneSelector"},
    {"AutotileScreen", "Autotile"},
    {"SnappingScreen", "Snapping"},
};
} // namespace

// ── Static helpers ──────────────────────────────────────────────────────────

const QString& PerScreenPathResolver::perScreenKey()
{
    static const QString s = QStringLiteral("PerScreen");
    return s;
}

bool PerScreenPathResolver::isPerScreenPrefix(const QString& groupName)
{
    if (groupName.isEmpty()) {
        return false;
    }
    for (const auto& m : kPerScreenMappings) {
        const auto prefixLen = static_cast<int>(qstrlen(m.prefix));
        if (groupName.size() > prefixLen && groupName.startsWith(QLatin1String(m.prefix))
            && groupName.at(prefixLen) == QLatin1Char(':')) {
            return true;
        }
    }
    return false;
}

QString PerScreenPathResolver::prefixToCategory(const QString& prefix)
{
    for (const auto& m : kPerScreenMappings) {
        if (prefix == QLatin1String(m.prefix)) {
            return QString::fromLatin1(m.category);
        }
    }
    return prefix;
}

QString PerScreenPathResolver::categoryToPrefix(const QString& category)
{
    for (const auto& m : kPerScreenMappings) {
        if (category == QLatin1String(m.category)) {
            return QString::fromLatin1(m.prefix);
        }
    }
    return category;
}

// ── IGroupPathResolver implementation ───────────────────────────────────────

PerScreenPathResolver::PerScreenPathResolver() = default;
PerScreenPathResolver::~PerScreenPathResolver() = default;

std::optional<QStringList> PerScreenPathResolver::toJsonPath(const QString& groupName) const
{
    if (!isPerScreenPrefix(groupName)) {
        return std::nullopt;
    }

    const int colonIdx = groupName.indexOf(QLatin1Char(':'));
    if (colonIdx < 0) {
        return std::nullopt;
    }

    const QString prefix = groupName.left(colonIdx);
    const QString screenId = groupName.mid(colonIdx + 1);
    if (screenId.isEmpty()) {
        // Empty screen id: reject with an empty path so the backend logs
        // a "malformed" warning instead of silently inserting an empty-string
        // key into the JSON tree.
        return QStringList{};
    }

    const QString category = prefixToCategory(prefix);
    return QStringList{perScreenKey(), category, screenId};
}

QStringList PerScreenPathResolver::reservedRootKeys() const
{
    return {perScreenKey()};
}

QStringList PerScreenPathResolver::enumerate(const QJsonObject& root) const
{
    QStringList out;
    const QJsonObject perScreen = root.value(perScreenKey()).toObject();
    for (auto catIt = perScreen.constBegin(); catIt != perScreen.constEnd(); ++catIt) {
        const QJsonObject category = catIt.value().toObject();
        const QString prefix = categoryToPrefix(catIt.key());
        for (auto screenIt = category.constBegin(); screenIt != category.constEnd(); ++screenIt) {
            out.append(prefix + QLatin1Char(':') + screenIt.key());
        }
    }
    return out;
}

} // namespace PlasmaZones

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowappearancecontroller.h"

#include "../config/settings.h"

#include <PhosphorRules/MatchExpression.h>

#include <optional>

namespace PlasmaZones {

namespace {
constexpr QLatin1StringView kScopeTiled{"tiled"};
constexpr QLatin1StringView kScopeNormal{"normal"};
constexpr QLatin1StringView kScopeAll{"all"};
constexpr QLatin1StringView kScopeCustom{"custom"};
} // namespace

QString WindowAppearanceController::perScreenGapRuleId(const QString& screenId) const
{
    if (screenId.isEmpty()) {
        return QString();
    }
    // Key by the stable EDID form so the id agrees with the v4→v5 migration and
    // the per-screen gap reader, which both key per-monitor gap rules by the
    // canonical stable id rather than the connector name.
    return QUuid::createUuidV5(ConfigDefaults::baselineGapRuleId(), Settings::canonicalPerScreenKey(screenId).toUtf8())
        .toString();
}

QString WindowAppearanceController::canonicalScreenId(const QString& screenId) const
{
    return Settings::canonicalPerScreenKey(screenId);
}

QJsonObject WindowAppearanceController::matchJsonForScope(const QString& scope) const
{
    if (scope == kScopeTiled) {
        return ConfigDefaults::tiledAndSnappedScopeMatch().toJson();
    }
    if (scope == kScopeNormal) {
        return ConfigDefaults::normalWindowsScopeMatch().toJson();
    }
    // "all" (and any unrecognized token) is the catch-all empty All{}.
    return PhosphorRules::MatchExpression{}.toJson();
}

QString WindowAppearanceController::scopeOfMatch(const QJsonObject& match) const
{
    const std::optional<PhosphorRules::MatchExpression> parsed = PhosphorRules::MatchExpression::fromJson(match);
    // An absent / empty / unparseable match, or any catch-all, is the "all" scope:
    // the baseline applies to every window.
    if (!parsed || parsed->isCatchAll()) {
        return QString(kScopeAll);
    }
    if (*parsed == ConfigDefaults::tiledAndSnappedScopeMatch()) {
        return QString(kScopeTiled);
    }
    if (*parsed == ConfigDefaults::normalWindowsScopeMatch()) {
        return QString(kScopeNormal);
    }
    // A valid but non-preset expression — e.g. a match hand-authored on the Rules
    // page. Report it as custom so the picker shows it rather than silently
    // coercing it to a preset and clobbering the user's intent on the next write.
    return QString(kScopeCustom);
}

} // namespace PlasmaZones

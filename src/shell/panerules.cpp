// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PaneRules.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorRules/ActionParams.h>
#include <PhosphorRules/ActionTypes.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(lcPaneRules, "phosphorshell.panerules")

// Namespace for the v5 rule ids. Fixed forever: changing it would seed a
// second copy of every pane rule beside the first.
const QUuid kPaneRuleNamespace(QStringLiteral("{6f1b0f4e-3a52-4e0c-9d5a-2c7e8b1d4a90}"));

// The 1/3 column preset, as the work-area fraction OpenColumnWidth takes.
constexpr double kThirdColumn = 1.0 / 3.0;
// Zone 1: the nearest fixed stand-in for "the zone nearest the chip".
constexpr int kControlCenterZone = 1;

constexpr QLatin1StringView kRulesKey("rules");
constexpr QLatin1StringView kIdKey("id");
} // namespace

namespace PhosphorShellApp::PaneRules {

QUuid ruleIdFor(const QString& appId)
{
    return QUuid::createUuidV5(kPaneRuleNamespace, appId);
}

int defaultZone()
{
    return kControlCenterZone;
}

PhosphorRules::Rule controlCenterRule(const QString& appId, int zoneNumber)
{
    using namespace PhosphorRules;

    Rule rule;
    rule.id = ruleIdFor(appId);
    rule.name = QStringLiteral("Phosphor control center pane");
    rule.enabled = true;
    rule.priority = 0;
    rule.managed = true;
    rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::Equals, appId);

    RuleAction snap;
    snap.type = QString(ActionType::SnapToZone);
    snap.params.insert(ActionParam::Zones, QJsonArray{qMax(1, zoneNumber)});

    RuleAction width;
    width.type = QString(ActionType::OpenColumnWidth);
    width.params.insert(ActionParam::Value, kThirdColumn);

    RuleAction placement;
    placement.type = QString(ActionType::OpenColumnPlacement);
    placement.params.insert(ActionParam::Value, QString(ColumnPlacementToken::NewColumn));

    rule.actions = {snap, width, placement};
    return rule;
}

// True when the call reached the daemon and it answered `true`.
bool callAcceptedRule(const char* method, const QString& json)
{
    const QDBusMessage reply = PhosphorProtocol::ClientHelpers::syncCall(PhosphorProtocol::Service::Interface::Rules,
                                                                         QString::fromLatin1(method), {json});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcPaneRules) << method << "failed:" << reply.errorMessage();
        return false;
    }
    return !reply.arguments().isEmpty() && reply.arguments().first().toBool();
}

bool updateControlCenterZone(const QString& appId, int zoneNumber)
{
    const PhosphorRules::Rule rule = controlCenterRule(appId, zoneNumber);
    const QString json = QString::fromUtf8(QJsonDocument(rule.toJson()).toJson(QJsonDocument::Compact));
    // Synchronous on purpose: the toplevel maps right after, and the rule
    // has to be in the daemon's store before the effect reads it.
    if (callAcceptedRule("updateRule", json)) {
        qCDebug(lcPaneRules) << "pane rule now snaps to zone" << zoneNumber;
        return true;
    }

    // updateRule does NOT upsert — RuleSet::updateRule returns false for an id
    // it does not hold. seed() is a two-round-trip async chain (getAllRules
    // then addRule), so the FIRST open can beat it, or the daemon can have
    // started after the shell. Without this the rewrite is refused and the
    // seed then installs the rule at defaultZone(), putting the pane in zone 1
    // no matter which chip was clicked. Adding it here carries the right zone
    // immediately, and is idempotent with the seed: addRule refuses a
    // colliding id, so whichever loses the race is a no-op.
    if (callAcceptedRule("addRule", json)) {
        qCInfo(lcPaneRules) << "pane rule seeded on demand, snapping to zone" << zoneNumber;
        return true;
    }
    qCWarning(lcPaneRules) << "daemon accepted neither an update nor an add of the pane rule for zone" << zoneNumber;
    return false;
}

void seed(QObject* parent, const PhosphorRules::Rule& rule)
{
    const QString ruleId = rule.id.toString();
    auto* lookup =
        new QDBusPendingCallWatcher(PhosphorProtocol::ClientHelpers::asyncCall(
                                        PhosphorProtocol::Service::Interface::Rules, QStringLiteral("getAllRules")),
                                    parent);
    QObject::connect(
        lookup, &QDBusPendingCallWatcher::finished, parent, [parent, rule, ruleId](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            const QDBusPendingReply<QString> reply = *w;
            if (reply.isError()) {
                qCInfo(lcPaneRules) << "daemon rules unavailable; pane rule not seeded:" << reply.error().message();
                return;
            }
            const QJsonObject set = QJsonDocument::fromJson(reply.value().toUtf8()).object();
            const QJsonArray rules = set.value(kRulesKey).toArray();
            for (const QJsonValue& v : rules) {
                if (v.toObject().value(kIdKey).toString() == ruleId) {
                    qCDebug(lcPaneRules) << "pane rule already present:" << rule.name;
                    return;
                }
            }
            const QString json = QString::fromUtf8(QJsonDocument(rule.toJson()).toJson(QJsonDocument::Compact));
            auto* add = new QDBusPendingCallWatcher(
                PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::Rules,
                                                           QStringLiteral("addRule"), {json}),
                parent);
            QObject::connect(
                add, &QDBusPendingCallWatcher::finished, parent, [name = rule.name](QDBusPendingCallWatcher* w2) {
                    w2->deleteLater();
                    const QDBusPendingReply<bool> ok = *w2;
                    if (ok.isError() || !ok.value()) {
                        qCWarning(lcPaneRules) << "daemon refused the pane rule" << name
                                               << (ok.isError() ? ok.error().message() : QStringLiteral("(rejected)"));
                        return;
                    }
                    qCInfo(lcPaneRules) << "seeded pane rule" << name;
                });
        });
}

} // namespace PhosphorShellApp::PaneRules

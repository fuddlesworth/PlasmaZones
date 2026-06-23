// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowruleadaptor.h"

#include "core/logging.h"

#include <PhosphorWindowRules/WindowRule.h>
#include <PhosphorWindowRules/WindowRuleSet.h>
#include <PhosphorWindowRules/WindowRuleStore.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace PlasmaZones {

namespace {

/// Parse a JSON object string. Returns an empty optional on malformed input
/// or a non-object document.
std::optional<QJsonObject> parseObject(const QString& json)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcDbus) << "WindowRuleAdaptor: malformed JSON object —" << err.errorString();
        return std::nullopt;
    }
    return doc.object();
}

} // namespace

WindowRuleAdaptor::WindowRuleAdaptor(PhosphorWindowRules::WindowRuleStore* store, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_store(store)
{
    if (m_store) {
        connect(m_store, &PhosphorWindowRules::WindowRuleStore::rulesChanged, this, &WindowRuleAdaptor::rulesChanged);
    }
}

void WindowRuleAdaptor::detach()
{
    if (m_store) {
        disconnect(m_store, nullptr, this, nullptr);
    }
    m_store = nullptr;
}

QString WindowRuleAdaptor::getAllRules()
{
    if (!m_store) {
        return QString();
    }
    const QJsonDocument doc(m_store->ruleSet().toJson());
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

bool WindowRuleAdaptor::setAllRules(const QString& rulesJson)
{
    if (!m_store) {
        return false;
    }
    // Reject oversize payloads at the D-Bus boundary rather than letting
    // QJsonDocument::fromJson allocate megabytes for a malformed/malicious
    // caller. The settings app pushes the whole rule set on every save, but
    // even an aggressively-large user rule set stays well under 1 MiB — the
    // cap is a generous order-of-magnitude headroom above realistic use,
    // not a tight bound.
    static constexpr qsizetype kMaxRuleSetBytes = 1 * 1024 * 1024; // 1 MiB
    if (rulesJson.size() > kMaxRuleSetBytes) {
        qCWarning(lcDbus) << "WindowRuleAdaptor::setAllRules: rejecting payload of" << rulesJson.size() << "bytes (cap"
                          << kMaxRuleSetBytes << ")";
        return false;
    }
    const auto obj = parseObject(rulesJson);
    if (!obj) {
        return false;
    }
    // Accept either the full { _version, rules } object or a bare object
    // whose `rules` array carries the list. We parse rules individually so a
    // version mismatch in the payload does not reject the whole call —
    // setAllRules is a daemon-trusted write of already-current-schema data.
    const QJsonValue rulesValue = obj->value(QLatin1String("rules"));
    if (!rulesValue.isArray()) {
        qCWarning(lcDbus) << "WindowRuleAdaptor::setAllRules: payload has no `rules` array";
        return false;
    }
    const QJsonArray rulesArray = rulesValue.toArray();
    QList<PhosphorWindowRules::WindowRule> rules;
    for (const QJsonValue& v : rulesArray) {
        if (!v.isObject()) {
            continue;
        }
        if (const auto rule = PhosphorWindowRules::WindowRule::fromJson(v.toObject())) {
            rules.append(*rule);
        }
    }
    // Drop-and-continue is intentional, but a silent drop hides a settings-app
    // ↔ daemon schema skew — warn with the counts so it is diagnosable.
    const int total = static_cast<int>(rulesArray.size());
    const int accepted = static_cast<int>(rules.size());
    if (accepted != total) {
        qCWarning(lcDbus) << "WindowRuleAdaptor::setAllRules: dropped" << (total - accepted) << "malformed rule(s) —"
                          << accepted << "accepted of" << total << "— possible settings-app/daemon schema skew";
    }
    // Propagate a persist failure to the D-Bus caller — a false return means
    // the in-memory set changed but the file write failed.
    return m_store->setAllRules(rules);
}

bool WindowRuleAdaptor::addRule(const QString& ruleJson)
{
    if (!m_store) {
        return false;
    }
    const auto obj = parseObject(ruleJson);
    if (!obj) {
        return false;
    }
    const auto rule = PhosphorWindowRules::WindowRule::fromJson(*obj);
    if (!rule) {
        qCWarning(lcDbus) << "WindowRuleAdaptor::addRule: rule failed validation";
        return false;
    }
    return m_store->addRule(*rule);
}

bool WindowRuleAdaptor::updateRule(const QString& ruleJson)
{
    if (!m_store) {
        return false;
    }
    const auto obj = parseObject(ruleJson);
    if (!obj) {
        return false;
    }
    const auto rule = PhosphorWindowRules::WindowRule::fromJson(*obj);
    if (!rule) {
        qCWarning(lcDbus) << "WindowRuleAdaptor::updateRule: rule failed validation";
        return false;
    }
    return m_store->updateRule(*rule);
}

bool WindowRuleAdaptor::removeRule(const QString& ruleId)
{
    if (!m_store) {
        return false;
    }
    const QUuid id = QUuid::fromString(ruleId);
    if (id.isNull()) {
        qCWarning(lcDbus) << "WindowRuleAdaptor::removeRule: malformed rule id —" << ruleId;
        return false;
    }
    return m_store->removeRule(id);
}

bool WindowRuleAdaptor::setRuleEnabled(const QString& ruleId, bool enabled)
{
    if (!m_store) {
        return false;
    }
    const QUuid id = QUuid::fromString(ruleId);
    if (id.isNull()) {
        qCWarning(lcDbus) << "WindowRuleAdaptor::setRuleEnabled: malformed rule id —" << ruleId;
        return false;
    }
    return m_store->setRuleEnabled(id, enabled);
}

bool WindowRuleAdaptor::setRulePriority(const QString& ruleId, int priority)
{
    if (!m_store) {
        return false;
    }
    const QUuid id = QUuid::fromString(ruleId);
    if (id.isNull()) {
        qCWarning(lcDbus) << "WindowRuleAdaptor::setRulePriority: malformed rule id —" << ruleId;
        return false;
    }
    return m_store->setRulePriority(id, priority);
}

} // namespace PlasmaZones

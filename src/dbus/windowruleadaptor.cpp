// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowruleadaptor.h"

#include "core/logging.h"

#include <PhosphorWindowRule/WindowRule.h>
#include <PhosphorWindowRule/WindowRuleSet.h>
#include <PhosphorWindowRule/WindowRuleStore.h>

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

WindowRuleAdaptor::WindowRuleAdaptor(PhosphorWindowRule::WindowRuleStore* store, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_store(store)
{
    if (m_store) {
        connect(m_store, &PhosphorWindowRule::WindowRuleStore::rulesChanged, this, &WindowRuleAdaptor::rulesChanged);
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
    QList<PhosphorWindowRule::WindowRule> rules;
    for (const QJsonValue& v : rulesValue.toArray()) {
        if (!v.isObject()) {
            continue;
        }
        if (const auto rule = PhosphorWindowRule::WindowRule::fromJson(v.toObject())) {
            rules.append(*rule);
        }
    }
    m_store->setAllRules(rules);
    return true;
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
    const auto rule = PhosphorWindowRule::WindowRule::fromJson(*obj);
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
    const auto rule = PhosphorWindowRule::WindowRule::fromJson(*obj);
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
    return m_store->removeRule(QUuid::fromString(ruleId));
}

bool WindowRuleAdaptor::setRuleEnabled(const QString& ruleId, bool enabled)
{
    if (!m_store) {
        return false;
    }
    return m_store->setRuleEnabled(QUuid::fromString(ruleId), enabled);
}

bool WindowRuleAdaptor::setRulePriority(const QString& ruleId, int priority)
{
    if (!m_store) {
        return false;
    }
    return m_store->setRulePriority(QUuid::fromString(ruleId), priority);
}

} // namespace PlasmaZones

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ruleadaptor.h"

#include "core/types/baselinecleanup.h"
#include "core/platform/logging.h"

#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleSet.h>
#include <PhosphorRules/RuleStore.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <optional>

namespace PlasmaZones {

namespace {

// Reject oversize payloads at the D-Bus boundary rather than letting
// QJsonDocument::fromJson allocate megabytes for a malformed/malicious caller.
// The check is on QString::size() (UTF-16 code units) so it stays O(1) and does
// not itself allocate a UTF-8 copy of a hostile payload; a code unit is at most
// 3 UTF-8 bytes, so this bounds the byte size within a small factor. The
// settings app pushes the whole rule set on every save, but even an
// aggressively-large user rule set stays well under 1 Mi code units — the cap is
// generous headroom above realistic use, not a tight bound. It lives here rather
// than in setAllRules alone because every entry point takes caller-controlled
// JSON: a single-rule addRule/updateRule payload is just as reachable from an
// arbitrary bus client.
constexpr qsizetype kMaxPayloadChars = 1 * 1024 * 1024; // ~1 MiB

/// Parse a caller-supplied JSON object string, bounded by @ref kMaxPayloadChars.
/// Returns an empty optional on an oversize payload, malformed input, or a
/// valid document that is not an object. @p context names the calling slot so a
/// rejection is attributable from the log alone.
std::optional<QJsonObject> parseObject(const QString& json, QLatin1String context)
{
    if (json.size() > kMaxPayloadChars) {
        qCWarning(lcDbusRules) << context << "rejecting payload of" << json.size() << "code units (cap"
                               << kMaxPayloadChars << ")";
        return std::nullopt;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        qCWarning(lcDbusRules) << context << "malformed JSON —" << err.errorString();
        return std::nullopt;
    }
    // Split from the parse failure above rather than sharing its message: for a
    // well-formed document that simply is not an object, err.errorString() reads
    // "no error occurred", so the combined warning contradicted itself.
    if (!doc.isObject()) {
        qCWarning(lcDbusRules) << context << "payload parsed but is not a JSON object";
        return std::nullopt;
    }
    return doc.object();
}

} // namespace

RuleAdaptor::RuleAdaptor(PhosphorRules::RuleStore* store, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_store(store)
{
    if (!m_store) {
        // Same shape as the sibling adaptors: a null collaborator degrades to
        // null-safe no-ops on every slot, but it is never intentional at
        // construction, so say so instead of failing silently.
        qCWarning(lcDbusRules) << "RuleAdaptor created with null store";
        return;
    }
    connect(m_store, &PhosphorRules::RuleStore::rulesChanged, this, &RuleAdaptor::rulesChanged);
}

void RuleAdaptor::detach()
{
    if (m_store) {
        // Named rather than blanket: the constructor makes exactly one
        // connection, so severing that one says what is being undone.
        disconnect(m_store, &PhosphorRules::RuleStore::rulesChanged, this, &RuleAdaptor::rulesChanged);
    }
    m_store = nullptr;
}

QString RuleAdaptor::getAllRules()
{
    if (!m_store) {
        return QString();
    }
    const QJsonDocument doc(m_store->ruleSet().toJson());
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

bool RuleAdaptor::setAllRules(const QString& rulesJson)
{
    if (!m_store) {
        return false;
    }
    const auto obj = parseObject(rulesJson, QLatin1String("RuleAdaptor::setAllRules:"));
    if (!obj) {
        return false;
    }
    // Accept either the full { _version, rules } object or a bare object whose
    // `rules` array carries the list, but refuse a version that is PRESENT and
    // disagrees with the schema this build reads. Re-persisting a skewed
    // payload would stamp it with the daemon's own current version on save,
    // silently laundering a cross-version downgrade into a file that claims to
    // be current. An ABSENT version stays tolerated — the bare-array shape
    // above is a documented caller spelling, not a skew signal.
    const QJsonValue versionValue = obj->value(QLatin1String("_version"));
    if (!versionValue.isUndefined() && versionValue.toInt(-1) != PhosphorRules::RuleSet::SchemaVersion) {
        qCWarning(lcDbusRules) << "RuleAdaptor::setAllRules: refusing payload stamped _version"
                               << versionValue.toInt(-1) << "— this build reads"
                               << PhosphorRules::RuleSet::SchemaVersion;
        return false;
    }
    const QJsonValue rulesValue = obj->value(QLatin1String("rules"));
    if (!rulesValue.isArray()) {
        qCWarning(lcDbusRules) << "RuleAdaptor::setAllRules: payload has no `rules` array";
        return false;
    }
    const QJsonArray rulesArray = rulesValue.toArray();
    QList<PhosphorRules::Rule> rules;
    for (const QJsonValue& v : rulesArray) {
        if (!v.isObject()) {
            continue;
        }
        const auto rule = PhosphorRules::Rule::fromJson(v.toObject());
        // Filter on isValid() and not merely on fromJson succeeding, because
        // isValid() is the predicate RuleSet::setRules applies when it decides
        // what to COMMIT. The two agree today (RuleAction::fromJson already
        // runs the registry validation isValid() re-runs), but the counts below
        // gate a data-loss refusal, and a guard that measures a different
        // population than the commit does is one loader tightening away from
        // waving through a full wipe.
        if (rule && rule->isValid()) {
            rules.append(*rule);
        }
    }
    // Drop-and-continue is intentional, but a silent drop hides a settings-app
    // ↔ daemon schema skew — warn with the counts so it is diagnosable.
    const int total = static_cast<int>(rulesArray.size());
    const int accepted = static_cast<int>(rules.size());
    if (accepted != total) {
        qCWarning(lcDbusRules) << "RuleAdaptor::setAllRules: dropped" << (total - accepted) << "rejected rule(s) —"
                               << accepted << "accepted of" << total << "— possible settings-app/daemon schema skew";
    }
    // A non-empty payload that parsed to ZERO accepted rules is a rejected
    // payload (whole-set schema skew), NOT an intentional clear — committing it
    // would wipe every persisted rule. A genuine clear sends an empty `rules`
    // array, which makes total == 0 and skips this guard.
    if (total > 0 && accepted == 0) {
        qCWarning(lcDbusRules) << "RuleAdaptor::setAllRules: refusing to commit — all" << total
                               << "rule(s) were malformed; treating as a rejected payload, not a clear";
        return false;
    }
    // Propagate a persist failure to the D-Bus caller — a false return means
    // the in-memory set changed but the file write failed.
    return m_store->setAllRules(rules);
}

bool RuleAdaptor::addRule(const QString& ruleJson)
{
    if (!m_store) {
        return false;
    }
    const auto obj = parseObject(ruleJson, QLatin1String("RuleAdaptor::addRule:"));
    if (!obj) {
        return false;
    }
    const auto rule = PhosphorRules::Rule::fromJson(*obj);
    if (!rule) {
        qCWarning(lcDbusRules) << "RuleAdaptor::addRule: rule failed validation";
        return false;
    }
    return m_store->addRule(*rule);
}

bool RuleAdaptor::updateRule(const QString& ruleJson)
{
    if (!m_store) {
        return false;
    }
    const auto obj = parseObject(ruleJson, QLatin1String("RuleAdaptor::updateRule:"));
    if (!obj) {
        return false;
    }
    const auto rule = PhosphorRules::Rule::fromJson(*obj);
    if (!rule) {
        qCWarning(lcDbusRules) << "RuleAdaptor::updateRule: rule failed validation";
        return false;
    }
    return m_store->updateRule(*rule);
}

bool RuleAdaptor::removeRule(const QString& ruleId)
{
    if (!m_store) {
        return false;
    }
    const QUuid id = QUuid::fromString(ruleId);
    if (id.isNull()) {
        qCWarning(lcDbusRules) << "RuleAdaptor::removeRule: malformed rule id —" << ruleId;
        return false;
    }
    return m_store->removeRule(id);
}

bool RuleAdaptor::setRuleEnabled(const QString& ruleId, bool enabled)
{
    if (!m_store) {
        return false;
    }
    const QUuid id = QUuid::fromString(ruleId);
    if (id.isNull()) {
        qCWarning(lcDbusRules) << "RuleAdaptor::setRuleEnabled: malformed rule id —" << ruleId;
        return false;
    }
    return m_store->setRuleEnabled(id, enabled);
}

bool RuleAdaptor::setRulePriority(const QString& ruleId, int priority)
{
    if (!m_store) {
        return false;
    }
    const QUuid id = QUuid::fromString(ruleId);
    if (id.isNull()) {
        qCWarning(lcDbusRules) << "RuleAdaptor::setRulePriority: malformed rule id —" << ruleId;
        return false;
    }
    return m_store->setRulePriority(id, priority);
}

void RuleAdaptor::reloadRules()
{
    if (!m_store) {
        return;
    }
    m_store->load();
}

void RuleAdaptor::resetManagedDefaults()
{
    if (!m_store) {
        return;
    }
    // Reload from disk first so we operate on the CURRENTLY PERSISTED set. The
    // global Restore Defaults calls Settings::reset() (which writes rules.json
    // from the settings process to drop the per-mode DisableEngine rules) just
    // before this; the daemon's borrowed rule store is NOT refreshed by the
    // reloadSettings() that follows (only an owned store reloads there), so its
    // in-memory set is stale. Without this reload, the setAllRules below would
    // write those dropped disable rules back. load() is idempotent (emits only
    // on a real change).
    m_store->load();

    // Window appearance (border / title bar / gap) defaults now live in the config
    // store, not in managed baseline rules. "Restore Defaults" resets those config
    // values on the settings side; here we only have to strip any stale managed
    // appearance baselines an older build left in rules.json, preserving every user
    // rule. Shared with the daemon's startup cleanup so the two can't drift.
    if (!stripStaleManagedAppearanceBaselines(*m_store)) {
        qCWarning(lcDbusRules) << "RuleAdaptor::resetManagedDefaults: failed to persist rules.json after stripping "
                                  "stale baseline appearance rules";
    }
}

} // namespace PlasmaZones

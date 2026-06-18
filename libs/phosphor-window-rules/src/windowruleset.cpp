// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWindowRules/WindowRuleSet.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSaveFile>
#include <QSet>

#include "windowrulelogging.h"

namespace PhosphorWindowRules {

namespace {

constexpr QLatin1StringView kKeyVersion{"_version"};
constexpr QLatin1StringView kKeyRules{"rules"};

/// Upper bound on a rule-store file. A legitimate window-rule store is a few
/// KB; anything past a few MB is corrupt or hostile and must not be slurped
/// whole into memory.
constexpr qint64 kMaxFileBytes = 8 * 1024 * 1024;

} // namespace

std::optional<WindowRule> WindowRuleSet::ruleById(const QUuid& id) const
{
    for (const WindowRule& rule : m_rules) {
        if (rule.id == id) {
            return rule;
        }
    }
    return std::nullopt;
}

bool WindowRuleSet::addRule(const WindowRule& rule)
{
    if (!rule.isValid()) {
        qCWarning(lcWindowRule) << "addRule: rejecting invalid rule. id:" << rule.id.toString();
        return false;
    }
    if (ruleById(rule.id)) {
        qCWarning(lcWindowRule) << "addRule: rejecting rule with a colliding id:" << rule.id.toString();
        return false;
    }
    m_rules.append(rule);
    ++m_revision;
    return true;
}

bool WindowRuleSet::updateRule(const WindowRule& rule)
{
    if (!rule.isValid()) {
        qCWarning(lcWindowRule) << "updateRule: rejecting invalid rule. id:" << rule.id.toString();
        return false;
    }
    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules.at(i).id == rule.id) {
            m_rules[i] = rule;
            ++m_revision;
            return true;
        }
    }
    qCWarning(lcWindowRule) << "updateRule: no rule with id:" << rule.id.toString();
    return false;
}

bool WindowRuleSet::removeRule(const QUuid& id)
{
    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules.at(i).id == id) {
            m_rules.removeAt(i);
            ++m_revision;
            return true;
        }
    }
    return false;
}

int WindowRuleSet::setRules(const QList<WindowRule>& rules)
{
    QList<WindowRule> validated;
    validated.reserve(rules.size());
    // Mirror fromJson's duplicate-id handling: keep the first entry for any id,
    // drop later collisions. With deterministic v5 UUIDs two identical source
    // entries derive the same id, so without this an in-memory set could hold
    // duplicate ids that a save/load round-trip would silently de-dup.
    QSet<QUuid> seenIds;
    for (const WindowRule& rule : rules) {
        if (!rule.isValid()) {
            qCWarning(lcWindowRule) << "setRules: dropping invalid rule. id:" << rule.id.toString();
            continue;
        }
        if (seenIds.contains(rule.id)) {
            qCWarning(lcWindowRule) << "setRules: dropping rule with a duplicate id:" << rule.id.toString();
            continue;
        }
        seenIds.insert(rule.id);
        validated.append(rule);
    }
    // Always assign + bump. A no-op-skip optimisation here is unsafe: the
    // round-trip addRule(X) → setRules(originalList) can produce a
    // post-validation candidate that equals the prior in-memory list, yet
    // downstream consumers keyed on `revision()` (RuleEvaluator's match cache,
    // any external cascade-cache) would see no bump and continue serving stale
    // results. The validation pass already dedupes and rejects bad inputs —
    // the skip wasn't load-bearing, only premature.
    m_rules = std::move(validated);
    ++m_revision;
    qCInfo(lcWindowRule) << "setRules: replaced rule list — count:" << m_rules.size() << "revision:" << m_revision;
    return m_rules.size();
}

void WindowRuleSet::clear()
{
    // Clearing an already-empty set is not a mutation — do not bump the
    // revision (a bump would needlessly invalidate the evaluator's cache).
    if (m_rules.isEmpty()) {
        return;
    }
    m_rules.clear();
    ++m_revision;
}

QJsonObject WindowRuleSet::toJson() const
{
    QJsonObject o;
    o.insert(kKeyVersion, SchemaVersion);
    QJsonArray arr;
    for (const WindowRule& rule : m_rules) {
        arr.append(rule.toJson());
    }
    o.insert(kKeyRules, arr);
    return o;
}

std::optional<WindowRuleSet> WindowRuleSet::fromJson(const QJsonObject& obj)
{
    // The library refuses any version other than its own — migration is the
    // config layer's job, never the library's.
    const QJsonValue versionValue = obj.value(kKeyVersion);
    if (!versionValue.isDouble() || versionValue.toInt() != SchemaVersion) {
        qCWarning(lcWindowRule).nospace()
            << "WindowRuleSet::fromJson refusing a non-v" << SchemaVersion << " document. _version: " << versionValue;
        return std::nullopt;
    }

    WindowRuleSet set;
    const QJsonValue rulesValue = obj.value(kKeyRules);
    if (!rulesValue.isArray()) {
        qCWarning(lcWindowRule) << "WindowRuleSet::fromJson: `rules` is missing or not an array — loading empty set.";
        return set;
    }
    for (const QJsonValue& v : rulesValue.toArray()) {
        if (!v.isObject()) {
            qCWarning(lcWindowRule) << "WindowRuleSet::fromJson: rule entry is not an object — dropping.";
            continue;
        }
        const auto rule = WindowRule::fromJson(v.toObject());
        if (!rule) {
            continue;
        }
        if (set.ruleById(rule->id)) {
            qCWarning(lcWindowRule) << "WindowRuleSet::fromJson: dropping rule with a duplicate id:"
                                    << rule->id.toString();
            continue;
        }
        // Semantic compatibility check — distinct from `WindowRule::fromJson`'s
        // structural validation. The rule is kept even on issue: a hand-edited
        // store can legitimately carry an action/match mismatch the user wants
        // to see (and fix from the settings UI) rather than have silently
        // dropped. Settings re-runs the same check to badge the offending rule.
        for (const ValidationIssue& issue : rule->validationIssues()) {
            qCWarning(lcWindowRule).nospace()
                << "WindowRuleSet::fromJson: rule has a validation issue — keeping the rule. id: "
                << rule->id.toString() << " action index: " << issue.actionIndex << " action type: " << issue.actionType
                << " message: " << issue.message;
        }
        set.m_rules.append(*rule);
    }
    // A freshly loaded set starts at revision 0 — the load is the baseline.
    set.m_revision = 0;
    return set;
}

std::optional<WindowRuleSet> WindowRuleSet::loadFromFile(const QString& path)
{
    QFile file(path);
    if (!file.exists()) {
        qCWarning(lcWindowRule) << "WindowRuleSet::loadFromFile: file does not exist:" << path;
        return std::nullopt;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcWindowRule) << "WindowRuleSet::loadFromFile: cannot open file:" << path << file.errorString();
        return std::nullopt;
    }
    // Reject an implausibly large file before reading it — a corrupt or
    // hostile store must not be slurped whole into memory.
    if (file.size() > kMaxFileBytes) {
        qCWarning(lcWindowRule) << "WindowRuleSet::loadFromFile: file exceeds the" << kMaxFileBytes
                                << "byte cap — refusing to load:" << path << "size:" << file.size();
        return std::nullopt;
    }
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcWindowRule) << "WindowRuleSet::loadFromFile: malformed JSON in" << path << error.errorString();
        return std::nullopt;
    }
    return fromJson(doc.object());
}

bool WindowRuleSet::saveToFile(const QString& path) const
{
    // QSaveFile gives atomic temp-write + rename — a crash mid-write never
    // leaves a truncated rule store.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcWindowRule) << "WindowRuleSet::saveToFile: cannot open file for writing:" << path
                                << file.errorString();
        return false;
    }
    const QJsonDocument doc(toJson());
    const QByteArray payload = doc.toJson(QJsonDocument::Indented);
    // A short write (fewer bytes than the payload) is as much a failure as a
    // negative return — a disk-full condition truncates without erroring.
    if (file.write(payload) != payload.size()) {
        qCWarning(lcWindowRule) << "WindowRuleSet::saveToFile: write failed:" << path << file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        qCWarning(lcWindowRule) << "WindowRuleSet::saveToFile: commit failed:" << path << file.errorString();
        return false;
    }
    return true;
}

} // namespace PhosphorWindowRules

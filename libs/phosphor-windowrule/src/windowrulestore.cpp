// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWindowRule/WindowRuleStore.h>

#include "windowrulelogging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace PhosphorWindowRule {

WindowRuleStore::WindowRuleStore(const QString& filePath, QObject* parent)
    : QObject(parent)
    , m_filePath(filePath)
{
    Q_ASSERT_X(!m_filePath.isEmpty(), "WindowRuleStore",
               "filePath is required — the library never derives config locations");
    // QSaveFile (inside WindowRuleSet::saveToFile) needs the parent directory
    // to exist. The path is fixed for the store's lifetime, so create the
    // directory once here rather than on every mutating save().
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    load();
}

void WindowRuleStore::load()
{
    if (!QFile::exists(m_filePath)) {
        // A missing store is the fresh-install / never-had-rules case — not
        // an error. The set stays empty.
        m_ruleSet = WindowRuleSet();
        qCInfo(lcWindowRule) << "WindowRuleStore: no" << m_filePath << "— starting with an empty rule set";
        Q_EMIT rulesChanged();
        return;
    }

    auto loaded = WindowRuleSet::loadFromFile(m_filePath);
    if (!loaded) {
        // Malformed file / version mismatch — WindowRuleSet already logged
        // the diagnostic. Fall back to an empty set rather than crashing the
        // daemon; the user can re-author rules from the settings UI.
        qCWarning(lcWindowRule) << "WindowRuleStore: failed to load" << m_filePath << "— using an empty rule set";
        m_ruleSet = WindowRuleSet();
        Q_EMIT rulesChanged();
        return;
    }

    m_ruleSet = std::move(*loaded);
    qCInfo(lcWindowRule) << "WindowRuleStore: loaded" << m_ruleSet.count() << "rules from" << m_filePath;
    Q_EMIT rulesChanged();
}

bool WindowRuleStore::save()
{
    // The parent directory is created once in the constructor — the path is
    // fixed for the store's lifetime.
    if (!m_ruleSet.saveToFile(m_filePath)) {
        qCWarning(lcWindowRule) << "WindowRuleStore: failed to save" << m_filePath;
        return false;
    }
    qCDebug(lcWindowRule) << "WindowRuleStore: saved" << m_ruleSet.count() << "rules to" << m_filePath;
    return true;
}

bool WindowRuleStore::setAllRules(const QList<WindowRule>& rules)
{
    // Build the candidate set first (setRules drops invalid rules) so a no-op
    // replacement can be detected against the post-validation shape — this
    // skips the persist + emit and returns true, per the header contract.
    WindowRuleSet candidate;
    candidate.setRules(rules);
    if (candidate == m_ruleSet) {
        return true;
    }
    m_ruleSet = std::move(candidate);
    const bool persisted = save();
    Q_EMIT rulesChanged();
    return persisted;
}

bool WindowRuleStore::addRule(const WindowRule& rule)
{
    if (!m_ruleSet.addRule(rule)) {
        return false;
    }
    const bool persisted = save();
    Q_EMIT rulesChanged();
    return persisted;
}

bool WindowRuleStore::updateRule(const WindowRule& rule)
{
    if (!m_ruleSet.updateRule(rule)) {
        return false;
    }
    const bool persisted = save();
    Q_EMIT rulesChanged();
    return persisted;
}

bool WindowRuleStore::removeRule(const QUuid& id)
{
    if (!m_ruleSet.removeRule(id)) {
        return false;
    }
    const bool persisted = save();
    Q_EMIT rulesChanged();
    return persisted;
}

bool WindowRuleStore::setRuleEnabled(const QUuid& id, bool enabled)
{
    const auto existing = m_ruleSet.ruleById(id);
    if (!existing) {
        return false;
    }
    if (existing->enabled == enabled) {
        // No-op — do not persist or emit on an unchanged value.
        return true;
    }
    WindowRule updated = *existing;
    updated.enabled = enabled;
    if (!m_ruleSet.updateRule(updated)) {
        return false;
    }
    const bool persisted = save();
    Q_EMIT rulesChanged();
    return persisted;
}

bool WindowRuleStore::setRulePriority(const QUuid& id, int priority)
{
    const auto existing = m_ruleSet.ruleById(id);
    if (!existing) {
        return false;
    }
    if (existing->priority == priority) {
        return true;
    }
    WindowRule updated = *existing;
    updated.priority = priority;
    if (!m_ruleSet.updateRule(updated)) {
        return false;
    }
    const bool persisted = save();
    Q_EMIT rulesChanged();
    return persisted;
}

} // namespace PhosphorWindowRule

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
    // QSaveFile (inside WindowRuleSet::saveToFile) needs the parent directory
    // to exist.
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    if (!m_ruleSet.saveToFile(m_filePath)) {
        qCWarning(lcWindowRule) << "WindowRuleStore: failed to save" << m_filePath;
        return false;
    }
    qCDebug(lcWindowRule) << "WindowRuleStore: saved" << m_ruleSet.count() << "rules to" << m_filePath;
    return true;
}

void WindowRuleStore::setAllRules(const QList<WindowRule>& rules)
{
    m_ruleSet.setRules(rules);
    save();
    Q_EMIT rulesChanged();
}

bool WindowRuleStore::addRule(const WindowRule& rule)
{
    if (!m_ruleSet.addRule(rule)) {
        return false;
    }
    save();
    Q_EMIT rulesChanged();
    return true;
}

bool WindowRuleStore::updateRule(const WindowRule& rule)
{
    if (!m_ruleSet.updateRule(rule)) {
        return false;
    }
    save();
    Q_EMIT rulesChanged();
    return true;
}

bool WindowRuleStore::removeRule(const QUuid& id)
{
    if (!m_ruleSet.removeRule(id)) {
        return false;
    }
    save();
    Q_EMIT rulesChanged();
    return true;
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
    save();
    Q_EMIT rulesChanged();
    return true;
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
    save();
    Q_EMIT rulesChanged();
    return true;
}

} // namespace PhosphorWindowRule

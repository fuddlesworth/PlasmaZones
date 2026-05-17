// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowrulestore.h"

#include "config/configdefaults.h"
#include "logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace PlasmaZones {

WindowRuleStore::WindowRuleStore(const QString& filePath, QObject* parent)
    : QObject(parent)
    , m_filePath(filePath.isEmpty() ? ConfigDefaults::windowRulesFilePath() : filePath)
{
    load();
}

void WindowRuleStore::load()
{
    if (!QFile::exists(m_filePath)) {
        // A missing store is the fresh-install / never-had-rules case — not
        // an error. The set stays empty.
        m_ruleSet = PhosphorWindowRule::WindowRuleSet();
        qCInfo(lcDaemon) << "WindowRuleStore: no" << m_filePath << "— starting with an empty rule set";
        Q_EMIT rulesChanged();
        return;
    }

    auto loaded = PhosphorWindowRule::WindowRuleSet::loadFromFile(m_filePath);
    if (!loaded) {
        // Malformed file / version mismatch — WindowRuleSet already logged
        // the diagnostic. Fall back to an empty set rather than crashing the
        // daemon; the user can re-author rules from the settings UI.
        qCWarning(lcDaemon) << "WindowRuleStore: failed to load" << m_filePath << "— using an empty rule set";
        m_ruleSet = PhosphorWindowRule::WindowRuleSet();
        Q_EMIT rulesChanged();
        return;
    }

    m_ruleSet = std::move(*loaded);
    qCInfo(lcDaemon) << "WindowRuleStore: loaded" << m_ruleSet.count() << "rules from" << m_filePath;
    Q_EMIT rulesChanged();
}

bool WindowRuleStore::save()
{
    // QSaveFile (inside WindowRuleSet::saveToFile) needs the parent directory
    // to exist.
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    if (!m_ruleSet.saveToFile(m_filePath)) {
        qCWarning(lcDaemon) << "WindowRuleStore: failed to save" << m_filePath;
        return false;
    }
    qCDebug(lcDaemon) << "WindowRuleStore: saved" << m_ruleSet.count() << "rules to" << m_filePath;
    return true;
}

void WindowRuleStore::setAllRules(const QList<PhosphorWindowRule::WindowRule>& rules)
{
    m_ruleSet.setRules(rules);
    save();
    Q_EMIT rulesChanged();
}

bool WindowRuleStore::addRule(const PhosphorWindowRule::WindowRule& rule)
{
    if (!m_ruleSet.addRule(rule)) {
        return false;
    }
    save();
    Q_EMIT rulesChanged();
    return true;
}

bool WindowRuleStore::updateRule(const PhosphorWindowRule::WindowRule& rule)
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
    PhosphorWindowRule::WindowRule updated = *existing;
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
    PhosphorWindowRule::WindowRule updated = *existing;
    updated.priority = priority;
    if (!m_ruleSet.updateRule(updated)) {
        return false;
    }
    save();
    Q_EMIT rulesChanged();
    return true;
}

} // namespace PlasmaZones

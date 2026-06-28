// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRules/RuleStore.h>

#include "rulelogging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace PhosphorRules {

RuleStore::RuleStore(const QString& filePath, QObject* parent)
    : QObject(parent)
    , m_filePath(filePath)
{
    // Q_ASSERT_X catches misuse in debug builds, but Q_ASSERT_X compiles out
    // in release. A release-build caller passing an empty path would
    // otherwise silently drift through: `QFile::exists("")` returns false
    // so `load()` becomes a no-op, every `save()` writes to an unwritable
    // path. Flag the misconfiguration loudly so packagers / embedders see
    // it in journalctl. The store still constructs — caller may swap the
    // path through a future setter, and a hard return here would change
    // the QObject construction contract.
    Q_ASSERT_X(!m_filePath.isEmpty(), "RuleStore", "filePath is required — the library never derives config locations");
    if (m_filePath.isEmpty()) {
        qCCritical(lcRule) << "RuleStore: constructed with empty filePath — load() and save() will be no-ops"
                           << "until the path is corrected. The library never derives config locations; callers"
                           << "must pass an absolute path.";
        return;
    }
    // Relative paths resolve against the process CWD, which can change via
    // chdir(); the resulting save() location would silently move with the
    // process. The header doc says "absolute path" — flag the misuse loudly
    // so the caller hears about it on the first construction rather than
    // discovering it later when a config write lands in the wrong tree.
    if (!QFileInfo(m_filePath).isAbsolute()) {
        qCWarning(lcRule) << "RuleStore: filePath" << m_filePath
                          << "is relative — load() / save() will resolve against the process CWD"
                          << "which may change at runtime. Pass an absolute path.";
    }
    // QSaveFile (inside RuleSet::saveToFile) needs the parent directory
    // to exist. The path is fixed for the store's lifetime, so create the
    // directory once here rather than on every mutating save().
    const QString parentDir = QFileInfo(m_filePath).absolutePath();
    if (!QDir().mkpath(parentDir)) {
        // Not fatal — load() still works for an existing file and a later
        // save() will report its own failure — but a failed mkpath almost
        // always means every save() will fail, so surface it now.
        qCWarning(lcRule) << "RuleStore: failed to create config directory" << parentDir << "— saves may fail";
    }
    load();
}

void RuleStore::load()
{
    // Compute the candidate set up front so every code path can compare it
    // against the current in-memory set before deciding to emit. Only emit
    // when the loaded content actually differs from what we already hold —
    // an idempotent re-load must not fire `rulesChanged` and trigger
    // downstream cache flushes for nothing.
    RuleSet candidate;
    if (!QFile::exists(m_filePath)) {
        // A missing store is the fresh-install / never-had-rules case — not
        // an error. The candidate stays empty.
        qCInfo(lcRule) << "RuleStore: no" << m_filePath << "— starting with an empty rule set";
    } else {
        auto loaded = RuleSet::loadFromFile(m_filePath);
        if (!loaded) {
            // Malformed file / version mismatch / size-cap rejection /
            // transient read failure. PRESERVE the in-memory set rather
            // than falling back to empty — a settings-app reload that
            // races the daemon mid-write would otherwise blank a live
            // rule list on a transient read race. The user can re-author
            // rules from the settings UI if the on-disk corruption is
            // permanent; in either case the in-memory view stays usable
            // until either save() rewrites the file or the loader
            // succeeds on a subsequent retry.
            qCCritical(lcRule) << "RuleStore: failed to load" << m_filePath
                               << "— keeping the prior in-memory rule set (count:" << m_ruleSet.count()
                               << ", revision:" << m_ruleSet.revision()
                               << "). Save / load may resync on the next mutation.";
            return;
        }
        candidate = std::move(*loaded);
        qCInfo(lcRule) << "RuleStore: loaded" << candidate.count() << "rules from" << m_filePath;
    }

    if (candidate == m_ruleSet) {
        // Idempotent re-load — content unchanged, do not emit.
        return;
    }
    // Apply the loaded list through the live `setRules` so the store's
    // monotonic revision counter advances past its prior peak. A move-
    // assignment would adopt the candidate's freshly-loaded revision
    // (`fromJson` starts a loaded set at revision 0), regressing the
    // store's counter — RuleEvaluator caches keyed `(windowId, revision)`
    // would re-hit on the next mutation that cycles the counter back to
    // a previously-used value and return stale ResolvedActions.
    m_ruleSet.setRules(candidate.rules());
    // Load reflects the on-disk state — the in-memory set is by definition
    // already "persisted" from this side, so the flag is unconditionally true.
    Q_EMIT rulesChanged(/*persisted=*/true);
}

bool RuleStore::save()
{
    // The parent directory is created once in the constructor — the path is
    // fixed for the store's lifetime.
    if (!m_ruleSet.saveToFile(m_filePath)) {
        qCWarning(lcRule) << "RuleStore: failed to save" << m_filePath;
        return false;
    }
    qCDebug(lcRule) << "RuleStore: saved" << m_ruleSet.count() << "rules to" << m_filePath;
    return true;
}

bool RuleStore::setAllRules(const QList<Rule>& rules)
{
    // Build a throw-away candidate first so the no-op fast path can compare
    // against the POST-validation shape (setRules drops invalid + duplicate
    // ids; an unsanitised list could differ from the live store on paper
    // yet collapse to the same set after validation).
    RuleSet candidate;
    candidate.setRules(rules);
    if (candidate == m_ruleSet) {
        return true;
    }
    // Mutate the live store in place rather than move-assigning the candidate.
    // A move-assignment would reset `m_ruleSet.revision()` to the candidate's
    // freshly-bumped value (1 after a single setRules call), regressing the
    // store's monotonic counter. RuleEvaluator's match cache and
    // priority-order index are keyed `(windowId, revision)` and
    // `(revision, rules.size())` — a stale (windowId, 1) cache entry that
    // happens to predate the regression would re-hit when the counter
    // cycles back to its prior values, returning a non-current
    // ResolvedActions for the same window. Calling the live `setRules` on
    // the existing instance bumps its own revision past the prior peak.
    m_ruleSet.setRules(candidate.rules());
    const bool persisted = save();
    Q_EMIT rulesChanged(persisted);
    return persisted;
}

bool RuleStore::addRule(const Rule& rule)
{
    if (!m_ruleSet.addRule(rule)) {
        return false;
    }
    const bool persisted = save();
    Q_EMIT rulesChanged(persisted);
    return persisted;
}

bool RuleStore::updateRule(const Rule& rule)
{
    if (!m_ruleSet.updateRule(rule)) {
        return false;
    }
    const bool persisted = save();
    Q_EMIT rulesChanged(persisted);
    return persisted;
}

bool RuleStore::removeRule(const QUuid& id)
{
    if (!m_ruleSet.removeRule(id)) {
        return false;
    }
    const bool persisted = save();
    Q_EMIT rulesChanged(persisted);
    return persisted;
}

bool RuleStore::setRuleEnabled(const QUuid& id, bool enabled)
{
    const auto existing = m_ruleSet.ruleById(id);
    if (!existing) {
        return false;
    }
    if (existing->enabled == enabled) {
        // No-op — do not persist or emit on an unchanged value.
        return true;
    }
    Rule updated = *existing;
    updated.enabled = enabled;
    if (!m_ruleSet.updateRule(updated)) {
        return false;
    }
    const bool persisted = save();
    Q_EMIT rulesChanged(persisted);
    return persisted;
}

bool RuleStore::setRulePriority(const QUuid& id, int priority)
{
    const auto existing = m_ruleSet.ruleById(id);
    if (!existing) {
        return false;
    }
    if (existing->priority == priority) {
        return true;
    }
    Rule updated = *existing;
    updated.priority = priority;
    if (!m_ruleSet.updateRule(updated)) {
        return false;
    }
    const bool persisted = save();
    Q_EMIT rulesChanged(persisted);
    return persisted;
}

} // namespace PhosphorRules

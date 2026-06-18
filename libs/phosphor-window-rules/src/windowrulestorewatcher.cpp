// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorWindowRules/WindowRuleStoreWatcher.h"

#include "PhosphorWindowRules/WindowRuleStore.h"

#include "windowrulelogging.h"

#include <PhosphorFsLoader/IScanStrategy.h>
#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QFileInfo>

namespace PhosphorWindowRules {

// Thin scan policy: every rescan reloads the store from disk and returns the
// store's file path so the base re-arms a per-file watch (surviving the
// atomic-rename inode swap). The store's idempotent load() makes self-write
// rescans no-ops, so no signature/self-write bookkeeping is needed here — the
// base's debounce already coalesces the temp-write + rename burst.
class WindowRuleStoreWatcher::ReloadStrategy : public PhosphorFsLoader::IScanStrategy
{
public:
    explicit ReloadStrategy(WindowRuleStore& store)
        : m_store(&store)
    {
    }

    QStringList performScan(const QStringList& /*directoriesInScanOrder*/) override
    {
        m_store->load();
        const QString filePath = m_store->filePath();
        // Only ask the base to watch the file once it exists; until then the
        // base's parent-directory watch covers the create event and promotes to
        // a direct file watch on the next rescan.
        if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
            return {};
        }
        return {filePath};
    }

private:
    WindowRuleStore* m_store;
};

WindowRuleStoreWatcher::WindowRuleStoreWatcher(WindowRuleStore& store, QObject* parent)
    : QObject(parent)
    , m_store(&store)
    , m_strategy(std::make_unique<ReloadStrategy>(store))
    // Parented to `this` AND held by unique_ptr: the QObject dtor unlinks the
    // child from its parent when the unique_ptr resets, so there is no
    // double-delete (the established pattern — see ScriptedAlgorithmLoader).
    // m_strategy is declared before m_watcher so it outlives the watcher that
    // borrows it by reference.
    , m_watcher(std::make_unique<PhosphorFsLoader::WatchedDirectorySet>(*m_strategy, this))
{
}

WindowRuleStoreWatcher::~WindowRuleStoreWatcher() = default;

void WindowRuleStoreWatcher::start()
{
    if (m_started) {
        return;
    }
    // WindowRuleStore asserts an empty path in debug and critically logs it in
    // release (where it still constructs) — which would leave this watcher
    // silently watching nothing. Surface that dead-watcher state rather than
    // quietly registering an empty directory.
    const QString filePath = m_store->filePath();
    if (filePath.isEmpty()) {
        qCWarning(lcWindowRule) << "WindowRuleStoreWatcher: store has an empty file path — watcher is inert";
        return;
    }
    m_started = true;
    // Watch the directory that holds windowrules.json. registerDirectory with
    // LiveReload::On installs the QFileSystemWatcher and runs one immediate
    // synchronous rescan (an idempotent reload if nothing changed since the
    // store's ctor load).
    const QString dir = QFileInfo(filePath).absolutePath();
    m_watcher->registerDirectory(dir, PhosphorFsLoader::LiveReload::On);
}

} // namespace PhosphorWindowRules

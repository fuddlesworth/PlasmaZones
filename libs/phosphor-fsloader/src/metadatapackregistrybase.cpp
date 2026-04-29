// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorFsLoader/MetadataPackRegistryBase.h>

#include <PhosphorFsLoader/IScanStrategy.h>
#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QtCore/QLoggingCategory>

namespace PhosphorFsLoader {

MetadataPackRegistryBase::MetadataPackRegistryBase(const QLoggingCategory& logCat, QObject* parent)
    : QObject(parent)
    , m_logCat(&logCat)
{
}

MetadataPackRegistryBase::~MetadataPackRegistryBase() = default;

void MetadataPackRegistryBase::initWatcher(IScanStrategy& strategy)
{
    Q_ASSERT_X(!m_watcher, "MetadataPackRegistryBase::initWatcher", "initWatcher called more than once");
    m_watcher = std::make_unique<WatchedDirectorySet>(strategy, this);
}

void MetadataPackRegistryBase::addSearchPath(const QString& path, LiveReload liveReload)
{
    // Single-path: priority direction is irrelevant — forward with the
    // canonical default. The `addSearchPaths` overload's `order`
    // parameter only matters for multi-path batches.
    addSearchPaths(QStringList{path}, liveReload, RegistrationOrder::LowestPriorityFirst);
}

void MetadataPackRegistryBase::addSearchPaths(const QStringList& paths, LiveReload liveReload, RegistrationOrder order)
{
    Q_ASSERT_X(m_watcher, "MetadataPackRegistryBase::addSearchPaths",
               "subclass forgot to call initWatcher() in its ctor body");

    // Pre-canonicalise + drop already-registered paths via the shared
    // helper — keeps the log line below from spamming "Added search path:
    // /foo/bar/" when /foo/bar is already registered (the base watcher's
    // `registerDirectories` is silent on dedup, so the filter has to run
    // upstream).
    const QStringList toRegister = WatchedDirectorySet::filterNewSearchPaths(paths, m_watcher->directories());
    if (toRegister.isEmpty()) {
        return;
    }
    // Single batched register — the watcher runs ONE synchronous scan
    // for the whole batch and fires the consumer's content-changed
    // signal exactly once if the strategy reports a signature change.
    // Avoids the N-rescans-on-startup amplification a loop of
    // single-path registrations would cause.
    m_watcher->registerDirectories(toRegister, liveReload, order);
    for (const QString& p : std::as_const(toRegister)) {
        qCInfo(*m_logCat) << "Added search path:" << p;
    }
}

QStringList MetadataPackRegistryBase::searchPaths() const
{
    Q_ASSERT_X(m_watcher, "MetadataPackRegistryBase::searchPaths",
               "subclass forgot to call initWatcher() in its ctor body");
    return m_watcher->directories();
}

void MetadataPackRegistryBase::setUserPath(const QString& path)
{
    Q_ASSERT_X(m_watcher, "MetadataPackRegistryBase::setUserPath",
               "subclass forgot to call initWatcher() in its ctor body");
    if (m_userPath == path) {
        return; // idempotent — same value, no work to do
    }
    m_userPath = path;
    onUserPathChanged(path);
    // If search paths have already been registered, the prior scan baked
    // in the OLD user-path classification — synchronous rescan refreshes
    // every pack's `isUser` flag against the new value. Without this,
    // callers who set the user path AFTER `addSearchPaths` would silently
    // get every pack flagged as system until an explicit `refresh()` ran.
    if (!m_watcher->directories().isEmpty()) {
        m_watcher->rescanNow();
    }
}

void MetadataPackRegistryBase::refresh()
{
    Q_ASSERT_X(m_watcher, "MetadataPackRegistryBase::refresh",
               "subclass forgot to call initWatcher() in its ctor body");
    qCDebug(*m_logCat) << "Refreshing registry";
    m_watcher->rescanNow();
}

} // namespace PhosphorFsLoader

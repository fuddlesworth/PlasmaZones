// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorFsLoader/MetadataPackRegistryBase.h>

#include <PhosphorFsLoader/IScanStrategy.h>
#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QtCore/QLoggingCategory>

namespace PhosphorFsLoader {

namespace {
/// Hard-fail (in both debug and release) if the subclass handed us a null
/// strategy. `Q_ASSERT_X` alone is inert in release, and the next line
/// (`m_watcher(... *m_strategy ...)`) would dereference null with UB. A
/// `qFatal` in a static gate makes both build modes crash with a clear
/// diagnostic the moment the contract is violated.
std::unique_ptr<IScanStrategy> requireNonNullStrategy(std::unique_ptr<IScanStrategy> strategy)
{
    if (!strategy) {
        qFatal(
            "MetadataPackRegistryBase: scan strategy must not be null — "
            "buildScanStrategy returned a default-constructed unique_ptr");
    }
    return strategy;
}
} // namespace

MetadataPackRegistryBase::MetadataPackRegistryBase(const QLoggingCategory& logCat,
                                                   std::unique_ptr<IScanStrategy> strategy, QObject* parent)
    : QObject(parent)
    , m_strategy(requireNonNullStrategy(std::move(strategy)))
    , m_watcher(std::make_unique<WatchedDirectorySet>(*m_strategy, this))
    , m_logCat(&logCat)
{
}

MetadataPackRegistryBase::~MetadataPackRegistryBase() = default;

void MetadataPackRegistryBase::addSearchPath(const QString& path, LiveReload liveReload)
{
    // Single-path: priority direction is irrelevant — forward with the
    // canonical default. The `addSearchPaths` overload's `order`
    // parameter only matters for multi-path batches.
    addSearchPaths(QStringList{path}, liveReload, RegistrationOrder::LowestPriorityFirst);
}

void MetadataPackRegistryBase::addSearchPaths(const QStringList& paths, LiveReload liveReload, RegistrationOrder order)
{
    // Pre-canonicalise + drop already-registered paths via the shared
    // helper — keeps the log line below from spamming "Added search path:
    // /foo/bar/" when /foo/bar is already registered (the base watcher's
    // `registerDirectories` is silent on dedup, so the filter has to run
    // upstream).
    const QStringList toRegister = WatchedDirectorySet::filterNewSearchPaths(paths, m_watcher->directories());
    if (toRegister.isEmpty()) {
        return;
    }
    // Log BEFORE the register call so the "Added search path: X" line
    // appears in the journal ahead of any rescan-completion / OnCommit
    // log lines emitted by the synchronous scan that follows. Logging
    // after the register inverts the apparent causality in debug logs.
    for (const QString& p : std::as_const(toRegister)) {
        qCInfo(*m_logCat) << "Added search path:" << p;
    }
    // Single batched register — the watcher runs ONE synchronous scan
    // for the whole batch and fires the consumer's content-changed
    // signal exactly once if the strategy reports a signature change.
    // Avoids the N-rescans-on-startup amplification a loop of
    // single-path registrations would cause.
    m_watcher->registerDirectories(toRegister, liveReload, order);
}

QStringList MetadataPackRegistryBase::searchPaths() const
{
    return m_watcher->directories();
}

void MetadataPackRegistryBase::setUserPath(const QString& path)
{
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
    qCDebug(*m_logCat) << "Refreshing registry";
    m_watcher->rescanNow();
}

} // namespace PhosphorFsLoader

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayoutApi/LayoutSourceProviderRegistry.h>

#include <QMutex>
#include <QMutexLocker>

#include <utility>

namespace PhosphorLayout {

namespace {
/// Guards @c pendingLayoutSourceProviders() against concurrent
/// @c LayoutSourceProviderRegistrar ctor calls and concurrent
/// @c buildFromRegistered snapshot loops.
///
/// In tree the list is append-only at static-init (stable once main()
/// runs) and snapshot-only at bundle build time, so contention is
/// theoretical. The mutex matters in two cases the @c @@todo(plugin-
/// compositor) marker on @c pendingLayoutSourceProviders flags as
/// future work: (1) a Qt plugin loader spawning a worker thread that
/// triggers @c dlopen of a provider library, and (2) two composition
/// roots (e.g. daemon + KCM in the same process) running their bundle
/// builds concurrently. Function-local static so first use pins the
/// initialisation order to whichever caller arrives first.
QMutex& providersMutex()
{
    static QMutex m;
    return m;
}
} // namespace

QList<PendingLayoutSourceProvider>& pendingLayoutSourceProviders()
{
    // Function-local static — Meyer's singleton. C++11 guarantees
    // thread-safe initialization of static locals (§6.7 [stmt.dcl]
    // p4), and any LayoutSourceProviderRegistrar that runs at static
    // init triggers initialization on its first call.
    //
    // Callers MUST hold @c providersMutex() across any access (read or
    // write). The function returns a reference rather than a snapshot
    // so existing callers don't allocate; the mutex contract is
    // documented in the header and enforced by the two in-tree call
    // sites (this file's registrar ctor and
    // LayoutSourceBundle::buildFromRegistered).
    static QList<PendingLayoutSourceProvider> s_pending;
    return s_pending;
}

QMutex& pendingLayoutSourceProvidersMutex()
{
    return providersMutex();
}

LayoutSourceProviderRegistrar::LayoutSourceProviderRegistrar(
    QString name, int priority, std::function<std::unique_ptr<ILayoutSourceFactory>(const FactoryContext&)> builder)
{
    QMutexLocker locker(&providersMutex());
    pendingLayoutSourceProviders().append({std::move(name), priority, std::move(builder)});
}

} // namespace PhosphorLayout

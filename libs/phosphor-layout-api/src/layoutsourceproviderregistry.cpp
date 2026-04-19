// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayoutApi/LayoutSourceProviderRegistry.h>

#include <utility>

namespace PhosphorLayout {

QList<PendingLayoutSourceProvider>& pendingLayoutSourceProviders()
{
    // Function-local static — Meyer's singleton. C++11 guarantees
    // thread-safe initialization of static locals (§6.7 [stmt.dcl]
    // p4), and any LayoutSourceProviderRegistrar that runs at static
    // init triggers initialization on its first call.
    static QList<PendingLayoutSourceProvider> s_pending;
    return s_pending;
}

LayoutSourceProviderRegistrar::LayoutSourceProviderRegistrar(
    QString name, int priority, std::function<std::unique_ptr<ILayoutSourceFactory>(const FactoryContext&)> builder)
{
    pendingLayoutSourceProviders().append({std::move(name), priority, std::move(builder)});
}

} // namespace PhosphorLayout

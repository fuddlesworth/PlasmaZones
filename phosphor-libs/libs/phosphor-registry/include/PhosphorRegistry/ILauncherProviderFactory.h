// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/IFactoryBase.h>
#include <PhosphorRegistry/ILauncherProvider.h>
#include <PhosphorRegistry/phosphorregistry_export.h>

#include <QtCore/qtclasshelpermacros.h>

QT_BEGIN_NAMESPACE
class QObject;
QT_END_NAMESPACE

namespace PhosphorRegistry {

// Factory for one query-answering provider inside the spotlight-style
// launcher (apps from .desktop files, calculator, window switcher,
// emoji, clipboard, command runner).
//
// Unlike the visual factories (IBarWidgetFactory, IControlCenterTileFactory),
// a launcher provider is a pure data source: it accepts a query
// string and returns matching results. The launcher's UI is owned
// by the launcher surface itself; providers contribute rows.
//
// Phase 1.3 shipped this returning a bare QObject* because the provider
// contract was not yet locked. Phase 4.2 locked it as ILauncherProvider
// (query in, rows out; see ILauncherProvider.h), and createProvider now
// returns that type directly, so the surface needs no qobject_cast and a
// factory cannot hand back something that is not a provider.
class PHOSPHORREGISTRY_EXPORT ILauncherProviderFactory : public IFactoryBase
{
public:
    ILauncherProviderFactory() = default;
    ~ILauncherProviderFactory() override = default;
    Q_DISABLE_COPY_MOVE(ILauncherProviderFactory)

    // Construct a provider rooted at parent. Same lifetime contract as
    // the visual factories: parent owns, factory does not retain.
    // Returning nullptr means "unavailable in this environment" (no
    // clipboard service, no foreign-toplevel support) and is not an
    // error; the surface simply has one provider fewer.
    // ABI NOTE. This signature returned QObject* before the launcher
    // surface existed, and narrowing it to ILauncherProvider* changed the
    // vtable slot's type without a PluginAbiVersion bump. That is safe only
    // because no plugin implements this interface yet: it has never shipped
    // in a release, and the one in-tree implementation is rebuilt with the
    // library. The first external provider makes the version binding, and
    // any change to this signature after that needs a bump.
    [[nodiscard]] virtual ILauncherProvider* createProvider(QObject* parent) = 0;
};

} // namespace PhosphorRegistry

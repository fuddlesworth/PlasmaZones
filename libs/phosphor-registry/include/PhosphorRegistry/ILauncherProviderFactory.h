// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/IFactoryBase.h>
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
// Phase 1.3 ships this as a documented header; the consuming
// Launcher surface lands in Phase 4.2. The createProvider() return
// type is intentionally QObject* (rather than a concrete
// ILauncherProvider interface) because the provider contract is not
// yet locked. When the launcher lands in Phase 4.2 this header will
// gain a concrete ILauncherProvider type and createProvider's
// return type will sharpen.
class PHOSPHORREGISTRY_EXPORT ILauncherProviderFactory : public IFactoryBase
{
public:
    ILauncherProviderFactory() = default;
    ~ILauncherProviderFactory() override = default;
    Q_DISABLE_COPY_MOVE(ILauncherProviderFactory)

    // Construct a provider QObject rooted at parent. The launcher
    // surface will Q_INVOKABLE / qobject_cast to the concrete
    // provider interface once that interface is defined (Phase 4.2).
    [[nodiscard]] virtual QObject* createProvider(QObject* parent) = 0;
};

} // namespace PhosphorRegistry

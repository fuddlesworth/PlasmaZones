// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/IFactoryBase.h>
#include <PhosphorRegistry/phosphorregistry_export.h>

QT_BEGIN_NAMESPACE
class QObject;
class QQmlEngine;
class QQuickItem;
QT_END_NAMESPACE

namespace PhosphorRegistry {

// Factory for a single top-bar widget (clock, workspaces, focused-app,
// tray, media, battery, etc.). Each consumed by the bar's host QML
// via Registry<IBarWidgetFactory>::factory(id)->createWidget(...).
//
// createWidget() returns a freshly-constructed QQuickItem the bar
// host parents into its layout. The factory does not retain
// ownership — the returned item lives under the parent argument and
// is destroyed when that parent is destroyed (typical QQuickItem
// parent-child lifetime).
class PHOSPHORREGISTRY_EXPORT IBarWidgetFactory : public IFactoryBase
{
public:
    // Construct a new QQuickItem for this factory.
    //
    // engine is the QML engine the widget should be instantiated
    // against (typically the shell's engine; passing a different
    // engine is a programming error). parent becomes the item's
    // QObject parent so the bar host's destruction cascades through.
    //
    // Returns nullptr if the factory cannot construct a widget right
    // now (e.g., a required external service is unavailable). The
    // bar host treats nullptr as "skip this slot, leave a gap or
    // suppress the layout entry" — never as an error to log/abort.
    [[nodiscard]] virtual QQuickItem* createWidget(QQmlEngine* engine, QObject* parent) = 0;
};

} // namespace PhosphorRegistry

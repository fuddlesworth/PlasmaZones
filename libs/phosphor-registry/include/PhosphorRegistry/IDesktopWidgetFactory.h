// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/IFactoryBase.h>
#include <PhosphorRegistry/phosphorregistry_export.h>

#include <QtCore/qtclasshelpermacros.h>

QT_BEGIN_NAMESPACE
class QObject;
class QQmlEngine;
class QQuickItem;
QT_END_NAMESPACE

namespace PhosphorRegistry {

// Factory for one desktop card / widget (calendar, weather, system
// monitor, media, shortcuts) — the kinds of things that sit on the
// dashboard or as standalone desktop tiles.
//
// Phase 1.3 ships this as a documented header; the consuming
// Dashboard / desktop-widget surface lands in Phase 4.4 (cards
// reused inside Control Center) and Phase 5 (standalone dashboard).
class PHOSPHORREGISTRY_EXPORT IDesktopWidgetFactory : public IFactoryBase
{
public:
    IDesktopWidgetFactory() = default;
    ~IDesktopWidgetFactory() override = default;
    Q_DISABLE_COPY_MOVE(IDesktopWidgetFactory)

    // Construct a widget QQuickItem rooted at parent. engine MUST
    // NOT be null. Same lifetime contract as
    // IBarWidgetFactory::createWidget. Returning nullptr means the
    // widget is unavailable in the current environment.
    [[nodiscard]] virtual QQuickItem* createWidget(QQmlEngine* engine, QObject* parent) = 0;
};

} // namespace PhosphorRegistry

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ShellEffects.h"

#include <QLoggingCategory>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegion>

#ifdef PHOSPHOR_SHELL_HAVE_KWINDOWSYSTEM
#include <KWindowEffects>
#endif

namespace {
Q_LOGGING_CATEGORY(lcEffects, "phosphorshell.effects")
}

namespace PhosphorShellApp {

ShellEffects::ShellEffects(QObject* parent)
    : QObject(parent)
{
}

bool ShellEffects::blurAvailable()
{
#ifdef PHOSPHOR_SHELL_HAVE_KWINDOWSYSTEM
    return true;
#else
    return false;
#endif
}

bool ShellEffects::setBlurBehind(QQuickItem* item, const QRect& region, const QRect& secondary)
{
    QQuickWindow* window = item ? item->window() : nullptr;
    if (!window) {
        return false;
    }
#ifdef PHOSPHOR_SHELL_HAVE_KWINDOWSYSTEM
    const bool enable = region.width() > 0 && region.height() > 0;
    KWindowEffects::enableBlurBehind(window, enable, enable ? QRegion(region).united(QRegion(secondary)) : QRegion());
    qCDebug(lcEffects) << (enable ? "blur behind" : "blur off for") << window->title() << region;
    return true;
#else
    Q_UNUSED(region)
    Q_UNUSED(secondary)
    qCDebug(lcEffects) << "no blur backend in this build; band stays a plain tint";
    return false;
#endif
}

bool ShellEffects::setOverviewRegions(QQuickItem* item, const QRect& bar, const QRect& preview)
{
    QQuickWindow* window = item ? item->window() : nullptr;
    if (!window || window->width() <= 0 || window->height() <= 0) {
        return false;
    }
    const QRegion bounds(QRect(0, 0, window->width(), window->height()));
    window->setMask(bounds.subtracted(QRegion(bar)));
#ifdef PHOSPHOR_SHELL_HAVE_KWINDOWSYSTEM
    KWindowEffects::enableBlurBehind(window, true, bounds.subtracted(QRegion(bar)).subtracted(QRegion(preview)));
#else
    Q_UNUSED(preview)
#endif
    window->update();
    return true;
}

} // namespace PhosphorShellApp

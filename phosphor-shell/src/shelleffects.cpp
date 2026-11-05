// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ShellEffects.h"

#include <QLoggingCategory>
#include <QQuickItem>
#include <QQuickWindow>

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

bool ShellEffects::setBlurBehind(QQuickItem* item, const QRect& region)
{
    QQuickWindow* window = item ? item->window() : nullptr;
    if (!window) {
        return false;
    }
#ifdef PHOSPHOR_SHELL_HAVE_KWINDOWSYSTEM
    const bool enable = region.width() > 0 && region.height() > 0;
    KWindowEffects::enableBlurBehind(window, enable, enable ? QRegion(region) : QRegion());
    qCDebug(lcEffects) << (enable ? "blur behind" : "blur off for") << window->title() << region;
    return true;
#else
    Q_UNUSED(region)
    qCDebug(lcEffects) << "no blur backend in this build; band stays a plain tint";
    return false;
#endif
}

} // namespace PhosphorShellApp

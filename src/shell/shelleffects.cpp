// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ShellEffects.h"

#include <QLoggingCategory>
#include <QPainterPath>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegion>

#ifdef PHOSPHOR_SHELL_HAVE_KWINDOWSYSTEM
#include <KWindowEffects>
#endif

namespace {
Q_LOGGING_CATEGORY(lcEffects, "phosphorshell.effects")
QRegion roundedRegion(const QRect& rect, qreal radius)
{
    if (rect.isEmpty())
        return {};
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    return QRegion(path.toFillPolygon().toPolygon());
}
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

bool ShellEffects::setBlurBehind(QQuickItem* item, const QRect& region, const QRect& secondary, qreal radius)
{
    QQuickWindow* window = item ? item->window() : nullptr;
    if (!window) {
        return false;
    }
#ifdef PHOSPHOR_SHELL_HAVE_KWINDOWSYSTEM
    const bool enable = region.width() > 0 && region.height() > 0;
    KWindowEffects::enableBlurBehind(
        window, enable, enable ? roundedRegion(region, radius).united(roundedRegion(secondary, radius)) : QRegion());
    qCDebug(lcEffects) << (enable ? "blur behind" : "blur off for") << window->title() << region;
    return true;
#else
    Q_UNUSED(region)
    Q_UNUSED(secondary)
    Q_UNUSED(radius)
    qCDebug(lcEffects) << "no blur backend in this build; band stays a plain tint";
    return false;
#endif
}

bool ShellEffects::setOverviewRegions(QQuickItem* item, const QRect& bar, const QRect& preview,
                                      const QVariantList& windows, qreal radius)
{
    QQuickWindow* window = item ? item->window() : nullptr;
    if (!window || window->width() <= 0 || window->height() <= 0) {
        return false;
    }
    const QRegion bounds(QRect(0, 0, window->width(), window->height()));
    const auto barRegion = roundedRegion(bar, radius);
    window->setMask(bounds.subtracted(barRegion));
#ifdef PHOSPHOR_SHELL_HAVE_KWINDOWSYSTEM
    QRegion apertures;
    for (const auto& rect : windows)
        apertures += roundedRegion(rect.toRectF().toAlignedRect(), radius);
    apertures &= QRegion(preview);
    KWindowEffects::enableBlurBehind(window, true, bounds.subtracted(barRegion).subtracted(apertures));
#else
    Q_UNUSED(preview)
    Q_UNUSED(windows)
#endif
    window->update();
    return true;
}

} // namespace PhosphorShellApp

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "shelloverview.h"
#include <core/output.h>
#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <QDBusConnection>
#include <QDBusMessage>
#include <cmath>
namespace PlasmaZones {
namespace {
const QString ObjectPath = QStringLiteral("/PlasmaZones/ShellOverview");
}
ShellOverview::ShellOverview(KWin::Effect* effect)
    : QObject(effect)
    , m_effect(effect)
    , m_ownerWatcher(this)
{
    auto bus = QDBusConnection::sessionBus();
    bus.registerObject(ObjectPath, this, QDBusConnection::ExportAllSlots);
    m_ownerWatcher.setConnection(bus);
    m_ownerWatcher.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(&m_ownerWatcher, &QDBusServiceWatcher::serviceUnregistered, this, &ShellOverview::restore);
    connect(KWin::effects, &KWin::EffectsHandler::screenRemoved, this, [this](KWin::LogicalOutput* output) {
        if (output == m_output)
            restore();
    });
    m_animation.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_rect = value.toRectF();
        KWin::effects->addRepaintFull();
    });
    connect(&m_animation, &QVariantAnimation::finished, this, [this] {
        if (m_closing)
            restore();
    });
}
ShellOverview::~ShellOverview()
{
    restore();
    QDBusConnection::sessionBus().unregisterObject(ObjectPath);
}
bool ShellOverview::active() const
{
    return !m_output.isNull();
}
bool ShellOverview::onOutput(KWin::LogicalOutput* output) const
{
    return active() && output == m_output;
}
bool ShellOverview::appliesTo(KWin::EffectWindow* window) const
{
    return window && onOutput(window->screen())
        && (window->isNormalWindow() || window->isDialog() || window->isDesktop())
        && !window->windowClass().contains(QLatin1String("phosphor-shell"))
        && !window->windowClass().contains(QLatin1String("plasmazones"));
}
bool ShellOverview::begin(const QString& screen, double x, double y, double width, double height, bool animate)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) || x < 0 || y < 0
        || width < 0.2 || height < 0.2 || x + width > 1 || y + height > 1)
        return false;
    auto* output = KWin::effects->findScreen(screen);
    auto* otherEffect = KWin::effects->activeFullScreenEffect();
    if (!output || (otherEffect && otherEffect != m_effect))
        return false;
    const QString owner = calledFromDBus() ? message().service() : QString();
    if (active() && owner != m_owner)
        return false;
    const QRectF geometry = QRectF(QRect(output->geometry()));
    const QRectF target(geometry.x() + x * geometry.width(), geometry.y() + y * geometry.height(),
                        width * geometry.width(), height * geometry.height());
    if (m_output != output) {
        restore();
        m_rect = geometry;
    }
    m_output = output;
    m_owner = owner;
    m_ownerWatcher.setWatchedServices(owner.isEmpty() ? QStringList() : QStringList{owner});
    m_closing = false;
    KWin::effects->setActiveFullScreenEffect(m_effect);
    m_animation.stop();
    m_animation.setStartValue(m_rect);
    m_animation.setEndValue(target);
    m_animation.setDuration(animate ? 260 : 0);
    m_animation.start();
    KWin::effects->addRepaintFull();
    return true;
}
void ShellOverview::end()
{
    if (!active() || (calledFromDBus() && message().service() != m_owner))
        return;
    m_closing = true;
    m_animation.stop();
    m_animation.setStartValue(m_rect);
    m_animation.setEndValue(QRectF(QRect(m_output->geometry())));
    m_animation.start();
}
void ShellOverview::restore()
{
    m_animation.stop();
    m_output.clear();
    m_owner.clear();
    m_ownerWatcher.setWatchedServices({});
    m_closing = false;
    if (KWin::effects->activeFullScreenEffect() == m_effect)
        KWin::effects->setActiveFullScreenEffect(nullptr);
    KWin::effects->addRepaintFull();
}
void ShellOverview::transform(KWin::EffectWindow* window, KWin::WindowPaintData& data) const
{
    if (!appliesTo(window))
        return;
    const auto screen = m_output->geometry();
    const auto frame = window->frameGeometry();
    const qreal sx = m_rect.width() / screen.width();
    const qreal sy = m_rect.height() / screen.height();
    data.setXTranslation(m_rect.x() + (frame.x() - screen.x() + data.xTranslation()) * sx - frame.x());
    data.setYTranslation(m_rect.y() + (frame.y() - screen.y() + data.yTranslation()) * sy - frame.y());
    data.setXScale(data.xScale() * sx);
    data.setYScale(data.yScale() * sy);
}
}

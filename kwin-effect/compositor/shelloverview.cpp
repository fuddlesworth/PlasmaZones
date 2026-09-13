// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "shelloverview.h"
#include "../plasmazoneseffect/plasmazoneseffect.h"
#include <core/output.h>
#include <window.h>
#include <KDecoration3/Decoration>
#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
namespace PlasmaZones {
namespace {
const QString ObjectPath = QStringLiteral("/PlasmaZones/ShellOverview");
bool navigable(KWin::EffectWindow* window)
{
    return window && !window->isDeleted() && !window->isSkipSwitcher()
        && (window->isNormalWindow() || window->isDialog())
        && !window->windowClass().contains(QLatin1String("phosphor-shell"))
        && !window->windowClass().contains(QLatin1String("plasmazonesd"));
}
}
ShellOverview::ShellOverview(KWin::Effect* effect)
    : QObject(effect)
    , m_effect(effect)
    , m_ownerWatcher(this)
{
    auto bus = QDBusConnection::sessionBus();
    bus.registerObject(ObjectPath, this, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
    m_windowChanges.setSingleShot(true);
    m_windowChanges.setInterval(16);
    connect(&m_windowChanges, &QTimer::timeout, this, [this] {
        updateDecorations();
        Q_EMIT windowsChanged();
    });
    connect(KWin::effects, &KWin::EffectsHandler::windowAdded, this, &ShellOverview::watchWindow);
    connect(KWin::effects, &KWin::EffectsHandler::windowClosed, this, [this](KWin::EffectWindow* window) {
        m_windowColors.remove(window);
        if (window->isNormalWindow() || window->isDialog())
            m_windowChanges.start();
    });
    connect(KWin::effects, &KWin::EffectsHandler::windowActivated, this, [this](KWin::EffectWindow* window) {
        if (navigable(window)) {
            m_lastFocusedWindow = static_cast<PlasmaZonesEffect*>(m_effect)->getWindowId(window);
            m_windowChanges.start();
        }
    });
    for (auto* window : KWin::effects->stackingOrder())
        watchWindow(window);
    if (auto* window = KWin::effects->activeWindow(); navigable(window))
        m_lastFocusedWindow = static_cast<PlasmaZonesEffect*>(m_effect)->getWindowId(window);
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
void ShellOverview::watchWindow(KWin::EffectWindow* window)
{
    if (!navigable(window))
        return;
    if (!m_windowColors.contains(window))
        m_windowColors.insert(window, m_nextColor++ % 4);
    const auto changed = [this] {
        m_windowChanges.start();
    };
    connect(window->window(), &KWin::Window::decorationChanged, this, changed);
    connect(window, &KWin::EffectWindow::windowFrameGeometryChanged, this, changed);
    connect(window, &KWin::EffectWindow::windowDesktopsChanged, this, changed);
    connect(window, &KWin::EffectWindow::minimizedChanged, this, changed);
    connect(window, &KWin::EffectWindow::windowHiddenChanged, this, changed);
    m_windowChanges.start();
}
void ShellOverview::updateDecorations()
{
    auto* effect = static_cast<PlasmaZonesEffect*>(m_effect);
    for (auto* window : KWin::effects->stackingOrder()) {
        if (!navigable(window))
            continue;
        if (auto* decoration = window->window()->decoration()) {
            decoration->setProperty("phosphorColorIndex", m_windowColors.value(window, 0));
            decoration->setProperty("phosphorFocused", effect->getWindowId(window) == m_lastFocusedWindow);
        }
    }
}
QString ShellOverview::windows(const QString& screen, int desktop) const
{
    auto* output = KWin::effects->findScreen(screen);
    const auto desktops = KWin::effects->desktops();
    if (!output || desktop < 1 || desktop > desktops.size())
        return QStringLiteral("[]");
    auto* effect = static_cast<PlasmaZonesEffect*>(m_effect);
    const auto screenId = effect->outputScreenId(output);
    QJsonArray result;
    for (auto* window : KWin::effects->stackingOrder()) {
        if (!navigable(window) || effect->getWindowScreenId(window) != screenId
            || !window->isOnDesktop(desktops[desktop - 1]) || !window->isOnCurrentActivity())
            continue;
        const auto rect = window->frameGeometry();
        const auto id = effect->getWindowId(window);
        result.append(QJsonObject{{QStringLiteral("windowId"), id},
                                  {QStringLiteral("appId"), effect->getWindowAppId(window)},
                                  {QStringLiteral("title"), window->caption()},
                                  {QStringLiteral("focused"), id == m_lastFocusedWindow},
                                  {QStringLiteral("colorIndex"), m_windowColors.value(window, 0)},
                                  {QStringLiteral("minimized"), window->isMinimized()},
                                  {QStringLiteral("x"), rect.x()},
                                  {QStringLiteral("y"), rect.y()},
                                  {QStringLiteral("width"), rect.width()},
                                  {QStringLiteral("height"), rect.height()}});
    }
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
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
bool ShellOverview::begin(const QString& screen, double x, double y, double width, double height, bool animate,
                          const QString& token)
{
    if (token.isEmpty() || token.size() > 128)
        return false;
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
    m_token = token;
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
void ShellOverview::end(const QString& token)
{
    if (!active() || token != m_token || (calledFromDBus() && message().service() != m_owner))
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
    m_token.clear();
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

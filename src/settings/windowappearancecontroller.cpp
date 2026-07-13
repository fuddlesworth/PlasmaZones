// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowappearancecontroller.h"

#include "../core/isettings.h"

#include <PhosphorEngine/PerScreenKeys.h>

namespace PlasmaZones {

namespace {
namespace PSK = PhosphorEngine::PerScreenSnappingKey;

// The global-scope value of a gap key, read from the matching ISettings getter.
QVariant globalGapValue(const ISettings* s, const QString& key)
{
    if (key == PSK::InnerGap)
        return s->innerGap();
    if (key == PSK::OuterGap)
        return s->outerGap();
    if (key == PSK::UsePerSideOuterGap)
        return s->usePerSideOuterGap();
    if (key == PSK::OuterGapTop)
        return s->outerGapTop();
    if (key == PSK::OuterGapBottom)
        return s->outerGapBottom();
    if (key == PSK::OuterGapLeft)
        return s->outerGapLeft();
    if (key == PSK::OuterGapRight)
        return s->outerGapRight();
    return {};
}

// Write a gap key's GLOBAL config value through the matching ISettings setter.
void writeGlobalGap(ISettings* s, const QString& key, const QVariant& value)
{
    if (key == PSK::InnerGap)
        s->setInnerGap(value.toInt());
    else if (key == PSK::OuterGap)
        s->setOuterGap(value.toInt());
    else if (key == PSK::UsePerSideOuterGap)
        s->setUsePerSideOuterGap(value.toBool());
    else if (key == PSK::OuterGapTop)
        s->setOuterGapTop(value.toInt());
    else if (key == PSK::OuterGapBottom)
        s->setOuterGapBottom(value.toInt());
    else if (key == PSK::OuterGapLeft)
        s->setOuterGapLeft(value.toInt());
    else if (key == PSK::OuterGapRight)
        s->setOuterGapRight(value.toInt());
}
} // namespace

WindowAppearanceController::WindowAppearanceController(ISettings& settings, QObject* parent)
    : PhosphorControl::PageController(QStringLiteral("window-appearance"), parent)
    , m_settings(&settings)
{
    // The underlying values ARE Q_PROPERTY on Settings and already emit only on
    // an actual value change, so a straight forward of each ISettings::*Changed
    // to the matching controller NOTIFY is enough — no local guard needed.
    connect(m_settings, &ISettings::showWindowBorderChanged, this,
            &WindowAppearanceController::showWindowBorderChanged);
    connect(m_settings, &ISettings::windowBorderScopeChanged, this,
            &WindowAppearanceController::windowBorderScopeChanged);
    connect(m_settings, &ISettings::windowBorderWidthChanged, this,
            &WindowAppearanceController::windowBorderWidthChanged);
    connect(m_settings, &ISettings::windowBorderRadiusChanged, this,
            &WindowAppearanceController::windowBorderRadiusChanged);
    connect(m_settings, &ISettings::windowBorderColorActiveChanged, this,
            &WindowAppearanceController::windowBorderColorActiveChanged);
    connect(m_settings, &ISettings::windowBorderColorInactiveChanged, this,
            &WindowAppearanceController::windowBorderColorInactiveChanged);
    connect(m_settings, &ISettings::hideWindowTitleBarsChanged, this,
            &WindowAppearanceController::hideWindowTitleBarsChanged);
    connect(m_settings, &ISettings::windowTitleBarScopeChanged, this,
            &WindowAppearanceController::windowTitleBarScopeChanged);
    connect(m_settings, &ISettings::focusFadeDurationChanged, this,
            &WindowAppearanceController::focusFadeDurationChanged);
    connect(m_settings, &ISettings::showWindowOpacityTintChanged, this,
            &WindowAppearanceController::showWindowOpacityTintChanged);
    connect(m_settings, &ISettings::windowOpacityTintScopeChanged, this,
            &WindowAppearanceController::windowOpacityTintScopeChanged);
    connect(m_settings, &ISettings::windowOpacityChanged, this, &WindowAppearanceController::windowOpacityChanged);
    connect(m_settings, &ISettings::windowTintStrengthChanged, this,
            &WindowAppearanceController::windowTintStrengthChanged);
    connect(m_settings, &ISettings::windowTintColorChanged, this, &WindowAppearanceController::windowTintColorChanged);
    connect(m_settings, &ISettings::innerGapChanged, this, &WindowAppearanceController::innerGapChanged);
    connect(m_settings, &ISettings::outerGapChanged, this, &WindowAppearanceController::outerGapChanged);
    connect(m_settings, &ISettings::usePerSideOuterGapChanged, this,
            &WindowAppearanceController::usePerSideOuterGapChanged);
    connect(m_settings, &ISettings::outerGapTopChanged, this, &WindowAppearanceController::outerGapTopChanged);
    connect(m_settings, &ISettings::outerGapBottomChanged, this, &WindowAppearanceController::outerGapBottomChanged);
    connect(m_settings, &ISettings::outerGapLeftChanged, this, &WindowAppearanceController::outerGapLeftChanged);
    connect(m_settings, &ISettings::outerGapRightChanged, this, &WindowAppearanceController::outerGapRightChanged);
}

// ── Window border / title bar ────────────────────────────────────────────────

bool WindowAppearanceController::showWindowBorder() const
{
    return m_settings->showWindowBorder();
}
QString WindowAppearanceController::windowBorderScope() const
{
    return m_settings->windowBorderScope();
}
int WindowAppearanceController::windowBorderWidth() const
{
    return m_settings->windowBorderWidth();
}
int WindowAppearanceController::windowBorderRadius() const
{
    return m_settings->windowBorderRadius();
}
QString WindowAppearanceController::windowBorderColorActive() const
{
    return m_settings->windowBorderColorActive();
}
QString WindowAppearanceController::windowBorderColorInactive() const
{
    return m_settings->windowBorderColorInactive();
}
bool WindowAppearanceController::hideWindowTitleBars() const
{
    return m_settings->hideWindowTitleBars();
}
QString WindowAppearanceController::windowTitleBarScope() const
{
    return m_settings->windowTitleBarScope();
}
int WindowAppearanceController::focusFadeDuration() const
{
    return m_settings->focusFadeDuration();
}

void WindowAppearanceController::setShowWindowBorder(bool show)
{
    m_settings->setShowWindowBorder(show);
}
void WindowAppearanceController::setWindowBorderScope(const QString& scope)
{
    m_settings->setWindowBorderScope(scope);
}
void WindowAppearanceController::setWindowBorderWidth(int width)
{
    m_settings->setWindowBorderWidth(width);
}
void WindowAppearanceController::setWindowBorderRadius(int radius)
{
    m_settings->setWindowBorderRadius(radius);
}
void WindowAppearanceController::setWindowBorderColorActive(const QString& color)
{
    m_settings->setWindowBorderColorActive(color);
}
void WindowAppearanceController::setWindowBorderColorInactive(const QString& color)
{
    m_settings->setWindowBorderColorInactive(color);
}
void WindowAppearanceController::setHideWindowTitleBars(bool hide)
{
    m_settings->setHideWindowTitleBars(hide);
}
void WindowAppearanceController::setWindowTitleBarScope(const QString& scope)
{
    m_settings->setWindowTitleBarScope(scope);
}
void WindowAppearanceController::setFocusFadeDuration(int ms)
{
    m_settings->setFocusFadeDuration(ms);
}

// ── Plain opacity+tint layer ─────────────────────────────────────────────────

bool WindowAppearanceController::showWindowOpacityTint() const
{
    return m_settings->showWindowOpacityTint();
}
QString WindowAppearanceController::windowOpacityTintScope() const
{
    return m_settings->windowOpacityTintScope();
}
double WindowAppearanceController::windowOpacity() const
{
    return m_settings->windowOpacity();
}
double WindowAppearanceController::windowTintStrength() const
{
    return m_settings->windowTintStrength();
}
QString WindowAppearanceController::windowTintColor() const
{
    return m_settings->windowTintColor();
}
void WindowAppearanceController::setShowWindowOpacityTint(bool show)
{
    m_settings->setShowWindowOpacityTint(show);
}
void WindowAppearanceController::setWindowOpacityTintScope(const QString& scope)
{
    m_settings->setWindowOpacityTintScope(scope);
}
void WindowAppearanceController::setWindowOpacity(double opacity)
{
    m_settings->setWindowOpacity(opacity);
}
void WindowAppearanceController::setWindowTintStrength(double strength)
{
    m_settings->setWindowTintStrength(strength);
}
void WindowAppearanceController::setWindowTintColor(const QString& color)
{
    m_settings->setWindowTintColor(color);
}

// ── Shared inner/outer gaps ──────────────────────────────────────────────────

int WindowAppearanceController::innerGap() const
{
    return m_settings->innerGap();
}
int WindowAppearanceController::outerGap() const
{
    return m_settings->outerGap();
}
bool WindowAppearanceController::usePerSideOuterGap() const
{
    return m_settings->usePerSideOuterGap();
}
int WindowAppearanceController::outerGapTop() const
{
    return m_settings->outerGapTop();
}
int WindowAppearanceController::outerGapBottom() const
{
    return m_settings->outerGapBottom();
}
int WindowAppearanceController::outerGapLeft() const
{
    return m_settings->outerGapLeft();
}
int WindowAppearanceController::outerGapRight() const
{
    return m_settings->outerGapRight();
}

void WindowAppearanceController::setInnerGap(int gap)
{
    m_settings->setInnerGap(gap);
}
void WindowAppearanceController::setOuterGap(int gap)
{
    m_settings->setOuterGap(gap);
}
void WindowAppearanceController::setUsePerSideOuterGap(bool enabled)
{
    m_settings->setUsePerSideOuterGap(enabled);
}
void WindowAppearanceController::setOuterGapTop(int gap)
{
    m_settings->setOuterGapTop(gap);
}
void WindowAppearanceController::setOuterGapBottom(int gap)
{
    m_settings->setOuterGapBottom(gap);
}
void WindowAppearanceController::setOuterGapLeft(int gap)
{
    m_settings->setOuterGapLeft(gap);
}
void WindowAppearanceController::setOuterGapRight(int gap)
{
    m_settings->setOuterGapRight(gap);
}

// ── Scope-aware gap read/write ───────────────────────────────────────────────

QVariant WindowAppearanceController::gapValue(const QString& screenName, const QString& key) const
{
    if (!screenName.isEmpty()) {
        // Per-monitor config override (short-keyed gap subset of the per-screen
        // autotile store). Fall back to the global value when this monitor has no
        // override for the key.
        const QVariantMap overrides = m_settings->getPerScreenAutotileSettings(screenName);
        const auto it = overrides.constFind(key);
        if (it != overrides.constEnd()) {
            return it.value();
        }
    }
    return globalGapValue(m_settings, key);
}

void WindowAppearanceController::writeGap(const QString& screenName, const QString& key, const QVariant& value)
{
    if (screenName.isEmpty()) {
        writeGlobalGap(m_settings, key, value);
    } else {
        m_settings->setPerScreenAutotileSetting(screenName, key, value);
    }
}

} // namespace PlasmaZones

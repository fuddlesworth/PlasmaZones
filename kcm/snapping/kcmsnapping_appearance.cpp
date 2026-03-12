// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Appearance getters/setters and Shader Effects for KCMSnapping
// Split from kcmsnapping.cpp to keep files under 500 lines.

#include "kcmsnapping.h"
#include <QStandardPaths>
#include "../../src/config/settings.h"
#include "../../src/core/constants.h"

namespace PlasmaZones {

// ── Appearance getters ──────────────────────────────────────────────────

bool KCMSnapping::useSystemColors() const
{
    return m_settings->useSystemColors();
}

QColor KCMSnapping::highlightColor() const
{
    return m_settings->highlightColor();
}

QColor KCMSnapping::inactiveColor() const
{
    return m_settings->inactiveColor();
}

QColor KCMSnapping::borderColor() const
{
    return m_settings->borderColor();
}

QColor KCMSnapping::labelFontColor() const
{
    return m_settings->labelFontColor();
}

qreal KCMSnapping::activeOpacity() const
{
    return m_settings->activeOpacity();
}

qreal KCMSnapping::inactiveOpacity() const
{
    return m_settings->inactiveOpacity();
}

int KCMSnapping::borderWidth() const
{
    return m_settings->borderWidth();
}

int KCMSnapping::borderRadius() const
{
    return m_settings->borderRadius();
}

bool KCMSnapping::enableBlur() const
{
    return m_settings->enableBlur();
}

QString KCMSnapping::labelFontFamily() const
{
    return m_settings->labelFontFamily();
}

qreal KCMSnapping::labelFontSizeScale() const
{
    return m_settings->labelFontSizeScale();
}

int KCMSnapping::labelFontWeight() const
{
    return m_settings->labelFontWeight();
}

bool KCMSnapping::labelFontItalic() const
{
    return m_settings->labelFontItalic();
}

bool KCMSnapping::labelFontUnderline() const
{
    return m_settings->labelFontUnderline();
}

bool KCMSnapping::labelFontStrikeout() const
{
    return m_settings->labelFontStrikeout();
}

// ── Appearance setters ──────────────────────────────────────────────────

void KCMSnapping::setUseSystemColors(bool use)
{
    if (m_settings->useSystemColors() != use) {
        m_settings->setUseSystemColors(use);
        Q_EMIT useSystemColorsChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setHighlightColor(const QColor& color)
{
    if (m_settings->highlightColor() != color) {
        m_settings->setHighlightColor(color);
        Q_EMIT highlightColorChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setInactiveColor(const QColor& color)
{
    if (m_settings->inactiveColor() != color) {
        m_settings->setInactiveColor(color);
        Q_EMIT inactiveColorChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setBorderColor(const QColor& color)
{
    if (m_settings->borderColor() != color) {
        m_settings->setBorderColor(color);
        Q_EMIT borderColorChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setLabelFontColor(const QColor& color)
{
    if (m_settings->labelFontColor() != color) {
        m_settings->setLabelFontColor(color);
        Q_EMIT labelFontColorChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setActiveOpacity(qreal opacity)
{
    if (!qFuzzyCompare(1.0 + m_settings->activeOpacity(), 1.0 + opacity)) {
        m_settings->setActiveOpacity(opacity);
        Q_EMIT activeOpacityChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setInactiveOpacity(qreal opacity)
{
    if (!qFuzzyCompare(1.0 + m_settings->inactiveOpacity(), 1.0 + opacity)) {
        m_settings->setInactiveOpacity(opacity);
        Q_EMIT inactiveOpacityChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setBorderWidth(int width)
{
    if (m_settings->borderWidth() != width) {
        m_settings->setBorderWidth(width);
        Q_EMIT borderWidthChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setBorderRadius(int radius)
{
    if (m_settings->borderRadius() != radius) {
        m_settings->setBorderRadius(radius);
        Q_EMIT borderRadiusChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setEnableBlur(bool enable)
{
    if (m_settings->enableBlur() != enable) {
        m_settings->setEnableBlur(enable);
        Q_EMIT enableBlurChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setLabelFontFamily(const QString& family)
{
    if (m_settings->labelFontFamily() != family) {
        m_settings->setLabelFontFamily(family);
        Q_EMIT labelFontFamilyChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setLabelFontSizeScale(qreal scale)
{
    scale = qBound(0.25, scale, 3.0);
    if (!qFuzzyCompare(m_settings->labelFontSizeScale(), scale)) {
        m_settings->setLabelFontSizeScale(scale);
        Q_EMIT labelFontSizeScaleChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setLabelFontWeight(int weight)
{
    if (m_settings->labelFontWeight() != weight) {
        m_settings->setLabelFontWeight(weight);
        Q_EMIT labelFontWeightChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setLabelFontItalic(bool italic)
{
    if (m_settings->labelFontItalic() != italic) {
        m_settings->setLabelFontItalic(italic);
        Q_EMIT labelFontItalicChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setLabelFontUnderline(bool underline)
{
    if (m_settings->labelFontUnderline() != underline) {
        m_settings->setLabelFontUnderline(underline);
        Q_EMIT labelFontUnderlineChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setLabelFontStrikeout(bool strikeout)
{
    if (m_settings->labelFontStrikeout() != strikeout) {
        m_settings->setLabelFontStrikeout(strikeout);
        Q_EMIT labelFontStrikeoutChanged();
        setNeedsSave(true);
    }
}

// ── Shader Effects ──────────────────────────────────────────────────────

bool KCMSnapping::enableShaderEffects() const
{
    return m_settings->enableShaderEffects();
}

int KCMSnapping::shaderFrameRate() const
{
    return m_settings->shaderFrameRate();
}

bool KCMSnapping::enableAudioVisualizer() const
{
    return m_settings->enableAudioVisualizer();
}

bool KCMSnapping::cavaAvailable() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("cava")).isEmpty();
}

int KCMSnapping::audioSpectrumBarCount() const
{
    return m_settings->audioSpectrumBarCount();
}

void KCMSnapping::setEnableShaderEffects(bool enable)
{
    if (m_settings->enableShaderEffects() != enable) {
        m_settings->setEnableShaderEffects(enable);
        Q_EMIT enableShaderEffectsChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setShaderFrameRate(int fps)
{
    if (m_settings->shaderFrameRate() != fps) {
        m_settings->setShaderFrameRate(fps);
        Q_EMIT shaderFrameRateChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setEnableAudioVisualizer(bool enable)
{
    if (m_settings->enableAudioVisualizer() != enable) {
        m_settings->setEnableAudioVisualizer(enable);
        Q_EMIT enableAudioVisualizerChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setAudioSpectrumBarCount(int count)
{
    // CAVA requires even bar count for stereo output
    const int even = (count % 2 != 0) ? count + 1 : count;
    const int clamped = qBound(Audio::MinBars, even, Audio::MaxBars);
    if (m_settings->audioSpectrumBarCount() != clamped) {
        m_settings->setAudioSpectrumBarCount(clamped);
        Q_EMIT audioSpectrumBarCountChanged();
        setNeedsSave(true);
    }
}

} // namespace PlasmaZones

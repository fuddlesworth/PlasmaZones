// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config/settings.h"
#include "config/settings/settings_detail.h"
#include "config/configdefaults.h"
#include "core/platform/logging.h"

namespace PlasmaZones {

using namespace settings_detail;

// ── Shaders (PhosphorConfig::Store-backed) ──────────────────────────────────

int Settings::shaderFrameRate() const
{
    return m_store->read<int>(ConfigDefaults::shadersGroup(), ConfigDefaults::frameRateKey());
}

void Settings::setShaderFrameRate(int fps)
{
    // Schema validator clamps on write, so a read-back after the write gives
    // the canonical value even if the caller passed something out of range.
    const int before = shaderFrameRate();
    m_store->write(ConfigDefaults::shadersGroup(), ConfigDefaults::frameRateKey(), fps);
    if (shaderFrameRate() == before) {
        return;
    }
    Q_EMIT shaderFrameRateChanged();
    Q_EMIT settingsChanged();
}

bool Settings::enableAudioVisualizer() const
{
    return m_store->read<bool>(ConfigDefaults::shadersAudioGroup(), ConfigDefaults::enabledKey());
}

void Settings::setEnableAudioVisualizer(bool enable)
{
    if (enableAudioVisualizer() == enable) {
        return;
    }
    m_store->write(ConfigDefaults::shadersAudioGroup(), ConfigDefaults::enabledKey(), enable);
    Q_EMIT enableAudioVisualizerChanged();
    Q_EMIT settingsChanged();
}

int Settings::audioSpectrumBarCount() const
{
    return m_store->read<int>(ConfigDefaults::shadersAudioGroup(), ConfigDefaults::barsKey());
}

void Settings::setAudioSpectrumBarCount(int count)
{
    const int before = audioSpectrumBarCount();
    m_store->write(ConfigDefaults::shadersAudioGroup(), ConfigDefaults::barsKey(), count);
    if (audioSpectrumBarCount() == before) {
        return;
    }
    Q_EMIT audioSpectrumBarCountChanged();
    Q_EMIT settingsChanged();
}
// ── Appearance (PhosphorConfig::Store-backed) ───────────────────────────────
// Colors group
P_STORE_GET(bool, useSystemColors, snappingZonesColorsGroup, useSystemKey, bool)
P_STORE_GET(QColor, highlightColor, snappingZonesColorsGroup, highlightKey, QColor)
P_STORE_SET_COLOR(setHighlightColor, snappingZonesColorsGroup, highlightKey, highlightColorChanged)
P_STORE_GET(QColor, inactiveColor, snappingZonesColorsGroup, inactiveKey, QColor)
P_STORE_SET_COLOR(setInactiveColor, snappingZonesColorsGroup, inactiveKey, inactiveColorChanged)
P_STORE_GET(QColor, borderColor, snappingZonesColorsGroup, borderKey, QColor)
P_STORE_SET_COLOR(setBorderColor, snappingZonesColorsGroup, borderKey, borderColorChanged)

// Labels group
P_STORE_GET(QColor, labelFontColor, snappingZonesLabelsGroup, fontColorKey, QColor)
P_STORE_SET_COLOR(setLabelFontColor, snappingZonesLabelsGroup, fontColorKey, labelFontColorChanged)
P_STORE_GET(QString, labelFontFamily, snappingZonesLabelsGroup, fontFamilyKey, QString)
P_STORE_SET_STRING(setLabelFontFamily, snappingZonesLabelsGroup, fontFamilyKey, labelFontFamilyChanged)
P_STORE_GET(qreal, labelFontSizeScale, snappingZonesLabelsGroup, fontSizeScaleKey, double)
P_STORE_SET_DOUBLE(setLabelFontSizeScale, snappingZonesLabelsGroup, fontSizeScaleKey, labelFontSizeScaleChanged)
P_STORE_GET(int, labelFontWeight, snappingZonesLabelsGroup, fontWeightKey, int)
P_STORE_SET_INT(setLabelFontWeight, snappingZonesLabelsGroup, fontWeightKey, labelFontWeightChanged)
P_STORE_GET(bool, labelFontItalic, snappingZonesLabelsGroup, fontItalicKey, bool)
P_STORE_SET_BOOL(setLabelFontItalic, snappingZonesLabelsGroup, fontItalicKey, labelFontItalicChanged)
P_STORE_GET(bool, labelFontUnderline, snappingZonesLabelsGroup, fontUnderlineKey, bool)
P_STORE_SET_BOOL(setLabelFontUnderline, snappingZonesLabelsGroup, fontUnderlineKey, labelFontUnderlineChanged)
P_STORE_GET(bool, labelFontStrikeout, snappingZonesLabelsGroup, fontStrikeoutKey, bool)
P_STORE_SET_BOOL(setLabelFontStrikeout, snappingZonesLabelsGroup, fontStrikeoutKey, labelFontStrikeoutChanged)

// Opacity group
P_STORE_GET(qreal, activeOpacity, snappingZonesOpacityGroup, activeKey, double)
P_STORE_SET_DOUBLE(setActiveOpacity, snappingZonesOpacityGroup, activeKey, activeOpacityChanged)
P_STORE_GET(qreal, inactiveOpacity, snappingZonesOpacityGroup, inactiveKey, double)
P_STORE_SET_DOUBLE(setInactiveOpacity, snappingZonesOpacityGroup, inactiveKey, inactiveOpacityChanged)

// Border group
P_STORE_GET(int, borderWidth, snappingZonesBorderGroup, widthKey, int)
P_STORE_SET_INT(setBorderWidth, snappingZonesBorderGroup, widthKey, borderWidthChanged)
P_STORE_GET(int, borderRadius, snappingZonesBorderGroup, radiusKey, int)
P_STORE_SET_INT(setBorderRadius, snappingZonesBorderGroup, radiusKey, borderRadiusChanged)

// ── Ordering (PhosphorConfig::Store-backed) ─────────────────────────────────
// On disk: comma-joined QString. In API: QStringList. The schema validator
// normalizes the canonical format (trim/dedup), so the round-trip through
// the store always produces the same string for any equivalent input. The
// parser below is still defensive (trim + skip-empty) in case a caller
// reads a string written before the validator was installed.

QStringList Settings::snappingLayoutOrder() const
{
    return parseCommaList(
        m_store->read<QString>(ConfigDefaults::orderingGroup(), ConfigDefaults::snappingLayoutOrderKey()));
}

void Settings::setSnappingLayoutOrder(const QStringList& order)
{
    // Read the canonical stored form before AND after writing so the
    // canonicalCommaList validator gets to pick the comparison points.
    // Comparing the user's (possibly non-canonical) input to the stored
    // canonical value would emit a spurious `changed` signal every time a
    // caller passed e.g. " a , b " while disk already holds "a,b".
    const QString before =
        m_store->read<QString>(ConfigDefaults::orderingGroup(), ConfigDefaults::snappingLayoutOrderKey());
    m_store->write(ConfigDefaults::orderingGroup(), ConfigDefaults::snappingLayoutOrderKey(),
                   order.join(QLatin1Char(',')));
    const QString after =
        m_store->read<QString>(ConfigDefaults::orderingGroup(), ConfigDefaults::snappingLayoutOrderKey());
    if (before == after) {
        return;
    }
    Q_EMIT snappingLayoutOrderChanged();
    Q_EMIT settingsChanged();
}

QStringList Settings::tilingAlgorithmOrder() const
{
    return parseCommaList(
        m_store->read<QString>(ConfigDefaults::orderingGroup(), ConfigDefaults::tilingAlgorithmOrderKey()));
}

void Settings::setTilingAlgorithmOrder(const QStringList& order)
{
    // See setSnappingLayoutOrder — post-write compare against the canonical
    // form avoids spurious change signals for equivalent non-canonical input.
    const QString before =
        m_store->read<QString>(ConfigDefaults::orderingGroup(), ConfigDefaults::tilingAlgorithmOrderKey());
    m_store->write(ConfigDefaults::orderingGroup(), ConfigDefaults::tilingAlgorithmOrderKey(),
                   order.join(QLatin1Char(',')));
    const QString after =
        m_store->read<QString>(ConfigDefaults::orderingGroup(), ConfigDefaults::tilingAlgorithmOrderKey());
    if (before == after) {
        return;
    }
    Q_EMIT tilingAlgorithmOrderChanged();
    Q_EMIT settingsChanged();
}

// ── Animations (PhosphorConfig::Store-backed) ───────────────────────────────
// Snapping + autotile geometry-change transitions. Phase 4 sub-commit 6
// migrated the on-disk format from five per-field keys to a single
// Profile JSON blob (decision S — no backwards compat for the old
// layout). Each per-field accessor now decomposes / composes the blob,
// preserving the Q_PROPERTY + ISettings interface for QML and the
// daemon's existing consumers.
//
// `animationsEnabled` stays as a standalone bool — it's an orthogonal
// on/off toggle rather than part of the Profile concept.

P_STORE_GET(bool, animationsEnabled, animationsGroup, enabledKey, bool)
P_STORE_SET_BOOL(setAnimationsEnabled, animationsGroup, enabledKey, animationsEnabledChanged)

// ── Decorations.Performance (PhosphorConfig::Store-backed) ──────────────────
// These bound WHEN the decoration chain animates, not how much work it does per
// frame. An animated pack repaints every window carrying it on every vsync, and
// that alone holds the GPU in its top performance state however cheap the frame
// is — so the only lever that returns the card to its idle clocks is to stop
// drawing when nothing needs to change.

P_STORE_GET(bool, decorationAnimateFocusedOnly, decorationsPerformanceGroup, animateFocusedOnlyKey, bool)
P_STORE_SET_BOOL(setDecorationAnimateFocusedOnly, decorationsPerformanceGroup, animateFocusedOnlyKey,
                 decorationAnimateFocusedOnlyChanged)

P_STORE_GET(bool, decorationPauseWhenIdle, decorationsPerformanceGroup, pauseWhenIdleKey, bool)
P_STORE_SET_BOOL(setDecorationPauseWhenIdle, decorationsPerformanceGroup, pauseWhenIdleKey,
                 decorationPauseWhenIdleChanged)

P_STORE_GET(int, decorationIdleTimeoutSec, decorationsPerformanceGroup, idleTimeoutSecKey, int)
P_STORE_SET_INT(setDecorationIdleTimeoutSec, decorationsPerformanceGroup, idleTimeoutSecKey,
                decorationIdleTimeoutSecChanged)

// ── Rendering (PhosphorConfig::Store-backed) ────────────────────────────────
// Validator (normalizeRenderingBackend in the schema) coerces unknown values
// to a known backend, so a hand-edited "Rendering.Backend = foobar" reads
// back as the default on next load.

P_STORE_GET(QString, renderingBackend, renderingGroup, backendKey, QString)
P_STORE_SET_STRING(setRenderingBackend, renderingGroup, backendKey, renderingBackendChanged)

// Shaders.Audio (ISettings) — the audio-spectrum analysis parameter set.
// enableAudioVisualizer / audioSpectrumBarCount live with the other
// handwritten shader accessors above; the rest are uniform store-backed
// scalars, so the macros apply.
P_STORE_GET(bool, audioAutosens, shadersAudioGroup, autosensKey, bool)
P_STORE_SET_BOOL(setAudioAutosens, shadersAudioGroup, autosensKey, audioAutosensChanged)
P_STORE_GET(int, audioSensitivity, shadersAudioGroup, sensitivityKey, int)
P_STORE_SET_INT(setAudioSensitivity, shadersAudioGroup, sensitivityKey, audioSensitivityChanged)
P_STORE_GET(int, audioNoiseReduction, shadersAudioGroup, noiseReductionKey, int)
P_STORE_SET_INT(setAudioNoiseReduction, shadersAudioGroup, noiseReductionKey, audioNoiseReductionChanged)
P_STORE_GET(int, audioLowerCutoffHz, shadersAudioGroup, lowerCutoffHzKey, int)
P_STORE_SET_INT(setAudioLowerCutoffHz, shadersAudioGroup, lowerCutoffHzKey, audioLowerCutoffHzChanged)
P_STORE_GET(int, audioHigherCutoffHz, shadersAudioGroup, higherCutoffHzKey, int)
P_STORE_SET_INT(setAudioHigherCutoffHz, shadersAudioGroup, higherCutoffHzKey, audioHigherCutoffHzChanged)
P_STORE_GET(bool, audioMonstercat, shadersAudioGroup, monstercatKey, bool)
P_STORE_SET_BOOL(setAudioMonstercat, shadersAudioGroup, monstercatKey, audioMonstercatChanged)
P_STORE_GET(bool, audioWaves, shadersAudioGroup, wavesKey, bool)
P_STORE_SET_BOOL(setAudioWaves, shadersAudioGroup, wavesKey, audioWavesChanged)
P_STORE_GET(QString, audioChannelMode, shadersAudioGroup, channelModeKey, QString)
P_STORE_SET_STRING(setAudioChannelMode, shadersAudioGroup, channelModeKey, audioChannelModeChanged)
P_STORE_GET(bool, audioReverse, shadersAudioGroup, reverseKey, bool)
P_STORE_SET_BOOL(setAudioReverse, shadersAudioGroup, reverseKey, audioReverseChanged)
P_STORE_GET(int, audioExtraSmoothing, shadersAudioGroup, extraSmoothingKey, int)
P_STORE_SET_INT(setAudioExtraSmoothing, shadersAudioGroup, extraSmoothingKey, audioExtraSmoothingChanged)
P_STORE_GET(QString, audioInputMethod, shadersAudioGroup, inputMethodKey, QString)
P_STORE_SET_STRING(setAudioInputMethod, shadersAudioGroup, inputMethodKey, audioInputMethodChanged)
P_STORE_GET(QString, audioInputSource, shadersAudioGroup, inputSourceKey, QString)
P_STORE_SET_STRING(setAudioInputSource, shadersAudioGroup, inputSourceKey, audioInputSourceChanged)

// ── Performance (PhosphorConfig::Store-backed) ──────────────────────────────

P_STORE_GET(int, pollIntervalMs, performanceGroup, pollIntervalMsKey, int)
P_STORE_SET_INT(setPollIntervalMs, performanceGroup, pollIntervalMsKey, pollIntervalMsChanged)
P_STORE_GET(int, minimumZoneSizePx, performanceGroup, minimumZoneSizePxKey, int)
P_STORE_SET_INT(setMinimumZoneSizePx, performanceGroup, minimumZoneSizePxKey, minimumZoneSizePxChanged)
P_STORE_GET(int, minimumZoneDisplaySizePx, performanceGroup, minimumZoneDisplaySizePxKey, int)
P_STORE_SET_INT(setMinimumZoneDisplaySizePx, performanceGroup, minimumZoneDisplaySizePxKey,
                minimumZoneDisplaySizePxChanged)

// ── PhosphorZones::Zone geometry (PhosphorConfig::Store-backed) ────────────────────────────
// Inner/outer gaps (uniform + per-side) plus adjacency threshold. Schema
// clampInt validators enforce the same ranges readValidatedInt used to.

// ── Shared inner/outer gaps (PhosphorConfig::Store-backed, "Gaps" group) ─────
// Read on demand through m_store (validator clamps to the declared gap range);
// setters route the write through the store and emit only on a real change.
// The autotile* gap forwarders in settings.h delegate to these getters, so
// tiling and snapping always resolve the same values.
P_STORE_GET(int, innerGap, gapsGroup, innerGapKey, int)
P_STORE_SET_INT(setInnerGap, gapsGroup, innerGapKey, innerGapChanged)
P_STORE_GET(int, outerGap, gapsGroup, outerGapKey, int)
P_STORE_SET_INT(setOuterGap, gapsGroup, outerGapKey, outerGapChanged)
P_STORE_GET(bool, usePerSideOuterGap, gapsGroup, usePerSideOuterGapKey, bool)
P_STORE_SET_BOOL(setUsePerSideOuterGap, gapsGroup, usePerSideOuterGapKey, usePerSideOuterGapChanged)
P_STORE_GET(int, outerGapTop, gapsGroup, outerGapTopKey, int)
P_STORE_SET_INT(setOuterGapTop, gapsGroup, outerGapTopKey, outerGapTopChanged)
P_STORE_GET(int, outerGapBottom, gapsGroup, outerGapBottomKey, int)
P_STORE_SET_INT(setOuterGapBottom, gapsGroup, outerGapBottomKey, outerGapBottomChanged)
P_STORE_GET(int, outerGapLeft, gapsGroup, outerGapLeftKey, int)
P_STORE_SET_INT(setOuterGapLeft, gapsGroup, outerGapLeftKey, outerGapLeftChanged)
P_STORE_GET(int, outerGapRight, gapsGroup, outerGapRightKey, int)
P_STORE_SET_INT(setOuterGapRight, gapsGroup, outerGapRightKey, outerGapRightChanged)

// ── Window decoration appearance (Store-backed, "Windows" group) ─────────────
P_STORE_GET(bool, showWindowBorder, windowsAppearanceGroup, showBorderKey, bool)
P_STORE_SET_BOOL(setShowWindowBorder, windowsAppearanceGroup, showBorderKey, showWindowBorderChanged)
P_STORE_GET(QString, windowBorderScope, windowsAppearanceGroup, borderScopeKey, QString)
P_STORE_SET_STRING(setWindowBorderScope, windowsAppearanceGroup, borderScopeKey, windowBorderScopeChanged)
P_STORE_GET(int, windowBorderWidth, windowsAppearanceGroup, widthKey, int)
P_STORE_SET_INT(setWindowBorderWidth, windowsAppearanceGroup, widthKey, windowBorderWidthChanged)
P_STORE_GET(int, windowBorderRadius, windowsAppearanceGroup, radiusKey, int)
P_STORE_SET_INT(setWindowBorderRadius, windowsAppearanceGroup, radiusKey, windowBorderRadiusChanged)
P_STORE_GET(QString, windowBorderColorActive, windowsAppearanceGroup, borderColorActiveKey, QString)
P_STORE_SET_STRING(setWindowBorderColorActive, windowsAppearanceGroup, borderColorActiveKey,
                   windowBorderColorActiveChanged)
P_STORE_GET(QString, windowBorderColorInactive, windowsAppearanceGroup, borderColorInactiveKey, QString)
P_STORE_SET_STRING(setWindowBorderColorInactive, windowsAppearanceGroup, borderColorInactiveKey,
                   windowBorderColorInactiveChanged)
P_STORE_GET(bool, hideWindowTitleBars, windowsAppearanceGroup, hideTitleBarsKey, bool)
P_STORE_SET_BOOL(setHideWindowTitleBars, windowsAppearanceGroup, hideTitleBarsKey, hideWindowTitleBarsChanged)
P_STORE_GET(QString, windowTitleBarScope, windowsAppearanceGroup, titleBarScopeKey, QString)
P_STORE_SET_STRING(setWindowTitleBarScope, windowsAppearanceGroup, titleBarScopeKey, windowTitleBarScopeChanged)
P_STORE_GET(int, focusFadeDuration, windowsAppearanceGroup, focusFadeDurationKey, int)
P_STORE_SET_INT(setFocusFadeDuration, windowsAppearanceGroup, focusFadeDurationKey, focusFadeDurationChanged)
// Plain opacity+tint layer (same "Windows" group).
P_STORE_GET(bool, showWindowOpacityTint, windowsAppearanceGroup, showOpacityTintKey, bool)
P_STORE_SET_BOOL(setShowWindowOpacityTint, windowsAppearanceGroup, showOpacityTintKey, showWindowOpacityTintChanged)
P_STORE_GET(QString, windowOpacityTintScope, windowsAppearanceGroup, opacityTintScopeKey, QString)
P_STORE_SET_STRING(setWindowOpacityTintScope, windowsAppearanceGroup, opacityTintScopeKey,
                   windowOpacityTintScopeChanged)
P_STORE_GET(double, windowOpacity, windowsAppearanceGroup, opacityKey, double)
P_STORE_SET_DOUBLE(setWindowOpacity, windowsAppearanceGroup, opacityKey, windowOpacityChanged)
P_STORE_GET(double, windowTintStrength, windowsAppearanceGroup, tintStrengthKey, double)
P_STORE_SET_DOUBLE(setWindowTintStrength, windowsAppearanceGroup, tintStrengthKey, windowTintStrengthChanged)
P_STORE_GET(QString, windowTintColor, windowsAppearanceGroup, tintColorKey, QString)
P_STORE_SET_STRING(setWindowTintColor, windowsAppearanceGroup, tintColorKey, windowTintColorChanged)

} // namespace PlasmaZones

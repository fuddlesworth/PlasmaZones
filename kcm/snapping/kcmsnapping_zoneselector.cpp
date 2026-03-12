// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Zones/Gaps, Zone Selector, Screens, Font helpers, Color import,
// Per-screen settings, and Monitor disable for KCMSnapping.
// Split from kcmsnapping.cpp to keep files under 500 lines.

#include "kcmsnapping.h"
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include "../common/perscreenhelpers.h"
#include "../common/screenhelper.h"
#include "../common/screenprovider.h"
#include "../../src/config/settings.h"
#include "../../src/core/utils.h"

namespace PlasmaZones {

// ── Zones / Gaps getters ────────────────────────────────────────────────

int KCMSnapping::zonePadding() const
{
    return m_settings->zonePadding();
}

int KCMSnapping::outerGap() const
{
    return m_settings->outerGap();
}

bool KCMSnapping::usePerSideOuterGap() const
{
    return m_settings->usePerSideOuterGap();
}

int KCMSnapping::outerGapTop() const
{
    return m_settings->outerGapTop();
}

int KCMSnapping::outerGapBottom() const
{
    return m_settings->outerGapBottom();
}

int KCMSnapping::outerGapLeft() const
{
    return m_settings->outerGapLeft();
}

int KCMSnapping::outerGapRight() const
{
    return m_settings->outerGapRight();
}

int KCMSnapping::adjacentThreshold() const
{
    return m_settings->adjacentThreshold();
}

// ── Zones / Gaps setters ────────────────────────────────────────────────

void KCMSnapping::setZonePadding(int padding)
{
    if (m_settings->zonePadding() != padding) {
        m_settings->setZonePadding(padding);
        Q_EMIT zonePaddingChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setOuterGap(int gap)
{
    if (m_settings->outerGap() != gap) {
        m_settings->setOuterGap(gap);
        Q_EMIT outerGapChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setUsePerSideOuterGap(bool enabled)
{
    if (m_settings->usePerSideOuterGap() != enabled) {
        m_settings->setUsePerSideOuterGap(enabled);
        Q_EMIT usePerSideOuterGapChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setOuterGapTop(int gap)
{
    if (m_settings->outerGapTop() != gap) {
        m_settings->setOuterGapTop(gap);
        Q_EMIT outerGapTopChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setOuterGapBottom(int gap)
{
    if (m_settings->outerGapBottom() != gap) {
        m_settings->setOuterGapBottom(gap);
        Q_EMIT outerGapBottomChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setOuterGapLeft(int gap)
{
    if (m_settings->outerGapLeft() != gap) {
        m_settings->setOuterGapLeft(gap);
        Q_EMIT outerGapLeftChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setOuterGapRight(int gap)
{
    if (m_settings->outerGapRight() != gap) {
        m_settings->setOuterGapRight(gap);
        Q_EMIT outerGapRightChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setAdjacentThreshold(int threshold)
{
    if (m_settings->adjacentThreshold() != threshold) {
        m_settings->setAdjacentThreshold(threshold);
        Q_EMIT adjacentThresholdChanged();
        setNeedsSave(true);
    }
}

// ── Zone Selector getters ───────────────────────────────────────────────

bool KCMSnapping::zoneSelectorEnabled() const
{
    return m_settings->zoneSelectorEnabled();
}

int KCMSnapping::zoneSelectorTriggerDistance() const
{
    return m_settings->zoneSelectorTriggerDistance();
}

int KCMSnapping::zoneSelectorPosition() const
{
    return m_settings->zoneSelectorPositionInt();
}

int KCMSnapping::zoneSelectorLayoutMode() const
{
    return m_settings->zoneSelectorLayoutModeInt();
}

int KCMSnapping::zoneSelectorPreviewWidth() const
{
    return m_settings->zoneSelectorPreviewWidth();
}

int KCMSnapping::zoneSelectorPreviewHeight() const
{
    return m_settings->zoneSelectorPreviewHeight();
}

bool KCMSnapping::zoneSelectorPreviewLockAspect() const
{
    return m_settings->zoneSelectorPreviewLockAspect();
}

int KCMSnapping::zoneSelectorGridColumns() const
{
    return m_settings->zoneSelectorGridColumns();
}

int KCMSnapping::zoneSelectorSizeMode() const
{
    return m_settings->zoneSelectorSizeModeInt();
}

int KCMSnapping::zoneSelectorMaxRows() const
{
    return m_settings->zoneSelectorMaxRows();
}

// ── Zone Selector setters ───────────────────────────────────────────────

void KCMSnapping::setZoneSelectorEnabled(bool enabled)
{
    if (m_settings->zoneSelectorEnabled() != enabled) {
        m_settings->setZoneSelectorEnabled(enabled);
        Q_EMIT zoneSelectorEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setZoneSelectorTriggerDistance(int distance)
{
    if (m_settings->zoneSelectorTriggerDistance() != distance) {
        m_settings->setZoneSelectorTriggerDistance(distance);
        Q_EMIT zoneSelectorTriggerDistanceChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setZoneSelectorPosition(int position)
{
    if (m_settings->zoneSelectorPositionInt() != position) {
        m_settings->setZoneSelectorPositionInt(position);
        Q_EMIT zoneSelectorPositionChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setZoneSelectorLayoutMode(int mode)
{
    if (m_settings->zoneSelectorLayoutModeInt() != mode) {
        m_settings->setZoneSelectorLayoutModeInt(mode);
        Q_EMIT zoneSelectorLayoutModeChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setZoneSelectorPreviewWidth(int width)
{
    if (m_settings->zoneSelectorPreviewWidth() != width) {
        m_settings->setZoneSelectorPreviewWidth(width);
        Q_EMIT zoneSelectorPreviewWidthChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setZoneSelectorPreviewHeight(int height)
{
    if (m_settings->zoneSelectorPreviewHeight() != height) {
        m_settings->setZoneSelectorPreviewHeight(height);
        Q_EMIT zoneSelectorPreviewHeightChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setZoneSelectorPreviewLockAspect(bool locked)
{
    if (m_settings->zoneSelectorPreviewLockAspect() != locked) {
        m_settings->setZoneSelectorPreviewLockAspect(locked);
        Q_EMIT zoneSelectorPreviewLockAspectChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setZoneSelectorGridColumns(int columns)
{
    if (m_settings->zoneSelectorGridColumns() != columns) {
        m_settings->setZoneSelectorGridColumns(columns);
        Q_EMIT zoneSelectorGridColumnsChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setZoneSelectorSizeMode(int mode)
{
    if (m_settings->zoneSelectorSizeModeInt() != mode) {
        m_settings->setZoneSelectorSizeModeInt(mode);
        Q_EMIT zoneSelectorSizeModeChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setZoneSelectorMaxRows(int rows)
{
    if (m_settings->zoneSelectorMaxRows() != rows) {
        m_settings->setZoneSelectorMaxRows(rows);
        Q_EMIT zoneSelectorMaxRowsChanged();
        setNeedsSave(true);
    }
}

// ── Screens ─────────────────────────────────────────────────────────────

QVariantList KCMSnapping::screens() const
{
    return m_screenHelper->screens();
}

void KCMSnapping::refreshScreens()
{
    m_screenHelper->refreshScreens();
}

// ── Font helpers ────────────────────────────────────────────────────────

QStringList KCMSnapping::fontStylesForFamily(const QString& family) const
{
    return QFontDatabase::styles(family);
}

int KCMSnapping::fontStyleWeight(const QString& family, const QString& style) const
{
    return QFontDatabase::weight(family, style);
}

bool KCMSnapping::fontStyleItalic(const QString& family, const QString& style) const
{
    return QFontDatabase::italic(family, style);
}

// ── Color import ────────────────────────────────────────────────────────

void KCMSnapping::loadColorsFromPywal()
{
    QString pywalPath = QDir::homePath() + QStringLiteral("/.cache/wal/colors.json");
    if (!QFile::exists(pywalPath)) {
        Q_EMIT colorImportError(
            tr("Pywal colors not found. Run 'wal' to generate colors first.\n\nExpected file: %1").arg(pywalPath));
        return;
    }

    QString error = m_settings->loadColorsFromFile(pywalPath);
    if (!error.isEmpty()) {
        Q_EMIT colorImportError(error);
        return;
    }

    emitColorChanged();
}

void KCMSnapping::loadColorsFromFile(const QString& filePath)
{
    QString error = m_settings->loadColorsFromFile(filePath);
    if (!error.isEmpty()) {
        Q_EMIT colorImportError(error);
        return;
    }

    emitColorChanged();
}

void KCMSnapping::emitColorChanged()
{
    Q_EMIT highlightColorChanged();
    Q_EMIT inactiveColorChanged();
    Q_EMIT borderColorChanged();
    Q_EMIT labelFontColorChanged();
    Q_EMIT useSystemColorsChanged();
    Q_EMIT colorImportSuccess();
    setNeedsSave(true);
}

// ── Per-screen settings ─────────────────────────────────────────────────

// Per-screen snapping
QVariantMap KCMSnapping::getPerScreenSnappingSettings(const QString& screenName) const
{
    return PerScreen::get(m_settings, screenName, &Settings::getPerScreenSnappingSettings);
}
void KCMSnapping::setPerScreenSnappingSetting(const QString& screenName, const QString& key, const QVariant& value)
{
    PerScreen::set(m_settings, screenName, key, value, &Settings::setPerScreenSnappingSetting);
    setNeedsSave(true);
}
void KCMSnapping::clearPerScreenSnappingSettings(const QString& screenName)
{
    PerScreen::clear(m_settings, screenName, &Settings::clearPerScreenSnappingSettings);
    setNeedsSave(true);
}
bool KCMSnapping::hasPerScreenSnappingSettings(const QString& screenName) const
{
    return PerScreen::has(m_settings, screenName, &Settings::hasPerScreenSnappingSettings);
}

// Per-screen zone selector
QVariantMap KCMSnapping::getPerScreenZoneSelectorSettings(const QString& screenName) const
{
    return PerScreen::get(m_settings, screenName, &Settings::getPerScreenZoneSelectorSettings);
}
void KCMSnapping::setPerScreenZoneSelectorSetting(const QString& screenName, const QString& key, const QVariant& value)
{
    PerScreen::set(m_settings, screenName, key, value, &Settings::setPerScreenZoneSelectorSetting);
    setNeedsSave(true);
}
void KCMSnapping::clearPerScreenZoneSelectorSettings(const QString& screenName)
{
    PerScreen::clear(m_settings, screenName, &Settings::clearPerScreenZoneSelectorSettings);
    setNeedsSave(true);
}
bool KCMSnapping::hasPerScreenZoneSelectorSettings(const QString& screenName) const
{
    return PerScreen::has(m_settings, screenName, &Settings::hasPerScreenZoneSelectorSettings);
}

// ── Monitor disable ─────────────────────────────────────────────────────

bool KCMSnapping::isMonitorDisabled(const QString& screenName) const
{
    return m_screenHelper->isMonitorDisabled(screenName);
}

void KCMSnapping::setMonitorDisabled(const QString& screenName, bool disabled)
{
    m_screenHelper->setMonitorDisabled(screenName, disabled);
}

} // namespace PlasmaZones

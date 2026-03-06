// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../services/ZoneManager.h"
#include "../services/SnappingService.h"
#include "../../core/constants.h"
#include "../../core/logging.h"
#include "../helpers/SettingsDbusQueries.h"

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QRegularExpression>

namespace PlasmaZones {

QString EditorController::validateZoneName(const QString& zoneId, const QString& name)
{
    // Empty names are allowed
    if (name.isEmpty()) {
        return QString();
    }

    // Check maximum length
    if (name.length() > 100) {
        return i18n("Zone name cannot exceed 100 characters");
    }

    // Check for invalid characters (allow alphanumeric, spaces, hyphens, underscores)
    // But be lenient - allow most characters for internationalization
    // Only block characters that could break JSON or filenames
    QRegularExpression invalidChars(QStringLiteral("[<>\"'\\\\]"));
    QRegularExpressionMatch match = invalidChars.match(name);
    if (match.hasMatch()) {
        return i18n("Zone name contains invalid characters: < > \" ' \\");
    }

    // Check for duplicate names (excluding the current zone)
    if (m_zoneManager) {
        QVariantList zones = m_zoneManager->zones();
        for (const QVariant& zoneVar : zones) {
            QVariantMap zone = zoneVar.toMap();
            QString otherZoneId = zone.value(QStringLiteral("id")).toString();
            if (otherZoneId != zoneId) {
                QString otherName = zone.value(QStringLiteral("name")).toString();
                if (otherName == name) {
                    return i18n("A zone with this name already exists");
                }
            }
        }
    }

    return QString(); // Valid
}

/**
 * @brief Validates a zone number
 * @param zoneId The zone ID (to exclude from duplicate check)
 * @param number The number to validate
 * @return Empty string if valid, error message otherwise
 */
QString EditorController::validateZoneNumber(const QString& zoneId, int number)
{
    // Check range
    if (number < 1) {
        return i18n("Zone number must be at least 1");
    }
    if (number > 99) {
        return i18n("Zone number cannot exceed 99");
    }

    // Check for duplicate numbers
    if (!m_zoneManager) {
        return QString(); // Can't check duplicates without manager
    }

    QVariantList zones = m_zoneManager->zones();
    for (const QVariant& zoneVar : zones) {
        QVariantMap zone = zoneVar.toMap();
        QString otherZoneId = zone[JsonKeys::Id].toString();

        // Skip the zone being updated
        if (otherZoneId == zoneId) {
            continue;
        }

        int otherNumber = zone[JsonKeys::ZoneNumber].toInt();
        if (otherNumber == number) {
            return i18n("Zone number %1 is already in use", number);
        }
    }

    return QString(); // Valid
}

void EditorController::setDefaultZoneColors(const QString& highlightColor, const QString& inactiveColor,
                                            const QString& borderColor)
{
    // Store defaults for use in template application
    m_defaultHighlightColor = highlightColor;
    m_defaultInactiveColor = inactiveColor;
    m_defaultBorderColor = borderColor;

    // Set in ZoneManager for new zone creation
    if (m_zoneManager) {
        m_zoneManager->setDefaultColors(highlightColor, inactiveColor, borderColor);
    }
}

void EditorController::loadEditorSettings()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));

    // Note: Per-layout zonePadding/outerGap overrides are loaded from the layout JSON
    // in loadLayout(). The global settings are cached here for performance (avoids D-Bus calls).
    refreshGlobalZonePadding();
    refreshGlobalOuterGap();

    // Load label font settings from global Appearance config (read-only in editor)
    KConfigGroup appearanceGroup = config->group(QStringLiteral("Appearance"));
    m_labelFontFamily = appearanceGroup.readEntry(QLatin1String("LabelFontFamily"), QString());
    m_labelFontSizeScale = qBound(0.25, appearanceGroup.readEntry(QLatin1String("LabelFontSizeScale"), 1.0), 3.0);
    m_labelFontWeight = appearanceGroup.readEntry(QLatin1String("LabelFontWeight"), 700);
    m_labelFontItalic = appearanceGroup.readEntry(QLatin1String("LabelFontItalic"), false);
    m_labelFontUnderline = appearanceGroup.readEntry(QLatin1String("LabelFontUnderline"), false);
    m_labelFontStrikeout = appearanceGroup.readEntry(QLatin1String("LabelFontStrikeout"), false);

    // Load snapping settings (backward compatible with single SnapInterval)
    bool gridEnabled = editorGroup.readEntry(QLatin1String("GridSnappingEnabled"), true);
    bool edgeEnabled = editorGroup.readEntry(QLatin1String("EdgeSnappingEnabled"), true);

    // Try to load separate X and Y intervals, fall back to single interval for backward compatibility
    qreal snapIntX = editorGroup.readEntry(QLatin1String("SnapIntervalX"), -1.0);
    qreal snapIntY = editorGroup.readEntry(QLatin1String("SnapIntervalY"), -1.0);
    qreal snapInt = editorGroup.readEntry(QLatin1String("SnapInterval"), EditorConstants::DefaultSnapInterval);

    // If separate intervals not found, use the single interval for both
    if (snapIntX < 0.0)
        snapIntX = snapInt;
    if (snapIntY < 0.0)
        snapIntY = snapInt;

    // Apply to snapping service
    m_snappingService->setGridSnappingEnabled(gridEnabled);
    m_snappingService->setEdgeSnappingEnabled(edgeEnabled);
    m_snappingService->setSnapIntervalX(snapIntX);
    m_snappingService->setSnapIntervalY(snapIntY);

    // Load app-specific keyboard shortcuts with validation
    // Note: Standard shortcuts (Save, Delete, Close) use Qt StandardKey (system shortcuts)
    loadShortcutSetting(editorGroup, QStringLiteral("EditorDuplicateShortcut"), QStringLiteral("Ctrl+D"),
                        m_editorDuplicateShortcut, [this]() {
                            Q_EMIT editorDuplicateShortcutChanged();
                        });

    loadShortcutSetting(editorGroup, QStringLiteral("EditorSplitHorizontalShortcut"), QStringLiteral("Ctrl+Shift+H"),
                        m_editorSplitHorizontalShortcut, [this]() {
                            Q_EMIT editorSplitHorizontalShortcutChanged();
                        });

    // Note: Default changed from Ctrl+Shift+V to Ctrl+Alt+V to avoid conflict with Paste with Offset
    loadShortcutSetting(editorGroup, QStringLiteral("EditorSplitVerticalShortcut"), QStringLiteral("Ctrl+Alt+V"),
                        m_editorSplitVerticalShortcut, [this]() {
                            Q_EMIT editorSplitVerticalShortcutChanged();
                        });

    loadShortcutSetting(editorGroup, QStringLiteral("EditorFillShortcut"), QStringLiteral("Ctrl+Shift+F"),
                        m_editorFillShortcut, [this]() {
                            Q_EMIT editorFillShortcutChanged();
                        });

    // Load snap override modifier
    int snapOverrideMod = editorGroup.readEntry(QLatin1String("SnapOverrideModifier"), 0x02000000);
    if (m_snapOverrideModifier != snapOverrideMod) {
        m_snapOverrideModifier = snapOverrideMod;
        Q_EMIT snapOverrideModifierChanged();
    }

    // Load fill-on-drop settings
    bool fillOnDropEn = editorGroup.readEntry(QLatin1String("FillOnDropEnabled"), true);
    if (m_fillOnDropEnabled != fillOnDropEn) {
        m_fillOnDropEnabled = fillOnDropEn;
        Q_EMIT fillOnDropEnabledChanged();
    }

    int fillOnDropMod = editorGroup.readEntry(QLatin1String("FillOnDropModifier"), 0x04000000); // Default: Ctrl
    if (m_fillOnDropModifier != fillOnDropMod) {
        m_fillOnDropModifier = fillOnDropMod;
        Q_EMIT fillOnDropModifierChanged();
    }
}

void EditorController::saveEditorSettings()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));

    // Save snapping settings (save both separate intervals and single for backward compatibility)
    editorGroup.writeEntry(QLatin1String("GridSnappingEnabled"), m_snappingService->gridSnappingEnabled());
    editorGroup.writeEntry(QLatin1String("EdgeSnappingEnabled"), m_snappingService->edgeSnappingEnabled());
    editorGroup.writeEntry(QLatin1String("SnapIntervalX"), m_snappingService->snapIntervalX());
    editorGroup.writeEntry(QLatin1String("SnapIntervalY"), m_snappingService->snapIntervalY());
    editorGroup.writeEntry(QLatin1String("SnapInterval"), m_snappingService->snapIntervalX()); // Backward compat

    // Save app-specific keyboard shortcuts
    // Note: Standard shortcuts (Save, Delete, Close) use Qt StandardKey (system shortcuts)
    editorGroup.writeEntry(QLatin1String("EditorDuplicateShortcut"), m_editorDuplicateShortcut);
    editorGroup.writeEntry(QLatin1String("EditorSplitHorizontalShortcut"), m_editorSplitHorizontalShortcut);
    editorGroup.writeEntry(QLatin1String("EditorSplitVerticalShortcut"), m_editorSplitVerticalShortcut);
    editorGroup.writeEntry(QLatin1String("EditorFillShortcut"), m_editorFillShortcut);

    // Save snap override modifier
    editorGroup.writeEntry(QLatin1String("SnapOverrideModifier"), m_snapOverrideModifier);

    // Save fill-on-drop settings
    editorGroup.writeEntry(QLatin1String("FillOnDropEnabled"), m_fillOnDropEnabled);
    editorGroup.writeEntry(QLatin1String("FillOnDropModifier"), m_fillOnDropModifier);

    config->sync();
}

} // namespace PlasmaZones

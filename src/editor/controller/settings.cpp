// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../services/ZoneManager.h"
#include "../services/SnappingService.h"
#include "../../core/constants.h"
#include "../../core/logging.h"
#include "../helpers/SettingsDbusQueries.h"

#include "pz_i18n.h"
#include "../../config/iconfigbackend.h"
#include "../../config/configmigration.h"
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
        return PzI18n::tr("Zone name cannot exceed 100 characters");
    }

    // Check for invalid characters (allow alphanumeric, spaces, hyphens, underscores)
    // But be lenient - allow most characters for internationalization
    // Only block characters that could break JSON or filenames
    QRegularExpression invalidChars(QStringLiteral("[<>\"'\\\\]"));
    QRegularExpressionMatch match = invalidChars.match(name);
    if (match.hasMatch()) {
        return PzI18n::tr("Zone name contains invalid characters: < > \" ' \\");
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
                    return PzI18n::tr("A zone with this name already exists");
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
        return PzI18n::tr("Zone number must be at least 1");
    }
    if (number > 99) {
        return PzI18n::tr("Zone number cannot exceed 99");
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
            return PzI18n::tr("Zone number %1 is already in use").arg(number);
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
    // Ensure INI→JSON migration has run (the daemon does this too, but the
    // editor may start before the daemon on first upgrade).
    PlasmaZones::ConfigMigration::ensureJsonConfig();

    auto backend = PlasmaZones::createDefaultConfigBackend();

    // Note: Per-layout zonePadding/outerGap overrides are loaded from the layout JSON
    // in loadLayout(). The global settings are cached here for performance (avoids D-Bus calls).
    refreshGlobalZonePadding();
    refreshGlobalOuterGap();
    refreshGlobalOverlayDisplayMode();

    // Load label font settings from global Appearance config (read-only in editor)
    {
        auto appearanceGroup = backend->group(QStringLiteral("Appearance"));
        m_labelFontFamily = appearanceGroup->readString(QStringLiteral("LabelFontFamily"));
        m_labelFontSizeScale =
            qBound(0.25, appearanceGroup->readDouble(QStringLiteral("LabelFontSizeScale"), 1.0), 3.0);
        m_labelFontWeight = appearanceGroup->readInt(QStringLiteral("LabelFontWeight"), 700);
        m_labelFontItalic = appearanceGroup->readBool(QStringLiteral("LabelFontItalic"), false);
        m_labelFontUnderline = appearanceGroup->readBool(QStringLiteral("LabelFontUnderline"), false);
        m_labelFontStrikeout = appearanceGroup->readBool(QStringLiteral("LabelFontStrikeout"), false);
    }

    auto editorGroup = backend->group(QStringLiteral("Editor"));

    // Load snapping settings (backward compatible with single SnapInterval)
    bool gridEnabled = editorGroup->readBool(QStringLiteral("GridSnappingEnabled"), true);
    bool edgeEnabled = editorGroup->readBool(QStringLiteral("EdgeSnappingEnabled"), true);

    // Try to load separate X and Y intervals, fall back to single interval for backward compatibility
    qreal snapIntX = editorGroup->readDouble(QStringLiteral("SnapIntervalX"), -1.0);
    qreal snapIntY = editorGroup->readDouble(QStringLiteral("SnapIntervalY"), -1.0);
    qreal snapInt = editorGroup->readDouble(QStringLiteral("SnapInterval"), EditorConstants::DefaultSnapInterval);

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
    loadShortcutSetting(*editorGroup, QStringLiteral("EditorDuplicateShortcut"), QStringLiteral("Ctrl+D"),
                        m_editorDuplicateShortcut, [this]() {
                            Q_EMIT editorDuplicateShortcutChanged();
                        });

    loadShortcutSetting(*editorGroup, QStringLiteral("EditorSplitHorizontalShortcut"), QStringLiteral("Ctrl+Shift+H"),
                        m_editorSplitHorizontalShortcut, [this]() {
                            Q_EMIT editorSplitHorizontalShortcutChanged();
                        });

    loadShortcutSetting(*editorGroup, QStringLiteral("EditorSplitVerticalShortcut"), QStringLiteral("Ctrl+Alt+V"),
                        m_editorSplitVerticalShortcut, [this]() {
                            Q_EMIT editorSplitVerticalShortcutChanged();
                        });

    loadShortcutSetting(*editorGroup, QStringLiteral("EditorFillShortcut"), QStringLiteral("Ctrl+Shift+F"),
                        m_editorFillShortcut, [this]() {
                            Q_EMIT editorFillShortcutChanged();
                        });

    // Load snap override modifier
    int snapOverrideMod = editorGroup->readInt(QStringLiteral("SnapOverrideModifier"), 0x02000000);
    if (m_snapOverrideModifier != snapOverrideMod) {
        m_snapOverrideModifier = snapOverrideMod;
        Q_EMIT snapOverrideModifierChanged();
    }

    // Load fill-on-drop settings
    bool fillOnDropEn = editorGroup->readBool(QStringLiteral("FillOnDropEnabled"), true);
    if (m_fillOnDropEnabled != fillOnDropEn) {
        m_fillOnDropEnabled = fillOnDropEn;
        Q_EMIT fillOnDropEnabledChanged();
    }

    int fillOnDropMod = editorGroup->readInt(QStringLiteral("FillOnDropModifier"), 0x04000000); // Default: Ctrl
    if (m_fillOnDropModifier != fillOnDropMod) {
        m_fillOnDropModifier = fillOnDropMod;
        Q_EMIT fillOnDropModifierChanged();
    }
}

void EditorController::saveEditorSettings()
{
    auto backend = PlasmaZones::createDefaultConfigBackend();
    auto editorGroup = backend->group(QStringLiteral("Editor"));

    // Save snapping settings
    editorGroup->writeBool(QStringLiteral("GridSnappingEnabled"), m_snappingService->gridSnappingEnabled());
    editorGroup->writeBool(QStringLiteral("EdgeSnappingEnabled"), m_snappingService->edgeSnappingEnabled());
    editorGroup->writeDouble(QStringLiteral("SnapIntervalX"), m_snappingService->snapIntervalX());
    editorGroup->writeDouble(QStringLiteral("SnapIntervalY"), m_snappingService->snapIntervalY());
    editorGroup->writeDouble(QStringLiteral("SnapInterval"), m_snappingService->snapIntervalX()); // Backward compat

    // Save app-specific keyboard shortcuts
    editorGroup->writeString(QStringLiteral("EditorDuplicateShortcut"), m_editorDuplicateShortcut);
    editorGroup->writeString(QStringLiteral("EditorSplitHorizontalShortcut"), m_editorSplitHorizontalShortcut);
    editorGroup->writeString(QStringLiteral("EditorSplitVerticalShortcut"), m_editorSplitVerticalShortcut);
    editorGroup->writeString(QStringLiteral("EditorFillShortcut"), m_editorFillShortcut);

    // Save snap override modifier
    editorGroup->writeInt(QStringLiteral("SnapOverrideModifier"), m_snapOverrideModifier);

    // Save fill-on-drop settings
    editorGroup->writeBool(QStringLiteral("FillOnDropEnabled"), m_fillOnDropEnabled);
    editorGroup->writeInt(QStringLiteral("FillOnDropModifier"), m_fillOnDropModifier);

    editorGroup.reset(); // release group before sync
    backend->sync();
}

} // namespace PlasmaZones

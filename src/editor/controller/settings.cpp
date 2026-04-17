// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../services/ZoneManager.h"
#include "../services/SnappingService.h"
#include "../../core/constants.h"
#include "../../core/logging.h"
#include "../helpers/SettingsDbusQueries.h"

#include "pz_i18n.h"
#include "../../config/configdefaults.h"
#include "../../config/configmigration.h"
#include "../../config/configbackends.h"
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
    // Single batched D-Bus round-trip for all 8 gap/overlay keys, not 8 sequential
    // getSetting() calls — this is on the ctor hot path before the QML engine starts.
    refreshGlobalGapOverlaySettings();

    // Load label font settings from global Snapping.Appearance.Labels config (read-only in editor)
    {
        auto labels = backend->group(ConfigDefaults::snappingAppearanceLabelsGroup());
        m_labelFontFamily = labels->readString(ConfigDefaults::fontFamilyKey());
        m_labelFontSizeScale =
            qBound(ConfigDefaults::labelFontSizeScaleMin(),
                   labels->readDouble(ConfigDefaults::fontSizeScaleKey(), ConfigDefaults::labelFontSizeScale()),
                   ConfigDefaults::labelFontSizeScaleMax());
        m_labelFontWeight = labels->readInt(ConfigDefaults::fontWeightKey(), ConfigDefaults::labelFontWeight());
        m_labelFontItalic = labels->readBool(ConfigDefaults::fontItalicKey(), ConfigDefaults::labelFontItalic());
        m_labelFontUnderline =
            labels->readBool(ConfigDefaults::fontUnderlineKey(), ConfigDefaults::labelFontUnderline());
        m_labelFontStrikeout =
            labels->readBool(ConfigDefaults::fontStrikeoutKey(), ConfigDefaults::labelFontStrikeout());
    }

    // Load editor snapping settings
    {
        auto editorSnapping = backend->group(ConfigDefaults::editorSnappingGroup());
        bool gridEnabled =
            editorSnapping->readBool(ConfigDefaults::gridEnabledKey(), ConfigDefaults::editorGridSnappingEnabled());
        bool edgeEnabled =
            editorSnapping->readBool(ConfigDefaults::edgeEnabledKey(), ConfigDefaults::editorEdgeSnappingEnabled());

        qreal snapIntX =
            editorSnapping->readDouble(ConfigDefaults::intervalXKey(), EditorConstants::DefaultSnapInterval);
        qreal snapIntY =
            editorSnapping->readDouble(ConfigDefaults::intervalYKey(), EditorConstants::DefaultSnapInterval);

        m_snappingService->setGridSnappingEnabled(gridEnabled);
        m_snappingService->setEdgeSnappingEnabled(edgeEnabled);
        m_snappingService->setSnapIntervalX(snapIntX);
        m_snappingService->setSnapIntervalY(snapIntY);

        int snapOverrideMod = editorSnapping->readInt(ConfigDefaults::overrideModifierKey(),
                                                      ConfigDefaults::editorSnapOverrideModifier());
        if (m_snapOverrideModifier != snapOverrideMod) {
            m_snapOverrideModifier = snapOverrideMod;
            Q_EMIT snapOverrideModifierChanged();
        }
    }

    // Load app-specific keyboard shortcuts
    {
        auto shortcuts = backend->group(ConfigDefaults::editorShortcutsGroup());
        loadShortcutSetting(*shortcuts, ConfigDefaults::duplicateKey(), ConfigDefaults::editorDuplicateShortcut(),
                            m_editorDuplicateShortcut, [this]() {
                                Q_EMIT editorDuplicateShortcutChanged();
                            });

        loadShortcutSetting(*shortcuts, ConfigDefaults::splitHorizontalKey(),
                            ConfigDefaults::editorSplitHorizontalShortcut(), m_editorSplitHorizontalShortcut, [this]() {
                                Q_EMIT editorSplitHorizontalShortcutChanged();
                            });

        loadShortcutSetting(*shortcuts, ConfigDefaults::splitVerticalKey(),
                            ConfigDefaults::editorSplitVerticalShortcut(), m_editorSplitVerticalShortcut, [this]() {
                                Q_EMIT editorSplitVerticalShortcutChanged();
                            });

        loadShortcutSetting(*shortcuts, ConfigDefaults::fillKey(), ConfigDefaults::editorFillShortcut(),
                            m_editorFillShortcut, [this]() {
                                Q_EMIT editorFillShortcutChanged();
                            });
    }

    // Load fill-on-drop settings
    {
        auto fillOnDrop = backend->group(ConfigDefaults::editorFillOnDropGroup());
        bool fillOnDropEn = fillOnDrop->readBool(ConfigDefaults::enabledKey(), ConfigDefaults::fillOnDropEnabled());
        if (m_fillOnDropEnabled != fillOnDropEn) {
            m_fillOnDropEnabled = fillOnDropEn;
            Q_EMIT fillOnDropEnabledChanged();
        }

        int fillOnDropMod = fillOnDrop->readInt(ConfigDefaults::modifierKey(), ConfigDefaults::fillOnDropModifier());
        if (m_fillOnDropModifier != fillOnDropMod) {
            m_fillOnDropModifier = fillOnDropMod;
            Q_EMIT fillOnDropModifierChanged();
        }
    }
}

void EditorController::saveEditorSettings()
{
    // Creates an ephemeral backend — reads from disk, writes one group, syncs.
    // If the daemon has unsaved in-memory changes to the same file, QSaveFile's
    // atomic rename prevents corruption but the daemon's next sync will overwrite
    // editor changes (and vice versa).  Proper fix requires IPC (D-Bus) for
    // cross-process settings writes; acceptable for now since the editor only
    // writes to the Editor group which the daemon doesn't modify at runtime.
    auto backend = PlasmaZones::createDefaultConfigBackend();

    // Save editor snapping settings
    {
        auto editorSnapping = backend->group(ConfigDefaults::editorSnappingGroup());
        editorSnapping->writeBool(ConfigDefaults::gridEnabledKey(), m_snappingService->gridSnappingEnabled());
        editorSnapping->writeBool(ConfigDefaults::edgeEnabledKey(), m_snappingService->edgeSnappingEnabled());
        editorSnapping->writeDouble(ConfigDefaults::intervalXKey(), m_snappingService->snapIntervalX());
        editorSnapping->writeDouble(ConfigDefaults::intervalYKey(), m_snappingService->snapIntervalY());
        editorSnapping->writeInt(ConfigDefaults::overrideModifierKey(), m_snapOverrideModifier);
    }

    // Save app-specific keyboard shortcuts
    {
        auto shortcuts = backend->group(ConfigDefaults::editorShortcutsGroup());
        shortcuts->writeString(ConfigDefaults::duplicateKey(), m_editorDuplicateShortcut);
        shortcuts->writeString(ConfigDefaults::splitHorizontalKey(), m_editorSplitHorizontalShortcut);
        shortcuts->writeString(ConfigDefaults::splitVerticalKey(), m_editorSplitVerticalShortcut);
        shortcuts->writeString(ConfigDefaults::fillKey(), m_editorFillShortcut);
    }

    // Save fill-on-drop settings
    {
        auto fillOnDrop = backend->group(ConfigDefaults::editorFillOnDropGroup());
        fillOnDrop->writeBool(ConfigDefaults::enabledKey(), m_fillOnDropEnabled);
        fillOnDrop->writeInt(ConfigDefaults::modifierKey(), m_fillOnDropModifier);
    }

    backend->sync();
}

} // namespace PlasmaZones

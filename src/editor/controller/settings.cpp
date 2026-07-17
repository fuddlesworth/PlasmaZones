// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../services/ZoneManager.h"
#include "../services/SnappingService.h"
#include "../../core/constants.h"
#include "../../core/logging.h"
#include "../helpers/SettingsDbusQueries.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include "phosphor_i18n.h"
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

    // Check maximum length (shared with the daemon's D-Bus boundary clamp)
    if (name.length() > MaxLayoutNameLength) {
        return PhosphorI18n::tr("Zone name cannot exceed %1 characters").arg(MaxLayoutNameLength);
    }

    // Check for invalid characters (allow alphanumeric, spaces, hyphens, underscores)
    // But be lenient - allow most characters for internationalization
    // Only block characters that could break JSON or filenames
    QRegularExpression invalidChars(QStringLiteral("[<>\"'\\\\]"));
    QRegularExpressionMatch match = invalidChars.match(name);
    if (match.hasMatch()) {
        return PhosphorI18n::tr("Zone name contains invalid characters: < > \" ' \\");
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
                    return PhosphorI18n::tr("A zone with this name already exists");
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
        return PhosphorI18n::tr("Zone number must be at least 1");
    }
    if (number > 99) {
        return PhosphorI18n::tr("Zone number cannot exceed 99");
    }

    // Check for duplicate numbers
    if (!m_zoneManager) {
        return QString(); // Can't check duplicates without manager
    }

    QVariantList zones = m_zoneManager->zones();
    for (const QVariant& zoneVar : zones) {
        QVariantMap zone = zoneVar.toMap();
        QString otherZoneId = zone[::PhosphorZones::ZoneJsonKeys::Id].toString();

        // Skip the zone being updated
        if (otherZoneId == zoneId) {
            continue;
        }

        int otherNumber = zone[::PhosphorZones::ZoneJsonKeys::ZoneNumber].toInt();
        if (otherNumber == number) {
            return PhosphorI18n::tr("Zone number %1 is already in use").arg(number);
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

    // Load label font settings from global Snapping.Zones.Labels config (read-only in editor)
    {
        auto labels = backend->group(ConfigDefaults::snappingZonesLabelsGroup());
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
    // Queue, don't write. Every caller is a property setter, and QML drives some
    // of them per mouse-move step (ControlBar binds snapIntervalX to
    // Slider.onMoved), so writing here put a whole-document rewrite, an fsync
    // and a full daemon config reparse on every tick of a drag. Restarting the
    // timer on each call collapses the burst into one write once the value
    // settles. setSnapInterval calls two setters for one logical change, which
    // now costs one write instead of two.
    m_editorSettingsSaveTimer.start();
}

void EditorController::flushEditorSettings()
{
    if (!writeEditorSettingsToDisk()) {
        return;
    }

    // Hand the daemon the new on-disk state. Async, and the reply is logged
    // rather than dropped: if the daemon is up but the reload errors, the editor
    // would otherwise proceed believing the two agree, and writeEditorSettings-
    // ToDisk's comment says what happens next — the daemon's following flush
    // silently reverts the user's settings. A missing daemon is a legitimate
    // no-op, so the failure is a log line and not a user-facing error.
    //
    // Not synchronous. The reason that used to be given here does not hold: the
    // notification's only consumer is the daemon and nothing in this process is
    // ordered against the reply (unlike the KCM, which clears a guard on it —
    // see reloadDaemonSettingsBlocking). Blocking never closed the clobber
    // window either, since the daemon can flush between the write and the
    // reparse whether or not this call waits. All it bought was up to 500 ms of
    // frozen UI on the settings path.
    PhosphorProtocol::ClientHelpers::reloadDaemonSettings(this, QStringLiteral("editor settings reload"));
}

void EditorController::flushEditorSettingsBlocking()
{
    if (!writeEditorSettingsToDisk()) {
        return;
    }

    // Teardown takes the blocking form. reloadDaemonSettings parents its reply
    // watcher to `this`, and the only caller here is ~EditorController — the
    // watcher would be a child of a half-destroyed object whose connection dies
    // with it, and the process may exit before the message is even written to
    // the bus. The reload would be silently lost, and the daemon would then
    // revert the settings the user changed on their way out. One bounded call
    // at exit buys delivery.
    PhosphorProtocol::ClientHelpers::reloadDaemonSettingsBlocking();
}

bool EditorController::writeEditorSettingsToDisk()
{
    // Write-in-process-then-reload, the same contract the settings app follows
    // (see src/settings/dbusutils.h — nothing in the tree writes a setting over
    // D-Bus). The ephemeral backend reads config.json fresh off disk, applies
    // the Editor.* groups below and flushes; the reload at the bottom then makes
    // the daemon reparse.
    //
    // The reload is load-bearing, not a courtesy. JsonBackend::sync rewrites the
    // WHOLE document from its in-memory root, and the daemon's root is only
    // refreshed by a reparse. Without the reload the daemon would keep the
    // pre-write snapshot and its next flush — any setting change, from any
    // source — would put the stale Editor.* values back, silently reverting the
    // editor's settings some arbitrary time later. Group ownership does not
    // protect against that: the clobber is whole-file, so it does not matter
    // that the daemon never edits Editor.* itself.
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

    if (!backend->sync()) {
        // Nothing reached disk, so there is nothing for the daemon to pick up
        // and its snapshot is still the truth. Reporting failure keeps the
        // callers from asking the daemon to reparse a file that never changed.
        qCWarning(lcEditor) << "Failed to write editor settings to" << ConfigDefaults::configFilePath();
        return false;
    }
    return true;
}

} // namespace PlasmaZones

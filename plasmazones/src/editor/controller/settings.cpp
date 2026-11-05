// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../services/ZoneManager.h"
#include "../services/SnappingService.h"
#include "core/types/constants.h"
#include "core/platform/logging.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ScrollAxisEnum.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorScreens/ScreenIdentity.h>

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
    static const QRegularExpression invalidChars(QStringLiteral("[<>\"'\\\\]"));
    const QRegularExpressionMatch match = invalidChars.match(name);
    if (match.hasMatch()) {
        return PhosphorI18n::tr("Zone name contains invalid characters: < > \" ' \\");
    }

    // Check for duplicate names (excluding the current zone)
    if (m_zoneManager) {
        QVariantList zones = m_zoneManager->zones();
        for (const QVariant& zoneVar : zones) {
            QVariantMap zone = zoneVar.toMap();
            QString otherZoneId = zone.value(::PhosphorZones::ZoneJsonKeys::Id).toString();
            if (otherZoneId != zoneId) {
                QString otherName = zone.value(::PhosphorZones::ZoneJsonKeys::Name).toString();
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
    // editor may start before the daemon on first upgrade). main() set this
    // process to leave an unparseable config.json in place, so a broken file
    // stays where the backend's write refusal protects it.
    PlasmaZones::ConfigMigration::ensureJsonConfig();

    auto backend = PlasmaZones::createDefaultConfigBackend();

    // Note: Per-layout zonePadding/outerGap overrides are loaded from the layout JSON
    // in loadLayout(). The global settings are cached here for performance (avoids D-Bus calls).
    // Single batched D-Bus round-trip for all 8 gap/overlay keys, not 8 sequential
    // getSetting() calls — this is on the ctor hot path before the QML engine starts.
    refreshGlobalGapOverlaySettings();

    // An unreadable config.json reads as all defaults, which is also what the
    // members start at, so the reads below still give the editor a usable
    // state. The backend refuses to write it until it is repaired, and the
    // warning it logged names the file.

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

    applyEditorOwnedSettings(*backend);

    // Strip axis, read ONLY (the editor never writes a scrolling setting).
    // Snapshotted from the same backend, and RE-snapshotted while the editor
    // runs: reloadLiveConfig below is wired to the daemon's settingsChanged
    // signal, so an axis authored in the settings app while the editor is
    // open reaches the preview without a restart.
    refreshScrollingStripAxisSnapshot(*backend);
}

void EditorController::applyEditorOwnedSettings(PhosphorConfig::IBackend& backend)
{
    static_assert(DefaultBackgroundOpacity == ConfigDefaults::editorBackgroundOpacity(),
                  "EditorController::m_backgroundOpacity starts at a literal to keep configdefaults.h out of "
                  "EditorController.h; that literal has to stay the configured default.");

    QStringList invalidShortcuts;
    const EditorOwnedConfig config = EditorOwnedConfig::read(backend, &invalidShortcuts);
    // Warn once per broken entry, not on every live reload, and again only if
    // it is repaired and then broken anew.
    for (const QString& key : std::as_const(invalidShortcuts)) {
        if (!m_warnedInvalidShortcuts.contains(key)) {
            qCWarning(lcEditor) << "Invalid editor shortcut" << key << "in" << ConfigDefaults::configFilePath()
                                << "- using the default";
        }
    }
    m_warnedInvalidShortcuts = invalidShortcuts;

    // A key edited here and not yet written keeps the local value: the file
    // still holds the older one, and adopting it would undo the edit before
    // its write lands. Every other key follows the file, so a settings-app
    // change to them is picked up even while one of ours is pending.
    using Key = EditorOwnedConfig::Key;
    const auto adopt = [this](Key key) {
        return !m_editorKeysEdited.testFlag(key);
    };
    // The service setters gate their own signals on a change.
    if (adopt(Key::GridSnapping)) {
        m_snappingService->setGridSnappingEnabled(config.gridSnappingEnabled);
    }
    if (adopt(Key::EdgeSnapping)) {
        m_snappingService->setEdgeSnappingEnabled(config.edgeSnappingEnabled);
    }
    if (adopt(Key::SnapIntervalX)) {
        m_snappingService->setSnapIntervalX(config.snapIntervalX);
    }
    if (adopt(Key::SnapIntervalY)) {
        m_snappingService->setSnapIntervalY(config.snapIntervalY);
    }
    // The rest are plain members announced here, only on a change.
    const auto adoptMember = [&adopt](Key key, auto& member, const auto& value, auto emitChanged) {
        if (adopt(key) && member != value) {
            member = value;
            emitChanged();
        }
    };
    adoptMember(Key::SnapOverrideModifier, m_snapOverrideModifier, config.snapOverrideModifier, [this]() {
        Q_EMIT snapOverrideModifierChanged();
    });
    adoptMember(Key::DuplicateShortcut, m_editorDuplicateShortcut, config.duplicateShortcut, [this]() {
        Q_EMIT editorDuplicateShortcutChanged();
    });
    adoptMember(Key::SplitHorizontalShortcut, m_editorSplitHorizontalShortcut, config.splitHorizontalShortcut,
                [this]() {
                    Q_EMIT editorSplitHorizontalShortcutChanged();
                });
    adoptMember(Key::SplitVerticalShortcut, m_editorSplitVerticalShortcut, config.splitVerticalShortcut, [this]() {
        Q_EMIT editorSplitVerticalShortcutChanged();
    });
    adoptMember(Key::FillShortcut, m_editorFillShortcut, config.fillShortcut, [this]() {
        Q_EMIT editorFillShortcutChanged();
    });
    adoptMember(Key::FillOnDropEnabled, m_fillOnDropEnabled, config.fillOnDropEnabled, [this]() {
        Q_EMIT fillOnDropEnabledChanged();
    });
    adoptMember(Key::FillOnDropModifier, m_fillOnDropModifier, config.fillOnDropModifier, [this]() {
        Q_EMIT fillOnDropModifierChanged();
    });
    // The schema already clamped it, so this compares like for like.
    if (adopt(Key::BackgroundOpacity) && !qFuzzyCompare(1.0 + m_backgroundOpacity, 1.0 + config.backgroundOpacity)) {
        m_backgroundOpacity = config.backgroundOpacity;
        Q_EMIT backgroundOpacityChanged();
    }
}

void EditorController::refreshScrollingStripAxisSnapshot(PhosphorConfig::IBackend& backend)
{
    // Both the global value and every per-screen override are snapshotted,
    // because the target screen changes without a settings reload and
    // templatePreviewVertical() has to answer for whichever screen is current.
    static_assert(ConfigDefaults::scrollingStripAxisAuto() == 0,
                  "EditorController::m_scrollingStripAxis defaults to the literal 0 to keep configdefaults.h out "
                  "of EditorController.h; that literal has to stay Auto.");

    auto scrolling = backend.group(ConfigDefaults::scrollingGroup());
    const int axis = scrolling->readInt(ConfigDefaults::stripAxisKey(), ConfigDefaults::scrollingStripAxis());
    bool axisChanged = false;
    if (m_scrollingStripAxis != axis) {
        m_scrollingStripAxis = axis;
        axisChanged = true;
    }

    // Per-screen groups are "<prefix><screen id or name>"; only the ones
    // that actually carry the key are recorded, so an absent screen falls
    // through to the global value rather than to a stale default.
    const QString prefix = ConfigDefaults::scrollingScreenGroupPrefix();
    QHash<QString, int> perScreen;
    const QStringList groups = backend.groupList();
    for (const QString& groupName : groups) {
        if (!groupName.startsWith(prefix))
            continue;
        const QString screenKey = groupName.mid(prefix.size());
        if (screenKey.isEmpty())
            continue;
        auto screenGroup = backend.group(groupName);
        if (!screenGroup->hasKey(ConfigDefaults::stripAxisKey()))
            continue;
        perScreen.insert(screenKey,
                         screenGroup->readInt(ConfigDefaults::stripAxisKey(), ConfigDefaults::scrollingStripAxis()));
    }
    if (m_perScreenStripAxis != perScreen) {
        m_perScreenStripAxis = perScreen;
        axisChanged = true;
    }

    // Through the refresh, not a bare emit: a config-input change does not
    // have to change the ANSWER (a per-screen override for some other
    // monitor, an Auto value that resolves the same way), and the property
    // must only announce a real flip.
    if (axisChanged)
        refreshTemplatePreviewVertical();
}

void EditorController::reloadLiveConfig()
{
    // The debounced settingsChanged / editorSettingsChanged arm: re-read from a
    // fresh backend (the daemon just rewrote the file) and let each snapshot's
    // own change gates decide whether anything is announced.
    auto backend = PlasmaZones::createDefaultConfigBackend();
    // A file broken mid-session reads as all defaults. Adopting that would
    // reset every editor setting and the strip axis for what is only a bad
    // edit, so keep the current state until the file reads cleanly again.
    if (backend->loadFailed()) {
        return;
    }
    refreshScrollingStripAxisSnapshot(*backend);
    // The Editor.* groups too, so a change made on the settings app's Editor
    // page while the editor is open shows up here. applyEditorOwnedSettings
    // keeps any key edited here and not yet written, and the write that
    // follows carries only those keys, so the editor never writes back a key
    // the settings app changed. The same key changed on both sides goes to
    // whichever write lands last. (The settings app's own Save still rewrites
    // the whole file from its copy; it adopts the editor's writes as their
    // reload reaches it, so only a write landing just before its Save is
    // lost. See SettingsController::onExternalSettingsChanged.)
    applyEditorOwnedSettings(*backend);

    // A Refused or abandoned write leaves its keys edited with nothing to
    // retry it. A reload that reads the file cleanly is the moment it can
    // land again, so restart the write from here rather than leave it for
    // whenever the user next changes something.
    if (m_editorKeysEdited && !m_editorSettingsSaveTimer.isActive()) {
        m_editorSettingsWriteFailures = 0;
        m_editorSettingsSaveTimer.start(EditorSettingsSaveDebounceMs);
    }
}

void EditorController::refreshTemplatePreviewVertical()
{
    // The ONE writer of the NOTIFY comparand: the getter is a pure read, so a
    // binding evaluating between an input change and this refresh cannot
    // consume the flip and suppress the emit for every other consumer.
    const bool vertical = resolveTemplatePreviewVertical();
    if (m_templatePreviewVertical != vertical) {
        m_templatePreviewVertical = vertical;
        Q_EMIT templatePreviewVerticalChanged();
    }
}

bool EditorController::templatePreviewVertical() const
{
    return resolveTemplatePreviewVertical();
}

bool EditorController::resolveTemplatePreviewVertical() const
{
    // Per-screen override first, then the global setting, then the Auto rule
    // against the target screen's size — the settings ladder the engine walks
    // (a SetScrollStripAxis rule can still outrank all three on the live
    // strip; the editor has no rule-resolution seam, and the settings app's
    // preview shares that documented limit).
    //
    // The screen key is matched against EVERY config spelling of the target:
    // ScreenIdentity::variantsFor answers the id and its connector-name
    // alternate (input first), which are the two forms per-screen groups are
    // stored under. The old fallback matched screenDisplayName, a decorated
    // human label ("DP-2 — Vendor Model") that is never a stored key on a
    // monitor with EDID metadata.
    int configured = m_scrollingStripAxis;
    const QStringList spellings = PhosphorScreens::ScreenIdentity::variantsFor(m_targetScreen);
    for (const QString& spelling : spellings) {
        const auto it = m_perScreenStripAxis.constFind(spelling);
        if (it != m_perScreenStripAxis.constEnd()) {
            configured = it.value();
            break;
        }
    }

    // Auto (and any out-of-range value a hand-edited config could carry) falls
    // through to the size rule.
    if (configured == ConfigDefaults::scrollingStripAxisVertical()) {
        return true;
    }
    if (configured != ConfigDefaults::scrollingStripAxisHorizontal()) {
        const QSize size = targetScreenSize();
        return PhosphorProtocol::autoScrollAxisFor(size.width(), size.height())
            == PhosphorProtocol::ScrollAxis::Vertical;
    }
    return false;
}

void EditorController::saveEditorSettings(EditorOwnedConfig::Keys changed)
{
    // Queue, don't write. Every caller is a property setter, and QML drives some
    // of them per mouse-move step (ControlBar binds the grid intervals and the
    // background opacity to Slider.onMoved), so writing here put a
    // whole-document rewrite, an fsync and a full daemon config reparse on
    // every tick of a drag. Restarting the timer on each call collapses the
    // burst into one write once the value settles. setSnapInterval calls two
    // setters for one logical change, which costs one write instead of two.
    m_editorKeysEdited |= changed;
    m_editorSettingsWriteFailures = 0;
    m_editorSettingsSaveTimer.start(EditorSettingsSaveDebounceMs);
}

void EditorController::flushEditorSettings()
{
    switch (writeEditorSettingsToDisk()) {
    case EditorSettingsWrite::Nothing:
        return;
    case EditorSettingsWrite::Refused:
        // Not transient: the file stays unreadable until someone repairs it,
        // so retrying on a timer would only repeat the backend's warning. The
        // edits stay queued, and the next change, the next live reload that
        // reads the file cleanly, or closing the editor tries again.
        return;
    case EditorSettingsWrite::Failed:
        // Possibly transient (a full disk, a rename race), so retry a few
        // times with a growing delay. After that the edits stay queued for
        // the next change, the next live reload, or closing the editor,
        // rather than looping on a disk that keeps failing.
        if (++m_editorSettingsWriteFailures <= MaxEditorSettingsWriteRetries) {
            m_editorSettingsSaveTimer.start(EditorSettingsSaveDebounceMs << m_editorSettingsWriteFailures);
        } else {
            qCWarning(lcEditor) << "Giving up retrying the editor settings write; the changes are kept and the next "
                                   "change, settings reload or closing the editor tries again";
        }
        return;
    case EditorSettingsWrite::Written:
        break;
    }
    m_editorSettingsWriteFailures = 0;

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
    if (writeEditorSettingsToDisk() != EditorSettingsWrite::Written) {
        return;
    }

    // Teardown takes the blocking form. reloadDaemonSettings parents its reply
    // watcher to `this`, which is about to be destroyed, and the process may
    // exit before the message is even written to the bus. The reload would be
    // silently lost, and the daemon would then revert the settings the user
    // changed on their way out. One bounded call at exit buys delivery.
    PhosphorProtocol::ClientHelpers::reloadDaemonSettingsBlocking();
}

EditorOwnedConfig EditorController::currentEditorOwnedConfig() const
{
    EditorOwnedConfig c;
    c.gridSnappingEnabled = m_snappingService->gridSnappingEnabled();
    c.edgeSnappingEnabled = m_snappingService->edgeSnappingEnabled();
    c.snapIntervalX = m_snappingService->snapIntervalX();
    c.snapIntervalY = m_snappingService->snapIntervalY();
    c.snapOverrideModifier = m_snapOverrideModifier;
    c.duplicateShortcut = m_editorDuplicateShortcut;
    c.splitHorizontalShortcut = m_editorSplitHorizontalShortcut;
    c.splitVerticalShortcut = m_editorSplitVerticalShortcut;
    c.fillShortcut = m_editorFillShortcut;
    c.fillOnDropEnabled = m_fillOnDropEnabled;
    c.fillOnDropModifier = m_fillOnDropModifier;
    c.backgroundOpacity = m_backgroundOpacity;
    return c;
}

EditorController::EditorSettingsWrite EditorController::writeEditorSettingsToDisk()
{
    if (!m_editorKeysEdited) {
        // Nothing edited here, so nothing to write and nothing for the daemon
        // to reparse.
        return EditorSettingsWrite::Nothing;
    }

    // Write-in-process-then-reload, the same contract the settings app follows
    // (see src/settings/dbusutils.h — nothing in the tree writes a setting over
    // D-Bus). The ephemeral backend reads config.json fresh off disk, applies
    // the keys edited here and flushes; the caller's reload then makes the
    // daemon reparse. Writing only the edited keys onto what disk holds now is
    // what keeps a settings-app change to any other Editor.* key intact.
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
    if (backend->loadFailed()) {
        // The backend would refuse the sync anyway. Saying so here keeps the
        // refusal from reading as a transient failure worth retrying. The
        // backend's own warning named the file; this one says what waits.
        qCWarning(lcEditor) << "Editor settings not written: the config file cannot be read. The change is kept "
                               "until the file reads cleanly or the editor closes";
        return EditorSettingsWrite::Refused;
    }

    currentEditorOwnedConfig().write(*backend, m_editorKeysEdited);

    if (!backend->sync()) {
        // Nothing reached disk, so there is nothing for the daemon to pick up
        // and its snapshot is still the truth. Drop the pending write from the
        // backend before it goes out of scope: its destructor would otherwise
        // retry the flush, and a write landing there would never be followed
        // by the daemon reload the caller skips on this result.
        backend->reparseConfiguration();
        qCWarning(lcEditor) << "Failed to write editor settings to" << ConfigDefaults::configFilePath();
        return EditorSettingsWrite::Failed;
    }
    m_editorKeysEdited = {};
    return EditorSettingsWrite::Written;
}

} // namespace PlasmaZones

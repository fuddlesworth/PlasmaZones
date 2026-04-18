// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shortcutmanager.h"

#include "../config/configdefaults.h"
#include "../config/settings.h"
#include "../core/logging.h"
#include "pz_i18n.h"

#include <PhosphorShortcuts/Factory.h>
#include <PhosphorShortcuts/IBackend.h>
#include <PhosphorShortcuts/Registry.h>

namespace PlasmaZones {

namespace {

// Stable string ids are documented contract: they appear in
// ~/.config/kglobalshortcutsrc under the "plasmazonesd" component and in
// XDG Portal settings UIs. Changing one is an on-disk rename that users pay
// for, so add new ones at the bottom; never rename existing.
constexpr auto kIdOpenEditor = "open_editor";
constexpr auto kIdOpenSettings = "open_settings";
constexpr auto kIdPreviousLayout = "previous_layout";
constexpr auto kIdNextLayout = "next_layout";
constexpr auto kIdMoveWindowLeft = "move_window_left";
constexpr auto kIdMoveWindowRight = "move_window_right";
constexpr auto kIdMoveWindowUp = "move_window_up";
constexpr auto kIdMoveWindowDown = "move_window_down";
constexpr auto kIdFocusZoneLeft = "focus_zone_left";
constexpr auto kIdFocusZoneRight = "focus_zone_right";
constexpr auto kIdFocusZoneUp = "focus_zone_up";
constexpr auto kIdFocusZoneDown = "focus_zone_down";
constexpr auto kIdPushToEmptyZone = "push_to_empty_zone";
constexpr auto kIdRestoreWindowSize = "restore_window_size";
constexpr auto kIdToggleWindowFloat = "toggle_window_float";
constexpr auto kIdSwapWindowLeft = "swap_window_left";
constexpr auto kIdSwapWindowRight = "swap_window_right";
constexpr auto kIdSwapWindowUp = "swap_window_up";
constexpr auto kIdSwapWindowDown = "swap_window_down";
constexpr auto kIdSwapVirtualScreenLeft = "swap_virtual_screen_left";
constexpr auto kIdSwapVirtualScreenRight = "swap_virtual_screen_right";
constexpr auto kIdSwapVirtualScreenUp = "swap_virtual_screen_up";
constexpr auto kIdSwapVirtualScreenDown = "swap_virtual_screen_down";
constexpr auto kIdRotateVirtualScreensCW = "rotate_virtual_screens_clockwise";
constexpr auto kIdRotateVirtualScreensCCW = "rotate_virtual_screens_counterclockwise";
constexpr auto kIdRotateWindowsCW = "rotate_windows_clockwise";
constexpr auto kIdRotateWindowsCCW = "rotate_windows_counterclockwise";
constexpr auto kIdCycleWindowForward = "cycle_window_forward";
constexpr auto kIdCycleWindowBackward = "cycle_window_backward";
constexpr auto kIdResnapToNewLayout = "resnap_to_new_layout";
constexpr auto kIdSnapAllWindows = "snap_all_windows";
constexpr auto kIdLayoutPicker = "layout_picker";
constexpr auto kIdToggleLayoutLock = "toggle_layout_lock";
constexpr auto kIdToggleAutotile = "toggle_autotile";
constexpr auto kIdFocusMaster = "focus_master";
constexpr auto kIdSwapMaster = "swap_master";
constexpr auto kIdIncreaseMasterRatio = "increase_master_ratio";
constexpr auto kIdDecreaseMasterRatio = "decrease_master_ratio";
constexpr auto kIdIncreaseMasterCount = "increase_master_count";
constexpr auto kIdDecreaseMasterCount = "decrease_master_count";
constexpr auto kIdRetile = "retile";

QString quickLayoutId(int slotZeroBased)
{
    return QStringLiteral("quick_layout_%1").arg(slotZeroBased + 1);
}

QString snapToZoneId(int slotZeroBased)
{
    return QStringLiteral("snap_to_zone_%1").arg(slotZeroBased + 1);
}

// QKeySequence(QString) silently returns an empty sequence on malformed
// input. Wrap with a warning log so a typo in the config surfaces from the
// logs instead of silently disabling a shortcut.
QKeySequence parseSequence(const QString& raw, const QString& contextId)
{
    if (raw.isEmpty()) {
        return {};
    }
    QKeySequence seq(raw);
    if (seq.isEmpty()) {
        qCWarning(lcShortcuts) << "Failed to parse shortcut sequence for" << contextId << ":" << raw;
    }
    return seq;
}

} // namespace

ShortcutManager::ShortcutManager(Settings* settings, LayoutManager* layoutManager, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_layoutManager(layoutManager)
    // unique_ptr owns lifetime — DON'T also pass `this` as Qt parent, or the
    // backend/registry end up with two owners (works today because Qt auto-
    // drops children as they're destroyed, but violates project style).
    , m_backend(Phosphor::Shortcuts::createBackend(Phosphor::Shortcuts::BackendHint::Auto, nullptr))
    , m_registry(std::make_unique<Phosphor::Shortcuts::Registry>(m_backend.get(), nullptr))
{
    Q_ASSERT(settings);
    Q_ASSERT(layoutManager);

    // Settings::settingsChanged() fires both on individual setter emits AND
    // on bulk load (KCM reload). A single connect handles both cases; the
    // Registry internally no-ops rebinds that don't change the sequence, so
    // there's no cost to the full-table refresh pattern.
    connect(m_settings, &Settings::settingsChanged, this, &ShortcutManager::updateShortcuts);
}

ShortcutManager::~ShortcutManager() = default;

void ShortcutManager::registerShortcuts()
{
    if (m_registrationInProgress) {
        qCWarning(lcShortcuts) << "registerShortcuts() called re-entrantly — ignoring";
        return;
    }
    m_registrationInProgress = true;

    buildEntries();

    for (const auto& e : std::as_const(m_entries)) {
        m_registry->bind(e.id, e.defaultSeq, e.description, e.fire);
        // Apply current sequence from settings after initial bind so the
        // first flush reflects the user's saved preference, not only the
        // compiled-in default.
        m_registry->rebind(e.id, e.currentSeq());
    }

    connect(
        m_registry.get(), &Phosphor::Shortcuts::Registry::ready, this,
        [this] {
            m_registrationInProgress = false;
            qCInfo(lcShortcuts) << "Registered" << m_entries.size() << "shortcuts";
            Q_EMIT shortcutsRegistered();
            if (m_settingsDirty) {
                m_settingsDirty = false;
                updateShortcuts();
            }
        },
        Qt::SingleShotConnection);

    m_registry->flush();
}

void ShortcutManager::updateShortcuts()
{
    if (m_registrationInProgress) {
        // Defer — the ready() callback above will call us again.
        m_settingsDirty = true;
        return;
    }
    if (m_entries.isEmpty()) {
        return;
    }
    rebindAll();
    m_registry->flush();
}

void ShortcutManager::unregisterShortcuts()
{
    m_registrationInProgress = false;
    m_settingsDirty = false;
    for (const auto& e : std::as_const(m_entries)) {
        m_registry->unbind(e.id);
    }
    m_entries.clear();
}

void ShortcutManager::registerAdhocShortcut(const QString& id, const QKeySequence& sequence, const QString& description,
                                            std::function<void()> callback)
{
    if (!m_registry) {
        return;
    }
    // Adhoc registration during the initial settings-driven batch would race
    // the batched BindShortcuts on the Portal backend (the per-batch Request
    // subscription gets torn down mid-flight when the adhoc flush fires).
    // All adhoc callers today register post-init in response to user actions
    // (drag start); crash here instead of letting the race manifest as a
    // silent grab-missing bug in production.
    Q_ASSERT_X(!m_registrationInProgress, "ShortcutManager::registerAdhocShortcut",
               "must not be called during initial shortcut registration");
    m_registry->bind(id, sequence, description, std::move(callback), /*persistent=*/false);
    // If the consumer ever passes a different sequence for the same id
    // after a prior register, bind() preserves currentSeq per contract,
    // so apply the requested sequence explicitly via rebind().
    m_registry->rebind(id, sequence);
    m_registry->flush();
}

void ShortcutManager::unregisterAdhocShortcut(const QString& id)
{
    if (!m_registry) {
        return;
    }
    m_registry->unbind(id);
    m_registry->flush();
}

void ShortcutManager::rebindAll()
{
    for (const auto& e : std::as_const(m_entries)) {
        m_registry->rebind(e.id, e.currentSeq());
    }
}

void ShortcutManager::buildEntries()
{
    m_entries.clear();
    m_entries.reserve(64);

    // Helper: build one entry with getter plumbing.
    // defGetter is a plain static free function (ConfigDefaults::xxx are static).
    // curGetter is a Settings member function (live config value).
    auto add = [this](const char* id, QString (*defGetter)(), QString (Settings::*curGetter)() const,
                      const char* i18nText, std::function<void()> fire) {
        Entry e;
        e.id = QString::fromLatin1(id);
        e.defaultSeq = parseSequence(defGetter(), e.id);
        e.description = PzI18n::tr(i18nText);
        Settings* s = m_settings;
        const QString idCopy = e.id;
        e.currentSeq = [s, curGetter, idCopy] {
            return parseSequence((s->*curGetter)(), idCopy);
        };
        e.fire = std::move(fire);
        m_entries.push_back(std::move(e));
    };

    // ─── Core ──────────────────────────────────────────────────────────────
    add(kIdOpenEditor, &ConfigDefaults::openEditorShortcut, &Settings::openEditorShortcut, "Open Zone Editor", [this] {
        Q_EMIT openEditorRequested();
    });
    add(kIdOpenSettings, &ConfigDefaults::openSettingsShortcut, &Settings::openSettingsShortcut, "Open Settings",
        [this] {
            Q_EMIT openSettingsRequested();
        });
    add(kIdPreviousLayout, &ConfigDefaults::previousLayoutShortcut, &Settings::previousLayoutShortcut,
        "Previous Layout", [this] {
            Q_EMIT previousLayoutRequested();
        });
    add(kIdNextLayout, &ConfigDefaults::nextLayoutShortcut, &Settings::nextLayoutShortcut, "Next Layout", [this] {
        Q_EMIT nextLayoutRequested();
    });

    // ─── Quick layout slots 1–9 (indexed getters) ──────────────────────────
    const QString quickDefaults[9] = {
        ConfigDefaults::quickLayout1Shortcut(), ConfigDefaults::quickLayout2Shortcut(),
        ConfigDefaults::quickLayout3Shortcut(), ConfigDefaults::quickLayout4Shortcut(),
        ConfigDefaults::quickLayout5Shortcut(), ConfigDefaults::quickLayout6Shortcut(),
        ConfigDefaults::quickLayout7Shortcut(), ConfigDefaults::quickLayout8Shortcut(),
        ConfigDefaults::quickLayout9Shortcut(),
    };
    for (int i = 0; i < 9; ++i) {
        Entry e;
        e.id = quickLayoutId(i);
        e.defaultSeq = parseSequence(quickDefaults[i], e.id);
        e.description = PzI18n::tr("Apply Layout %1").arg(i + 1);
        Settings* s = m_settings;
        const QString idCopy = e.id;
        e.currentSeq = [s, i, idCopy] {
            return parseSequence(s->quickLayoutShortcut(i), idCopy);
        };
        const int slot = i + 1;
        e.fire = [this, slot] {
            Q_EMIT quickLayoutRequested(slot);
        };
        m_entries.push_back(std::move(e));
    }

    // ─── Move window ───────────────────────────────────────────────────────
    add(kIdMoveWindowLeft, &ConfigDefaults::moveWindowLeftShortcut, &Settings::moveWindowLeftShortcut,
        "Move Window Left", [this] {
            Q_EMIT moveWindowRequested(NavigationDirection::Left);
        });
    add(kIdMoveWindowRight, &ConfigDefaults::moveWindowRightShortcut, &Settings::moveWindowRightShortcut,
        "Move Window Right", [this] {
            Q_EMIT moveWindowRequested(NavigationDirection::Right);
        });
    add(kIdMoveWindowUp, &ConfigDefaults::moveWindowUpShortcut, &Settings::moveWindowUpShortcut, "Move Window Up",
        [this] {
            Q_EMIT moveWindowRequested(NavigationDirection::Up);
        });
    add(kIdMoveWindowDown, &ConfigDefaults::moveWindowDownShortcut, &Settings::moveWindowDownShortcut,
        "Move Window Down", [this] {
            Q_EMIT moveWindowRequested(NavigationDirection::Down);
        });

    // ─── Focus zone ────────────────────────────────────────────────────────
    add(kIdFocusZoneLeft, &ConfigDefaults::focusZoneLeftShortcut, &Settings::focusZoneLeftShortcut, "Focus Zone Left",
        [this] {
            Q_EMIT focusZoneRequested(NavigationDirection::Left);
        });
    add(kIdFocusZoneRight, &ConfigDefaults::focusZoneRightShortcut, &Settings::focusZoneRightShortcut,
        "Focus Zone Right", [this] {
            Q_EMIT focusZoneRequested(NavigationDirection::Right);
        });
    add(kIdFocusZoneUp, &ConfigDefaults::focusZoneUpShortcut, &Settings::focusZoneUpShortcut, "Focus Zone Up", [this] {
        Q_EMIT focusZoneRequested(NavigationDirection::Up);
    });
    add(kIdFocusZoneDown, &ConfigDefaults::focusZoneDownShortcut, &Settings::focusZoneDownShortcut, "Focus Zone Down",
        [this] {
            Q_EMIT focusZoneRequested(NavigationDirection::Down);
        });

    // ─── Non-directional navigation ────────────────────────────────────────
    add(kIdPushToEmptyZone, &ConfigDefaults::pushToEmptyZoneShortcut, &Settings::pushToEmptyZoneShortcut,
        "Move Window to Empty Zone", [this] {
            Q_EMIT pushToEmptyZoneRequested();
        });
    add(kIdRestoreWindowSize, &ConfigDefaults::restoreWindowSizeShortcut, &Settings::restoreWindowSizeShortcut,
        "Restore Window Size", [this] {
            Q_EMIT restoreWindowSizeRequested();
        });
    add(kIdToggleWindowFloat, &ConfigDefaults::toggleWindowFloatShortcut, &Settings::toggleWindowFloatShortcut,
        "Toggle Window Floating", [this] {
            Q_EMIT toggleWindowFloatRequested();
        });

    // ─── Swap window ───────────────────────────────────────────────────────
    add(kIdSwapWindowLeft, &ConfigDefaults::swapWindowLeftShortcut, &Settings::swapWindowLeftShortcut,
        "Swap Window Left", [this] {
            Q_EMIT swapWindowRequested(NavigationDirection::Left);
        });
    add(kIdSwapWindowRight, &ConfigDefaults::swapWindowRightShortcut, &Settings::swapWindowRightShortcut,
        "Swap Window Right", [this] {
            Q_EMIT swapWindowRequested(NavigationDirection::Right);
        });
    add(kIdSwapWindowUp, &ConfigDefaults::swapWindowUpShortcut, &Settings::swapWindowUpShortcut, "Swap Window Up",
        [this] {
            Q_EMIT swapWindowRequested(NavigationDirection::Up);
        });
    add(kIdSwapWindowDown, &ConfigDefaults::swapWindowDownShortcut, &Settings::swapWindowDownShortcut,
        "Swap Window Down", [this] {
            Q_EMIT swapWindowRequested(NavigationDirection::Down);
        });

    // ─── Swap virtual screen ───────────────────────────────────────────────
    add(kIdSwapVirtualScreenLeft, &ConfigDefaults::swapVirtualScreenLeftShortcut,
        &Settings::swapVirtualScreenLeftShortcut, "Swap Virtual Screen Left", [this] {
            Q_EMIT swapVirtualScreenRequested(NavigationDirection::Left);
        });
    add(kIdSwapVirtualScreenRight, &ConfigDefaults::swapVirtualScreenRightShortcut,
        &Settings::swapVirtualScreenRightShortcut, "Swap Virtual Screen Right", [this] {
            Q_EMIT swapVirtualScreenRequested(NavigationDirection::Right);
        });
    add(kIdSwapVirtualScreenUp, &ConfigDefaults::swapVirtualScreenUpShortcut, &Settings::swapVirtualScreenUpShortcut,
        "Swap Virtual Screen Up", [this] {
            Q_EMIT swapVirtualScreenRequested(NavigationDirection::Up);
        });
    add(kIdSwapVirtualScreenDown, &ConfigDefaults::swapVirtualScreenDownShortcut,
        &Settings::swapVirtualScreenDownShortcut, "Swap Virtual Screen Down", [this] {
            Q_EMIT swapVirtualScreenRequested(NavigationDirection::Down);
        });

    // ─── Rotate virtual screens ────────────────────────────────────────────
    add(kIdRotateVirtualScreensCW, &ConfigDefaults::rotateVirtualScreensClockwiseShortcut,
        &Settings::rotateVirtualScreensClockwiseShortcut, "Rotate Virtual Screens Clockwise", [this] {
            Q_EMIT rotateVirtualScreensRequested(true);
        });
    add(kIdRotateVirtualScreensCCW, &ConfigDefaults::rotateVirtualScreensCounterclockwiseShortcut,
        &Settings::rotateVirtualScreensCounterclockwiseShortcut, "Rotate Virtual Screens Counterclockwise", [this] {
            Q_EMIT rotateVirtualScreensRequested(false);
        });

    // ─── Snap to zone slots 1–9 (indexed getters) ──────────────────────────
    const QString snapDefaults[9] = {
        ConfigDefaults::snapToZone1Shortcut(), ConfigDefaults::snapToZone2Shortcut(),
        ConfigDefaults::snapToZone3Shortcut(), ConfigDefaults::snapToZone4Shortcut(),
        ConfigDefaults::snapToZone5Shortcut(), ConfigDefaults::snapToZone6Shortcut(),
        ConfigDefaults::snapToZone7Shortcut(), ConfigDefaults::snapToZone8Shortcut(),
        ConfigDefaults::snapToZone9Shortcut(),
    };
    for (int i = 0; i < 9; ++i) {
        Entry e;
        e.id = snapToZoneId(i);
        e.defaultSeq = parseSequence(snapDefaults[i], e.id);
        e.description = PzI18n::tr("Snap to Zone %1").arg(i + 1);
        Settings* s = m_settings;
        const QString idCopy = e.id;
        e.currentSeq = [s, i, idCopy] {
            return parseSequence(s->snapToZoneShortcut(i), idCopy);
        };
        const int zoneNumber = i + 1;
        e.fire = [this, zoneNumber] {
            Q_EMIT snapToZoneRequested(zoneNumber);
        };
        m_entries.push_back(std::move(e));
    }

    // ─── Rotate windows ────────────────────────────────────────────────────
    add(kIdRotateWindowsCW, &ConfigDefaults::rotateWindowsClockwiseShortcut, &Settings::rotateWindowsClockwiseShortcut,
        "Rotate Windows Clockwise", [this] {
            Q_EMIT rotateWindowsRequested(true);
        });
    add(kIdRotateWindowsCCW, &ConfigDefaults::rotateWindowsCounterclockwiseShortcut,
        &Settings::rotateWindowsCounterclockwiseShortcut, "Rotate Windows Counterclockwise", [this] {
            Q_EMIT rotateWindowsRequested(false);
        });

    // ─── Cycle window in zone ──────────────────────────────────────────────
    add(kIdCycleWindowForward, &ConfigDefaults::cycleWindowForwardShortcut, &Settings::cycleWindowForwardShortcut,
        "Cycle Window Forward in Zone", [this] {
            Q_EMIT cycleWindowsInZoneRequested(true);
        });
    add(kIdCycleWindowBackward, &ConfigDefaults::cycleWindowBackwardShortcut, &Settings::cycleWindowBackwardShortcut,
        "Cycle Window Backward in Zone", [this] {
            Q_EMIT cycleWindowsInZoneRequested(false);
        });

    // ─── Misc layout ops ───────────────────────────────────────────────────
    add(kIdResnapToNewLayout, &ConfigDefaults::resnapToNewLayoutShortcut, &Settings::resnapToNewLayoutShortcut,
        "Reapply Layout to Windows", [this] {
            Q_EMIT resnapToNewLayoutRequested();
        });
    add(kIdSnapAllWindows, &ConfigDefaults::snapAllWindowsShortcut, &Settings::snapAllWindowsShortcut,
        "Snap All Windows to Zones", [this] {
            Q_EMIT snapAllWindowsRequested();
        });
    add(kIdLayoutPicker, &ConfigDefaults::layoutPickerShortcut, &Settings::layoutPickerShortcut, "Open Layout Picker",
        [this] {
            Q_EMIT layoutPickerRequested();
        });
    add(kIdToggleLayoutLock, &ConfigDefaults::toggleLayoutLockShortcut, &Settings::toggleLayoutLockShortcut,
        "Toggle Layout Lock", [this] {
            Q_EMIT toggleLayoutLockRequested();
        });

    // ─── Autotile ──────────────────────────────────────────────────────────
    add(kIdToggleAutotile, &ConfigDefaults::autotileToggleShortcut, &Settings::autotileToggleShortcut,
        "Toggle Autotile", [this] {
            Q_EMIT toggleAutotileRequested();
        });
    add(kIdFocusMaster, &ConfigDefaults::autotileFocusMasterShortcut, &Settings::autotileFocusMasterShortcut,
        "Focus Master Window", [this] {
            Q_EMIT focusMasterRequested();
        });
    add(kIdSwapMaster, &ConfigDefaults::autotileSwapMasterShortcut, &Settings::autotileSwapMasterShortcut,
        "Swap with Master", [this] {
            Q_EMIT swapWithMasterRequested();
        });
    add(kIdIncreaseMasterRatio, &ConfigDefaults::autotileIncMasterRatioShortcut,
        &Settings::autotileIncMasterRatioShortcut, "Increase Master Ratio", [this] {
            Q_EMIT increaseMasterRatioRequested();
        });
    add(kIdDecreaseMasterRatio, &ConfigDefaults::autotileDecMasterRatioShortcut,
        &Settings::autotileDecMasterRatioShortcut, "Decrease Master Ratio", [this] {
            Q_EMIT decreaseMasterRatioRequested();
        });
    add(kIdIncreaseMasterCount, &ConfigDefaults::autotileIncMasterCountShortcut,
        &Settings::autotileIncMasterCountShortcut, "Increase Master Count", [this] {
            Q_EMIT increaseMasterCountRequested();
        });
    add(kIdDecreaseMasterCount, &ConfigDefaults::autotileDecMasterCountShortcut,
        &Settings::autotileDecMasterCountShortcut, "Decrease Master Count", [this] {
            Q_EMIT decreaseMasterCountRequested();
        });
    add(kIdRetile, &ConfigDefaults::autotileRetileShortcut, &Settings::autotileRetileShortcut, "Retile Windows",
        [this] {
            Q_EMIT retileRequested();
        });
}

} // namespace PlasmaZones

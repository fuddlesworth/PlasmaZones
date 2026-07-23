// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorShortcuts/IAdhocRegistrar.h>

#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

#include <functional>
#include <memory>

namespace PhosphorShortcuts {
class IBackend;
class Registry;
} // namespace PhosphorShortcuts

namespace PhosphorZones {
class LayoutRegistry;
}

namespace PlasmaZones {

class Settings;

/**
 * @brief Navigation direction for keyboard navigation
 */
enum class NavigationDirection {
    Left,
    Right,
    Up,
    Down
};

/**
 * @brief Manages global keyboard shortcuts for PlasmaZones.
 *
 * Thin glue layer on top of PhosphorShortcuts::Registry. Owns one entry per
 * PlasmaZones action, wires each entry's current sequence to the matching
 * Settings getter, and fans activation into the domain-specific Q_SIGNALS
 * below. The actual key-grab mechanism (KGlobalAccel / XDG Portal /
 * D-Bus trigger) is selected inside the PhosphorShortcuts library.
 */
class ShortcutManager : public QObject, public PhosphorShortcutsIntegration::IAdhocRegistrar
{
    Q_OBJECT

public:
    explicit ShortcutManager(Settings* settings, PhosphorZones::LayoutRegistry* layoutManager,
                             QObject* parent = nullptr);
    ~ShortcutManager() override;

    void registerShortcuts();
    void updateShortcuts();
    void unregisterShortcuts();

    /**
     * Register an ad-hoc shortcut that lives outside the main settings-driven
     * table. Used by subsystems that need a transient grab bound to a UI state
     * (e.g. the cancel-overlay Escape grab bound while the layout picker or
     * snap assist is showing). Batches with an immediate flush to the backend.
     * Idempotent — re-registering the same id updates the callback and
     * description in place.
     */
    void registerAdhocShortcut(const QString& id, const QKeySequence& sequence, const QString& description,
                               std::function<void()> callback) override;

    /**
     * Release an ad-hoc shortcut previously bound via registerAdhocShortcut().
     * Idempotent; unknown ids are ignored.
     */
    void unregisterAdhocShortcut(const QString& id) override;

    /**
     * Catalog of every settings-driven shortcut for the cheatsheet overlay,
     * one QVariantMap per row, sorted by display category:
     *   id (QString), label (translated QString),
     *   category (translated QString), categoryOrder (int),
     *   triggers (QStringList — the user's EFFECTIVE keys via backend
     *   read-back, falling back to the config value), assigned (bool),
     *   mode ("all" | "snapping" | "autotile" — which tiling mode the
     *   action is meaningful in; the overlay filters on it).
     * Ad-hoc/transient grabs never appear. Empty before registerShortcuts().
     */
    QVariantList cheatsheetModel() const;

    /// One collapsible cheatsheet family: parallel id / expected-final-token
    /// lists, the combined row label, and the tail token for the merged chip.
    struct CheatsheetFamily
    {
        QStringList ids;
        QStringList expectedLastTokens;
        QString combinedLabel;
        QString tailToken;
    };

    /**
     * Pure family-compression pass over cheatsheet rows (static so tests can
     * drive it without a shortcut backend). A family collapses into one row
     * when every member is assigned, carries exactly one trigger, ends in its
     * expected token, and shares the modifier prefix; any deviation keeps the
     * individual rows. Returns the surviving rows, unsorted.
     */
    static QVariantList compressCheatsheetFamilies(QVector<QVariantMap> rows,
                                                   const QVector<CheatsheetFamily>& families);

Q_SIGNALS:
    /**
     * The cheatsheet catalog's contents changed: a sequence was rebound
     * (in-process or externally via System Settings / compositor) or the
     * registration batch settled. Consumers re-query cheatsheetModel().
     */
    void cheatsheetModelChanged();

    void openEditorRequested();
    void openSettingsRequested();
    void toggleCheatsheetRequested();
    void previousLayoutRequested();
    void nextLayoutRequested();
    void quickLayoutRequested(int number);

    void moveWindowRequested(NavigationDirection direction);
    void spanWindowRequested(NavigationDirection direction);
    void focusZoneRequested(NavigationDirection direction);
    void pushToEmptyZoneRequested();
    void restoreWindowSizeRequested();
    void toggleWindowFloatRequested();
    void swapWindowRequested(NavigationDirection direction);
    void snapToZoneRequested(int zoneNumber);
    void rotateWindowsRequested(bool clockwise);
    void cycleWindowsInZoneRequested(bool forward);
    void resnapToNewLayoutRequested();
    void snapAllWindowsRequested();
    void layoutPickerRequested();
    void toggleLayoutLockRequested();

    void toggleAutotileRequested();
    void focusMasterRequested();
    void swapWithMasterRequested();
    void increaseMasterRatioRequested();
    void decreaseMasterRatioRequested();
    void increaseMasterCountRequested();
    void decreaseMasterCountRequested();
    void retileRequested();

    void swapVirtualScreenRequested(NavigationDirection direction);
    void rotateVirtualScreensRequested(bool clockwise);

private:
    struct Entry
    {
        QString id;
        QKeySequence defaultSeq;
        QString description;
        // Reads the current key sequence from m_settings (handles per-slot
        // getters for the quick-layout / snap-to-zone arrays).
        std::function<QKeySequence()> currentSeq;
        // Emits the domain Q_SIGNAL corresponding to this id.
        std::function<void()> fire;
    };

    // Adhoc registration deferred because the initial settings-driven
    // registration batch was in flight when the caller arrived. Drained from
    // the Registry ready() callback so subsystems that bind shortcuts in
    // response to user actions (e.g. the cancel-overlay Escape grab bound when
    // the layout picker or snap assist first shows) don't silently lose their
    // grab when that overlay appears in the first few hundred ms after daemon
    // startup on a Portal compositor.
    struct PendingAdhocOp
    {
        enum Kind {
            Register,
            Unregister
        };
        Kind kind;
        QString id;
        QKeySequence sequence;
        QString description;
        std::function<void()> callback;
    };

    void buildEntries();
    /// Re-applies every entry's current sequence; returns true when any
    /// binding actually differed from the registry's stored sequence.
    bool rebindAll();
    void drainPendingAdhocOps();

    Settings* m_settings = nullptr;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;

    std::unique_ptr<PhosphorShortcuts::IBackend> m_backend;
    std::unique_ptr<PhosphorShortcuts::Registry> m_registry;

    QVector<Entry> m_entries;
    QVector<PendingAdhocOp> m_pendingAdhocOps;

    bool m_registrationInProgress = false;
    bool m_settingsDirty = false;
};

} // namespace PlasmaZones

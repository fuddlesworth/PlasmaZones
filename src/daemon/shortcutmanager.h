// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>

namespace Phosphor::Shortcuts {
class IBackend;
class Registry;
} // namespace Phosphor::Shortcuts

namespace PlasmaZones {

class Settings;
class LayoutManager;

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
 * Thin glue layer on top of Phosphor::Shortcuts::Registry. Owns one entry per
 * PlasmaZones action, wires each entry's current sequence to the matching
 * Settings getter, and fans activation into the domain-specific Q_SIGNALS
 * below. The actual key-grab mechanism (KGlobalAccel / XDG Portal /
 * D-Bus trigger) is selected inside the PhosphorShortcuts library.
 */
class ShortcutManager : public QObject
{
    Q_OBJECT

public:
    explicit ShortcutManager(Settings* settings, LayoutManager* layoutManager, QObject* parent = nullptr);
    ~ShortcutManager() override;

    void registerShortcuts();
    void updateShortcuts();
    void unregisterShortcuts();

    /// Registry handle for subsystems that need to register their own
    /// ad-hoc shortcuts (e.g. WindowDragAdaptor for the Escape cancel).
    Phosphor::Shortcuts::Registry* registry() const
    {
        return m_registry.get();
    }

Q_SIGNALS:
    void openEditorRequested();
    void openSettingsRequested();
    void previousLayoutRequested();
    void nextLayoutRequested();
    void quickLayoutRequested(int number);

    void moveWindowRequested(NavigationDirection direction);
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

    void shortcutsRegistered();

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

    void buildEntries();
    void rebindAll();

    Settings* m_settings = nullptr;
    LayoutManager* m_layoutManager = nullptr;

    std::unique_ptr<Phosphor::Shortcuts::IBackend> m_backend;
    std::unique_ptr<Phosphor::Shortcuts::Registry> m_registry;

    QVector<Entry> m_entries;

    bool m_registrationInProgress = false;
    bool m_settingsDirty = false;
};

} // namespace PlasmaZones

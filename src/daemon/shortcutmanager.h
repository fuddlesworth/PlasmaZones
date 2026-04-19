// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorShortcuts/IAdhocRegistrar.h>

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
}
namespace PhosphorZones {
class LayoutManager;
}
namespace PlasmaZones {

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
class ShortcutManager : public QObject, public Phosphor::Shortcuts::Integration::IAdhocRegistrar
{
    Q_OBJECT

public:
    explicit ShortcutManager(Settings* settings, PhosphorZones::LayoutManager* layoutManager,
                             QObject* parent = nullptr);
    ~ShortcutManager() override;

    void registerShortcuts();
    void updateShortcuts();
    void unregisterShortcuts();

    /**
     * Register an ad-hoc shortcut that lives outside the main settings-driven
     * table. Used by subsystems that need a transient grab bound to a code
     * path (e.g. WindowDragAdaptor's Escape cancel during a drag). Batches
     * with an immediate flush to the backend. Idempotent — re-registering the
     * same id updates the callback and description in place.
     */
    void registerAdhocShortcut(const QString& id, const QKeySequence& sequence, const QString& description,
                               std::function<void()> callback) override;

    /**
     * Release an ad-hoc shortcut previously bound via registerAdhocShortcut().
     * Idempotent; unknown ids are ignored.
     */
    void unregisterAdhocShortcut(const QString& id) override;

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
    // response to user actions (e.g. WindowDragAdaptor's Escape cancel on
    // drag start) don't silently lose their grab when the drag fires in the
    // first few hundred ms after daemon startup on a Portal compositor.
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
    void rebindAll();
    void drainPendingAdhocOps();

    Settings* m_settings = nullptr;
    PhosphorZones::LayoutManager* m_layoutManager = nullptr;

    std::unique_ptr<Phosphor::Shortcuts::IBackend> m_backend;
    std::unique_ptr<Phosphor::Shortcuts::Registry> m_registry;

    QVector<Entry> m_entries;
    QVector<PendingAdhocOp> m_pendingAdhocOps;

    bool m_registrationInProgress = false;
    bool m_settingsDirty = false;
};

} // namespace PlasmaZones

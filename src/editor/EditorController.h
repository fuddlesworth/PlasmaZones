// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// FILE-SIZE EXCEPTION (sanctioned), CEILING 1250 LINES: this is one Q_OBJECT
// class declaration, and moc needs the whole class in a single header, so the
// only available "split" would fragment one class across several headers and
// cost every reader the hunt for where a property lives (the settings.h and
// settingscontroller.h precedent). The bulk here is the Q_PROPERTY /
// Q_INVOKABLE surface QML binds to, which cannot move without rewriting every
// binding in src/editor/qml. The IMPLEMENTATION is already split by concern
// under src/editor/controller/ (settings.cpp, scrollingtemplate.cpp, and
// siblings), which is where the real per-concern boundary is.
//
// The ceiling is a budget, not a description of where the file sits: a new
// declaration that would pass it has to buy its room by retiring another. Long
// rationale belongs on the definition in the matching controller/*.cpp when it
// will not fit here.

#pragma once

#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QFont>
#include <QImage>
#include <QRectF>
#include <QTimer>
#include <QUuid>
#include <QScreen>
#include <QQuickWindow>
#include <QSize>
#include "core/types/constants.h"
#include <PhosphorZones/LayoutRegistry.h>
#include "core/platform/logging.h"
#include "undo/UndoController.h"
#include "EditorGapsModel.h"
#include "EditorTemplateModel.h"

#include <memory>
#include <optional>

namespace PhosphorZones {
class Layout;
class ScrollingTemplateStore;
}

namespace PhosphorConfig {
// Forward-declared for refreshScrollingStripAxisSnapshot's reference
// parameter; the .cpp includes the backend headers directly.
class IBackend;
}

namespace PhosphorRules {
// Forward-declared for the std::unique_ptr<RuleStoreWatcher> member.
// (The complete RuleStore type is pulled in transitively via
// PhosphorZones/LayoutRegistry.h; the .cpp includes RuleStore.h directly.)
class RuleStoreWatcher;
}

namespace PlasmaZones {

class ILayoutService;
class ZoneManager;
class SnappingService;
class TemplateService;
class EditorGapsModel;

/**
 * @brief Controller for the layout editor
 *
 * Manages zone editing operations and communicates with the daemon via D-Bus.
 * Exposed to QML for the editor UI.
 */
class EditorController : public QObject
{
    Q_OBJECT

    // The gap sub-model calls markUnsaved() and reaches the shared undo stack.
    friend class EditorGapsModel;
    // The scrolling-template sub-model does the same.
    friend class EditorTemplateModel;

    // PhosphorZones::Layout properties
    Q_PROPERTY(QString layoutId READ layoutId NOTIFY layoutIdChanged)
    Q_PROPERTY(QString layoutName READ layoutName WRITE setLayoutName NOTIFY layoutNameChanged)
    Q_PROPERTY(QVariantList zones READ zones NOTIFY zonesChanged)
    // Lightweight version counter for efficient QML binding dependencies.
    // Use this instead of accessing 'zones' when you only need to detect changes.
    // Avoids copying the entire QVariantList just to create a binding dependency.
    Q_PROPERTY(int zonesVersion READ zonesVersion NOTIFY zonesChanged)
    Q_PROPERTY(QString selectedZoneId READ selectedZoneId WRITE setSelectedZoneId NOTIFY selectedZoneIdChanged)
    Q_PROPERTY(QStringList selectedZoneIds READ selectedZoneIds WRITE setSelectedZoneIds NOTIFY selectedZoneIdsChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectedZoneIdsChanged)
    Q_PROPERTY(bool hasMultipleSelection READ hasMultipleSelection NOTIFY selectedZoneIdsChanged)
    Q_PROPERTY(bool hasUnsavedChanges READ hasUnsavedChanges NOTIFY hasUnsavedChangesChanged)

    // Snapping settings
    Q_PROPERTY(bool gridSnappingEnabled READ gridSnappingEnabled WRITE setGridSnappingEnabled NOTIFY
                   gridSnappingEnabledChanged)
    Q_PROPERTY(bool edgeSnappingEnabled READ edgeSnappingEnabled WRITE setEdgeSnappingEnabled NOTIFY
                   edgeSnappingEnabledChanged)
    Q_PROPERTY(qreal snapIntervalX READ snapIntervalX WRITE setSnapIntervalX NOTIFY snapIntervalXChanged)
    Q_PROPERTY(qreal snapIntervalY READ snapIntervalY WRITE setSnapIntervalY NOTIFY snapIntervalYChanged)
    Q_PROPERTY(
        qreal snapInterval READ snapInterval WRITE setSnapInterval NOTIFY snapIntervalChanged) // Backward compatibility
    Q_PROPERTY(
        bool gridOverlayVisible READ gridOverlayVisible WRITE setGridOverlayVisible NOTIFY gridOverlayVisibleChanged)

    // Keyboard shortcuts (app-specific configurable shortcuts only)
    // Standard shortcuts (Save, Delete, Close) use Qt StandardKey (system shortcuts)
    Q_PROPERTY(QString editorDuplicateShortcut READ editorDuplicateShortcut NOTIFY editorDuplicateShortcutChanged)
    Q_PROPERTY(QString editorSplitHorizontalShortcut READ editorSplitHorizontalShortcut NOTIFY
                   editorSplitHorizontalShortcutChanged)
    Q_PROPERTY(
        QString editorSplitVerticalShortcut READ editorSplitVerticalShortcut NOTIFY editorSplitVerticalShortcutChanged)
    Q_PROPERTY(QString editorFillShortcut READ editorFillShortcut NOTIFY editorFillShortcutChanged)
    Q_PROPERTY(int snapOverrideModifier READ snapOverrideModifier WRITE setSnapOverrideModifier NOTIFY
                   snapOverrideModifierChanged)

    // Fill on drop settings
    Q_PROPERTY(bool fillOnDropEnabled READ fillOnDropEnabled WRITE setFillOnDropEnabled NOTIFY fillOnDropEnabledChanged)
    Q_PROPERTY(
        int fillOnDropModifier READ fillOnDropModifier WRITE setFillOnDropModifier NOTIFY fillOnDropModifierChanged)

    // Screen
    Q_PROPERTY(QString targetScreen READ targetScreen WRITE setTargetScreen NOTIFY targetScreenChanged)

    // Scrolling-template preview axis: true when the strip on the TARGET
    // SCREEN runs vertically. Read-only and derived, never a preference of its
    // own — the strip canvas is a picture of what the engine will do with this
    // template, so it resolves the same per-screen-override / global-setting /
    // Auto ladder the engine resolves, against the target screen's size. A
    // template itself carries no axis: its column extents are fractions ALONG
    // the strip whichever way that strip happens to run.
    Q_PROPERTY(bool templatePreviewVertical READ templatePreviewVertical NOTIFY templatePreviewVerticalChanged)

    // PhosphorZones::Zone gap settings (per-layout override + global mirrors).
    // Extracted into a sub-model exposed by pointer; QML reads
    // controller.gaps.outerGapTop and friends.
    Q_PROPERTY(PlasmaZones::EditorGapsModel* gaps READ gaps CONSTANT)

    // Overlay display mode override
    Q_PROPERTY(
        int overlayDisplayMode READ overlayDisplayMode WRITE setOverlayDisplayMode NOTIFY overlayDisplayModeChanged)
    Q_PROPERTY(bool hasOverlayDisplayModeOverride READ hasOverlayDisplayModeOverride NOTIFY overlayDisplayModeChanged)
    Q_PROPERTY(int globalOverlayDisplayMode READ globalOverlayDisplayMode NOTIFY globalOverlayDisplayModeChanged)

    // Full screen geometry mode
    Q_PROPERTY(bool useFullScreenGeometry READ useFullScreenGeometry WRITE setUseFullScreenGeometry NOTIFY
                   useFullScreenGeometryChanged)

    // Aspect ratio classification (0=Any, 1=Standard, 2=Ultrawide, 3=SuperUltrawide, 4=Portrait)
    Q_PROPERTY(int aspectRatioClass READ aspectRatioClass WRITE setAspectRatioClass NOTIFY aspectRatioClassChanged)

    // Target screen size (for fixed geometry coordinate conversion)
    Q_PROPERTY(QSize targetScreenSize READ targetScreenSize NOTIFY targetScreenSizeChanged)

    // Virtual screen offset within the physical monitor.
    // When editing a VS, the drawing area must be positioned at this offset so zones
    // align with the VS region, not the full physical monitor.
    Q_PROPERTY(QRect virtualScreenRect READ virtualScreenRect NOTIFY targetScreenSizeChanged)
    Q_PROPERTY(bool isVirtualScreen READ isVirtualScreen NOTIFY targetScreenSizeChanged)

    // Usable area insets — offset from full screen geometry to available geometry (panels/taskbars)
    Q_PROPERTY(int insetLeft READ insetLeft NOTIFY usableAreaInsetsChanged)
    Q_PROPERTY(int insetTop READ insetTop NOTIFY usableAreaInsetsChanged)
    Q_PROPERTY(int insetRight READ insetRight NOTIFY usableAreaInsetsChanged)
    Q_PROPERTY(int insetBottom READ insetBottom NOTIFY usableAreaInsetsChanged)

    // Label font settings (read-only from global Appearance config)
    Q_PROPERTY(QString labelFontFamily READ labelFontFamily CONSTANT)
    Q_PROPERTY(qreal labelFontSizeScale READ labelFontSizeScale CONSTANT)
    Q_PROPERTY(int labelFontWeight READ labelFontWeight CONSTANT)
    Q_PROPERTY(bool labelFontItalic READ labelFontItalic CONSTANT)
    Q_PROPERTY(bool labelFontUnderline READ labelFontUnderline CONSTANT)
    Q_PROPERTY(bool labelFontStrikeout READ labelFontStrikeout CONSTANT)

    // Visibility filtering (Tier 2 per-context allow-lists)
    Q_PROPERTY(QStringList allowedScreens READ allowedScreens WRITE setAllowedScreens NOTIFY allowedScreensChanged)
    Q_PROPERTY(QVariantList allowedDesktops READ allowedDesktops WRITE setAllowedDesktops NOTIFY allowedDesktopsChanged)
    Q_PROPERTY(
        QStringList allowedActivities READ allowedActivities WRITE setAllowedActivities NOTIFY allowedActivitiesChanged)

    // Screen model for the TopBar screen selector (includes virtual screens)
    Q_PROPERTY(QVariantList screenModel READ screenModel NOTIFY availableScreenIdsChanged)

    // Context info for visibility UI
    Q_PROPERTY(QStringList availableScreenIds READ availableScreenIds NOTIFY availableScreenIdsChanged)
    Q_PROPERTY(int virtualDesktopCount READ virtualDesktopCount NOTIFY virtualDesktopCountChanged)
    Q_PROPERTY(QStringList virtualDesktopNames READ virtualDesktopNames NOTIFY virtualDesktopNamesChanged)
    Q_PROPERTY(bool activitiesAvailable READ activitiesAvailable NOTIFY activitiesAvailableChanged)
    Q_PROPERTY(QVariantList availableActivities READ availableActivities NOTIFY availableActivitiesChanged)

    // Preview mode (read-only view for autotile layouts)
    Q_PROPERTY(bool previewMode READ previewMode NOTIFY previewModeChanged)

    // Editing mode: which domain object the editor is editing, orthogonal to
    // previewMode. 0 = ModeLayout, 1 = ModeScrollingTemplate. The mode
    // follows whichever object last loaded or was created (launch shapes,
    // createNewLayout / loadLayout, the template pair); a failed load never
    // flips it.
    Q_PROPERTY(int editorMode READ editorMode NOTIFY editorModeChanged)

    // Scrolling-template edit state sub-model (see EditorTemplateModel for
    // the contract); exposed by pointer the way `gaps` is. The template's
    // name / id / dirty flag reuse layoutName / layoutId / hasUnsavedChanges
    // so the TopBar, save gating, and confirm dialogs work unchanged.
    Q_PROPERTY(PlasmaZones::EditorTemplateModel* scrollingTemplate READ scrollingTemplate CONSTANT)

    // Clipboard operations
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY canPasteChanged)
    Q_PROPERTY(UndoController* undoController READ undoController CONSTANT)
    /// PlasmaZones::MaxLayoutNameLength for QML name fields, so the cap
    /// cannot silently desync from the C++ clamp.
    Q_PROPERTY(int maxLayoutNameLength READ maxLayoutNameLength CONSTANT)

public:
    explicit EditorController(QObject* parent = nullptr);
    ~EditorController() override;

    // Preview mode
    bool previewMode() const;
    void setPreviewMode(bool preview);

    // Editing mode values for the editorMode property. Deliberately not a
    // Q_ENUM: the controller is a context property and its type is never
    // QML-registered, so QML compares against these documented ints.
    static constexpr int ModeLayout = 0;
    static constexpr int ModeScrollingTemplate = 1;

    int editorMode() const
    {
        return m_editorMode;
    }

    EditorTemplateModel* scrollingTemplate() const
    {
        return m_scrollingTemplate;
    }

    // Property getters
    QString layoutId() const;
    QString layoutName() const;
    QVariantList zones() const; // Delegates to ZoneManager
    int zonesVersion() const
    {
        return m_zonesVersion;
    } // Lightweight change counter
    QString selectedZoneId() const;
    QStringList selectedZoneIds() const;
    int selectionCount() const;
    bool hasMultipleSelection() const;
    bool hasUnsavedChanges() const;
    bool gridSnappingEnabled() const;
    bool edgeSnappingEnabled() const;
    qreal snapIntervalX() const;
    qreal snapIntervalY() const;
    qreal snapInterval() const; // Backward compatibility
    bool gridOverlayVisible() const;
    QString editorDuplicateShortcut() const;
    QString editorSplitHorizontalShortcut() const;
    QString editorSplitVerticalShortcut() const;
    QString editorFillShortcut() const;
    int snapOverrideModifier() const;
    bool fillOnDropEnabled() const;
    int fillOnDropModifier() const;
    QString targetScreen() const;
    bool templatePreviewVertical() const;
    EditorGapsModel* gaps() const
    {
        return m_gaps;
    }
    int overlayDisplayMode() const;
    bool hasOverlayDisplayModeOverride() const;
    int globalOverlayDisplayMode() const;
    bool useFullScreenGeometry() const;
    int aspectRatioClass() const;
    QSize targetScreenSize() const;
    QRect virtualScreenRect() const
    {
        return m_virtualScreenRect;
    }
    bool isVirtualScreen() const
    {
        return m_virtualScreenRect.isValid();
    }
    int insetLeft() const;
    int insetTop() const;
    int insetRight() const;
    int insetBottom() const;
    void refreshUsableAreaInsets();
    bool canPaste() const;
    UndoController* undoController() const;
    int maxLayoutNameLength() const
    {
        return MaxLayoutNameLength;
    }

    // Font settings getters
    QString labelFontFamily() const
    {
        return m_labelFontFamily;
    }
    qreal labelFontSizeScale() const
    {
        return m_labelFontSizeScale;
    }
    int labelFontWeight() const
    {
        return m_labelFontWeight;
    }
    bool labelFontItalic() const
    {
        return m_labelFontItalic;
    }
    bool labelFontUnderline() const
    {
        return m_labelFontUnderline;
    }
    bool labelFontStrikeout() const
    {
        return m_labelFontStrikeout;
    }

    // Visibility filtering getters
    QStringList allowedScreens() const
    {
        return m_allowedScreens;
    }
    QVariantList allowedDesktops() const;
    QStringList allowedActivities() const
    {
        return m_allowedActivities;
    }
    QStringList availableScreenIds() const
    {
        return m_availableScreenIds;
    }
    QVariantList screenModel() const;

    int virtualDesktopCount() const
    {
        return m_virtualDesktopCount;
    }
    QStringList virtualDesktopNames() const
    {
        return m_virtualDesktopNames;
    }
    bool activitiesAvailable() const
    {
        return m_activitiesAvailable;
    }
    QVariantList availableActivities() const
    {
        return m_availableActivities;
    }

    // Visibility filtering setters
    void setAllowedScreens(const QStringList& screens);
    void setAllowedDesktops(const QVariantList& desktops);
    void setAllowedActivities(const QStringList& activities);

    // Property setters
    void setLayoutName(const QString& name);
    void setLayoutNameDirect(const QString& name); // For undo/redo (bypasses command creation)
    void setSelectedZoneId(const QString& zoneId);
    Q_INVOKABLE void setSelectedZoneIds(const QStringList& zoneIds);
    void setSelectedZoneIdsDirect(const QStringList& zoneIds); // For undo/redo (bypasses command creation)
    void setGridSnappingEnabled(bool enabled);
    void setEdgeSnappingEnabled(bool enabled);
    void setSnapIntervalX(qreal interval);
    void setSnapIntervalY(qreal interval);
    void setSnapInterval(qreal interval); // Backward compatibility: sets both X and Y
    void setGridOverlayVisible(bool visible);
    void setSnapOverrideModifier(int modifier);
    void setFillOnDropEnabled(bool enabled);
    void setFillOnDropModifier(int modifier);
    /**
     * @brief Request a switch to @p screenName, loading that screen's layout.
     *
     * REQUEST, not command. Switching screens replaces the loaded layout, so
     * with unsaved edits in hand the switch would destroy them. When
     * hasUnsavedChanges() is set this therefore parks @p screenName as the
     * pending target, emits targetScreenChangeRequiresConfirmation(), and
     * returns WITHOUT changing anything — targetScreen() still reports the old
     * screen, so a QML binding on it stays put until the user answers. The UI
     * answers with confirmPendingTargetScreen() (after saveLayout(), or to
     * discard) or cancelPendingTargetScreen().
     *
     * With no unsaved changes the switch applies immediately.
     */
    void setTargetScreen(const QString& screenName);

    /// Apply the screen parked by setTargetScreen(). No-op when nothing is
    /// pending. The caller decides whether the outgoing edits were saved
    /// first — this always applies, discarding whatever is still unsaved.
    Q_INVOKABLE void confirmPendingTargetScreen();

    /// Drop the screen parked by setTargetScreen(), keeping the current screen
    /// and its unsaved edits. No-op when nothing is pending.
    Q_INVOKABLE void cancelPendingTargetScreen();

    /**
     * @brief Request that the editor show what a set of launch arguments names.
     *
     * REQUEST, not command, for the same reason setTargetScreen() is one: the
     * `--new` and `--layout <id>` paths both REPLACE the loaded layout, so with
     * unsaved edits in hand they would destroy them. A second
     * `plasmazones-editor --layout Y` is forwarded to the running instance
     * rather than opening a window of its own, so this is reachable from any
     * launcher, shortcut or settings button while the user has work in flight.
     *
     * With unsaved edits this parks the request, emits
     * launchRequestRequiresConfirmation() and changes nothing. The UI answers
     * with confirmPendingLaunch() (after saveLayout(), or to discard) or
     * cancelPendingLaunch(). With a clean editor — every initial launch, since
     * a freshly constructed controller has nothing unsaved — it applies at once.
     */
    void requestLaunch(const QString& screenId, const QString& layoutId, bool createNew, bool preview,
                       const QString& templateId = QString(), bool newTemplate = false);

    /// Apply the launch parked by requestLaunch(). No-op when nothing is
    /// pending. The caller decides whether the outgoing edits were saved
    /// first — this always applies, discarding whatever is still unsaved.
    Q_INVOKABLE void confirmPendingLaunch();

    /// Drop the launch parked by requestLaunch(), keeping the loaded layout and
    /// its unsaved edits. No-op when nothing is pending.
    Q_INVOKABLE void cancelPendingLaunch();

    /**
     * @brief Show a QML Window fullscreen on the target screen from C++
     *
     * On Wayland, QWindow::setScreen() has no effect on xdg-shell surfaces
     * (QTBUG-88997). KWin determines the output from the window's initial
     * geometry when the surface is first committed. This method positions
     * the window within the target screen's bounds before calling
     * showFullScreen(), giving KWin the correct output hint.
     */
    Q_INVOKABLE void showFullScreenOnTargetScreen(QQuickWindow* window);
    void setTargetScreenDirect(const QString& screenName); // Sets screen without loading layout (for initialization)
    Q_INVOKABLE void clearOverlayDisplayModeOverride();

    // Fetches every gap and overlay key (zonePadding + outerGap cluster +
    // overlayDisplayMode) in a single daemon round-trip. Hands the gap globals
    // to EditorGapsModel::applyGlobalSettings (which emits its own
    // globalZonePaddingChanged / globalOuterGapChanged) and refreshes the
    // overlay-mode global here, emitting globalOverlayDisplayModeChanged only
    // when it changed. Called from loadEditorSettings() on the startup hot path.
    void refreshGlobalGapOverlaySettings();
    void setOverlayDisplayMode(int mode);
    void setUseFullScreenGeometry(bool enabled);
    void setAspectRatioClass(int cls);

    // Gap override setters - Direct (for undo/redo, bypass command creation)
    void setOverlayDisplayModeDirect(int mode);
    void setUseFullScreenGeometryDirect(bool enabled);

    // Visibility setters - Direct (for undo/redo, bypass command creation)
    void setAllowedScreensDirect(const QStringList& screens);
    void setAllowedDesktopsDirect(const QList<int>& desktops);
    void setAllowedActivitiesDirect(const QStringList& activities);

public Q_SLOTS:
    // PhosphorZones::Layout operations; loadLayout's bool = payload
    // resolved (false leaves the session, mode included, intact).
    void createNewLayout();
    bool loadLayout(const QString& layoutId);

    // Scrolling-template operations. Both flip into template mode;
    // saveLayout() then dispatches to the template save path, and
    // loadScrollingTemplate's bool follows loadLayout's contract.
    bool loadScrollingTemplate(const QString& templateId);
    void createNewScrollingTemplate();

    // Daemon scrollingTemplatesChanged subscriber: the store watches
    // nothing, so this reload is how other-process writes reach us.
    void reloadLocalTemplates();
    /// Daemon settingsChanged subscriber (debounced by
    /// m_stripAxisReloadTimer): re-snapshots the strip-axis inputs so an
    /// axis authored in the settings app while the editor is open reaches
    /// the template preview without a restart.
    void reloadScrollingStripAxis();
    /// Persist the current layout. False = the save did not land
    /// (layoutSaveFailed carries the reason, the unsaved flag stays set); a
    /// caller chaining a layout-replacing action (screen switch, close)
    /// must gate on it or it discards the work the user just saved.
    bool saveLayout();
    void discardChanges();

    // D-Bus subscriber slot — wired in the ctor to all of the daemon's
    // layout-mutation signals (layoutCreated/Deleted/Changed/ListChanged/
    // PropertyChanged). Forces an in-process PhosphorZones::LayoutRegistry
    // reload so loadLayout()'s instant-open fast path resolves against the
    // daemon's view rather than the snapshot scanned at launch. The registry
    // watches nothing, so this explicit reload is the only way a daemon-side
    // layout write reaches the editor.
    void reloadLocalLayouts();

    // PhosphorZones::Zone CRUD operations (using zone IDs)
    QString addZone(qreal x, qreal y, qreal width, qreal height);
    void updateZoneGeometry(const QString& zoneId, qreal x, qreal y, qreal width, qreal height,
                            bool skipSnapping = false);
    void updateZoneName(const QString& zoneId, const QString& name);
    void updateZoneNumber(const QString& zoneId, int number);
    void updateZoneColor(const QString& zoneId, const QString& colorType, const QString& color);
    Q_INVOKABLE void updateZoneAppearance(const QString& zoneId, const QString& propertyName, const QVariant& value);
    void deleteZone(const QString& zoneId);
    QString duplicateZone(const QString& zoneId);

    // PhosphorZones::Zone splitting - split a zone horizontally or vertically
    Q_INVOKABLE QString splitZone(const QString& zoneId, bool horizontal);

    // Helper: get zone index by ID
    // Delegates to ZoneManager
    int zoneIndexById(const QString& zoneId) const;

    /**
     * @brief Get complete zone data by ID
     * @param zoneId PhosphorZones::Zone ID to retrieve
     * @return Complete zone data as QVariantMap, or empty map if not found
     *
     * Performance optimization: O(1) lookup instead of O(n) JavaScript loop in QML.
     * Delegates to ZoneManager::getZoneById().
     */
    Q_INVOKABLE QVariantMap getZoneById(const QString& zoneId) const;

    // Divider resizing - find zones that share an edge
    Q_INVOKABLE QVariantList getZonesSharingEdge(const QString& zoneId, qreal edgeX, qreal edgeY,
                                                 qreal threshold = 0.01);
    Q_INVOKABLE void resizeZonesAtDivider(const QString& zoneId1, const QString& zoneId2, qreal newDividerX,
                                          qreal newDividerY, bool isVertical);

    // Auto-fill operations
    Q_INVOKABLE QVariantMap findAdjacentZones(const QString& zoneId);
    Q_INVOKABLE bool expandToFillSpace(const QString& zoneId, qreal mouseX = -1, qreal mouseY = -1);
    Q_INVOKABLE QVariantMap calculateFillRegion(const QString& zoneId, qreal mouseX, qreal mouseY);
    Q_INVOKABLE void deleteZoneWithFill(const QString& zoneId, bool autoFill = true);

    // Z-order operations
    Q_INVOKABLE void bringToFront(const QString& zoneId);
    Q_INVOKABLE void sendToBack(const QString& zoneId);
    Q_INVOKABLE void bringForward(const QString& zoneId);
    Q_INVOKABLE void sendBackward(const QString& zoneId);

    // Template operations
    void applyTemplate(const QString& templateType, int columns = 2, int rows = 2);
    void clearAllZones();

    // Snapping (delegates to SnappingService)
    QVariantMap snapGeometry(qreal x, qreal y, qreal width, qreal height, const QString& excludeZoneId = QString());
    Q_INVOKABLE QVariantMap snapGeometrySelective(qreal x, qreal y, qreal width, qreal height,
                                                  const QString& excludeZoneId, bool snapLeft, bool snapRight,
                                                  bool snapTop, bool snapBottom);

    // Keyboard navigation
    Q_INVOKABLE QString selectNextZone();
    Q_INVOKABLE QString selectPreviousZone();
    Q_INVOKABLE bool moveSelectedZone(int direction, qreal step = 0.01); // 0=left, 1=right, 2=up, 3=down
    Q_INVOKABLE bool resizeSelectedZone(int direction, qreal step = 0.01); // 0=left, 1=right, 2=top, 3=bottom

    // Multi-selection manipulation
    Q_INVOKABLE void addToSelection(const QString& zoneId);
    Q_INVOKABLE void removeFromSelection(const QString& zoneId);
    Q_INVOKABLE void toggleSelection(const QString& zoneId);
    Q_INVOKABLE void selectRange(const QString& fromId, const QString& toId);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool isSelected(const QString& zoneId) const;

    /**
     * @brief Check if all selected zones have useCustomColors enabled
     * @return true if all selected zones use custom colors, false otherwise
     *
     * Performance optimization: O(n) C++ lookup instead of O(n*m) JavaScript nested loops.
     */
    Q_INVOKABLE bool allSelectedUseCustomColors() const;

    /**
     * @brief Select zones that intersect with a rectangle
     * @param x Rectangle X in relative coordinates (0.0-1.0)
     * @param y Rectangle Y in relative coordinates (0.0-1.0)
     * @param width Rectangle width in relative coordinates
     * @param height Rectangle height in relative coordinates
     * @param additive If true, add to existing selection; if false, replace
     * @return List of selected zone IDs
     *
     * Performance optimization: Avoids QVariantList copy and JavaScript iteration.
     * Used during marquee/rectangle selection drag operations.
     */
    Q_INVOKABLE QStringList selectZonesInRect(qreal x, qreal y, qreal width, qreal height, bool additive);

    // Batch operations for multi-selection
    Q_INVOKABLE void deleteSelectedZones();
    Q_INVOKABLE QStringList duplicateSelectedZones();
    Q_INVOKABLE bool moveSelectedZones(int direction, qreal step = 0.01);
    Q_INVOKABLE bool resizeSelectedZones(int direction, qreal step = 0.01);

    // Multi-zone drag operations
    /**
     * @brief Starts a multi-zone drag operation
     * @param primaryZoneId The zone being directly dragged
     * @param startX Initial X position of the primary zone
     * @param startY Initial Y position of the primary zone
     */
    Q_INVOKABLE void startMultiZoneDrag(const QString& primaryZoneId, qreal startX, qreal startY);

    /**
     * @brief Updates positions during multi-zone drag
     * @param primaryZoneId The zone being directly dragged
     * @param newX New X position of the primary zone
     * @param newY New Y position of the primary zone
     */
    Q_INVOKABLE void updateMultiZoneDrag(const QString& primaryZoneId, qreal newX, qreal newY);

    /**
     * @brief Ends the multi-zone drag operation
     * @param commit If true, commits the geometry changes; if false, cancels
     */
    Q_INVOKABLE void endMultiZoneDrag(bool commit);

    /**
     * @brief Checks if a multi-zone drag is in progress
     */
    Q_INVOKABLE bool isMultiZoneDragActive() const;

    // Batch appearance operations for multi-selection
    /**
     * @brief Updates an appearance property for all selected zones
     * @param propertyName Property to update (useCustomColors, opacity, borderWidth, borderRadius)
     * @param value New value for the property
     */
    Q_INVOKABLE void updateSelectedZonesAppearance(const QString& propertyName, const QVariant& value);

    /**
     * @brief Updates a color property for all selected zones
     * @param colorType Color to update (highlightColor, inactiveColor, borderColor)
     * @param color New color value (ARGB hex string)
     */
    Q_INVOKABLE void updateSelectedZonesColor(const QString& colorType, const QString& color);

    // Per-zone geometry mode operations
    /**
     * @brief Toggle a zone between Relative and Fixed geometry mode
     * @param zoneId PhosphorZones::Zone to toggle
     *
     * Converts between modes using the target screen resolution.
     * Creates an undo command for the toggle.
     */
    Q_INVOKABLE void toggleZoneGeometryMode(const QString& zoneId);

    /**
     * @brief Update fixed geometry for a zone (for spinbox edits)
     * @param zoneId PhosphorZones::Zone to update
     * @param x, y, w, h Fixed pixel coordinates
     */
    Q_INVOKABLE void updateZoneFixedGeometry(const QString& zoneId, qreal x, qreal y, qreal w, qreal h);

    /**
     * @brief Apply geometry mode and coordinates directly (for undo/redo)
     * @param zoneId PhosphorZones::Zone to update
     * @param mode Geometry mode (0=Relative, 1=Fixed)
     * @param relativeGeo Relative geometry
     * @param fixedGeo Fixed geometry
     */
    void applyZoneGeometryMode(const QString& zoneId, int mode, const QRectF& relativeGeo, const QRectF& fixedGeo);

    // Validation
    Q_INVOKABLE QString validateZoneName(const QString& zoneId, const QString& name);
    Q_INVOKABLE QString validateZoneNumber(const QString& zoneId, int number);

    // Default colors (for theme-based defaults)
    Q_INVOKABLE void setDefaultZoneColors(const QString& highlightColor, const QString& inactiveColor,
                                          const QString& borderColor);

    // Visibility filtering toggle methods
    Q_INVOKABLE void toggleScreenAllowed(const QString& screenName);
    Q_INVOKABLE QString screenDisplayName(const QString& screenIdOrName) const;
    Q_INVOKABLE void toggleDesktopAllowed(int desktop);
    Q_INVOKABLE void toggleActivityAllowed(const QString& activityId);

    // Import/Export operations
    Q_INVOKABLE void importLayout(const QString& filePath);
    Q_INVOKABLE void exportLayout(const QString& filePath);

    // Clipboard operations
    /**
     * @brief Copies selected zones to clipboard
     * @param zoneIds List of zone IDs to copy
     */
    Q_INVOKABLE void copyZones(const QStringList& zoneIds);

    /**
     * @brief Cuts selected zones (copy + delete)
     * @param zoneIds List of zone IDs to cut
     */
    Q_INVOKABLE void cutZones(const QStringList& zoneIds);

    /**
     * @brief Pastes zones from clipboard
     * @param withOffset If true, offset pasted zones by 2% to avoid overlap
     * @return List of newly pasted zone IDs, or empty list on failure
     */
    Q_INVOKABLE QStringList pasteZones(bool withOffset = false);

Q_SIGNALS:
    void layoutIdChanged();
    void layoutNameChanged();
    void zonesChanged();
    void selectedZoneIdChanged();
    void selectedZoneIdsChanged();
    void hasUnsavedChangesChanged();
    void gridSnappingEnabledChanged();
    void edgeSnappingEnabledChanged();
    void snapIntervalXChanged();
    void snapIntervalYChanged();
    void snapIntervalChanged(); // For backward compatibility
    void gridOverlayVisibleChanged();
    void editorDuplicateShortcutChanged();
    void editorSplitHorizontalShortcutChanged();
    void editorSplitVerticalShortcutChanged();
    void editorFillShortcutChanged();
    void snapOverrideModifierChanged();
    void fillOnDropEnabledChanged();
    void fillOnDropModifierChanged();
    void targetScreenChanged();
    void templatePreviewVerticalChanged();

    /// A screen switch was requested while unsaved edits were pending, so it
    /// was parked instead of applied. The UI prompts, then calls
    /// confirmPendingTargetScreen() or cancelPendingTargetScreen().
    void targetScreenChangeRequiresConfirmation(const QString& screenName);

    /// A forwarded launch (`plasmazones-editor --new` / `--layout <id>`) wants
    /// to replace the loaded layout while it has unsaved edits. The UI must
    /// answer with confirmPendingLaunch() or cancelPendingLaunch(), or the
    /// parked request lingers.
    void launchRequestRequiresConfirmation();

    void usableAreaInsetsChanged();
    void overlayDisplayModeChanged();
    void globalOverlayDisplayModeChanged();
    void useFullScreenGeometryChanged();
    void aspectRatioClassChanged();
    void targetScreenSizeChanged();

    // Visibility filtering signals
    void allowedScreensChanged();
    void allowedDesktopsChanged();
    void allowedActivitiesChanged();
    void availableScreenIdsChanged();
    void virtualDesktopCountChanged();
    void virtualDesktopNamesChanged();
    void activitiesAvailableChanged();
    void availableActivitiesChanged();

    // Incremental update signals (to avoid full Repeater rebuilds)
    void zoneGeometryChanged(const QString& zoneId);
    void zoneNameChanged(const QString& zoneId);
    void zoneNumberChanged(const QString& zoneId);
    void zoneColorChanged(const QString& zoneId);
    void zoneAdded(const QString& zoneId);
    void zoneRemoved(const QString& zoneId);

    void layoutSaved();
    void layoutExported();
    void layoutLoadFailed(const QString& error);
    void layoutSaveFailed(const QString& error);
    /// Relayed from ILayoutService::errorOccurred. The service's messages are
    /// self-describing ("Failed to load layout: …"), so QML toasts them
    /// verbatim; layoutLoadFailed/layoutSaveFailed carry only the
    /// controller-side failures whose toast adds the operation prefix.
    void serviceErrorOccurred(const QString& error);
    void editorClosed();

    // Validation signals
    void zoneNameValidationError(const QString& zoneId, const QString& error);
    void zoneNumberValidationError(const QString& zoneId, const QString& error);

    // Preview mode signal
    void previewModeChanged();

    // Editing mode (template-state signals live on EditorTemplateModel)
    void editorModeChanged();

    // Clipboard signals
    void canPasteChanged();
    void clipboardOperationFailed(const QString& error);

private:
    void markUnsaved();
    void cacheVirtualScreenGeometry(const QString& screenName);
    /// Carry out the gated screen switch (caller cleared the replace
    /// decision). forceLayoutMode = the plain-screen launch shape: flips out
    /// of template/preview mode at apply time and loads even same-screen.
    void applyTargetScreen(const QString& screenName, bool forceLayoutMode = false);
    void applyUsableAreaInsets(const QRect& fullGeom, const QRect& availGeom);
    void setInsets(int left, int top, int right, int bottom);

    /**
     * @brief Z-order operation types for changeZOrderImpl
     */
    enum class ZOrderOp {
        BringToFront,
        SendToBack,
        BringForward,
        SendBackward
    };

    /**
     * @brief Internal implementation for all z-order operations
     * @param zoneId PhosphorZones::Zone to modify
     * @param op Z-order operation to perform
     * @param actionName Undo action display name (already translated)
     */
    void changeZOrderImpl(const QString& zoneId, ZOrderOp op, const QString& actionName);

    /**
     * @brief Check if required services are ready for operations
     * @param operation Description of the operation (for logging)
     * @return true if both m_undoController and m_zoneManager are valid
     */
    bool servicesReady(const char* operation) const;

    /**
     * @brief Sync single-selection with multi-selection and emit signals
     *
     * Updates m_selectedZoneId to match first item in m_selectedZoneIds
     * and emits the appropriate changed signals. Call after modifying
     * m_selectedZoneIds directly.
     */
    void syncSelectionSignals();

    /**
     * @brief Loads editor settings from KConfig
     */
    void loadEditorSettings();

    /**
     * @brief Saves editor settings to KConfig
     */
    /// Queue an editor-settings write. Coalesced behind
    /// m_editorSettingsSaveTimer — see flushEditorSettings for the real work.
    void saveEditorSettings();
    /// Write the editor settings to config.json and tell the daemon to reparse.
    /// Runs off m_editorSettingsSaveTimer.
    void flushEditorSettings();
    /// flushEditorSettings for teardown: same write, but the daemon reload is
    /// blocking so it is actually delivered before the process goes away. Only
    /// ~EditorController calls this — see the note there.
    void flushEditorSettingsBlocking();
    /// The config write both flush paths share. Returns false when nothing
    /// reached disk, in which case there is nothing for the daemon to reload.
    bool writeEditorSettingsToDisk();

    /**
     * @brief Handles clipboard content changes
     *
     * Updates canPaste property and emits signal when clipboard state changes.
     * This enables reactive QML bindings to update when clipboard changes.
     */
    void onClipboardChanged();

    // PhosphorZones::Layout data
    QString m_layoutId;
    QString m_layoutName;
    int m_zonesVersion = 0; // Increments on any zone change (lightweight binding dependency)
    QString m_selectedZoneId; // Use ID instead of index (backward compat: synced with first of m_selectedZoneIds)
    QStringList m_selectedZoneIds; // Multi-selection: list of selected zone IDs
    bool m_hasUnsavedChanges = false;
    bool m_isNewLayout = false;
    bool m_previewMode = false;

    // ─── Editing mode + scrolling-template state ─────────────────────────
    int m_editorMode = ModeLayout;
    /// Edit-state sub-model (child QObject; reaches back for undo + dirty).
    EditorTemplateModel* m_scrollingTemplate = nullptr;
    /// Local read view of the template files, the template sibling of
    /// m_localLayoutManager: instant opens without the daemon, offline save
    /// fallback, reloaded on the daemon's scrollingTemplatesChanged signal.
    std::unique_ptr<PhosphorZones::ScrollingTemplateStore> m_templateStore;

    /// Flip the editing mode, resetting cross-mode UI state (selection).
    void setEditorModeInternal(int mode);
    /// The template save path saveLayout() dispatches to in template mode.
    bool saveScrollingTemplateNow();

    // Services (dependency injection)
    ILayoutService* m_layoutService = nullptr;
    ZoneManager* m_zoneManager = nullptr;
    SnappingService* m_snappingService = nullptr;
    TemplateService* m_templateService = nullptr;
    UndoController* m_undoController = nullptr;
    // Per-layout gap override sub-model (child QObject; reaches back into this
    // controller to push undo commands and mark the layout dirty).
    EditorGapsModel* m_gaps = nullptr;

    // ─── DECLARATION ORDER INVARIANT ─────────────────────────────────
    // m_localRuleStore is borrowed by BOTH m_localRuleStoreWatcher and
    // m_localLayoutManager below. Members destruct in reverse declaration
    // order, so the store must be declared FIRST to outlive both borrowers:
    //   1. ~m_localLayoutManager, ~m_localRuleStoreWatcher (drop borrowed
    //      pointers into the store).
    //   2. ~m_localRuleStore last.
    // Do not reorder without revisiting every borrower's destructor.
    /// Owned Rule store backing the in-process LayoutRegistry's
    /// assignment cascade. Declared before m_localLayoutManager so it
    /// outlives the registry that borrows it (members destruct in reverse
    /// declaration order). Points at the shared rules.json so the
    /// editor's local registry sees the same rule set the daemon writes.
    std::unique_ptr<PhosphorRules::RuleStore> m_localRuleStore;
    /// Opt-in cross-process auto-reload of m_localRuleStore. The editor has no
    /// daemon D-Bus rules-reload path, so this watcher is its only way to pick
    /// up another process's rules.json writes while it is open. Declared
    /// after the store it borrows so it tears down first.
    std::unique_ptr<PhosphorRules::RuleStoreWatcher> m_localRuleStoreWatcher;
    std::unique_ptr<PhosphorZones::LayoutRegistry> m_localLayoutManager;

    /// Debounces D-Bus layout-mutation bursts (layoutCreated / layoutDeleted /
    /// layoutChanged / layoutListChanged / layoutPropertyChanged) into a
    /// single reloadLocalLayouts() call. Mirrors the SettingsController
    /// m_layoutLoadTimer pattern so a typical editor save — which fires
    /// layoutChanged + layoutListChanged back-to-back — only hits the
    /// PhosphorZones::LayoutRegistry once.
    QTimer m_layoutReloadTimer;
    /// Coalesces saveEditorSettings() bursts. Every snapping/fill-on-drop
    /// setter calls saveEditorSettings, and ControlBar drives snapIntervalX
    /// from Slider.onMoved — one call per mouse-move step. Each one rewrites
    /// the whole config document, fsyncs it, and makes the daemon reparse its
    /// entire config, so running that per tick is a drag-long stutter. Batch
    /// the burst into one write once the value settles.
    QTimer m_editorSettingsSaveTimer;
    /// Debounces settingsChanged bursts (a settings-app Save emits per key
    /// group) into one reloadScrollingStripAxis() re-read.
    QTimer m_stripAxisReloadTimer;

    /// The one snapshot writer for the strip-axis inputs (global value +
    /// per-screen overrides), shared by loadEditorSettings and the
    /// settingsChanged reload; routes a genuine change through
    /// refreshTemplatePreviewVertical's own change gate.
    void refreshScrollingStripAxisSnapshot(PhosphorConfig::IBackend& backend);

    /// Recompute zone geometry for every manual layout against the primary
    /// screen so a layout opened through the in-process registry carries its
    /// fixed-geometry zones at their authored dimensions — see
    /// SettingsController for the matching implementation.
    void recalcLocalLayouts();

    bool m_gridOverlayVisible = true; // Grid overlay visibility (independent of snapping)

    // Keyboard shortcuts (app-specific, loaded from settings)
    // Standard shortcuts (Save, Delete, Close) use Qt StandardKey (system shortcuts)
    QString m_editorDuplicateShortcut = QStringLiteral("Ctrl+D");
    QString m_editorSplitHorizontalShortcut = QStringLiteral("Ctrl+Shift+H");
    QString m_editorSplitVerticalShortcut =
        QStringLiteral("Ctrl+Alt+V"); // Note: Ctrl+Shift+V conflicts with Paste with Offset
    QString m_editorFillShortcut = QStringLiteral("Ctrl+Shift+F");
    int m_snapOverrideModifier = 0x02000000; // Qt::ShiftModifier

    // Fill on drop settings
    bool m_fillOnDropEnabled = true; // Enabled by default
    int m_fillOnDropModifier = 0x04000000; // Qt::ControlModifier (different from snap override)

    // Screen
    QString m_targetScreen;

    // Strip-axis inputs for templatePreviewVertical(), snapshotted by
    // loadEditorSettings(). The tri-state config enum (Auto / Horizontal /
    // Vertical), NOT PhosphorProtocol::ScrollAxis — the two numberings differ
    // on purpose and are never cast between.
    /// Auto. Spelled as the literal rather than ConfigDefaults::
    /// scrollingStripAxis() so this header need not pull in configdefaults.h;
    /// loadEditorSettings() carries a static_assert tying the two together.
    int m_scrollingStripAxis = 0;
    /// Per-screen StripAxis overrides, keyed by the screen id or name the
    /// group was stored under. Absent screen means "use the global value".
    QHash<QString, int> m_perScreenStripAxis;
    /// The NOTIFY comparand: the last answer refreshTemplatePreviewVertical()
    /// resolved, and that refresh is its ONLY writer. The getter computes
    /// fresh per read instead of stamping, so a binding evaluating between an
    /// input change and the refresh cannot consume the flip and suppress the
    /// change signal for every other consumer.
    bool m_templatePreviewVertical = false;
    /// The pure resolution behind both the getter and the refresh: per-screen
    /// override (looked up under every ScreenIdentity spelling of the
    /// target), then the global setting, then the Auto size rule. Const and
    /// side-effect free.
    bool resolveTemplatePreviewVertical() const;
    /// Re-resolve the preview axis and emit the change signal only when the
    /// answer actually flipped. Every input that feeds the axis routes here
    /// rather than emitting directly.
    void refreshTemplatePreviewVertical();
    /// Launch arguments parked by requestLaunch() while unsaved edits are
    /// pending. nullopt when nothing is awaiting confirmation.
    struct PendingLaunch
    {
        QString screenId;
        QString layoutId;
        bool createNew = false;
        bool preview = false;
        QString templateId;
        bool newTemplate = false;
    };
    std::optional<PendingLaunch> m_pendingLaunch;
    /// Apply a launch request outright, replacing whatever is loaded.
    void applyLaunch(const PendingLaunch& launch);
    /// Shared template session-begin tail (signal-order contract on the definition).
    void beginTemplateSession(const QString& id, const QString& name, bool isNew, const QVariantMap& state,
                              bool isSystem);

    /// Screen parked by setTargetScreen() / a plain-screen launch while
    /// unsaved edits are pending (optional: "" is a legitimate value). The
    /// bool is the launch shape's intent — confirm then also leaves
    /// template/preview mode and loads. Both reset on any mode change.
    std::optional<QString> m_pendingTargetScreen;
    bool m_pendingTargetEditsLayout = false;
    QSize m_virtualScreenSize; ///< Cached VS geometry size (valid when m_targetScreen is virtual)
    QRect m_virtualScreenRect; ///< Cached VS absolute geometry (position within physical monitor)
    /// Layout-derived reference-size override. When valid, takes precedence
    /// over m_virtualScreenSize in targetScreenSize() — used for fixed-
    /// geometry layouts whose zone bounding box defines the canvas the
    /// editor / preview should render against, independent of the live
    /// screen size. Cleared for relative-only layouts.
    QSize m_layoutBoundsOverride;
    int m_insetLeft = 0;
    int m_insetTop = 0;
    int m_insetRight = 0;
    int m_insetBottom = 0;

    // Default colors (for theme-based defaults, set from QML)
    QString m_defaultHighlightColor;
    QString m_defaultInactiveColor;
    QString m_defaultBorderColor;

    // PhosphorZones::Zone settings (per-layout override, -1 = use global)
    // Per-layout zone-padding / edge-gap overrides live in m_gaps (see the
    // gaps property); overlay display mode stays here.
    int m_overlayDisplayMode = -1; // -1 = use global setting
    bool m_useFullScreenGeometry = false;
    int m_aspectRatioClass = 0; // 0 = Any (AspectRatioClass::Any)
    int m_cachedGlobalOverlayDisplayMode = 0; // Cached global overlay display mode

    // Clipboard state
    bool m_canPaste = false;

    // Visibility filtering state
    QStringList m_allowedScreens;
    QList<int> m_allowedDesktopsInt;
    QStringList m_allowedActivities;
    QStringList m_availableScreenIds;
    int m_virtualDesktopCount = 1;
    QStringList m_virtualDesktopNames;
    bool m_activitiesAvailable = false;
    QVariantList m_availableActivities;

    // Label font settings (read-only from Appearance config)
    QString m_labelFontFamily;
    qreal m_labelFontSizeScale = 1.0;
    int m_labelFontWeight = QFont::Bold;
    bool m_labelFontItalic = false;
    bool m_labelFontUnderline = false;
    bool m_labelFontStrikeout = false;

    // Multi-zone drag state
    bool m_multiZoneDragActive = false;
    QString m_dragPrimaryZoneId;
    qreal m_dragStartX = 0.0;
    qreal m_dragStartY = 0.0;
    QMap<QString, QPointF> m_dragInitialPositions; // Initial positions of all selected zones
};

} // namespace PlasmaZones

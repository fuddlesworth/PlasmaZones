// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <atomic>
#include <memory>

#include "../core/interfaces.h"
#include "../core/layout.h"
#include "vulkan_support.h"

class QQmlEngine;

namespace PlasmaZones {
class CavaService;
class WindowThumbnailService;
}
class QQuickWindow;
class QScreen;
class QTimer;

namespace PlasmaZones {

class Zone;

/**
 * @brief Manages zone overlay windows
 *
 * Creates and manages overlay windows per screen, updates appearance
 * from settings, and provides zone highlighting/visual feedback.
 */
class OverlayService : public IOverlayService
{
    Q_OBJECT

    Q_PROPERTY(bool visible READ isVisible NOTIFY visibilityChanged)
    Q_PROPERTY(bool zoneSelectorVisible READ isZoneSelectorVisible NOTIFY zoneSelectorVisibilityChanged)

public:
    /**
     * @brief Per-screen overlay state, grouping window pointers, physical screen
     * references, and geometry that were previously stored in parallel QHash maps.
     */
    struct PerScreenOverlayState
    {
        QQuickWindow* overlayWindow = nullptr;
        QScreen* overlayPhysScreen = nullptr;
        QRect overlayGeometry;
        QMetaObject::Connection overlayGeomConnection; ///< geometryChanged connection for overlay
        QQuickWindow* zoneSelectorWindow = nullptr;
        QScreen* zoneSelectorPhysScreen = nullptr;
        /// Intended geometry of the zone selector window. On Wayland LayerShell, QWindow::geometry()
        /// is unreliable until the compositor acknowledges the surface position. This field stores
        /// the geometry we requested so hit-testing in updateSelectorPosition() can use it immediately.
        QRect zoneSelectorGeometry;
        QQuickWindow* layoutOsdWindow = nullptr;
        QScreen* layoutOsdPhysScreen = nullptr;
        QQuickWindow* navigationOsdWindow = nullptr;
        QScreen* navigationOsdPhysScreen = nullptr;
    };

    explicit OverlayService(QObject* parent = nullptr);
    ~OverlayService() override;

    // IOverlayService interface
    bool isVisible() const override;
    void show() override;
    void showAtPosition(int cursorX, int cursorY) override;
    void hide() override;
    void toggle() override;

    void updateLayout(Layout* layout) override;
    void updateSettings(ISettings* settings) override;
    void updateGeometries() override;

    // Zone highlighting for overlay display (IOverlayService interface)
    void highlightZone(const QString& zoneId) override;
    void highlightZones(const QStringList& zoneIds) override;
    void clearHighlight() override;

    // Additional methods
    void setLayout(Layout* layout);
    Layout* layout() const
    {
        return m_layout;
    }

    void setSettings(ISettings* settings);
    void setLayoutManager(ILayoutManager* layoutManager);
    void setCurrentVirtualDesktop(int desktop);
    void setCurrentActivity(const QString& activityId);

    /**
     * @brief Set which layout types appear in the zone picker
     *
     * When autotile mode is active, show only dynamic layouts.
     * When manual mode is active, show only manual layouts.
     * The autotile feature gate (KCM setting) controls whether dynamic layouts
     * are ever visible.
     */
    void setLayoutFilter(bool includeManual, bool includeAutotile);

    /**
     * @brief Set screens to exclude from overlay display
     *
     * Used to suppress the overlay on autotile-managed screens in mixed
     * multi-monitor mode. The overlay will not be shown or updated on
     * screens whose names appear in the set.
     */
    void setExcludedScreens(const QSet<QString>& screenIds);

    // Screen management
    void setupForScreen(QScreen* screen);
    void removeScreen(QScreen* screen);
    void handleScreenAdded(QScreen* screen);
    void handleScreenRemoved(QScreen* screen);

    // Zone selector management (IOverlayService interface)
    bool isZoneSelectorVisible() const override;
    void showZoneSelector(const QString& targetScreenId = QString()) override;
    void hideZoneSelector() override;
    void updateSelectorPosition(int cursorX, int cursorY) override;
    void scrollZoneSelector(int angleDeltaY) override;

    // Mouse position for shader effects
    void updateMousePosition(int cursorX, int cursorY) override;

    // Filtered layout count for trigger edge computation
    int visibleLayoutCount(const QString& screenId) const override;

    // Selected zone from zone selector (IOverlayService interface)
    bool hasSelectedZone() const override;
    QString selectedLayoutId() const override
    {
        return m_selectedLayoutId;
    }
    int selectedZoneIndex() const override
    {
        return m_selectedZoneIndex;
    }
    QRect getSelectedZoneGeometry(QScreen* screen) const override;
    QRect getSelectedZoneGeometry(const QString& screenId) const override;
    void clearSelectedZone() override;

    // Layout OSD (visual preview when switching layouts)
    // screenId: target screen (empty = screen under cursor, fallback to primary)
    void showLayoutOsd(Layout* layout, const QString& screenId = QString());
    void showLayoutOsd(const QString& id, const QString& name, const QVariantList& zones, int category,
                       bool autoAssign = false, const QString& screenId = QString(), bool showMasterDot = false,
                       bool producesOverlappingZones = false, const QString& zoneNumberDisplay = QStringLiteral("all"),
                       int masterCount = 1);
    void showLockedLayoutOsd(Layout* layout, const QString& screenId = QString());
    void showDisabledOsd(const QString& reason, const QString& screenId = QString());

    /**
     * @brief Pre-create Layout OSD QML windows for all connected screens.
     *
     * First-time QML compilation of LayoutOsd.qml takes ~100-300ms (component
     * loading, scene graph creation, Wayland layer-shell registration).  Call
     * this early (deferred from daemon start) so the first layout switch OSD
     * appears instantly instead of blocking the event loop.
     */
    void warmUpLayoutOsd();

    /**
     * @brief Pre-create Navigation OSD QML windows for all connected screens.
     *
     * Same rationale as warmUpLayoutOsd(): avoids ~100-300ms QML compilation
     * delay on the first keyboard navigation action after daemon start or
     * after the dismiss timer destroys the previous window.
     */
    void warmUpNavigationOsd();

    // Navigation OSD (feedback for keyboard navigation)
    void showNavigationOsd(bool success, const QString& action, const QString& reason,
                           const QString& sourceZoneId = QString(), const QString& targetZoneId = QString(),
                           const QString& screenId = QString());

    // Shader preview overlay (editor Shader Settings dialog - dedicated window avoids multi-pass clear issues)
    void showShaderPreview(int x, int y, int width, int height, const QString& screenId, const QString& shaderId,
                           const QString& shaderParamsJson, const QString& zonesJson) override;
    void updateShaderPreview(int x, int y, int width, int height, const QString& shaderParamsJson,
                             const QString& zonesJson) override;
    void hideShaderPreview() override;

    // Snap Assist overlay (window picker after snapping)
    void showSnapAssist(const QString& screenId, const QString& emptyZonesJson, const QString& candidatesJson) override;
    void hideSnapAssist() override;
    bool isSnapAssistVisible() const override;
    void setSnapAssistThumbnail(const QString& kwinHandle, const QString& dataUrl) override;

    // Layout Picker overlay (interactive layout browser + resnap)
    void showLayoutPicker(const QString& screenId = QString());
    bool isLayoutPickerVisible() const;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

public Q_SLOTS:
    void hideLayoutOsd();
    void hideNavigationOsd();
    void hideLayoutPicker();
    void onZoneSelected(const QString& layoutId, int zoneIndex, const QVariant& relativeGeometry);

    // Shader error reporting from QML
    void onShaderError(const QString& errorLog);

private Q_SLOTS:
    void onSnapAssistWindowSelected(const QString& windowId, const QString& zoneId, const QString& geometryJson);
    void onLayoutPickerSelected(const QString& layoutId);

private:
    // Sync CAVA service state (start/stop/reconfigure) with current settings.
    void syncCavaState();

    // Refresh zone selector and overlay windows that are currently visible.
    // Skips hidden windows — showZoneSelector()/show() refresh before showing.
    void refreshVisibleWindows();

    // Hide overlay/selector windows on screens where the current context is disabled,
    // then update remaining visible windows. Used by setCurrentVirtualDesktop/Activity.
    void hideDisabledAndRefresh();

    void createOverlayWindow(QScreen* screen);
    void destroyOverlayWindow(QScreen* screen);
    void dismissOverlayWindow(QScreen* screen);
    void updateOverlayWindow(QScreen* screen);
    void recreateOverlayWindowsOnTypeMismatch();

    /**
     * @brief Create/destroy/update overlay windows keyed by screen ID
     *
     * Virtual-screen-aware overloads. The screenId can be a physical screen ID
     * or a virtual screen ID (format "physicalId/vs:N"). The physScreen is the
     * backing QScreen* for Wayland layer-shell parenting.
     */
    void createOverlayWindow(const QString& screenId, QScreen* physScreen, const QRect& geometry);
    void destroyOverlayWindow(const QString& screenId);
    void updateOverlayWindow(const QString& screenId, QScreen* physScreen);

    void updateLabelsTextureForWindow(QQuickWindow* window, const QVariantList& patched, QScreen* screen,
                                      Layout* screenLayout);
    QVariantList buildZonesList(QScreen* screen) const;
    QVariantList buildZonesList(const QString& screenId, QScreen* physScreen) const;
    QVariantList buildLayoutsList(const QString& screenId = QString()) const;
    QVariantMap zoneToVariantMap(Zone* zone, QScreen* screen, Layout* layout = nullptr) const;
    QVariantMap zoneToVariantMap(Zone* zone, const QString& screenId, QScreen* physScreen, const QRect& overlayGeometry,
                                 Layout* layout = nullptr) const;

    /**
     * @brief Resolve the layout for a given screen with fallback chain
     *
     * Tries: per-screen assignment → activeLayout → m_layout
     */
    Layout* resolveScreenLayout(QScreen* screen) const;
    Layout* resolveScreenLayout(const QString& screenId) const;

    std::unique_ptr<QQmlEngine> m_engine;
    QHash<QString, PerScreenOverlayState> m_screenStates;
    QPointer<Layout> m_layout;
    QPointer<ISettings> m_settings;
    ILayoutManager* m_layoutManager = nullptr;
    int m_currentVirtualDesktop = 1; // Current virtual desktop (1-based)
    QString m_currentActivity; // Current KDE activity (empty = all activities)
    bool m_visible = false;
    bool m_zoneSelectorVisible = false;
    bool m_zoneSelectorRecreationPending =
        false; // Guard against re-entrant showZoneSelector during deferred recreation
    QString m_currentOverlayScreenId; // Effective screen ID overlay is shown on (single-monitor mode, for #136)

    // Zone selector selection tracking
    QString m_selectedLayoutId;
    int m_selectedZoneIndex = -1;
    QRectF m_selectedZoneRelGeo;

    // Layout OSD and Navigation OSD windows are stored in m_screenStates

    // Shader preview overlay (editor dialog)
    QQuickWindow* m_shaderPreviewWindow = nullptr;
    QPointer<QScreen> m_shaderPreviewScreen;
    QString m_shaderPreviewShaderId; // Shader ID for param translation in updateShaderPreview
    QString m_shaderPreviewScreenId; // Virtual screen ID from showShaderPreview (avoids re-resolving from QScreen*)

    // Snap Assist overlay (window picker after snapping)
    QQuickWindow* m_snapAssistWindow = nullptr;
    QPointer<QScreen> m_snapAssistScreen;
    QString m_snapAssistScreenId;
    std::unique_ptr<WindowThumbnailService> m_thumbnailService;
    QVariantList m_snapAssistCandidates; // Mutable copy for async thumbnail updates
    QStringList m_thumbnailCaptureQueue; // Sequential capture to avoid overwhelming KWin
    QHash<QString, QString> m_thumbnailCache; // kwinHandle -> dataUrl; reused across continuation
    // Layout Picker overlay (interactive layout browser)
    QQuickWindow* m_layoutPickerWindow = nullptr;
    QPointer<QScreen> m_layoutPickerScreen;
    QString m_layoutPickerScreenId;

    bool m_screenAddedConnected = false; // Guard for screenAdded connection (lambdas can't use UniqueConnection)

    // Persistent 1x1 keep-alive window that prevents Qt from tearing down
    // global Wayland/Vulkan protocol objects when all other windows are destroyed.
    QPointer<QQuickWindow> m_keepAliveWindow;

    // Track screens with failed window creation to prevent log spam
    QHash<QString, bool> m_navigationOsdCreationFailed;
    // Deduplicate navigation feedback (prevent duplicate OSDs from Qt signal + D-Bus signal)
    QString m_lastNavigationActionKey; // "action:reason" composite key
    QString m_lastNavigationScreenId;
    QElapsedTimer m_lastNavigationTime;

    void createZoneSelectorWindow(const QString& screenId, QScreen* physScreen, const QRect& geom);
    void destroyZoneSelectorWindow(const QString& screenId);
    void updateZoneSelectorWindow(const QString& screenId);
    void showLayoutOsdImpl(Layout* layout, const QString& screenId, bool locked);
    void createLayoutOsdWindow(const QString& screenId, QScreen* physScreen);
    void destroyLayoutOsdWindow(const QString& screenId);
    void createNavigationOsdWindow(const QString& screenId, QScreen* physScreen);
    void destroyNavigationOsdWindow(const QString& screenId);

    void destroyIfTypeMismatch(const QString& screenId);
    void createShaderPreviewWindow(QScreen* screen, const QString& screenId = QString());
    void destroyShaderPreviewWindow();

    /// Destroy all overlay, OSD, zone selector, snap assist, and layout picker windows
    /// backed by the given physical screen. Used by both virtualScreensChanged and handleScreenRemoved.
    void destroyAllWindowsForPhysicalScreen(QScreen* screen);

    void createSnapAssistWindow(QScreen* physScreen);
    void destroySnapAssistWindow();

    void createLayoutPickerWindow(QScreen* physScreen);
    void destroyLayoutPickerWindow();

    /** Update a candidate's thumbnail in m_snapAssistCandidates and push to QML. */
    void updateSnapAssistCandidateThumbnail(const QString& kwinHandle, const QString& dataUrl);
    /** Process next thumbnail in queue (sequential capture to avoid KWin overload). */
    void processNextThumbnailCapture();

    /**
     * @brief Re-assert a window's screen and geometry before showing on Wayland
     *
     * The QPA plugin binds the Wayland output once during LayerSurface/platform
     * window construction. Set QWindow::screen() BEFORE the window is shown.
     */
    static void assertWindowOnScreen(QWindow* window, QScreen* screen, const QRect& geometry = QRect());

    /**
     * @brief Prepare layout OSD window for display
     * @param window Output: the prepared window (nullptr on failure)
     * @param screenGeom Output: screen geometry
     * @param aspectRatio Output: calculated aspect ratio
     * @param screenId Target screen (empty = primary)
     * @return true if window is ready, false on failure
     */
    bool prepareLayoutOsdWindow(QQuickWindow*& window, QScreen*& outPhysScreen, QRect& screenGeom, qreal& aspectRatio,
                                const QString& screenId = QString());

    /**
     * @brief Create a QML window from a resource URL
     * @param qmlUrl QML resource URL (e.g., "qrc:/ui/ZoneOverlay.qml")
     * @param screen Screen to assign the window to
     * @param windowType Description for logging (e.g., "overlay", "zone selector")
     * @return Created window with C++ ownership, or nullptr on failure
     *
     * Handles common QML window creation: component loading, error checking,
     * QQuickWindow casting, ownership, and screen assignment.
     */
    QQuickWindow* createQmlWindow(const QUrl& qmlUrl, QScreen* screen, const char* windowType,
                                  const QVariantMap& initialProperties = QVariantMap());

    // Audio viz: push spectrum to overlay windows
    void onAudioSpectrumUpdated(const QVector<float>& spectrum);

    // Shader support methods
    bool useShaderForScreen(QScreen* screen) const;
    bool useShaderForScreen(const QString& screenId) const;
    bool anyScreenUsesShader() const;
    bool canUseShaders() const;
    void startShaderAnimation();
    void stopShaderAnimation();
    void updateShaderUniforms();
    void updateZonesForAllWindows();

    /**
     * @brief Initialize and show overlay for a given screen or cursor position
     * @param cursorScreen Screen where cursor is located (nullptr = show on all monitors)
     *
     * This is the common implementation for show() and showAtPosition().
     * Extracts ~100 lines of duplicate code from both methods.
     */
    void initializeOverlay(QScreen* cursorScreen, const QPoint& cursorPos = QPoint(-1, -1));

private Q_SLOTS:
    // System sleep/resume handler (connected to logind PrepareForSleep signal)
    void onPrepareForSleep(bool goingToSleep);

private:
    // Shader timing (shared across all monitors for synchronized effects)
    QElapsedTimer m_shaderTimer;
    std::atomic<qint64> m_lastFrameTime{0};
    std::atomic<int> m_frameCount{0};
    QTimer* m_shaderUpdateTimer = nullptr;
    mutable QMutex m_shaderTimerMutex;

    // Shader state
    bool m_zoneDataDirty = true;
    QString m_pendingShaderError;

    // Monotonic counter for unique layer-shell scope strings.
    // Appended to each configureLayerSurface() scope so KWin sees every
    // new surface as unique, avoiding configure rate-limiting after rapid
    // destroy/recreate cycles on Vulkan.
    uint64_t m_scopeGeneration = 0;

    // CAVA audio visualization
    std::unique_ptr<CavaService> m_cavaService;

    // Zone data version for shader synchronization
    int m_zoneDataVersion = 0;

    // Layout filter: which types to include in zone picker (set by Daemon)
    bool m_includeManualLayouts = true;
    bool m_includeAutotileLayouts = false;

    // Screens excluded from overlay display (autotile-managed screens)
    QSet<QString> m_excludedScreens;

    // Fallback QVulkanInstance for when 'auto' backend resolves to Vulkan
#if QT_CONFIG(vulkan)
    std::unique_ptr<QVulkanInstance> m_fallbackVulkanInstance;
#endif
};

} // namespace PlasmaZones

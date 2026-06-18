// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

# Phase 2: DRM/KMS Backend + Session Management

## Deliverables

- DRM backend with atomic modesetting
- GBM buffer allocation for scanout framebuffers
- Page-flip event handling integrated with wayland event loop
- Multi-output support with hotplug via udev
- Session management via libseat (VT switching, DRM master)
- `DrmScreenProvider` implementing `PhosphorScreens::IScreenProvider`

## Class Hierarchy

```
IBackend
└── DrmBackend
    ├── owns Session (libseat wrapper)
    ├── owns UdevMonitor (hotplug detection)
    ├── owns DrmDevice (one per GPU)
    └── creates DrmOutput per connector

DrmDevice
├── fd (DRM master file descriptor)
├── GbmDevice (gbm_device* for buffer allocation)
├── DrmConnector[] (discovered connectors)
├── DrmCrtc[] (available CRTCs)
├── DrmPlane[] (available planes: primary, cursor, overlay)
└── allocates DrmOutput per enabled connector

DrmOutput : IOutput
├── DrmConnector (which connector)
├── DrmCrtc (assigned CRTC)
├── DrmPlane (primary plane)
├── DrmPlane (cursor plane, optional)
├── GbmSwapchain (front/back buffers)
├── owns page-flip state machine
└── drives frame scheduling

Session
├── libseat_seat* handle
├── session enable/disable callbacks
└── device open/close proxying

UdevMonitor
├── udev_monitor* for DRM subsystem
├── fd integrated into wayland event loop
└── emits hotplug events to DrmBackend
```

## File Map

```
libs/phosphor-compositor-core/src/backend/
├── drm_backend.h
├── drm_backend.cpp
├── drm_device.h
├── drm_device.cpp
├── drm_output.h
├── drm_output.cpp
├── drm_connector.h          — RAII wrapper for drmModeConnector
├── drm_crtc.h               — RAII wrapper for drmModeCrtc
├── drm_plane.h              — RAII wrapper for drmModePlane + properties
├── drm_properties.h         — Property ID cache (avoids repeated drmModeObjectGetProperties)
├── drm_properties.cpp
├── gbm_swapchain.h          — Double/triple buffer management
├── gbm_swapchain.cpp
├── session.h                — libseat wrapper
├── session.cpp
├── udev_monitor.h           — udev hotplug monitor
├── udev_monitor.cpp
└── drm_screen_provider.h    — PhosphorScreens::IScreenProvider impl
    drm_screen_provider.cpp
```

## DRM Atomic Modesetting Flow

```
1. Open DRM device fd (via libseat_open_device)
2. Enumerate resources:
   - drmModeGetResources → connectors, CRTCs, encoders
   - drmModeGetPlaneResources → planes
3. For each connected connector:
   a. Find compatible encoder → compatible CRTC (avoid conflicts)
   b. Find primary plane compatible with CRTC
   c. Select preferred mode (or user-configured mode)
   d. Create GbmSwapchain for this output
4. Build atomic commit:
   - CRTC properties: MODE_ID (blob), ACTIVE=1
   - Connector properties: CRTC_ID
   - Plane properties: FB_ID, CRTC_ID, SRC_*, CRTC_*
5. drmModeAtomicCommit(DRM_MODE_ATOMIC_ALLOW_MODESET)
6. Output is now displaying
```

### Atomic Commit for Frame Presentation

```cpp
void DrmOutput::presentFrame(gbm_bo* bo) {
    uint32_t fbId = getFbForBo(bo);  // cached lookup (FB created once at buffer allocation)

    auto* req = drmModeAtomicAlloc();
    if (!req) return;

    // Set primary plane to new framebuffer
    drmModeAtomicAddProperty(req, m_primaryPlane.id, m_primaryPlane.propFbId, fbId);
    drmModeAtomicAddProperty(req, m_primaryPlane.id, m_primaryPlane.propCrtcId, m_crtc.id);
    drmModeAtomicAddProperty(req, m_primaryPlane.id, m_primaryPlane.propSrcX, 0);
    drmModeAtomicAddProperty(req, m_primaryPlane.id, m_primaryPlane.propSrcY, 0);
    drmModeAtomicAddProperty(req, m_primaryPlane.id, m_primaryPlane.propSrcW,
                             m_mode.hdisplay << 16);
    drmModeAtomicAddProperty(req, m_primaryPlane.id, m_primaryPlane.propSrcH,
                             m_mode.vdisplay << 16);
    drmModeAtomicAddProperty(req, m_primaryPlane.id, m_primaryPlane.propCrtcX, 0);
    drmModeAtomicAddProperty(req, m_primaryPlane.id, m_primaryPlane.propCrtcY, 0);
    drmModeAtomicAddProperty(req, m_primaryPlane.id, m_primaryPlane.propCrtcW,
                             m_mode.hdisplay);
    drmModeAtomicAddProperty(req, m_primaryPlane.id, m_primaryPlane.propCrtcH,
                             m_mode.vdisplay);

    uint32_t flags = DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;
    int ret = drmModeAtomicCommit(m_device.fd(), req, flags, this);
    drmModeAtomicFree(req);

    if (ret == 0) {
        m_pageFlipPending = true;
    } else if (ret == -EBUSY) {
        // Previous flip not done yet — skip this frame (no-op)
    } else if (ret == -EACCES || ret == -EPERM) {
        // Lost DRM master (VT switch race) — stop rendering until session reactivates
        m_pageFlipPending = false;
    } else {
        // -EINVAL or other: invalid plane/mode configuration — log and skip
        qWarning("Atomic commit failed: %d", ret);
    }
}
```

## GBM Swapchain

```cpp
class GbmSwapchain {
public:
    GbmSwapchain(gbm_device* gbmDevice, int width, int height,
                 uint32_t format, uint64_t modifier);
    ~GbmSwapchain();  // calls drmModeRmFB + gbm_bo_destroy for each buffer
    GbmSwapchain(const GbmSwapchain&) = delete;
    GbmSwapchain& operator=(const GbmSwapchain&) = delete;
    GbmSwapchain(GbmSwapchain&&) = delete;
    GbmSwapchain& operator=(GbmSwapchain&&) = delete;

    /// Acquire the next buffer for rendering.
    /// Returns nullptr if all buffers are in-flight (shouldn't happen with double-buffer).
    gbm_bo* acquire();

    /// Release a buffer after page-flip completes.
    void release(gbm_bo* bo);

    /// Current buffer being rendered to.
    gbm_bo* current() const;

    int width() const;
    int height() const;
    uint32_t format() const;
    uint64_t modifier() const;

private:
    struct Buffer {
        gbm_bo* bo = nullptr;
        uint32_t fbId = 0;
        bool inFlight = false;
    };
    QList<Buffer> m_buffers;  // typically 2 (double-buffered)
    int m_currentIndex = 0;
    gbm_device* m_gbmDevice;
};
```

## Page-Flip State Machine

```
         ┌──────────────┐
         │   IDLE       │  (output has no pending flip, can accept new frame)
         └──────┬───────┘
                │ damage exists + clients need frame callback
                ▼
         ┌──────────────┐
         │  RENDERING   │  (renderer is compositing for this output)
         └──────┬───────┘
                │ render complete, submit to DRM
                ▼
         ┌──────────────┐
         │  FLIPPING    │  (atomic commit submitted, waiting for page-flip event)
         └──────┬───────┘
                │ DRM_EVENT_FLIP_COMPLETE received
                │ → release old buffer
                │ → dispatch frame callbacks to clients
                │ → if new damage exists: goto RENDERING
                ▼
         ┌──────────────┐
         │   IDLE       │
         └──────────────┘
```

## Session Management

```cpp
class Session : public QObject {
    Q_OBJECT
public:
    explicit Session(QObject* parent = nullptr);
    ~Session() override;

    bool initialize();
    void shutdown();

    /// Open a device via libseat (returns fd with appropriate permissions).
    int openDevice(const QString& path);
    void closeDevice(int deviceId);

    /// File descriptor for event polling.
    int fd() const;

    /// Dispatch pending session events (call when fd is readable).
    void dispatch();

    /// Is the session currently active (we have DRM master)?
    bool isActive() const;

Q_SIGNALS:
    void activated();    // Session gained — restore DRM, resume rendering
    void deactivated();  // Session lost — release DRM, stop rendering

private:
    static void handleEnable(libseat_seat* seat, void* data);
    static void handleDisable(libseat_seat* seat, void* data);

    libseat_seat* m_seat = nullptr;
    bool m_active = false;
};
```

### VT Switch Flow

```
User presses Ctrl+Alt+F2:
  1. Kernel sends VT switch request to libseat
  2. libseat calls handleDisable callback
  3. Session emits deactivated()
  4. DrmBackend receives deactivated:
     - Drop DRM master (libseat_switch_session)
     - Disable all outputs (stop rendering)
     - Store current output state

User returns (Ctrl+Alt+F1):
  1. Kernel grants VT back
  2. libseat calls handleEnable callback
  3. Session emits activated()
  4. DrmBackend receives activated:
     - Reacquire DRM master
     - Re-enumerate connectors (may have changed)
     - Restore output modes
     - Resume rendering (full repaint needed)
```

## UdevMonitor

```cpp
class UdevMonitor : public QObject {
    Q_OBJECT
public:
    explicit UdevMonitor(QObject* parent = nullptr);
    ~UdevMonitor() override;

    bool initialize();
    int fd() const;  // for event loop integration
    void dispatch(); // process pending events

Q_SIGNALS:
    void deviceAdded(const QString& syspath, dev_t devnum);
    void deviceRemoved(const QString& syspath, dev_t devnum);
    void deviceChanged(const QString& syspath, dev_t devnum);  // connector hotplug

private:
    struct udev* m_udev = nullptr;
    struct udev_monitor* m_monitor = nullptr;
};
```

### Hotplug Flow

```
Monitor connected:
  1. udev emits "change" event for DRM device
  2. UdevMonitor.dispatch() emits deviceChanged()
  3. DrmBackend handles:
     a. Re-enumerate connectors (drmModeGetResources)
     b. Detect new connected connector
     c. Allocate CRTC + plane
     d. Create DrmOutput
     e. Create GbmSwapchain
     f. Perform initial modeset
     g. Insert SceneOutput into scene graph
     h. Emit IScreenProvider::screenAdded()
     i. Notify clients via wl_output global

Monitor disconnected:
  1. Same udev path
  2. DrmBackend detects connector disconnected:
     a. Emit IScreenProvider::screenRemoved()
     b. Destroy SceneOutput (removes from scene graph)
     c. Destroy GbmSwapchain
     d. Release CRTC + plane
     e. Destroy DrmOutput
     f. Remove wl_output global (clients get wl_registry.global_remove)
```

## DrmScreenProvider (IScreenProvider implementation)

```cpp
class DrmScreenProvider : public PhosphorScreens::IScreenProvider {
    Q_OBJECT
public:
    explicit DrmScreenProvider(DrmBackend* backend, QObject* parent = nullptr);

    QList<PhosphorScreens::PhysicalScreen> screens() const override;
    PhosphorScreens::PhysicalScreen primaryScreen() const override;

private Q_SLOTS:
    void onOutputAdded(DrmOutput* output);
    void onOutputRemoved(DrmOutput* output);
    void onOutputModeChanged(DrmOutput* output);

private:
    PhosphorScreens::PhysicalScreen toPhysicalScreen(const DrmOutput* output) const;
    DrmBackend* m_backend;
};
```

Maps DRM connector names to `PhysicalScreen::name` (e.g., "DP-3"), EDID data to `PhysicalScreen::identifier`, and connector geometry to `PhysicalScreen::geometry`.

## DRM Property Cache

Avoid calling `drmModeObjectGetProperties` every frame by caching property IDs at output creation:

```cpp
struct PlaneProperties {
    uint32_t fbId;
    uint32_t crtcId;
    uint32_t srcX, srcY, srcW, srcH;
    uint32_t crtcX, crtcY, crtcW, crtcH;
    uint32_t type;        // PRIMARY, CURSOR, OVERLAY
    uint32_t rotation;
    uint32_t inFormats;   // supported formats blob
};

struct CrtcProperties {
    uint32_t active;
    uint32_t modeId;
    uint32_t gammaLut;
    uint32_t ctm;
    uint32_t vrrEnabled;
};

struct ConnectorProperties {
    uint32_t crtcId;
    uint32_t dpms;
    uint32_t edid;        // EDID blob
    uint32_t maxBpc;
};
```

## Verification

1. `phosphor-compositor --backend=drm` launches on a TTY, shows solid color on all connected outputs
2. `Ctrl+Alt+F2` switches VT; `Ctrl+Alt+F1` returns (no crash, outputs restored)
3. Plug a monitor: appears as new output with correct mode
4. Unplug a monitor: output removed cleanly, no crash
5. `wayland-info` shows correct output modes/geometry
6. DrmScreenProvider emits `screenAdded`/`screenRemoved` matching physical events
7. Unit tests:
   - `test_drm_property_cache` — parse properties from mock data
   - `test_gbm_swapchain` — acquire/release buffer cycling
   - `test_session_lifecycle` — mock libseat enable/disable callbacks

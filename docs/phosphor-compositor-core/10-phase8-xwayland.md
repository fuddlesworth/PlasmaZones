// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

# Phase 8: XWayland Bridge + PipeWire Screen Sharing

## Deliverables

- XWayland subprocess lifecycle management
- X11 window manager via libxcb (reparenting WM for managed windows)
- X11 ↔ Wayland surface mapping (X11 window → wl_surface)
- Override-redirect windows as unmanaged overlays
- EWMH compliance (_NET_WM_STATE, _NET_WM_WINDOW_TYPE, etc.)
- Selection bridging (X11 clipboard ↔ Wayland wl_data_device)
- DnD bridging (X11 XDND ↔ Wayland drag-and-drop)
- PipeWire DMA-BUF frame export (per-output and per-window streams)

## Class Hierarchy

```
XWaylandManager
├── owns XWaylandProcess (subprocess lifecycle)
├── owns X11WindowManager (libxcb WM logic)
├── owns X11SelectionBridge (clipboard + DnD)
├── maps xcb_window_t → XWaylandSurface
└── connects to compositor via XWayland wl_surface association

XWaylandProcess
├── spawns `Xwayland -rootless -wm <fd> -listenfd <fd>`
├── manages socket pair (WM fd for X11 connection)
├── manages display lock (/tmp/.X<N>-lock)
├── lifecycle: start, ready (SIGUSR1), crash → restart
└── DISPLAY environment for child processes

X11WindowManager
├── xcb_connection_t* (connected via WM fd)
├── xcb_screen_t* root screen
├── WM atoms (intern + cache all needed atoms)
├── Reparenting: client windows reparented to frame windows
├── SubstructureRedirect on root (receive MapRequest, ConfigureRequest)
├── EWMH: _NET_SUPPORTED, _NET_WM_STATE, _NET_WM_WINDOW_TYPE
├── stacking: _NET_CLIENT_LIST_STACKING
└── focus: _NET_ACTIVE_WINDOW, WM_TAKE_FOCUS

XWaylandSurface
├── xcb_window_t xWindow
├── Surface* waylandSurface (associated via xwayland protocol)
├── WindowType (normal, dialog, splash, utility, dnd, menu, notification)
├── Decorations (motif hints → SSD/CSD decision)
├── geometry (X11 reported → mapped to compositor-space)
├── overrideRedirect flag
├── NET_WM_PID, WM_CLASS, WM_NAME
└── stateFlags (maximized, fullscreen, demands_attention, etc.)

X11SelectionBridge
├── bridges wl_data_device ↔ X11 selection (CLIPBOARD, PRIMARY)
├── MIME type ↔ X11 atom target conversion
├── handles INCR protocol for large transfers
├── DnD: XDND protocol ↔ wl_data_device drag events
└── owns X11 selection owner window (proxy)

PipeWireManager
├── owns PipeWireConnection (pw_main_loop, pw_context)
├── owns ScreenCastStream per active share
├── registers org.freedesktop.impl.portal.ScreenCast D-Bus
└── negotiates stream params (format, size, framerate)

ScreenCastStream
├── pw_stream* (PipeWire stream handle)
├── source: Output (full screen) or Window (single toplevel)
├── DMA-BUF export: compositor render → fd → PipeWire buffer
├── SHM fallback: GPU readback → memfd → PipeWire buffer
├── cursor embedding option (composite cursor into frame)
└── damage-driven: only push frames when source changes
```

## File Map

```
libs/phosphor-compositor-core/src/xwayland/
├── CMakeLists.txt
├── xwayland_manager.h
├── xwayland_manager.cpp
├── xwayland_process.h
├── xwayland_process.cpp
├── x11_window_manager.h
├── x11_window_manager.cpp
├── xwayland_surface.h
├── xwayland_surface.cpp
├── x11_atoms.h                 — Atom interning + cache
├── x11_atoms.cpp
├── x11_selection_bridge.h
├── x11_selection_bridge.cpp
├── x11_dnd_bridge.h            — XDND ↔ Wayland DnD
├── x11_dnd_bridge.cpp
├── ewmh.h                      — _NET_WM_* property helpers
└── ewmh.cpp

libs/phosphor-compositor-core/src/pipewire/
├── CMakeLists.txt
├── pipewire_manager.h
├── pipewire_manager.cpp
├── screencast_stream.h
├── screencast_stream.cpp
├── pipewire_buffer.h           — DMA-BUF/SHM buffer management
└── pipewire_buffer.cpp
```

## XWayland Startup Sequence

```
Compositor                           XWayland                        X11 Clients
    │                                    │                               │
    │ 1. Create socket pair (wmFd)       │                               │
    │ 2. Create Xwayland listen socket   │                               │
    │ 3. Lock /tmp/.X<N>-lock (O_EXCL)   │                               │
    │ 4. Fork + exec Xwayland            │                               │
    │    args: -rootless               │                               │
    │          -wm <wmFd>                │                               │
    │          -listenfd <listenFd>      │                               │
    │                                    │                               │
    │                                    │ Starts, opens wmFd             │
    │                                    │ Listens on X socket            │
    │  ←── SIGUSR1 (ready) ─────────────│                               │
    │  (timeout: 5s; if XWayland exits   │                               │
    │   or timeout fires before SIGUSR1: │                               │
    │   clean up stale /tmp/.X<N>-lock,  │                               │
    │   retry up to 3 times with         │                               │
    │   exponential backoff (1s,2s,4s),  │                               │
    │   then disable XWayland)           │                               │
    │                                    │                               │
    │ 5. Connect to wmFd via xcb         │                               │
    │    (become the X11 WM)             │                               │
    │ 6. xcb_change_window_attributes    │                               │
    │    SubstructureRedirect on root    │                               │
    │ 7. Intern atoms (EWMH, ICCCM)     │                               │
    │ 8. Set _NET_SUPPORTED              │                               │
    │ 9. Set WAYLAND_DISPLAY in env      │                               │
    │                                    │                               │
    │                                    │                  ←─────────────│ Client connects
    │                                    │ MapRequest ────→ │             │
    │ SubstructureRedirect delivers      │                               │
    │ MapRequest to compositor           │                               │
    │                                    │                               │
```

## X11 Window Management

### MapRequest Handling

```cpp
void X11WindowManager::handleMapRequest(xcb_map_request_event_t* event) {
    xcb_window_t window = event->window;

    // Read window attributes
    auto* attrs = xcb_get_window_attributes_reply(m_conn,
        xcb_get_window_attributes(m_conn, window), nullptr);
    if (!attrs) return;

    bool overrideRedirect = attrs->override_redirect;
    free(attrs);

    if (overrideRedirect) {
        // Override-redirect: map without managing (menus, tooltips, DnD previews)
        xcb_map_window(m_conn, window);
        // Will appear as unmanaged XWaylandSurface
        return;
    }

    // Read properties for identity
    auto wmClass = readWmClass(window);
    auto wmName = readWmName(window);
    auto windowType = readNetWmWindowType(window);
    auto motifHints = readMotifHints(window);

    // Create XWaylandSurface tracking object (QObject parented to this → auto-deleted)
    auto* xSurface = new XWaylandSurface(window, this);  // parent ownership via QObject
    xSurface->setWindowClass(wmClass);
    xSurface->setTitle(wmName);
    xSurface->setWindowType(windowType);
    xSurface->setDecorationsFromMotif(motifHints);

    m_surfaces.insert(window, xSurface);  // non-owning lookup; parent QObject owns lifetime

    // Reparent into frame (for WM resize borders on X11 side)
    // OR: skip reparenting (we do SSD on compositor side)
    // Decision: no reparenting — our SSD applies same as native Wayland

    // Map the window
    xcb_map_window(m_conn, window);

    // Send initial configure
    configureWindow(xSurface);

    // The X11 window will create a wl_surface via XWayland's internal Wayland client
    // Association happens via xwayland_surface_v1 protocol (next section)
}
```

### Surface Association

```
XWayland internally:
  1. X11 client creates a window → XWayland creates a wl_surface for it
  2. XWayland sends xwayland_surface_v1.set_serial(wl_surface, serial)
  3. Compositor matches serial → associates xcb_window_t with wl_surface

After association:
  - XWaylandSurface.waylandSurface is set
  - Compositor treats it like any other surface for rendering
  - But stacking/focus/sizing controlled via X11 WM protocol (configure events)
```

### EWMH State Mapping

```
_NET_WM_STATE_MAXIMIZED_HORZ + VERT → XdgToplevel-like maximize
_NET_WM_STATE_FULLSCREEN → fullscreen (map to output)
_NET_WM_STATE_HIDDEN → minimized
_NET_WM_STATE_ABOVE → keep-above in stacking
_NET_WM_STATE_BELOW → keep-below in stacking
_NET_WM_STATE_DEMANDS_ATTENTION → highlight in taskbar/tab

_NET_WM_WINDOW_TYPE:
  _NET_WM_WINDOW_TYPE_NORMAL → managed toplevel
  _NET_WM_WINDOW_TYPE_DIALOG → dialog (parent-attached, may not be resizable)
  _NET_WM_WINDOW_TYPE_SPLASH → splash screen (centered, no decoration)
  _NET_WM_WINDOW_TYPE_UTILITY → utility (tool palette, stays with parent)
  _NET_WM_WINDOW_TYPE_TOOLTIP → tooltip (override-redirect like)
  _NET_WM_WINDOW_TYPE_NOTIFICATION → notification (overlay layer)
  _NET_WM_WINDOW_TYPE_DND → DnD icon (follows cursor)
```

## Selection (Clipboard) Bridge

### X11 → Wayland Transfer

```
X11 App copies:
  1. X11 app calls XSetSelectionOwner(CLIPBOARD)
  2. Compositor (via XFixes) receives XFixesSelectionNotify (ownership change event)
  3. X11SelectionBridge creates a proxy DataSource for Wayland
  4. Bridge sets this DataSource as the wl_data_device selection
  5. Wayland client requests paste:
     a. wl_data_offer.receive(mime, fd)
     b. Bridge converts MIME → X11 target atom
     c. Bridge requests SelectionConvert from X11 owner
     d. X11 owner writes data to bridge's X11 property
     e. Bridge reads property, writes to fd, closes fd
```

### Wayland → X11 Transfer

```
Wayland app copies (DataSource set):
  1. X11 app requests paste: XConvertSelection(CLIPBOARD, target, property)
  2. Compositor (owning CLIPBOARD as WM) receives SelectionRequest
  3. Bridge converts X11 target atom → MIME type
  4. Bridge calls wl_data_source.send(mime, fd)
  5. Wayland app writes data to fd
  6. Bridge reads from fd, stores in X11 property
  7. Bridge sends SelectionNotify to X11 requester
  8. X11 app reads property
```

### INCR Protocol (Large Transfers)

```
For data exceeding max request size (derived from xcb_get_maximum_request_length):
  1. Set property to INCR atom (signals incremental transfer)
  2. Send SelectionNotify
  3. Wait for PropertyNotify(deleted) from requester
     — timeout: 5s per chunk; if exceeded, abort transfer and clean up
  4. Write chunk to property (max ~64KB per chunk)
  5. Wait for PropertyNotify(deleted) again
  6. Repeat 4-5 until all data transferred
  7. Set property to zero-length (signals end)
  If requester disconnects mid-transfer: PropertyNotify stops arriving → timeout fires → abort.
```

## PipeWire Screen Sharing

### Architecture

```
OBS / Browser                 D-Bus Portal                    Compositor
     │                            │                               │
     │ CreateSession() ──────────→│                               │
     │                            │ o.f.i.portal.ScreenCast       │
     │ SelectSources() ─────────→│                               │
     │                            │ (user picks output/window)    │
     │                            │──→ permission granted          │
     │ Start() ──────────────────→│                               │
     │                            │ start_stream(node_id) ───────→│
     │                            │                               │ Create ScreenCastStream
     │  ←── streams: [{node_id}] ─│                               │
     │                            │                               │
     │ Connect to PipeWire node   │                               │
     │ (via pw_stream_connect)    │                               │
     │                            │                               │
     │                            │               frame ready ←───│ (on damage)
     │ dequeue_buffer ←───────────────────────────────────────────│
     │ (DMA-BUF fd)              │                               │
     │ process frame             │                               │
     │ queue_buffer ─────────────────────────────────────────────→│
     │                            │                               │
```

### ScreenCastStream Implementation

```cpp
class ScreenCastStream {
public:
    enum SourceType { Output, Window };

    ScreenCastStream(SourceType type, void* source, PipeWireManager* manager);
    ~ScreenCastStream();

    bool start();
    void stop();

    uint32_t nodeId() const;  // PipeWire node ID for clients to connect

private:
    void onProcess();         // PipeWire calls this when buffer available
    void onFrameReady();      // Compositor calls this when source has new content

    // DMA-BUF path (preferred)
    void exportDmaBuf(pw_buffer* buffer);

    // SHM fallback (if client can't handle DMA-BUF)
    void exportShm(pw_buffer* buffer);

    pw_stream* m_stream = nullptr;
    spa_hook m_streamListener;

    SourceType m_sourceType;
    std::variant<DrmOutput*, WindowEntry*> m_source;

    // Buffer format negotiation result
    uint32_t m_format;       // DRM fourcc
    uint64_t m_modifier;     // DRM modifier
    QSize m_size;
    spa_video_info_raw m_videoInfo;
};
```

### DMA-BUF Export Path

```cpp
void ScreenCastStream::exportDmaBuf(pw_buffer* pwBuf) {
    spa_buffer* buf = pwBuf->buffer;
    spa_data* data = &buf->datas[0];

    // Render the source into a DMA-BUF
    gbm_bo* bo = nullptr;

    if (m_sourceType == Output) {
        // Use a dedicated export buffer (NOT the active scanout buffer — that would race
        // with page-flip). The renderer copies the composited frame into this export BO
        // after flip completes. For true zero-copy, use a separate DRM plane or
        // export the previous (now-idle) front buffer.
        bo = std::get<DrmOutput*>(m_source)->acquireExportBuffer();
    } else {
        // Window: render just this window's content to an offscreen buffer
        bo = m_renderer->renderWindowToBuffer(std::get<WindowEntry*>(m_source), m_size);
    }

    if (!bo) return;  // buffer acquisition failed — skip this frame

    // Export GBM BO as DMA-BUF fd.
    // PipeWire takes ownership of this fd upon pw_stream_queue_buffer.
    // Do NOT close it after queuing — PipeWire closes it when the buffer is recycled.
    int fd = gbm_bo_get_fd(bo);
    if (fd < 0) return;  // export failed — skip this frame
    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t offset = gbm_bo_get_offset(bo, 0);

    data->type = SPA_DATA_DmaBuf;
    data->fd = fd;
    data->mapoffset = 0;
    data->maxsize = stride * m_size.height();
    data->data = nullptr;  // DMA-BUF: no CPU mapping

    // Set chunk metadata
    spa_chunk* chunk = data->chunk;
    chunk->offset = offset;
    chunk->size = stride * m_size.height();
    chunk->stride = stride;

    // Add DMA-BUF metadata
    spa_meta_header* header = spa_buffer_find_meta_data(buf, SPA_META_Header, sizeof(*header));
    if (header) {
        header->pts = -1;  // presentation time (could use DRM timestamp)
        header->dts_offset = 0;
        header->seq = m_frameSequence++;
    }

    pw_stream_queue_buffer(m_stream, pwBuf);
}
```

### Cursor Metadata

```cpp
void ScreenCastStream::addCursorMetadata(spa_buffer* buf) {
    auto* cursor = spa_buffer_find_meta_data(buf, SPA_META_Cursor, sizeof(spa_meta_cursor));
    if (!cursor) return;

    // Cursor position relative to stream source
    QPoint cursorPos = m_cursor->position() - m_sourceRect.topLeft();
    cursor->id = 1;
    cursor->position = SPA_POINT(cursorPos.x(), cursorPos.y());
    cursor->hotspot = SPA_POINT(m_cursor->hotspot().x(), m_cursor->hotspot().y());

    // Optionally embed cursor bitmap
    if (cursor->bitmap_offset) {
        auto* bitmap = SPA_MEMBER(cursor, cursor->bitmap_offset, spa_meta_bitmap);
        bitmap->format = SPA_VIDEO_FORMAT_RGBA;
        bitmap->size = SPA_RECTANGLE(m_cursor->image().width(), m_cursor->image().height());
        bitmap->stride = m_cursor->image().bytesPerLine();
        memcpy(SPA_MEMBER(bitmap, bitmap->offset, void),
               m_cursor->image().constBits(),
               m_cursor->image().sizeInBytes());
    }
}
```

## XWayland Surface Lifecycle

```
X11 Client                  XWayland              Compositor WM           Scene Graph
     │                          │                       │                      │
     │ XCreateWindow ─────────→│                       │                      │
     │ XMapWindow ────────────→│                       │                      │
     │                          │ MapRequest ─────────→│                      │
     │                          │                       │ Create XWaylandSurface│
     │                          │                       │ Read properties       │
     │                          │                       │                      │
     │                          │ (internally creates   │                      │
     │                          │  wl_surface for this  │                      │
     │                          │  X11 window)          │                      │
     │                          │                       │                      │
     │                          │ xwayland_surface_v1   │                      │
     │                          │ .set_serial ─────────→│                      │
     │                          │                       │ Associate X11 window │
     │                          │                       │  with wl_surface     │
     │                          │                       │                      │
     │                          │ wl_surface.commit ───→│                      │
     │                          │                       │ Insert into scene ──→│
     │                          │                       │ (same as native      │
     │                          │                       │  Wayland toplevel)   │
     │                          │                       │                      │
```

## Verification

1. Firefox (X11) launches, renders correctly, receives input
2. Steam (X11) client renders, game windows work
3. Right-click menu in X11 app: override-redirect positioned correctly
4. Copy text in Wayland app → paste in X11 app (and vice versa)
5. Drag file from Nautilus (X11) → drop in Wayland app
6. OBS captures entire screen via PipeWire (correct content, steady framerate)
7. OBS captures single window via PipeWire
8. Firefox WebRTC screen share works (picks output via portal)
9. XWayland crash → auto-restart, X11 apps recover
10. Unit tests:
    - `test_xwayland_startup` — mock socket pair, verify protocol sequence
    - `test_ewmh_state_mapping` — _NET_WM_STATE → compositor state flags
    - `test_selection_x11_to_wayland` — mock X11 selection → verify wl_data_offer
    - `test_selection_wayland_to_x11` — mock DataSource → verify X11 SelectionNotify
    - `test_override_redirect` — unmanaged window positioned correctly
    - `test_pipewire_stream_lifecycle` — create/start/stop/destroy stream
    - `test_dmabuf_export` — verify buffer metadata correct

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

# Phase 0: Build Skeleton + Headless Backend

## Deliverables

- CMake project structure for `libs/phosphor-compositor-core/`
- `IBackend` abstract interface
- `HeadlessBackend` implementation (virtual output, no hardware)
- `Server` class wrapping `wl_display` lifecycle
- `Compositor` top-level wiring object
- `src/compositor/main.cpp` binary
- Qt event loop ↔ wayland event loop bridge

## Class Hierarchy

```
IBackend (abstract)
├── HeadlessBackend      — virtual outputs for testing
└── DrmBackend (Phase 2) — real hardware

IOutput (abstract)
├── HeadlessOutput       — virtual output with configurable mode
└── DrmOutput (Phase 2)  — DRM connector/CRTC/plane

Server
├── owns wl_display*
├── owns wl_event_loop*
├── manages client connections
└── hosts protocol globals

Compositor
├── owns Backend (unique_ptr<IBackend>)
├── owns Server
├── owns SceneGraph (Phase 1)
├── owns Renderer (Phase 3)
├── owns InputManager (Phase 4)
├── owns WindowManager (Phase 5)
└── owns PluginHost (Phase 9)
```

## File Map

```
libs/phosphor-compositor-core/
├── CMakeLists.txt
├── include/PhosphorCompositorCore/
│   ├── IBackend.h
│   ├── IOutput.h
│   ├── Server.h
│   └── Compositor.h
└── src/
    └── backend/
        ├── CMakeLists.txt
        ├── ibackend.cpp          (default virtual dtor)
        ├── ioutput.cpp           (default virtual dtor)
        ├── headless_backend.h
        ├── headless_backend.cpp
        ├── headless_output.h
        └── headless_output.cpp

src/compositor/
├── CMakeLists.txt
└── main.cpp
```

## Interface Definitions

### IBackend

```cpp
// include/PhosphorCompositorCore/IBackend.h
#pragma once

#include <QList>
#include <memory>

namespace PhosphorCompositorCore {

class IOutput;

enum class BackendType { Headless, Drm };

class PHOSPHORCOMPOSITORCORE_EXPORT IBackend {
public:
    virtual ~IBackend() = default;

    virtual BackendType type() const = 0;

    /// Initialize the backend. Called once after construction.
    /// Returns false on fatal initialization failure.
    virtual bool initialize() = 0;

    /// Shut down the backend. Release all hardware resources.
    virtual void shutdown() = 0;

    /// All currently connected outputs.
    virtual QList<IOutput*> outputs() const = 0;

    /// Create a new virtual output (headless only; DRM ignores).
    virtual IOutput* createOutput(int width, int height, int refreshMhz) = 0;
};

} // namespace PhosphorCompositorCore
```

### IOutput

```cpp
// include/PhosphorCompositorCore/IOutput.h
#pragma once

#include <QRect>
#include <QString>

namespace PhosphorCompositorCore {

struct OutputMode {
    int width = 0;
    int height = 0;
    int refreshMhz = 60000;  // millihertz (60000 = 60Hz)
    bool preferred = false;
};

class PHOSPHORCOMPOSITORCORE_EXPORT IOutput {
public:
    virtual ~IOutput() = default;

    /// Connector name (e.g., "HEADLESS-1", "DP-3")
    virtual QString name() const = 0;

    /// Physical size in mm (0,0 for headless)
    virtual QSize physicalSize() const = 0;

    /// Current mode
    virtual OutputMode currentMode() const = 0;

    /// All available modes
    virtual QList<OutputMode> modes() const = 0;

    /// Position in the global output layout
    virtual QPoint position() const = 0;
    virtual void setPosition(QPoint pos) = 0;

    /// Scale factor (1.0, 1.25, 1.5, 2.0, ...)
    virtual double scale() const = 0;
    virtual void setScale(double scale) = 0;

    /// Transform (rotation + flip)
    enum Transform { Normal, Rotate90, Rotate180, Rotate270,
                     Flipped, FlippedRotate90, FlippedRotate180, FlippedRotate270 };
    virtual Transform transform() const = 0;
    virtual void setTransform(Transform t) = 0;

    /// Is this output enabled (receiving frames)?
    virtual bool isEnabled() const = 0;
    virtual void setEnabled(bool enabled) = 0;

    /// Request a frame be rendered for this output.
    /// Called by the frame scheduler when damage exists or clients need callbacks.
    virtual void scheduleFrame() = 0;
};

} // namespace PhosphorCompositorCore
```

### Server

```cpp
// include/PhosphorCompositorCore/Server.h
#pragma once

#include <QString>

struct wl_display;
struct wl_event_loop;

namespace PhosphorCompositorCore {

class PHOSPHORCOMPOSITORCORE_EXPORT Server {
public:
    Server();
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;

    /// Initialize the Wayland display server.
    /// Creates the display, event loop, and opens the socket.
    /// @param socketName Override socket name (empty = auto "wayland-N")
    /// @return true on success
    bool initialize(const QString& socketName = {});

    /// Shut down: close socket, destroy display.
    void shutdown();

    /// The Wayland display (for registering globals).
    wl_display* display() const;

    /// The event loop (for adding fds, timers).
    wl_event_loop* eventLoop() const;

    /// The socket name clients should connect to.
    QString socketName() const;

    /// Dispatch pending events (non-blocking).
    void dispatchEvents();

    /// Flush outgoing data to all clients.
    void flushClients();

private:
    wl_display* m_display = nullptr;
    QString m_socketName;
};

} // namespace PhosphorCompositorCore
```

### Compositor

```cpp
// include/PhosphorCompositorCore/Compositor.h
#pragma once

#include <QObject>
#include <memory>

namespace PhosphorCompositorCore {

class IBackend;
class Server;

class PHOSPHORCOMPOSITORCORE_EXPORT Compositor : public QObject {
    Q_OBJECT
public:
    explicit Compositor(QObject* parent = nullptr);
    ~Compositor() override;

    /// Initialize all subsystems. Returns false on fatal error.
    bool initialize();

    /// Shut down all subsystems in reverse order.
    void shutdown();

    Server& server() const;
    IBackend& backend() const;

Q_SIGNALS:
    void initialized();
    void shutdownComplete();

private:
    std::unique_ptr<Server> m_server;
    std::unique_ptr<IBackend> m_backend;
};

} // namespace PhosphorCompositorCore
```

## HeadlessBackend Implementation

```cpp
// src/backend/headless_backend.h
#pragma once

#include "PhosphorCompositorCore/IBackend.h"
#include <QList>
#include <memory>

namespace PhosphorCompositorCore {

class HeadlessOutput;

class HeadlessBackend : public IBackend {
public:
    HeadlessBackend() = default;
    ~HeadlessBackend() override;

    BackendType type() const override { return BackendType::Headless; }
    bool initialize() override;
    void shutdown() override;
    QList<IOutput*> outputs() const override;
    IOutput* createOutput(int width, int height, int refreshMhz) override;

private:
    QList<std::unique_ptr<HeadlessOutput>> m_outputs;
    int m_nextId = 1;
};

} // namespace PhosphorCompositorCore
```

```cpp
// src/backend/headless_output.h
#pragma once

#include "PhosphorCompositorCore/IOutput.h"

namespace PhosphorCompositorCore {

class HeadlessOutput : public IOutput {
public:
    HeadlessOutput(int id, int width, int height, int refreshMhz);

    QString name() const override;
    QSize physicalSize() const override { return {0, 0}; }
    OutputMode currentMode() const override;
    QList<OutputMode> modes() const override;
    QPoint position() const override { return m_position; }
    void setPosition(QPoint pos) override { m_position = pos; }
    double scale() const override { return m_scale; }
    void setScale(double scale) override { m_scale = scale; }
    Transform transform() const override { return m_transform; }
    void setTransform(Transform t) override { m_transform = t; }
    bool isEnabled() const override { return m_enabled; }
    void setEnabled(bool enabled) override { m_enabled = enabled; }
    void scheduleFrame() override;  // no-op for headless (or fire timer)

private:
    int m_id;
    OutputMode m_mode;
    QPoint m_position;
    double m_scale = 1.0;
    Transform m_transform = Normal;
    bool m_enabled = true;
};

} // namespace PhosphorCompositorCore
```

## Event Loop Bridge (main.cpp)

```cpp
// src/compositor/main.cpp

#include "PhosphorCompositorCore/Compositor.h"
#include "PhosphorCompositorCore/Server.h"

#include <QCoreApplication>
#include <QSocketNotifier>
#include <QTimer>

#include <wayland-server-core.h>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("phosphor-compositor"));

    PhosphorCompositorCore::Compositor compositor;
    if (!compositor.initialize()) {
        qCritical("Failed to initialize compositor");
        return 1;
    }

    // Bridge: wayland fd → Qt event loop
    auto* eventLoop = compositor.server().eventLoop();
    int wlFd = wl_event_loop_get_fd(eventLoop);

    QSocketNotifier notifier(wlFd, QSocketNotifier::Read);
    // Single handler: dispatch wayland events then flush outgoing data to clients.
    // No idle timer needed — the socket notifier fires whenever clients send data,
    // and wayland's internal timers (key repeat, frame scheduling) use timerfd
    // which the wayland event loop dispatches within wl_event_loop_dispatch.
    QObject::connect(&notifier, &QSocketNotifier::activated, [&]() {
        wl_event_loop_dispatch(eventLoop, 0);
        compositor.server().flushClients();
    });

    // Set WAYLAND_DISPLAY for child processes
    qputenv("WAYLAND_DISPLAY", compositor.server().socketName().toUtf8());

    qInfo("Compositor running on %s", qPrintable(compositor.server().socketName()));

    return app.exec();
}
```

## State Machine: Server Lifecycle

```
         initialize()
    ┌──────────────────┐
    │                  ▼
 [Created] ──→ [Running] ──→ [ShuttingDown] ──→ [Destroyed]
                  │  ▲              │
                  │  │              │
                  │  └──────────────┘
                  │    (error recovery)
                  │
                  ▼
            [Accepting Clients]
            - wl_display_add_socket_auto()
            - Advertising globals
            - Dispatching events
```

## Data Flow: Client Connection

```
1. Client connects to $WAYLAND_DISPLAY socket
2. wl_display accepts connection → creates wl_client
3. Client sends wl_display.get_registry
4. Server responds with all registered globals (wl_compositor, wl_output, ...)
5. Client binds globals it needs
6. Server creates wl_resource for each binding
7. Client is now "live" — can create surfaces, request input, etc.
```

## Verification

1. Build: `cmake --build build --target phosphor-compositor`
2. Run: `./build/src/compositor/phosphor-compositor`
3. In another terminal: `WAYLAND_DISPLAY=wayland-N wayland-info`
4. Expected output: lists `wl_compositor` v6, `wl_output` v4 (virtual 1920x1080@60Hz)
5. Unit test: `test_headless_backend` — create backend, create output, verify mode
6. Unit test: `test_server` — create server, verify socket exists, connect client

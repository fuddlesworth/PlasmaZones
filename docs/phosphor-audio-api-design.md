# PhosphorAudio — API Design Document

## Overview

PhosphorAudio is the audio spectrum provider library for Phosphor-based
shells, compositors, and window managers. It defines a backend-agnostic
interface for real-time audio spectrum data and ships a concrete CAVA-based
implementation.

In a shell ecosystem, audio-reactive visuals are a **system-level service**,
not an application feature. Overlay shaders, panel widgets, desktop effects,
lock screen animations, notification styling, and third-party plugins all
need the same spectrum data. PhosphorAudio ensures each consumer links one
library instead of reimplementing subprocess management, audio detection,
output parsing, and temporal smoothing independently.

**License:** LGPL-2.1-or-later
**Namespace:** `PhosphorAudio`
**Depends on:** Qt6::Core
**Build artefact:** `libPhosphorAudio.so` (SHARED)

---

## Dependency Graph

```
                     PhosphorAudio (SHARED, Qt6::Core only)
                                    │
        ┌───────────────┬───────────┼───────────────┬──────────────┐
        │               │           │               │              │
   daemon           editor     phosphor-overlay   panel widgets  third-party
   (OverlayService,  (shader   [future]           [future]       plugins
    syncCavaState)    preview)                                    [future]
```

Every consumer takes `IAudioSpectrumProvider*` via constructor injection
or setter. The daemon and editor wire in `CavaSpectrumProvider` as the
default; alternative backends are substitutable at the injection point.

---

## Design Principles

1. **Interface-first, backend-agnostic.** `IAudioSpectrumProvider` is the
   stable contract. The CAVA subprocess is an implementation detail behind
   `CavaSpectrumProvider`. Future backends (PipeWire native, PulseAudio
   monitor, JACK client, test-harness stub) implement the same interface
   without touching existing code.

2. **Qt6::Core only.** The library has zero dependencies beyond Qt Core
   (`QProcess`, `QStandardPaths`, `QObject`). No Qt Quick, no Gui, no
   Wayland, no KDE frameworks. This keeps link cost minimal for the KWin
   effect plugin and any other cost-sensitive consumer.

3. **Normalized output.** Spectrum values are always `float` in `[0.0, 1.0]`.
   Consumers never deal with backend-specific ranges (CAVA's `ascii_max_range`,
   PipeWire sample magnitudes, etc.).

4. **EMA temporal smoothing built-in.** `CavaSpectrumProvider` applies
   exponential moving average (alpha = 0.5 at 60 fps, ~33 ms time constant)
   to reduce frame-to-frame jitter. Consumers receive smooth data without
   post-processing.

5. **Auto-detection over configuration.** `CavaSpectrumProvider` detects
   PipeWire vs PulseAudio automatically by checking the PipeWire runtime
   socket and `pw-cli` availability. No user configuration required for
   the common case.

6. **Graceful degradation.** When CAVA is not installed, `isAvailable()`
   returns `false` and `start()` emits `errorOccurred`. Consumers check
   availability before wiring up spectrum-dependent features. No crash,
   no hang, no blocking.

---

## Public API

### `IAudioSpectrumProvider` (abstract interface)

```cpp
namespace PhosphorAudio {

class PHOSPHORAUDIO_EXPORT IAudioSpectrumProvider : public QObject
{
    Q_OBJECT

public:
    explicit IAudioSpectrumProvider(QObject* parent = nullptr);

    virtual bool isAvailable() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

    virtual int barCount() const = 0;
    virtual void setBarCount(int count) = 0;

    virtual int framerate() const = 0;
    virtual void setFramerate(int fps) = 0;

    virtual QVector<float> spectrum() const = 0;

Q_SIGNALS:
    void spectrumUpdated(const QVector<float>& spectrum);
    void runningChanged(bool running);
    void errorOccurred(const QString& message);
};

}
```

**Signals:**
- `spectrumUpdated(spectrum)` — emitted on each parsed frame. `spectrum` contains `barCount()` normalized floats. Hot path — consumers should avoid copying.
- `runningChanged(running)` — emitted on process state transitions.
- `errorOccurred(message)` — emitted on start failure or runtime crash (not during intentional `stop()`).

**Bar count contract:**
- CAVA requires even bar count (stereo split). `setBarCount()` rounds to even, then clamps to `[MinBars, MaxBars]`.
- Changing bar count while running triggers an async restart.

### `CavaSpectrumProvider` (concrete CAVA backend)

```cpp
namespace PhosphorAudio {

class PHOSPHORAUDIO_EXPORT CavaSpectrumProvider : public IAudioSpectrumProvider
{
    Q_OBJECT

public:
    explicit CavaSpectrumProvider(QObject* parent = nullptr);
    ~CavaSpectrumProvider() override;

    static bool isCavaInstalled();
    static QString detectAudioMethod();

    // IAudioSpectrumProvider implementation
    bool isAvailable() const override;
    void start() override;
    void stop() override;
    bool isRunning() const override;
    int barCount() const override;
    void setBarCount(int count) override;
    int framerate() const override;
    void setFramerate(int fps) override;
    QVector<float> spectrum() const override;
};

}
```

**Static utilities:**
- `isCavaInstalled()` — checks `$PATH` for the `cava` binary. Call before constructing to skip instantiation entirely when CAVA is absent.
- `detectAudioMethod()` — returns `"pipewire"` or `"pulse"` based on runtime environment. Exposed as public utility for diagnostic / settings UIs.

**Subprocess lifecycle:**
- `start()` is idempotent and async — no blocking `waitForStarted()`.
- `stop()` sends `SIGTERM`, waits 500 ms, then `SIGKILL`. Destructor calls `stop()`.
- Async restart on `setBarCount()` / `setFramerate()` while running: terminates current process, restarts with new config on `finished` signal (single-shot connection).

**Config generation:**
- CAVA receives its config via stdin (Kurve-style heredoc). No temp files, no filesystem pollution.
- `ascii_max_range = 1000`, `bar_delimiter = 59` (semicolon), `frame_delimiter = 10` (newline).

### `AudioDefaults` (constants)

```cpp
namespace PhosphorAudio::Defaults {
    inline constexpr int MinBars = 16;
    inline constexpr int MaxBars = 256;
    inline constexpr int DefaultBarCount = 64;
    inline constexpr int DefaultFramerate = 60;
    inline constexpr int MinFramerate = 30;
    inline constexpr int MaxFramerate = 144;
}
```

Replaces the `PlasmaZones::Audio` namespace previously in `src/core/constants.h`.

---

## Integration Pattern

### Daemon (OverlayService)

```cpp
// Construction (daemon.cpp or OverlayService ctor):
m_audioProvider = std::make_unique<PhosphorAudio::CavaSpectrumProvider>(this);
connect(m_audioProvider.get(),
        &PhosphorAudio::IAudioSpectrumProvider::spectrumUpdated,
        this, &OverlayService::onAudioSpectrumUpdated);

// Settings sync:
void OverlayService::syncCavaState()
{
    if (!m_audioProvider || !m_settings) return;
    if (m_settings->enableAudioVisualizer()) {
        m_audioProvider->setBarCount(m_settings->audioSpectrumBarCount());
        m_audioProvider->setFramerate(m_settings->shaderFrameRate());
        if (!m_audioProvider->isRunning()) m_audioProvider->start();
    } else {
        if (m_audioProvider->isRunning()) m_audioProvider->stop();
    }
}
```

### Editor (shader preview)

```cpp
void EditorController::startAudioCapture()
{
    if (m_audioProvider && m_audioProvider->isRunning()) return;
    if (!PhosphorAudio::CavaSpectrumProvider::isCavaInstalled()) return;
    if (!m_audioProvider) {
        m_audioProvider = new PhosphorAudio::CavaSpectrumProvider(this);
        connect(m_audioProvider,
                &PhosphorAudio::IAudioSpectrumProvider::spectrumUpdated,
                this, [this](const QVector<float>& spectrum) {
            m_audioSpectrum = spectrum;
            Q_EMIT audioSpectrumChanged();
        });
    }
    m_audioProvider->setBarCount(barCountFromSettings);
    m_audioProvider->start();
}
```

### Future: phosphor-overlay extraction

When overlay extraction happens, `OverlayService` takes
`IAudioSpectrumProvider*` via constructor injection instead of owning
a `unique_ptr`:

```cpp
explicit OverlayService(
    Phosphor::Screens::ScreenManager* screenManager,
    ShaderRegistry* shaderRegistry,
    PhosphorAudio::IAudioSpectrumProvider* audioProvider,  // nullable
    QObject* parent = nullptr);
```

The daemon composition root constructs the provider and threads it in.
Overlay code only calls `isRunning()`, `spectrum()`, and connects to
`spectrumUpdated` — never `start()` / `stop()` (lifecycle is the
daemon's responsibility).

---

## Future Backends

### PipeWire native

```cpp
class PipeWireSpectrumProvider : public IAudioSpectrumProvider { ... };
```

Uses `libpipewire` to attach a capture stream to the default audio
sink's monitor. Eliminates the CAVA subprocess, reduces latency
(in-process FFT), and handles PipeWire node routing natively.
Requires `libpipewire-0.3-dev` as a build dependency — gated behind
a CMake option (`PHOSPHOR_AUDIO_PIPEWIRE`).

### Test stub

```cpp
class SineWaveSpectrumProvider : public IAudioSpectrumProvider { ... };
```

Generates deterministic sine-wave spectrum data at configurable frequency
and amplitude. No audio hardware required. Used in unit tests and shader
development without CAVA installed.

### JACK client

```cpp
class JackSpectrumProvider : public IAudioSpectrumProvider { ... };
```

For pro-audio Linux setups running JACK instead of PipeWire/PulseAudio.
Lower priority — PipeWire's JACK compatibility layer covers most cases.

---

## Testing

Unit tests (`libs/phosphor-audio/tests/test_phosphoraudio.cpp`):

- **Defaults validation** — `MinBars` / `MaxBars` are even, constraints are sane.
- **Construction** — provider starts not-running, default bar count / framerate.
- **Bar count clamping** — rounds to even, clamps to `[MinBars, MaxBars]`.
- **Framerate clamping** — clamps to `[MinFramerate, MaxFramerate]`.
- **Interface polymorphism** — concrete type usable through `IAudioSpectrumProvider*`.

Integration testing (CAVA must be installed):
- Start / stop lifecycle, spectrum signal emission, bar count hot-swap.
- These are manual / CI-gated since they require a running audio server.

---

## Migration from PlasmaZones::CavaService

| Before | After |
|---|---|
| `#include "cavaservice.h"` | `#include <PhosphorAudio/CavaSpectrumProvider.h>` |
| `PlasmaZones::CavaService` | `PhosphorAudio::CavaSpectrumProvider` |
| `CavaService::isAvailable()` | `CavaSpectrumProvider::isCavaInstalled()` or `provider->isAvailable()` |
| `PlasmaZones::Audio::MinBars` | `PhosphorAudio::Defaults::MinBars` |
| `PlasmaZones::Audio::MaxBars` | `PhosphorAudio::Defaults::MaxBars` |
| `daemon/cavaservice.cpp` in CMake source lists | `PhosphorAudio::PhosphorAudio` in `target_link_libraries` |
| `m_cavaService` member | `m_audioProvider` member |
| `CavaService::spectrumUpdated` | `IAudioSpectrumProvider::spectrumUpdated` |

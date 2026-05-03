<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-audio

> Audio spectrum input for audio-reactive shaders and overlays.

## Responsibility

Give shader effects and QML overlays a lightweight audio-spectrum feed
without linking Qt Multimedia or writing a PulseAudio / PipeWire client.
The lib offers one contract and one bundled implementation that shells out
to the user's existing `cava` install; consumers wire the emitted bar
vector into a `ShaderEffect` UBO and get audio-reactive visuals for free.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorAudio::IAudioSpectrumProvider` | Provider contract: `start`, `stop`, bar count, framerate, `spectrum()` snapshot. |
| `PhosphorAudio::CavaSpectrumProvider`   | `cava`-backed provider. Detects install, picks an audio method (PulseAudio / PipeWire / ALSA), builds a throwaway config, emits normalized FFT bars. |

## Typical use

```cpp
auto* provider = new PhosphorAudio::CavaSpectrumProvider(parent);
provider->setBarCount(64);
provider->setFramerate(60);
QObject::connect(provider, &IAudioSpectrumProvider::spectrumUpdated,
                 shaderEffect, &MyShader::setBars);
provider->start();
```

## Design notes

- **No direct audio backend.** `CavaSpectrumProvider` calls out to `cava`
  because `cava` already handles PulseAudio vs PipeWire vs ALSA detection
  and owns the FFT. The lib just parses its framed byte output.
- **Graceful degradation.** `isAvailable()` returns false when `cava` is
  not installed; consumers should hide or disable audio-reactive overlays
  in that case rather than hard-fail.

## Dependencies

- `QtCore`

## See also

- [`phosphor-shaders`](../phosphor-shaders/README.md) — spectrum feeds shader UBOs through `IUniformExtension`.
- [`phosphor-rendering`](../phosphor-rendering/README.md) — host item that consumes the spectrum.

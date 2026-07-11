<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-audio

> Audio spectrum input for audio-reactive shaders and overlays.

## Responsibility

A lightweight audio-spectrum feed for shader effects and QML overlays.
One contract (`IAudioSpectrumProvider`) and one bundled implementation
that shells out to the user's `cava` install. Consumers wire the emitted
bar vector into a `ShaderEffect` UBO.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorAudio::IAudioSpectrumProvider` | Provider contract: `start`, `stop`, `options()`/`setOptions()` (full `SpectrumOptions` parameter set), `spectrum()` snapshot. |
| `PhosphorAudio::CavaSpectrumProvider`   | `cava`-backed provider. Detects install, auto-picks PipeWire or PulseAudio (other backends such as ALSA via the `inputMethod` override), builds a throwaway config, emits normalized FFT bars. |

## Typical use

```cpp
auto* provider = new PhosphorAudio::CavaSpectrumProvider(parent);
PhosphorAudio::SpectrumOptions options;
options.barCount = 64;
options.framerate = 60;
provider->setOptions(options);
QObject::connect(provider, &IAudioSpectrumProvider::spectrumUpdated,
                 shaderEffect, &MyShader::setBars);
provider->start();
```

## Design notes

- **No direct audio backend.** `CavaSpectrumProvider` spawns `cava`,
  which owns the audio capture and the FFT. The lib picks PipeWire or
  PulseAudio (or an explicit `inputMethod` override) for the generated
  config and parses cava's framed byte output.
- **Graceful degradation.** `isAvailable()` returns false when `cava` is
  not installed. Consumers should hide or disable audio-reactive overlays
  in that case rather than hard-fail.

## Dependencies

- `QtCore`

## See also

- [`phosphor-shaders`](../phosphor-shaders/README.md) — spectrum feeds shader UBOs through `IUniformExtension`.
- [`phosphor-rendering`](../phosphor-rendering/README.md) — host item that consumes the spectrum.

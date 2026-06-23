<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-service-clipboard

A clipboard-history service for Phosphor-based desktop shells.

## Responsibility

Watches the session clipboard, keeps a de-duplicated, capped, on-disk history,
and can re-apply any entry. It is the policy / history / persistence layer over
`phosphor-wayland`'s `ClipboardDevice` (a `wlr-data-control` client). It composes
the device rather than binding the protocol itself, so its public surface is a
clean Qt/QML type with no Wayland types leaking out. It surfaces the
history as a model and leaves a shell to render the picker. No UI is provided here.

- Watch the clipboard independent of keyboard focus (`wlr-data-control`).
- Keep a de-duplicated, capped history that survives restarts.
- Never read or keep a sensitive selection (password-manager hints).
- Re-apply a history entry to the clipboard on request.

The clipboard-manager UI itself is a future shell consumer of this library.

## Key types

| Type             | Role                                                                                  |
|------------------|---------------------------------------------------------------------------------------|
| `ClipboardService` | The clipboard-history host. Exposes `history` (a list model, most-recent first, with `preview` / `mimeType` / `offeredTypes` / `timestamp` roles) and `count`. `copy(index)` re-applies an entry, `remove(index)` and `clear()` prune. Loads from disk on construction and persists on every change. A plain instantiable QML type, not a singleton. |

## Typical use

C++ shell composition root:

```cpp
#include <PhosphorServiceClipboard/QmlRegistration.h>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    PhosphorServiceClipboard::registerQmlTypes();
    // ... load shell.qml
}
```

QML clipboard picker (binds the history, pastes a chosen entry):

```qml
import Phosphor.Service.Clipboard 1.0

ClipboardService {
    id: clipboard
}

ListView {
    model: clipboard.history
    delegate: ItemDelegate {
        required property int index
        required property string preview
        text: preview
        onClicked: clipboard.copy(index) // re-apply this entry to the clipboard
    }
}
```

The CLI doubles as the worked example and the acceptance harness:

```sh
# watch the clipboard and log each new entry
phosphor-service-clipboard-cli watch
# print the persisted history
phosphor-service-clipboard-cli list
# re-apply history entry 2 (serves the selection until Ctrl+C)
phosphor-service-clipboard-cli copy 2
```

## Design notes

- **Composes the foundation primitive.** The `wlr-data-control` client lives in
  `phosphor-wayland` (`ClipboardDevice`). This library links it privately and
  builds the history / dedup / persistence / model layer on top. It binds no
  protocols itself.
- **Reads asynchronously.** Each new selection is read off a pipe on the event
  loop (`receive` returns immediately and the bytes arrive via a signal), so a large
  or slow producer never blocks the UI. The model is driven through an
  `IClipboardSource` seam, so its policy is unit-tested with a fake source and no
  compositor.
- **Never persist secrets.** A selection carrying a sensitivity hint (the KDE
  `x-kde-passwordManagerHint`) is dropped entirely and is never read, kept, or
  written. The on-disk store also refuses sensitive entries as a second line of
  defence, mirroring `phosphor-service-polkit`'s never-store-the-secret stance.
- **On-disk history.** A JSON index plus per-entry content blobs (named by
  SHA-256, so identical content shares a blob) under
  `~/.local/share/phosphor-clipboard`. Writes are atomic and orphaned blobs are
  pruned. The history loads on startup and saves on every change.
- **A model, not a single host.** Clipboard is inherently a list, so the facade
  exposes a `QAbstractItemModel` (the `phosphor-service-notifications`
  host-backed-model shape), distinct from the single-active-item shape of
  `phosphor-service-polkit` / `phosphor-service-idle`.

## Dependencies

- `phosphor-wayland` (private link; provides the `ClipboardDevice`
  `wlr-data-control` client and the vendored protocol code). No separate
  `wayland-protocols` dependency.
- Qt6 >= 6.6 (Core, Qml). The CLI additionally uses Qt6 Gui for
  `QGuiApplication`.
- A compositor advertising `wlr-data-control-unstable-v1` for the live path (the
  library loads inert without it, and the persisted history still loads and reads).

## Status

Shipped. `ClipboardService` watches the session clipboard through
`PhosphorWayland::ClipboardDevice` (`wlr-data-control`), de-duplicates and caps a
history (default 100 entries, most-recent first), persists it as a JSON index +
SHA-256 content blobs under `~/.local/share/phosphor-clipboard`, and re-applies an
entry via `copy()`. Sensitive selections (the `x-kde-passwordManagerHint`) are
dropped before they are ever read. The `examples/phosphor-service-clipboard-cli`
demo provides `watch` / `list` / `copy` against a live session. Four test
binaries pin the deterministic surface with no `wlr-data-control`: the smoke
harness (registration idempotency, inert construction), the QML-engine load test,
the history-model unit test (preferred-MIME selection, dedup move-to-front, cap
eviction, sensitive drop, preview generation), and the store unit test
(text / binary round-trip, sensitive exclusion, orphan-blob pruning,
corrupt-index recovery). Image thumbnail rendering, primary-selection history,
and the clipboard-manager UI are future shell consumers.

<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-service-mpris

MPRIS2 (`org.mpris.MediaPlayer2.*`) media-player discovery and control for Phosphor-based desktop shells.

## Responsibility

Watches the session bus for MPRIS players, surfaces their metadata + playback state, and forwards transport controls. No UI. The shell decides how a now-playing card, transport bar, or media pop-out is rendered.

## Key types

| Type                | Role                                                                                                                                  |
|---------------------|---------------------------------------------------------------------------------------------------------------------------------------|
| `MprisPlayer`       | One player. Identity, track metadata, playback state, position, volume, loop / shuffle, capability flags, and invokables for play / pause / stop / togglePlaying / next / previous / seek / setPosition / raise / quit. |
| `MprisHost`         | Watches the session bus for `org.mpris.MediaPlayer2.*` names; owns the live `MprisPlayer` set; emits `playerAdded` / `playerRemoved`. |
| `MprisPlayerModel`  | `QAbstractListModel` over `MprisHost`. Roles: `player`, `identity`, `playbackState`, `trackTitle`, `trackArtist`, `artUrl`.            |

## Typical use

C++ shell composition root:

```cpp
#include <PhosphorServiceMpris/QmlRegistration.h>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    PhosphorServiceMpris::registerQmlTypes();
    // ... load shell.qml
}
```

QML consumer:

```qml
import Phosphor.Service.Mpris 1.0

MprisHost { id: mpris }
MprisPlayerModel { id: players; host: mpris }

Repeater {
    model: players
    delegate: Label {
        text: identity + " · " + trackTitle + " · " + trackArtist
    }
}
```

## Design notes

- **Async D-Bus.** All property fetches go through `QDBusPendingCallWatcher`. The GUI thread is never blocked on a player that's slow to respond.
- **PropertiesChanged + NameOwnerChanged.** Player discovery hangs off `NameOwnerChanged` for `org.mpris.MediaPlayer2.*` services, and property updates ride `PropertiesChanged`. Both connect via `QDBusConnection::connect` with a SLOT() string because Qt's D-Bus API doesn't expose a lambda-friendly overload for those signals.
- **Row mirror.** `MprisPlayerModel` keeps its own `QList<MprisPlayer*>` instead of indexing into the host's, so the model's `beginInsertRows` / `endRemoveRows` transaction boundaries always straddle the actual mutation regardless of when the host emits its add/remove signals relative to its own list state.

## Dependencies

- Qt6 ≥ 6.6 (Core, Qml, DBus)
- A running session bus with one or more `org.mpris.MediaPlayer2.*` players

## Status

Shipped. Extracted from the original `phosphor-services` umbrella as one of four per-domain siblings. The umbrella is gone, with no backwards-compat shim (per `feedback_no_legacy_shims`). The C++ + QML API is unchanged from its pre-extraction form (`PhosphorServices::Mpris*` becomes `PhosphorServiceMpris::Mpris*`, `Phosphor.Services` QML module becomes `Phosphor.Service.Mpris`).

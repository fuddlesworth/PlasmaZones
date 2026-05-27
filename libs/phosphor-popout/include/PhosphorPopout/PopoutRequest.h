// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorPopout/phosphorpopout_export.h>

#include <QObject>
#include <QPointF>
#include <QString>
#include <QVariantMap>

class QQmlComponent;
class QScreen;

namespace PhosphorPopout {

// Anchor on the host screen where the popout should appear. BarLeft /
// BarCenter / BarRight are convenience names tied to the bar geometry
// the shell has chosen; the transport resolves them to actual
// coordinates. ScreenCenter and AtPointer ignore the bar. Custom
// passes the request's customAnchor through verbatim.
enum class Anchor {
    BarLeft,
    BarCenter,
    BarRight,
    ScreenCenter,
    AtPointer,
    Custom,
};

// Exclusivity model. Driven by the arbitration policy in
// PopoutController.
//
//   Cooperative   one per scope. Opening another Cooperative request
//                 with the same scope closes the prior one. The default
//                 for control center, launcher, calendar, anything you
//                 want to swap focus between.
//
//   Modal         suppresses every Cooperative across every scope while
//                 it's open. New Cooperative requests are rejected
//                 (open() returns an empty handle). Used for OSDs and
//                 confirm dialogs.
//
//   Detached      floats independent of the above. Ignored by scope
//                 arbitration. Stays open when modals open and close.
//                 Used for pinned overlays.
enum class ExclusiveMode {
    Cooperative,
    Modal,
    Detached,
};

// A popout open request. Stable id, content delegate, screen, anchor,
// exclusivity, and forwarded props. Built as a value type so callers
// can fill it on the stack and pass by const&.
struct PHOSPHORPOPOUT_EXPORT PopoutRequest
{
    // Stable identifier for this popout, e.g. "control-center" or
    // "launcher". Used by isOpen() / toggle() / closeAll(). Two requests
    // with the same id are treated as the same logical popout (toggling
    // toggles the existing instance rather than opening a second).
    QString popoutId;

    // QML component that renders the popout content. Owned by the caller;
    // the popout service does not take ownership. May be null only when
    // the caller has wired a custom transport that doesn't need a QML
    // delegate (tests, native-rendered overlays).
    QQmlComponent* content = nullptr;

    // Screen to open on. Null means "pick the active screen", which the
    // transport defines (focused output, last-used output, etc.).
    QScreen* targetScreen = nullptr;

    Anchor anchor = Anchor::BarCenter;

    // Used only when anchor == Custom. Interpreted by the transport in
    // screen-local coordinates.
    QPointF customAnchor;

    ExclusiveMode exclusive = ExclusiveMode::Cooperative;

    // Cooperative-mode grouping. Two Cooperative popouts in different
    // scopes can be open at the same time. Defaults to "default" so
    // callers that don't care all share one scope. Common patterns are
    // "default" (one popout per process), or per-screen ("screen-DP-1")
    // when the shell wants one cooperative popout per output.
    QString scope = QStringLiteral("default");

    // Whether the popout should request keyboard focus on open. Modals
    // typically yes; cooperatives sometimes no (the bar's calendar
    // popout doesn't need keyboard while it's just showing dates).
    bool keyboardFocus = true;

    // Whether the popout should close when focus moves away from it.
    // Toggleable so plugins can pin a popout open (the user is dragging
    // a value, focus is moving to a dialog they spawned, etc.).
    bool dismissOnFocusLoss = true;

    // Arbitrary props forwarded to the QML delegate via the transport.
    // The transport plumbs these into the delegate's properties before
    // showing the surface.
    QVariantMap props;
};

} // namespace PhosphorPopout

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorPopout/phosphorpopout_export.h>

#include <QObject>
#include <QPointF>
#include <QQmlComponent>
#include <QScreen>
#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

namespace PhosphorPopout {

// Namespace metaobject so QML can see the Anchor and ExclusiveMode
// enums. QML_ELEMENT on a Q_NAMESPACE publishes the NAMESPACE as the
// QML type name, so after `import Phosphor.Popout` consumers reach the
// values through it: `PhosphorPopout.Anchor.BarCenter` and
// `PhosphorPopout.ExclusiveMode.Cooperative`. The enums are not in
// scope unqualified — a bare `Anchor.BarCenter` is a ReferenceError.
Q_NAMESPACE_EXPORT(PHOSPHORPOPOUT_EXPORT)
QML_ELEMENT

// Anchor on the host screen where the popout should appear. BarLeft,
// BarCenter, and BarRight are convenience names tied to the bar
// geometry the shell has chosen. The transport resolves them to
// actual coordinates. ScreenCenter and AtPointer ignore the bar.
// Custom forwards customAnchor verbatim.
enum class Anchor {
    BarLeft,
    BarCenter,
    BarRight,
    ScreenCenter,
    AtPointer,
    Custom,
};
Q_ENUM_NS(Anchor)

// Exclusivity model. Driven by the arbitration policy in
// PopoutController.
//
//   Cooperative   one per scope. Opening another Cooperative request
//                 with the same scope closes the prior one. The
//                 default for control center, launcher, calendar.
//
//   Modal         suppresses every Cooperative across every scope
//                 while it's open. New Cooperative requests are
//                 rejected with an empty handle. Used for OSDs and
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
Q_ENUM_NS(ExclusiveMode)

// A popout open request. Stable id, content delegate, screen, anchor,
// exclusivity, and forwarded props. Q_GADGET + QML_VALUE_TYPE so QML
// callers can build one declaratively as `PopoutRequest { ... }`.
//
// One-shot value type. Mutating fields after passing to
// IPopoutService::open has no effect on the live popout; rebuild and
// re-open if the popout state needs to change. Fields use MEMBER
// Q_PROPERTY without NOTIFY because the request is read once at open
// time and never re-read.
//
// Lifetime of `content`. The caller retains ownership of the
// QQmlComponent. The caller must keep it alive until the corresponding
// popoutClosed signal fires for the handle that open() returned.
// Deleting the component earlier will dangle inside the transport.
class PHOSPHORPOPOUT_EXPORT PopoutRequest
{
    Q_GADGET
    QML_VALUE_TYPE(popoutRequest)
    // STRUCTURED_VALUE is what lets QML build one from an object literal,
    // which is how every caller actually passes a request:
    // `Popouts.toggle({ "popoutId": "power", ... })`. Without it QML has no
    // conversion from a JS object to this gadget and the call fails with
    // "Passing incompatible arguments to C++ functions from JavaScript is
    // not allowed" — a TypeError at the call site, not a compile error.
    QML_STRUCTURED_VALUE

    Q_PROPERTY(QString popoutId MEMBER popoutId)
    Q_PROPERTY(QQmlComponent* content MEMBER content)
    Q_PROPERTY(QScreen* targetScreen MEMBER targetScreen)
    Q_PROPERTY(PhosphorPopout::Anchor anchor MEMBER anchor)
    Q_PROPERTY(QPointF customAnchor MEMBER customAnchor)
    Q_PROPERTY(PhosphorPopout::ExclusiveMode exclusive MEMBER exclusive)
    Q_PROPERTY(QString scope MEMBER scope)
    Q_PROPERTY(bool keyboardFocus MEMBER keyboardFocus)
    Q_PROPERTY(bool dismissOnFocusLoss MEMBER dismissOnFocusLoss)
    Q_PROPERTY(QVariantMap props MEMBER props)

public:
    // Default cooperative-scope identifier. Single shared instance so
    // every default-constructed PopoutRequest reuses one refcounted
    // QString rather than allocating a fresh "default" per construction.
    // Shells issuing popouts on every focus change benefit from this.
    // A function-local static, not a header-scope inline QString: the
    // inline form gains a static initializer plus exit-time destructor in
    // every linking binary and sits in SIOF territory; the local static
    // initializes on first use instead.
    static const QString& defaultScope()
    {
        static const QString scope = QStringLiteral("default");
        return scope;
    }

    // Stable identifier for this popout. Examples are "control-center"
    // or "launcher". Used by isOpen, toggle, and closeAll. Two requests
    // with the same id are treated as the same logical popout. Toggle
    // toggles the existing instance rather than opening a second.
    QString popoutId;

    // QML component that renders the popout content. Owned by the
    // caller. The popout service does not take ownership. May be null
    // only when the caller has wired a custom transport that doesn't
    // need a QML delegate. Examples are tests and native-rendered
    // overlays. The caller must keep the component alive until
    // popoutClosed fires for the returned handle.
    QQmlComponent* content = nullptr;

    // Screen to open on. Null means "pick the active screen". The
    // transport defines that meaning. Examples are the focused output
    // or the last-used output.
    QScreen* targetScreen = nullptr;

    // Where the popout sits. The default is BarCenter, which used to be a
    // harmless synonym for "centred" because every anchor centred; it now
    // routes to the bar-hang branch, so a caller that expresses no opinion
    // gets a popout hanging below the bar rather than in the middle of the
    // screen. That IS the right default for this shell, where every popout
    // is triggered from the bar, but it is no longer a neutral value: a
    // caller that wants the middle of the screen must ask for ScreenCenter.
    Anchor anchor = Anchor::BarCenter;

    // Used only when anchor == Custom. Interpreted by the transport
    // in screen-local coordinates. A default-constructed QPointF
    // (0, 0) is a valid screen-origin anchor and the transport will
    // place the popout there; callers using Anchor::Custom must
    // populate this explicitly or accept origin placement.
    QPointF customAnchor;

    ExclusiveMode exclusive = ExclusiveMode::Cooperative;

    // Cooperative-mode grouping. Two Cooperative popouts in different
    // scopes can be open at the same time. Defaults to "default" so
    // callers that don't care all share one scope. Common patterns
    // are "default" for one popout per process, or "screen-DP-1"
    // when the shell wants one cooperative popout per output.
    QString scope = defaultScope();

    // Whether the popout should request keyboard focus on open.
    // Modals typically yes. Cooperatives sometimes no. The bar's
    // calendar popout does not need keyboard while it's just showing
    // dates.
    bool keyboardFocus = true;

    // Whether the popout should close when focus moves away from it.
    // Toggleable so plugins can pin a popout open. Examples are the
    // user dragging a value or focus moving to a dialog the popout
    // spawned.
    bool dismissOnFocusLoss = true;

    // Arbitrary props forwarded to the QML delegate via the transport.
    // The transport plumbs these into the delegate's properties before
    // showing the surface.
    QVariantMap props;
};

} // namespace PhosphorPopout

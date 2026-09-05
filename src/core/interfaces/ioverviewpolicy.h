// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <QObject>
#include <QString>

namespace PlasmaZones {

/// The policy half of the workspace overview as the D-Bus adaptor sees it.
/// The adaptor lives in the core library and the concrete policy
/// (OverviewController) in the daemon, so the adaptor programs against this
/// seam: an open gate it drives, a replay payload it reads, and the two
/// notifications it relays to the wire.
class PLASMAZONES_EXPORT IOverviewPolicy : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~IOverviewPolicy() override = default;

    /// The streaming gate. Opening builds and publishes the first model
    /// synchronously so the replay that follows the open on the same
    /// round-trip is answered; closing stops the stream.
    virtual void setOpen(bool open) = 0;
    virtual bool isOpen() const = 0;
    /// The last published model, empty while closed.
    virtual QString modelJson() const = 0;

Q_SIGNALS:
    /// A new model payload (already generation-stamped). Never emitted while
    /// closed.
    void modelPublished(const QString& modelJson);
    /// The policy needs the overview closed (the current activity changed
    /// underneath it).
    void closeRequested();
};

} // namespace PlasmaZones

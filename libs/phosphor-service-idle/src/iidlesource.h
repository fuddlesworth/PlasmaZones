// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal (not installed) seam for one single-timeout idle source. The state
// machine drives a ladder of these; production wraps PhosphorWayland::IdleNotifier
// (an ext-idle-notify-v1 client), while tests substitute a fake that fires the
// idled / resumed edges directly. Keeping the machine behind this interface is
// what lets the stage logic be unit-tested without a live compositor.

#include <QObject>

#include <chrono>
#include <functional>
#include <memory>

namespace PhosphorServiceIdle {

/// One idle source: armed with a timeout, it emits `idled()` after that much
/// inactivity and `resumed()` on the next activity. Abstract so the real
/// IdleNotifier adapter and a test fake are interchangeable.
class IIdleSource : public QObject
{
    Q_OBJECT

public:
    using Ptr = std::unique_ptr<IIdleSource>;

    explicit IIdleSource(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    // Out-of-line (defined in iidlesource.cpp) so it anchors the vtable and gives
    // AUTOMOC a translation unit to attach the Q_OBJECT metaobject to.
    ~IIdleSource() override;

    /// Arm (or re-arm) this source to fire after @p timeout of inactivity.
    virtual void setTimeout(std::chrono::milliseconds timeout) = 0;
    [[nodiscard]] virtual std::chrono::milliseconds timeout() const = 0;

Q_SIGNALS:
    /// The timeout elapsed without activity.
    void idled();
    /// Activity resumed after the source had idled.
    void resumed();
};

/// Creates a fresh, unconfigured idle source. The state machine calls
/// `setTimeout` on the result for each stage. Injected so tests pass a factory
/// of fakes.
using IdleSourceFactory = std::function<IIdleSource::Ptr()>;

} // namespace PhosphorServiceIdle

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal to FacetHost. Not installed.

#include "PhosphorControl/PageController.h"

#include <functional>

namespace PhosphorControl {

/// Stands in for a page whose module has not loaded, and for the structural
/// category and module nodes. Never dirty, and Apply and Discard succeed at
/// once, so it never holds up a batch.
///
/// A module page's placeholder is shown by FacetUnavailablePage.qml when its
/// module could not load: unavailableReason says why, and retry() asks the
/// host to load the module again when retryable is true.
class FacetPlaceholderPage final : public PageController
{
    Q_OBJECT
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY unavailableChanged)
    Q_PROPERTY(bool retryable READ isRetryable NOTIFY unavailableChanged)

public:
    using PageController::PageController;

    bool isDirty() const override
    {
        return false;
    }
    void apply() override
    {
        Q_EMIT applyResult(true, QString());
    }
    void discard() override
    {
        Q_EMIT discardResult(true, QString());
    }

    QString unavailableReason() const
    {
        return m_reason;
    }
    bool isRetryable() const
    {
        return m_retryable && m_retry != nullptr;
    }

    void setUnavailable(const QString& reason, bool retryable)
    {
        if (m_reason == reason && m_retryable == retryable) {
            return;
        }
        m_reason = reason;
        m_retryable = retryable;
        Q_EMIT unavailableChanged();
    }
    void setRetry(std::function<void()> retry)
    {
        m_retry = std::move(retry);
    }

    Q_INVOKABLE void retry()
    {
        if (isRetryable()) {
            m_retry();
        }
    }

Q_SIGNALS:
    void unavailableChanged();

private:
    QString m_reason;
    bool m_retryable = false;
    std::function<void()> m_retry;
};

} // namespace PhosphorControl

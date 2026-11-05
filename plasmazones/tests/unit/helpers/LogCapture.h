// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLatin1String>
#include <QLoggingCategory>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace PlasmaZones::TestHelpers {

/// Per-category log capture for tests that need to assert WHICH branch
/// produced an outcome, not merely that the outcome happened.
///
/// Three suites grew near-identical copies of this; they are consolidated
/// here because the copies did not merely duplicate code, they duplicated an
/// invariant that turned out to be subtly wrong in all three at once.
///
/// Two properties are load-bearing:
///
///  - BOTH severities are requested explicitly. The engines' branch markers
///    are a mix of qCDebug and qCInfo, and a debug-only rule left the info
///    lines relying on Qt's default floor. Tighten a category to
///    QtWarningMsg — a routine change nothing here would flag — and every
///    POSITIVE assertion fails loudly while every NEGATIVE one silently
///    passes against an empty sink. That asymmetry is why absence-based
///    tests must also call verifyCaptureNonEmpty.
///
///  - The handler filters on the category. An unfiltered handler let any
///    other category's output satisfy a contains() assertion.
///
/// Scope note on the reset: setFilterRules replaces only the API rule layer;
/// QT_LOGGING_RULES and the config file are separate layers Qt keeps
/// independently, so clearing here cannot destroy a developer's environment
/// rules. There is no public getFilterRules, so a true save/restore is not
/// available — resetting the API layer is both what we can do and what we
/// want.
class CategoryLogCapture
{
public:
    explicit CategoryLogCapture(QLatin1String category)
        : m_category(category)
    {
        QLoggingCategory::setFilterRules(QString(QLatin1String("%1.debug=true\n%1.info=true")).arg(category));
        sink().clear();
        activeCategory() = category;
        m_previous = qInstallMessageHandler(&CategoryLogCapture::handler);
    }

    ~CategoryLogCapture()
    {
        qInstallMessageHandler(m_previous);
        QLoggingCategory::setFilterRules(QString());
    }

    CategoryLogCapture(const CategoryLogCapture&) = delete;
    CategoryLogCapture& operator=(const CategoryLogCapture&) = delete;

    /// Lines captured so far.
    QStringList lines() const
    {
        return sink();
    }

private:
    static QStringList& sink()
    {
        static QStringList s;
        return s;
    }
    static QLatin1String& activeCategory()
    {
        static QLatin1String c{""};
        return c;
    }
    static void handler(QtMsgType, const QMessageLogContext& ctx, const QString& msg)
    {
        if (ctx.category && QLatin1String(ctx.category) == activeCategory()) {
            sink().append(msg);
        }
    }

    QLatin1String m_category;
    QtMessageHandler m_previous = nullptr;
};

/// Run @p fn with @p category captured and return the lines it logged.
template<typename Fn>
inline QStringList captureCategoryLogs(QLatin1String category, Fn&& fn)
{
    CategoryLogCapture capture(category);
    fn();
    return capture.lines();
}

} // namespace PlasmaZones::TestHelpers

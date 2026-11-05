// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "phosphorcontrol_export.h"

namespace PhosphorControl {

/**
 * Minimal QML i18n context that routes through Qt's translation system.
 *
 * Install on a QQmlEngine so QML expressions can call i18n()/i18nc()/
 * i18np() as bare-word functions:
 *
 *   engine.rootContext()->setContextObject(new LocalizedContext(&engine));
 *
 * The Q_INVOKABLE methods delegate to QCoreApplication::translate() with
 * a configurable translation context — by default the application name.
 *
 * Apps that already use KLocalizedContext (KF6CoreAddons) should prefer
 * that — it provides richer plural forms and shares its catalogue with
 * the broader KDE translation infrastructure. This class exists for
 * Qt-only consumers that don't want a KF6 dependency.
 */
class PHOSPHORCONTROL_EXPORT LocalizedContext : public QObject
{
    Q_OBJECT
    Q_PROPERTY(
        QString translationContext READ translationContext WRITE setTranslationContext NOTIFY translationContextChanged)
    QML_NAMED_ELEMENT(LocalizedContext)

public:
    explicit LocalizedContext(QObject* parent = nullptr);
    ~LocalizedContext() override;

    /**
     * Returns the current translation context — the explicit setter value
     * if non-empty, otherwise the live QCoreApplication::applicationName().
     *
     * Note: because the empty-state getter reads applicationName() at
     * call time, the property's effective value can change without a
     * NOTIFY emit if `setApplicationName()` is called externally while
     * `m_context` is empty. This is intentional: consumers wanting to
     * track late-binding application-name changes get them for free.
     * Consumers wanting a stable value should call setTranslationContext()
     * explicitly once at startup.
     */
    QString translationContext() const;
    void setTranslationContext(const QString& ctx);

    Q_INVOKABLE QString i18n(const QString& text) const;
    Q_INVOKABLE QString i18nc(const QString& context, const QString& text) const;
    Q_INVOKABLE QString i18np(const QString& singular, const QString& plural, int n) const;
    Q_INVOKABLE QString i18ncp(const QString& context, const QString& singular, const QString& plural, int n) const;

Q_SIGNALS:
    void translationContextChanged();

private:
    /// Cached UTF-8 form of `effective context` used by every i18n*() call.
    /// Cleared on setTranslationContext() and on applicationNameChanged.
    /// Mutable because the cache is lazily filled from a const accessor.
    QByteArray cachedEffectiveContext() const;

    /// Cache UTF-8 encodings of disambiguation strings used by i18nc /
    /// i18ncp. QML pages reuse a small set ("ButtonLabel", "MenuItem"),
    /// so encoding once and re-looking-up by QString key avoids the per-
    /// binding toUtf8 cost. Capped (see MaxDisambiguationCacheEntries)
    /// to keep pathological caller bugs from growing the cache without
    /// bound; once full, falls back to encoding on every call.
    QByteArray cachedDisambiguation(const QString& context) const;

    /// Cache UTF-8 encodings of source-text strings passed to i18n /
    /// i18nc / i18np / i18ncp. Same rationale as cachedDisambiguation —
    /// every QML text binding hands the same source string back on
    /// each retranslate sweep, so encoding once removes the per-call
    /// toUtf8 cost.
    QByteArray cachedSourceText(const QString& text) const;

    QString m_context;
    mutable QByteArray m_effectiveContextCache;
    /// Explicit validity bit so an empty effective context (no
    /// QCoreApplication, or unset applicationName) still memoizes the
    /// empty bytearray. Without this, `isEmpty()`-based gating would
    /// re-encode on every call when the resolved context is empty.
    mutable bool m_effectiveContextValid = false;
    mutable QHash<QString, QByteArray> m_disambiguationCache;
    /// One-shot flag for the cache-full warning so the cliff is
    /// debuggable but doesn't spam the log on every subsequent call.
    mutable bool m_disambiguationCacheFullWarned = false;
    mutable QHash<QString, QByteArray> m_sourceTextCache;
    /// One-shot flag for the source-text cache-full warning, paired
    /// with m_sourceTextCache above.
    mutable bool m_sourceTextCacheFullWarned = false;
};

} // namespace PhosphorControl

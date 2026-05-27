// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorSettingsUi/LocalizedContext.h"

#include <QCoreApplication>

namespace PhosphorSettingsUi {

namespace {
// Resolve the live translation context. If the user has set one
// explicitly, honour it; otherwise read `QCoreApplication::applicationName()`
// lazily on every call so the context tracks late `setApplicationName()`
// calls (typical pattern: QApplication is created, settings are loaded,
// then setApplicationName runs — a constructor-time capture would miss
// that and yield an empty Qt translation context).
QByteArray effectiveContext(const QString& explicitCtx)
{
    if (!explicitCtx.isEmpty()) {
        return explicitCtx.toUtf8();
    }
    return QCoreApplication::applicationName().toUtf8();
}
} // namespace

LocalizedContext::LocalizedContext(QObject* parent)
    : QObject(parent)
{
    // When no explicit override is set, our translationContext() follows
    // QCoreApplication::applicationName(). Forward that signal so QML
    // bindings re-evaluate when applicationName() changes post-construction
    // (typical pattern: QApplication created, settings loaded, then
    // setApplicationName called).
    if (auto* app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::applicationNameChanged, this, [this]() {
            if (m_context.isEmpty()) {
                Q_EMIT translationContextChanged();
            }
        });
    }
}

LocalizedContext::~LocalizedContext() = default;

QString LocalizedContext::translationContext() const
{
    // Surface the resolved context — explicit override if set, otherwise
    // the current applicationName() snapshot.
    return m_context.isEmpty() ? QCoreApplication::applicationName() : m_context;
}

void LocalizedContext::setTranslationContext(const QString& ctx)
{
    if (m_context == ctx) {
        return;
    }
    m_context = ctx;
    Q_EMIT translationContextChanged();
}

QString LocalizedContext::i18n(const QString& text) const
{
    const QByteArray ctx = effectiveContext(m_context);
    return QCoreApplication::translate(ctx.constData(), text.toUtf8().constData());
}

QString LocalizedContext::i18nc(const QString& context, const QString& text) const
{
    const QByteArray ctx = effectiveContext(m_context);
    return QCoreApplication::translate(ctx.constData(), text.toUtf8().constData(), context.toUtf8().constData());
}

namespace {
// Manual %n → number replacement for the catalog-miss fallback path.
// When QTranslator returns the source verbatim (no .qm loaded or no
// entry for this key) we still want %n substituted so callers don't
// see the literal "%n" in the UI.
QString substitutePlaceholderN(QString s, int n)
{
    return s.replace(QStringLiteral("%n"), QString::number(n));
}
} // namespace

QString LocalizedContext::i18np(const QString& singular, const QString& plural, int n) const
{
    // The catalog is keyed by the singular form; translators supply
    // the locale-correct plural variants in the .ts file's
    // <numerusform> blocks. Qt's translate() selects the right one
    // based on `n` and the current locale's CLDR plural rule (handles
    // Russian/Polish/Arabic's 3-6 forms; NOT just English's binary).
    //
    // On a catalog hit, translate() returns the chosen variant with
    // %n already substituted. On a miss it ALSO substitutes %n in the
    // source (Qt behaviour for `n != -1`), so a naive comparison
    // against the raw `singular` would mistake the miss for a hit.
    // Compare against the source with %n pre-substituted, then fall
    // back to picking singular/plural by English rules.
    const QByteArray ctx = effectiveContext(m_context);
    const QByteArray src = singular.toUtf8();
    const QString translated = QCoreApplication::translate(ctx.constData(), src.constData(), nullptr, n);
    const QString srcWithN = substitutePlaceholderN(singular, n);
    if (translated != srcWithN) {
        return translated;
    }
    return substitutePlaceholderN((n == 1) ? singular : plural, n);
}

QString LocalizedContext::i18ncp(const QString& context, const QString& singular, const QString& plural, int n) const
{
    const QByteArray ctx = effectiveContext(m_context);
    const QByteArray src = singular.toUtf8();
    const QByteArray disambiguation = context.toUtf8();
    const QString translated =
        QCoreApplication::translate(ctx.constData(), src.constData(), disambiguation.constData(), n);
    const QString srcWithN = substitutePlaceholderN(singular, n);
    if (translated != srcWithN) {
        return translated;
    }
    return substitutePlaceholderN((n == 1) ? singular : plural, n);
}

} // namespace PhosphorSettingsUi

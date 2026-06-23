// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorControl/LocalizedContext.h"

#include <QCoreApplication>
#include <QDebug>

namespace PhosphorControl {

namespace {
// Hard ceiling on the per-instance disambiguation cache. QML pages typically
// reuse a small set of disambiguation keys ("ButtonLabel", "MenuItem"); a
// large value (e.g. 1000) would mask a caller bug that synthesises unique
// disambiguations per binding. Once the cache fills, cachedDisambiguation
// falls back to per-call encoding rather than evicting.
constexpr int kMaxDisambiguationCacheEntries = 128;
// Same rationale for the source-text cache used by i18n*(). QML text
// bindings reuse a stable set of source strings — caching the UTF-8
// encoding once per source string avoids the per-binding toUtf8 cost
// across every locale-change retranslate sweep. Bigger than the
// disambiguation cap because the source-string corpus is naturally
// larger (every translatable string in the UI) while still bounding
// memory against the pathological case where a caller synthesises
// unique strings per binding.
constexpr int kMaxSourceTextCacheEntries = 256;
} // namespace

LocalizedContext::LocalizedContext(QObject* parent)
    : QObject(parent)
{
    // When no explicit override is set, our translationContext() follows
    // QCoreApplication::applicationName(). Forward that signal so QML
    // bindings re-evaluate when applicationName() changes post-construction
    // (typical pattern: QApplication created, settings loaded, then
    // setApplicationName called). Also invalidates the cached UTF-8
    // effective-context so the next i18n*() call re-reads the new name.
    if (auto* app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::applicationNameChanged, this, [this]() {
            // Only invalidate when m_context is empty — otherwise the
            // explicit override is the effective value and the cached
            // UTF-8 encoding stays correct. Re-invalidating on every
            // app-name change when an override is set would re-encode
            // the same bytes on the next i18n() call for no reason.
            if (m_context.isEmpty()) {
                m_effectiveContextValid = false;
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
    // Compare against the resolved (effective) value, not the explicit
    // member: setting "MyApp" when applicationName() is also "MyApp"
    // is a no-op for QML bindings even though m_context flips from
    // empty to non-empty internally. Only emit when the resolved
    // context actually changes.
    const QString previousEffective = translationContext();
    m_context = ctx;
    m_effectiveContextValid = false;
    if (translationContext() == previousEffective) {
        return;
    }
    Q_EMIT translationContextChanged();
}

QByteArray LocalizedContext::cachedEffectiveContext() const
{
    // Hot path: every QML text binding re-evaluates on locale/lang change
    // and calls into i18n*() — repeatedly re-encoding the application
    // name to UTF-8 each call is wasteful. Cache the encoded form and
    // invalidate via setTranslationContext / applicationNameChanged. Use
    // an explicit `valid` bool rather than `isEmpty()` so an empty
    // applicationName (no QCoreApplication, or unset) still memoizes the
    // empty bytearray instead of re-encoding on every call.
    if (!m_effectiveContextValid) {
        m_effectiveContextCache =
            m_context.isEmpty() ? QCoreApplication::applicationName().toUtf8() : m_context.toUtf8();
        m_effectiveContextValid = true;
    }
    return m_effectiveContextCache;
}

QByteArray LocalizedContext::cachedDisambiguation(const QString& context) const
{
    // Same hot-path rationale as cachedEffectiveContext — disambiguation
    // strings come from a small named set in practice. constFind avoids
    // detach on the read path; insertion only happens for new keys.
    const auto it = m_disambiguationCache.constFind(context);
    if (it != m_disambiguationCache.constEnd()) {
        return it.value();
    }
    if (m_disambiguationCache.size() >= kMaxDisambiguationCacheEntries) {
        // Don't grow without bound — a caller synthesising unique keys
        // per binding would silently leak memory. Encode without
        // caching. Warn once so the misuse pattern is debuggable:
        // every subsequent fall-through is uncached toUtf8(), so the
        // performance cliff matters but should never be invisible.
        if (!m_disambiguationCacheFullWarned) {
            m_disambiguationCacheFullWarned = true;
            qWarning() << "LocalizedContext: disambiguation cache full at" << kMaxDisambiguationCacheEntries
                       << "entries — falling back to uncached UTF-8 encoding for new contexts. Likely caller bug: "
                          "synthesising unique disambiguations per binding.";
        }
        return context.toUtf8();
    }
    QByteArray encoded = context.toUtf8();
    m_disambiguationCache.insert(context, encoded);
    return encoded;
}

QByteArray LocalizedContext::cachedSourceText(const QString& text) const
{
    // Same hot-path rationale as cachedDisambiguation — every i18n()
    // call re-encodes the source string to UTF-8 just to hand it to
    // QCoreApplication::translate(). QML rebinds run the encoding
    // again on every retranslate sweep (e.g. language change), so the
    // cost compounds. Cache the encoding keyed by source QString;
    // bounded to kMaxSourceTextCacheEntries so a caller synthesising
    // unique strings per binding doesn't leak memory.
    const auto it = m_sourceTextCache.constFind(text);
    if (it != m_sourceTextCache.constEnd()) {
        return it.value();
    }
    if (m_sourceTextCache.size() >= kMaxSourceTextCacheEntries) {
        if (!m_sourceTextCacheFullWarned) {
            m_sourceTextCacheFullWarned = true;
            qWarning() << "LocalizedContext: source-text cache full at" << kMaxSourceTextCacheEntries
                       << "entries — falling back to uncached UTF-8 encoding for new source strings. Likely caller "
                          "bug: synthesising unique strings per binding.";
        }
        return text.toUtf8();
    }
    QByteArray encoded = text.toUtf8();
    m_sourceTextCache.insert(text, encoded);
    return encoded;
}

QString LocalizedContext::i18n(const QString& text) const
{
    const QByteArray ctx = cachedEffectiveContext();
    const QByteArray src = cachedSourceText(text);
    return QCoreApplication::translate(ctx.constData(), src.constData());
}

QString LocalizedContext::i18nc(const QString& context, const QString& text) const
{
    const QByteArray ctx = cachedEffectiveContext();
    const QByteArray disambiguation = cachedDisambiguation(context);
    const QByteArray src = cachedSourceText(text);
    return QCoreApplication::translate(ctx.constData(), src.constData(), disambiguation.constData());
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
    //
    // KNOWN LIMITATION: when a translator legitimately translates an
    // English source string into the same English form (common for
    // en_GB↔en_US identical entries, proper nouns, single-word terms),
    // `translated == srcWithN` produces a false negative and we fall
    // back to the English binary rule. The CLDR-correct plural is then
    // lost. This is acceptable for the lib's intended audience (Qt-only
    // apps without KLocalizedContext); apps that need bulletproof plural
    // selection should depend on KF6CoreAddons KLocalizedContext.
    const QByteArray ctx = cachedEffectiveContext();
    const QByteArray src = cachedSourceText(singular);
    const QString translated = QCoreApplication::translate(ctx.constData(), src.constData(), nullptr, n);
    const QString srcWithN = substitutePlaceholderN(singular, n);
    if (translated != srcWithN) {
        return translated;
    }
    return substitutePlaceholderN((n == 1) ? singular : plural, n);
}

QString LocalizedContext::i18ncp(const QString& context, const QString& singular, const QString& plural, int n) const
{
    // Same catalog-hit caveat as i18np above.
    const QByteArray ctx = cachedEffectiveContext();
    const QByteArray src = cachedSourceText(singular);
    const QByteArray disambiguation = cachedDisambiguation(context);
    const QString translated =
        QCoreApplication::translate(ctx.constData(), src.constData(), disambiguation.constData(), n);
    const QString srcWithN = substitutePlaceholderN(singular, n);
    if (translated != srcWithN) {
        return translated;
    }
    return substitutePlaceholderN((n == 1) ? singular : plural, n);
}

} // namespace PhosphorControl

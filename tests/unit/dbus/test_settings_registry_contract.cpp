// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_registry_contract.cpp
 * @brief Every setting the KWin effect fetches over D-Bus must be registered in
 *        SettingsAdaptor's getter registry.
 *
 * This is a tripwire for a silent-failure class that has already shipped once.
 *
 * SettingsAdaptor::getSetting resolves keys through m_getters — a HAND-MAINTAINED
 * map populated by the REGISTER_*_SETTING block — not through Qt property
 * reflection. Adding a setting to ISettings / Settings / the schema and wiring the
 * effect to fetch it therefore compiles, links, and runs perfectly while the fetch
 * silently misses.
 *
 * It used to miss quietly: getSetting answered an unknown key with a VALID,
 * empty-string variant, so `QVariant("").toBool()` gave false and the effect wrote
 * that straight into its member — which not only disabled the feature but INVERTED
 * any default-true setting. getSetting now sends a D-Bus error instead, so the
 * effect's callback never runs and the caller keeps its own default. That makes the
 * failure safe, but it does not make it visible. This test does.
 *
 * Nothing here is hand-maintained. The test scans EVERY effect source it finds on disk
 * and extracts every key each one fetches, in each of the shapes the effect writes them
 * (see keysFetchedByEffect below). A new setting, in a new file, is therefore checked
 * automatically, with no list to forget — and a fetch written in a shape the scrape does
 * NOT recognise fails the test loudly rather than vanishing from it, which would have
 * been the same silent hole all over again.
 *
 * That last property is the one to defend. This scrape has twice been written in a way
 * that quietly resolved a fetch to the wrong key while its own self-check still
 * balanced, which is the very failure it is here to prevent. If you change it, make the
 * unrecognised case LOUD.
 */

#include <QTest>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QObject>
#include <QRegularExpression>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>

#include "../helpers/IsolatedConfigGuard.h"
#include "config/settings.h"
#include "core/shaderregistry.h"
#include "dbus/settingsadaptor.h"

#include <PhosphorAnimation/PhosphorProfileRegistry.h>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

/// Source with its COMMENTS REMOVED.
///
/// The scrape works on raw text, so a comment that merely mentions `loadSettingAsync(`
/// inflates the call-site tally and fails the test pointing at prose. That is the safe
/// direction, but a tripwire that cries wolf is a tripwire someone eventually loosens —
/// and loosening this one is how it came to be broken repeatedly. Strip the comments and
/// it counts only code. String literals are left alone: a key IS a string literal, so they
/// are the one thing the scrape must still be able to see.
QString withoutComments(const QString& src)
{
    QString out;
    out.reserve(src.size());
    bool inLine = false;
    bool inBlock = false;
    bool inString = false;
    for (qsizetype i = 0; i < src.size(); ++i) {
        const QChar c = src.at(i);
        const QChar next = (i + 1 < src.size()) ? src.at(i + 1) : QChar();
        if (inLine) {
            if (c == QLatin1Char('\n')) {
                inLine = false;
                out.append(c);
            }
            continue;
        }
        if (inBlock) {
            if (c == QLatin1Char('*') && next == QLatin1Char('/')) {
                inBlock = false;
                ++i;
            }
            continue;
        }
        // STRING LITERALS ARE NOT COMMENTS, and this has to know the difference. A literal
        // containing "//" — a URL, a path, a glob, a snippet of GLSL — would otherwise look
        // like the start of a comment, and everything after it on the line (a real fetch,
        // say) would be silently deleted from what the scrape sees. Silently. No effect
        // source contains such a literal today; relying on that is exactly the kind of
        // luck this file has already run out of, repeatedly.
        if (inString) {
            if (c == QLatin1Char('\\')) {
                out.append(c);
                if (i + 1 < src.size()) {
                    out.append(src.at(i + 1)); // an escaped quote does not end the literal
                    ++i;
                }
                continue;
            }
            if (c == QLatin1Char('"')) {
                inString = false;
            }
            out.append(c);
            continue;
        }
        // A CHAR literal. '"' would otherwise open a phantom string that runs to the next
        // quote anywhere in the file, swallowing everything in between — including a real
        // fetch — from what the scrape sees.
        if (c == QLatin1Char('\'')) {
            out.append(c);
            for (++i; i < src.size(); ++i) {
                const QChar q = src.at(i);
                out.append(q);
                if (q == QLatin1Char('\\') && i + 1 < src.size()) {
                    out.append(src.at(++i));
                    continue;
                }
                if (q == QLatin1Char('\'')) {
                    break;
                }
            }
            continue;
        }
        // A RAW string, R"delim(...)delim". Its body may contain quotes, backslashes and
        // "//" freely, none of which mean anything — treating it as an ordinary string ends
        // it at the first inner quote, and everything after that on the line reads as a
        // comment and is deleted. The effect embeds GLSL, which is exactly where a raw
        // string shows up.
        if (c == QLatin1Char('R') && next == QLatin1Char('"')) {
            const qsizetype open = src.indexOf(QLatin1Char('('), i + 2);
            if (open > 0) {
                const QString delim = src.mid(i + 2, open - (i + 2));
                const QString close = QLatin1Char(')') + delim + QLatin1Char('"');
                const qsizetype end = src.indexOf(close, open + 1);
                if (end > 0) {
                    out.append(src.mid(i, end + close.size() - i));
                    i = end + close.size() - 1;
                    continue;
                }
            }
        }
        if (c == QLatin1Char('"')) {
            inString = true;
            out.append(c);
            continue;
        }
        if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
            inLine = true;
            continue;
        }
        if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
            inBlock = true;
            ++i;
            continue;
        }
        out.append(c);
    }
    return out;
}

QString readSource(const QString& path, QString* whyFailed)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *whyFailed = QStringLiteral("cannot open %1").arg(path);
        return {};
    }
    const QString stripped = withoutComments(QString::fromUtf8(f.readAll()));
    if (stripped.isEmpty()) {
        // Check what we RETURN, not what we read. Every caller treats {} as failure and
        // prints *whyFailed, and a file that is entirely comments (a licence header and
        // nothing else) reads non-empty but strips to nothing — so guarding the raw text
        // left the caller bailing out with a blank explanation.
        *whyFailed = QStringLiteral("%1 has no code in it").arg(path);
    }
    return stripped;
}

/// Every setting key the effect fetches, scraped from its own sources rather than
/// duplicated here — a hand-copied list is exactly the thing that drifts.
///
/// WHAT counts as a fetch is discovered, not listed: the set is seeded with
/// loadSettingAsync and grows to include any function that hands its own key parameter to
/// a member of the set (the audio wrappers do exactly that). A wrapper added tomorrow is
/// covered without touching this file, which matters because the alternative — a list of
/// names here — is another thing to forget, and forgetting it took the numerator and the
/// denominator out of agreement and left a wrapper's keys unchecked.
///
/// Every fetch must NAME its key at the call site, in one of two shapes:
///
///   loadSettingAsync(QStringLiteral("key"), …)   a literal
///   loadSettingAsync(SettingProperty::X, …)      a named constant, resolved from
///                                                ServiceConstants.h
///
/// and so must every call to a discovered wrapper.
///
/// There used to be a third shape — bind the constant to a local `kName` first, then
/// pass the alias — and resolving an identifier back to a key is where this scrape went
/// wrong repeatedly, each time binding a fetch to the wrong key while its own self-check balanced. The aliases are gone
/// from the effect (the constant is named where it is used), and the scrape no longer tries to resolve identifiers at
/// all: a fetch whose key it cannot read off the call site is UNRESOLVED, and unresolved is LOUD.
///
/// Still not covered, and deliberately so rather than silently: a key assembled at
/// runtime, or a fetch hidden behind a macro. Neither exists, and both would have to be
/// written on purpose.
///
/// @p unaccounted receives any fetch call site that matches none of the above and is
/// not a known non-fetch (the loadSettingAsync definition, its forwarding call, and
/// the audio wrappers' own bodies, all of which take the key as a PARAMETER). The
/// caller asserts it is empty: a fetch written in a shape this function does not know
/// would otherwise simply not be seen, and the test would pass green on precisely the
/// bug it exists to catch. It has already caught itself doing that once.
QStringList keysFetchedByEffect(QString* whyFailed, QStringList* unaccounted)
{
    const QString constantsSrc =
        readSource(QStringLiteral(PLASMAZONES_PROTOCOL_INC_DIR "/PhosphorProtocol/ServiceConstants.h"), whyFailed);
    if (constantsSrc.isEmpty()) {
        return {};
    }

    // SettingProperty::Name -> "actualKey", from ServiceConstants.h — scoped to THAT
    // namespace block. The map is keyed on the bare identifier, and the header already has
    // identifiers that repeat across namespaces (ServiceName, ObjectPath, Interface). A
    // header-wide scrape would let a future SettingProperty member that shares a name with
    // any of them bind a fetch to the WRONG key, silently, which is the bug this file has
    // now had written into it repeatedly.
    const qsizetype nsStart = constantsSrc.indexOf(QLatin1String("namespace SettingProperty {"));
    if (nsStart < 0) {
        *whyFailed = QStringLiteral("ServiceConstants.h has no SettingProperty namespace — did it move?");
        return {};
    }
    // Find the namespace's real end by MATCHING BRACES, not by looking for the first
    // `\n}`. The namespace is a flat list of constants today, so the two agree — but the
    // moment anyone nests anything in it (an inner namespace, a struct, a function body),
    // the naive scan stops at the INNER closing brace and silently truncates the slice.
    // Every constant below the truncation point then fails to scrape, and the declaration
    // self-check just below turns that into a hard failure with a misleading reason.
    qsizetype nsEnd = -1;
    int depth = 0;
    for (qsizetype i = constantsSrc.indexOf(QLatin1Char('{'), nsStart); i >= 0 && i < constantsSrc.size(); ++i) {
        if (constantsSrc.at(i) == QLatin1Char('{')) {
            ++depth;
        } else if (constantsSrc.at(i) == QLatin1Char('}')) {
            if (--depth == 0) {
                nsEnd = i;
                break;
            }
        }
    }
    if (nsEnd < 0) {
        *whyFailed = QStringLiteral("SettingProperty namespace is not closed as expected");
        return {};
    }
    const QString settingPropertySrc = constantsSrc.mid(nsStart, nsEnd - nsStart);

    QHash<QString, QString> constants;
    static const QRegularExpression constRe(
        QLatin1String("inline constexpr QLatin1String ([A-Za-z0-9_]+)\\(\"([A-Za-z0-9_]+)\"\\)"));
    auto cit = constRe.globalMatch(settingPropertySrc);
    while (cit.hasNext()) {
        const auto m = cit.next();
        constants.insert(m.captured(1), m.captured(2));
    }
    // Prove the scrape SAW every constant in the block. Miss one — because clang-format
    // wrapped a long declaration, or someone wrote `constexpr auto` — and every fetch
    // through it silently stops being checked. Counting declarations is cheap; discovering
    // months later that a key was never guarded is not.
    const qsizetype declared = settingPropertySrc.count(QLatin1String("constexpr"));
    if (declared != constants.size()) {
        *whyFailed = QStringLiteral(
                         "scraped %1 of %2 SettingProperty constants — one is declared in a shape the "
                         "scrape does not recognise, and every fetch through it would go unchecked")
                         .arg(constants.size())
                         .arg(declared);
        return {};
    }

    // The set of functions that FETCH a setting, discovered from the source rather than
    // listed here. Seeded with the primitive; any function whose body passes its OWN key
    // parameter to a member of the set joins it. That picks up the effect's forwarder and
    // both audio wrappers today, and any wrapper written tomorrow, with no list to forget.
    //
    // Why it must be discovered: the numerator once accepted any `load*()` while the
    // denominator counted only the text `loadSettingAsync(`. A wrapper call site whose key
    // was not in a recognised shape was therefore invisible to BOTH — never scraped, never
    // counted, tally balanced, key unchecked, test green. The two sides must cover the SAME
    // population or the tally proves nothing, and that is the hole this test has fallen into
    // seven times.

    // EVERY effect source, RECURSIVELY, found at test time. A hand-written file list is a
    // list to forget, and so is a hand-written directory list: the effect already has
    // three source directories, and a fetch in a new one would simply never be opened —
    // no tally, no failure, no key checked.
    // Every C++ extension, not just the two the effect happens to use today. A fetch moved
    // into a .hpp or pulled into an .inl would otherwise never be opened, and an unopened
    // file has no tally and no failure — it just quietly stops being checked.
    QStringList files;
    QDirIterator it(QStringLiteral(PLASMAZONES_EFFECT_SRC_DIR),
                    {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.hpp"), QStringLiteral("*.cc"),
                     QStringLiteral("*.cxx"), QStringLiteral("*.inl")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files << it.next();
    }
    if (files.isEmpty()) {
        *whyFailed = QStringLiteral("found no effect sources under %1").arg(QLatin1String(PLASMAZONES_EFFECT_SRC_DIR));
        return {};
    }

    // Read every source ONCE (comments stripped), then discover the fetcher set over the
    // whole corpus before scanning: a wrapper may be defined in one file and called from
    // another.
    QHash<QString, QString> sources;
    for (const QString& path : files) {
        const QString src = readSource(path, whyFailed);
        if (src.isEmpty()) {
            return {};
        }
        sources.insert(path, src);
    }

    // Seed: the primitive. A function JOINS the set by handing its OWN key parameter to a
    // member of the set — which is what a wrapper is. Both shapes the effect writes are
    // recognised, the member function and the `const auto f = [this](const QString& name`
    // lambda, and the loop runs to a fixed point so a wrapper of a wrapper is caught too.
    // Their key parameter names are collected as well: a fetcher call keyed on one of THOSE
    // is plumbing, not a fetch, and that is how the tally below tells the two apart.
    QSet<QString> fetchers{QStringLiteral("loadSettingAsync")};
    QSet<QString> keyParams;
    static const QRegularExpression memberRe(
        QLatin1String("(?:[A-Za-z0-9_]+::)?([A-Za-z0-9_]+)\\(const QString& ([A-Za-z0-9_]+)"));
    static const QRegularExpression lambdaRe(
        QLatin1String("auto ([A-Za-z0-9_]+) = \\[[^\\]]*\\]\\(const QString& ([A-Za-z0-9_]+)"));
    bool grew = true;
    while (grew) {
        grew = false;
        for (const QString& src : std::as_const(sources)) {
            for (const QRegularExpression* re : {&memberRe, &lambdaRe}) {
                auto wit = re->globalMatch(src);
                while (wit.hasNext()) {
                    const auto m = wit.next();
                    const QString fn = m.captured(1);
                    const QString param = m.captured(2);
                    if (fetchers.contains(fn)) {
                        keyParams.insert(param); // the primitive's own declaration lands here
                        continue;
                    }
                    // Does its body hand its own key parameter to something already known to
                    // fetch? Look at the text following the signature.
                    const QString body = src.mid(m.capturedEnd(0), 4000);
                    for (const QString& known : std::as_const(fetchers)) {
                        if (body.contains(known + QLatin1Char('(') + param + QLatin1Char(','))
                            || body.contains(known + QLatin1String("(this, ") + param + QLatin1Char(','))) {
                            fetchers.insert(fn);
                            keyParams.insert(param);
                            grew = true;
                            break;
                        }
                    }
                }
            }
        }
    }
    if (fetchers.size() < 2) {
        // The discovery found the seed and nothing else. Today the effect HAS wrappers, so
        // that means the shapes moved and this scan is now blind to them — which is exactly
        // the silent-hole failure this whole mechanism exists to prevent.
        *whyFailed = QStringLiteral(
            "discovered no setting-fetch wrappers beyond loadSettingAsync — the wrapper "
            "shape has changed and their keys are no longer being checked");
        return {};
    }

    // Longest name first: `loadSettingAsync` must not be matched as a prefix of a longer one.
    QStringList fetcherNames(fetchers.cbegin(), fetchers.cend());
    std::sort(fetcherNames.begin(), fetcherNames.end(), [](const QString& a, const QString& b) {
        return a.size() != b.size() ? a.size() > b.size() : a < b;
    });
    QStringList paramNames(keyParams.cbegin(), keyParams.cend());
    paramNames.sort();
    const QString anyFetcher =
        QStringLiteral("(?<![A-Za-z0-9_])(?:%1)\\(\\s*(?:this,\\s*)?").arg(fetcherNames.join(QLatin1Char('|')));

    // NAMES a key: a literal, or a SettingProperty constant.
    const QRegularExpression fetchRe(anyFetcher
                                     + QLatin1String("(?:QStringLiteral\\(\"([A-Za-z0-9_]+)\"\\)"
                                                     "|PhosphorProtocol::Service::SettingProperty::([A-Za-z0-9_]+))"));
    // ANY call to ANY fetcher — the denominator, drawn from the SAME population as the
    // numerator. That is the whole point of deriving it: a fetch either names its key (and
    // is scraped) or it does not (and is unaccounted, loudly). There is nowhere left to fall
    // between the two, which is where a wrapper's keys used to disappear.
    const QRegularExpression anyFetchRe(anyFetcher);
    // Plumbing, not a fetch: the declaration and definition of a fetcher, and a forward that
    // passes a fetcher's own key parameter through. Keyed on the parameter names discovered
    // above, so a renamed parameter shows up as unaccounted rather than being waved through.
    const QRegularExpression nonFetchRe(
        anyFetcher
        + QStringLiteral("(?:const QString& [A-Za-z0-9_]+|(?:%1)\\s*,)").arg(paramNames.join(QLatin1Char('|'))));

    QStringList keys;
    for (const QString& path : files) {
        const QString src = sources.value(path);

        int scraped = 0;
        auto it = fetchRe.globalMatch(src);
        while (it.hasNext()) {
            const auto m = it.next();
            // Two shapes, both of which NAME the key at the call site: a literal, or a
            // SettingProperty constant resolved from ServiceConstants.h. Anything else
            // resolves to nothing and trips the tally below, loudly.
            // A constant we cannot RESOLVE is reported here, not skipped. The tally cannot
            // catch it: the tally recognises a call site by its SHAPE, and this call has the
            // right shape — so it balanced perfectly while the key it fetches was never
            // compared against the registry at all. Anything the scrape cannot read is LOUD,
            // with no exceptions, because every exception has turned out to be one of these.
            const QString key = !m.captured(1).isEmpty() ? m.captured(1) : constants.value(m.captured(2));
            if (key.isEmpty()) {
                *unaccounted << QStringLiteral(
                                    "%1: cannot resolve SettingProperty::%2 — is it declared in "
                                    "ServiceConstants.h in the shape the scrape expects?")
                                    .arg(QFileInfo(path).fileName(), m.captured(2));
                continue;
            }
            keys << key;
            ++scraped;
        }

        // Every call to ANY fetcher must be accounted for: it either names a key (and was
        // scraped just above), or it is one of the plumbing shapes that takes the key as a
        // parameter. Anything else is unaccounted, and unaccounted is loud.
        int total = 0;
        auto ait = anyFetchRe.globalMatch(src);
        while (ait.hasNext()) {
            ait.next();
            ++total;
        }
        int nonFetch = 0;
        auto nit = nonFetchRe.globalMatch(src);
        while (nit.hasNext()) {
            nit.next();
            ++nonFetch;
        }
        if (scraped + nonFetch != total) {
            *unaccounted << QStringLiteral(
                                "%1: %2 fetcher call sites, %3 name a key, %4 plumbing — %5 unaccounted "
                                "for")
                                .arg(QFileInfo(path).fileName())
                                .arg(total)
                                .arg(scraped)
                                .arg(nonFetch)
                                .arg(total - scraped - nonFetch);
        }
    }

    if (keys.isEmpty()) {
        *whyFailed = QStringLiteral("scraped 0 keys — has the call shape changed?");
    }
    return keys;
}

/// Every setting key the EDITOR fetches. Same contract as the effect, same failure when it
/// breaks — the editor is a separate process reading the same registry over the same bus,
/// and an unregistered key answers with an error, so the editor silently keeps its built-in
/// default. That is how a gap overlay renders with the wrong inset and nothing says why.
///
/// The editor fetches in two shapes, and both must NAME their keys:
///
///   queryBoolSetting(QStringLiteral("key"), default)   one key at the call site
///   querySettingsBatch(kSomeKeys)                      a named QStringList of literals,
///                                                      declared in the same file
///
/// A batch call whose list cannot be found and read is UNACCOUNTED, not skipped: a list
/// assembled at runtime, or moved to another TU, would otherwise take its keys out of this
/// test's sight while leaving it green.
QStringList keysFetchedByEditor(QString* whyFailed, QStringList* unaccounted)
{
    QStringList files;
    QDirIterator it(QStringLiteral(PLASMAZONES_EDITOR_SRC_DIR),
                    {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.hpp"), QStringLiteral("*.cc"),
                     QStringLiteral("*.cxx"), QStringLiteral("*.inl")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files << it.next();
    }
    if (files.isEmpty()) {
        *whyFailed = QStringLiteral("found no editor sources under %1").arg(QLatin1String(PLASMAZONES_EDITOR_SRC_DIR));
        return {};
    }

    // A single-key fetch: query*Setting(QStringLiteral("key"), …). The helper's own
    // declaration and definition take the key as a `const QString&` parameter and so do not
    // match, which is what keeps them out of the tally.
    static const QRegularExpression directRe(
        QLatin1String("(?<![A-Za-z0-9_])query[A-Za-z0-9_]*Setting\\(\\s*QStringLiteral\\(\"([A-Za-z0-9_]+)\"\\)"));
    static const QRegularExpression directCallRe(
        QLatin1String("(?<![A-Za-z0-9_])query[A-Za-z0-9_]*Setting\\(\\s*(?!const QString&)"));
    static const QRegularExpression batchRe(
        QLatin1String("(?<![A-Za-z0-9_])querySettingsBatch\\(\\s*(?!const QStringList&)([A-Za-z0-9_]+)\\s*\\)"));
    static const QRegularExpression batchCallRe(
        QLatin1String("(?<![A-Za-z0-9_])querySettingsBatch\\(\\s*(?!const QStringList&)"));
    static const QRegularExpression literalRe(QLatin1String("QStringLiteral\\(\"([A-Za-z0-9_]+)\"\\)"));

    QStringList keys;
    for (const QString& path : files) {
        const QString src = readSource(path, whyFailed);
        if (src.isEmpty()) {
            return {};
        }

        int direct = 0;
        auto dit = directRe.globalMatch(src);
        while (dit.hasNext()) {
            keys << dit.next().captured(1);
            ++direct;
        }
        int directCalls = 0;
        auto dcit = directCallRe.globalMatch(src);
        while (dcit.hasNext()) {
            dcit.next();
            ++directCalls;
        }
        if (direct != directCalls) {
            *unaccounted << QStringLiteral("%1: %2 single-key fetches, %3 name their key — %4 do not")
                                .arg(QFileInfo(path).fileName())
                                .arg(directCalls)
                                .arg(direct)
                                .arg(directCalls - direct);
        }

        // Each batch call names a QStringList. Find its declaration in the same file and
        // read the literals out of its braces.
        int batches = 0;
        auto bit = batchRe.globalMatch(src);
        while (bit.hasNext()) {
            const QString listName = bit.next().captured(1);
            ++batches;
            const qsizetype declPos = src.indexOf(QStringLiteral("QStringList %1 = {").arg(listName));
            const qsizetype open = declPos >= 0 ? src.indexOf(QLatin1Char('{'), declPos) : -1;
            const qsizetype close = open >= 0 ? src.indexOf(QLatin1Char('}'), open) : -1;
            if (close < 0) {
                *unaccounted << QStringLiteral(
                                    "%1: querySettingsBatch(%2) — cannot find %2's literal key list in "
                                    "this file, so its keys are never checked")
                                    .arg(QFileInfo(path).fileName(), listName);
                continue;
            }
            const QString body = src.mid(open, close - open);
            auto lit = literalRe.globalMatch(body);
            int found = 0;
            while (lit.hasNext()) {
                keys << lit.next().captured(1);
                ++found;
            }
            if (found == 0) {
                *unaccounted << QStringLiteral(
                                    "%1: querySettingsBatch(%2) — %2 holds no string literals, so its keys "
                                    "are built some other way and go unchecked")
                                    .arg(QFileInfo(path).fileName(), listName);
            }
        }
        int batchCalls = 0;
        auto bcit = batchCallRe.globalMatch(src);
        while (bcit.hasNext()) {
            bcit.next();
            ++batchCalls;
        }
        if (batches != batchCalls) {
            *unaccounted << QStringLiteral(
                                "%1: %2 batch fetches, %3 pass a named key list — %4 pass something this "
                                "scrape cannot read")
                                .arg(QFileInfo(path).fileName())
                                .arg(batchCalls)
                                .arg(batches)
                                .arg(batchCalls - batches);
        }
    }

    if (keys.isEmpty()) {
        *whyFailed = QStringLiteral("scraped 0 editor keys — has the call shape changed?");
    }
    return keys;
}

} // namespace

class TestSettingsRegistryContract : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// The fixture has to be able to SEE every key production registers, or this test
    /// reports a correctly-registered key as missing and invites someone to "fix" it by
    /// deleting the assertion. initializeRegistry() gates its getters behind TWO things,
    /// and both must be satisfied here:
    ///   - a qobject_cast to the concrete Settings (so a StubSettings is not enough),
    ///   - a non-null profile registry (motionProfileTree hangs off this one).
    /// The shader registry is passed for completeness; nothing in initializeRegistry
    /// gates on it. IsolatedConfigGuard keeps the real Settings off the developer's own
    /// config.
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_settings = new Settings(nullptr);
        m_shaderRegistry = new ShaderRegistry(nullptr);
        m_profileRegistry = new PhosphorAnimation::PhosphorProfileRegistry(nullptr);
        m_parent = new QObject(nullptr);
        m_adaptor = new SettingsAdaptor(m_settings, m_shaderRegistry, m_profileRegistry, m_parent);
    }

    void cleanup()
    {
        delete m_parent;
        m_parent = nullptr;
        m_adaptor = nullptr;
        delete m_profileRegistry;
        m_profileRegistry = nullptr;
        delete m_shaderRegistry;
        m_shaderRegistry = nullptr;
        delete m_settings;
        m_settings = nullptr;
        m_guard.reset();
    }

    /**
     * The load-bearing assertion. If this fails, someone added a setting the effect
     * fetches and forgot the REGISTER_*_SETTING line — the feature is dead on the
     * effect side, however complete the rest of the wiring looks.
     */
    void testEveryKeyTheEffectFetchesIsRegistered()
    {
        QString why;
        QStringList unaccounted;
        const QStringList fetched = keysFetchedByEffect(&why, &unaccounted);
        QVERIFY2(!fetched.isEmpty(), qPrintable(why));

        // Every fetch call site must be either a key we scraped or a known non-fetch. A
        // fetch written in a shape this test does not recognise would otherwise be
        // invisible to it, and the test would pass green while the key it fetches went
        // unregistered — the exact failure this file prevents.
        QVERIFY2(unaccounted.isEmpty(),
                 qPrintable(QStringLiteral("Unrecognised setting-fetch call shape: %1. Teach keysFetchedByEffect() "
                                           "the new shape, or the key it fetches goes unchecked.")
                                .arg(unaccounted.join(QStringLiteral("; ")))));

        // One named temporary: iterators taken from two separate getSettingKeys()
        // calls would belong to different lists.
        const QStringList registeredKeys = m_adaptor->getSettingKeys();
        const QSet<QString> registered(registeredKeys.cbegin(), registeredKeys.cend());

        QStringList missing;
        for (const QString& key : fetched) {
            if (!registered.contains(key)) {
                missing << key;
            }
        }

        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("The KWin effect fetches these settings, but SettingsAdaptor does not "
                                           "register them, so getSetting answers with an error and the effect "
                                           "silently keeps its built-in default: %1. Add a REGISTER_*_SETTING line "
                                           "for each in settingsadaptor.cpp.")
                                .arg(missing.join(QStringLiteral(", ")))));
    }

    /// The editor is a SECOND process reading the SAME registry over the same bus, and it
    /// fails the same way: an unregistered key answers with an error and the editor keeps
    /// its built-in default, silently. The effect had a tripwire and the editor did not, so
    /// its two dozen keys were guarded by nothing at all.
    void testEveryKeyTheEditorFetchesIsRegistered()
    {
        QString why;
        QStringList unaccounted;
        const QStringList fetched = keysFetchedByEditor(&why, &unaccounted);
        QVERIFY2(!fetched.isEmpty(), qPrintable(why));

        QVERIFY2(unaccounted.isEmpty(),
                 qPrintable(QStringLiteral("Unrecognised setting-fetch call shape in the editor: %1. Teach "
                                           "keysFetchedByEditor() the new shape, or the key it fetches goes "
                                           "unchecked.")
                                .arg(unaccounted.join(QStringLiteral("; ")))));

        const QStringList registeredKeys = m_adaptor->getSettingKeys();
        const QSet<QString> registered(registeredKeys.cbegin(), registeredKeys.cend());

        QStringList missing;
        for (const QString& key : fetched) {
            if (!registered.contains(key)) {
                missing << key;
            }
        }

        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("The editor fetches these settings, but SettingsAdaptor does not register "
                                           "them, so getSetting answers with an error and the editor silently keeps "
                                           "its built-in default: %1. Add a REGISTER_*_SETTING line for each in "
                                           "settingsadaptor.cpp.")
                                .arg(missing.join(QStringLiteral(", ")))));
    }

    /**
     * The Decorations.Performance keys specifically, spelled out. The scrape above
     * would catch a regression on these too, but naming them pins the exact bug that
     * shipped: all three were absent from the registry, and PauseWhenIdle's default
     * of true was being inverted to false on every startup.
     */
    void testDecorationPerformanceKeysAreRegistered()
    {
        const QStringList keys = m_adaptor->getSettingKeys();
        QVERIFY(keys.contains(QStringLiteral("decorationAnimateFocusedOnly")));
        QVERIFY(keys.contains(QStringLiteral("decorationPauseWhenIdle")));
        QVERIFY(keys.contains(QStringLiteral("decorationIdleTimeoutSec")));
    }

    /**
     * A registered bool must come back as a real bool. The dead-wire bug was invisible
     * precisely because the miss returned a valid empty STRING, which coerces to false
     * without complaint — so type, not just presence, is the thing worth pinning.
     */
    void testRegisteredBoolRoundTripsAsBool()
    {
        // FALSE first. The setting defaults to true on a fresh config, so writing true
        // and reading it back would assert the default rather than the write, and the
        // true leg would pass even if the setter did nothing at all.
        m_settings->setDecorationPauseWhenIdle(false);
        const QVariant off = m_adaptor->getSetting(QStringLiteral("decorationPauseWhenIdle")).variant();
        QCOMPARE(off.typeId(), QMetaType::Bool);
        QCOMPARE(off.toBool(), false);

        m_settings->setDecorationPauseWhenIdle(true);
        const QVariant on = m_adaptor->getSetting(QStringLiteral("decorationPauseWhenIdle")).variant();
        QCOMPARE(on.typeId(), QMetaType::Bool);
        QCOMPARE(on.toBool(), true);
    }

    /**
     * An EMPTY key is a caller bug in exactly the way an unknown one is, and must answer
     * the same way. It used to return a valid empty string, which is the silent shape
     * this whole file exists to stamp out.
     */
    void testEmptyKeyDoesNotReturnAValidEmptyValue()
    {
        const QVariant v = m_adaptor->getSetting(QString()).variant();
        QVERIFY2(!v.isValid(), "An empty key must answer like an unknown one: no valid value.");
    }

    /**
     * An UNKNOWN key must not answer with a valid empty value. Called directly (not
     * over the bus) there is no error reply to send, so the contract is an invalid
     * variant — which QVariant::toBool() still turns into false, but which a caller
     * can at least detect. Over the bus this path sends a real D-Bus error and
     * loadSettingAsync's callback never runs.
     */
    void testUnknownKeyDoesNotReturnAValidEmptyValue()
    {
        const QVariant v = m_adaptor->getSetting(QStringLiteral("thisSettingDoesNotExist")).variant();
        QVERIFY2(!v.isValid(),
                 "An unknown key must not answer with a valid value: that is what made a forgotten "
                 "registration indistinguishable from a legitimately empty setting.");
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    Settings* m_settings = nullptr;
    ShaderRegistry* m_shaderRegistry = nullptr;
    PhosphorAnimation::PhosphorProfileRegistry* m_profileRegistry = nullptr;
    QObject* m_parent = nullptr;
    SettingsAdaptor* m_adaptor = nullptr;
};

QTEST_MAIN(TestSettingsRegistryContract)
#include "test_settings_registry_contract.moc"

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// What's New state for SettingsController: reading the bundled release
// history out of the Qt resource, the "new to you" baseline, and the
// last-seen-version stamp the dialog writes when it opens.
//
// All methods here are members of PlasmaZones::SettingsController. Same class
// as settingscontroller.cpp, separate translation unit, no API change.

#include "settingscontroller.h"
#include "version.h"

#include "config/configdefaults.h"
#include "core/platform/logging.h"

#include <PhosphorFsLoader/SchemaValidator.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSettings>
#include <QStringList>
#include <QVersionNumber>

#include <algorithm>

namespace PlasmaZones {

QVariantList SettingsController::whatsNewEntries() const
{
    return m_whatsNewEntries;
}

QString SettingsController::whatsNewBaselineVersion() const
{
    return m_whatsNewBaselineVersion;
}

int SettingsController::unseenWhatsNewReleaseCount() const
{
    return m_unseenWhatsNewReleaseCount;
}

// Reads :/whatsnew.json into m_whatsNewEntries, newest release first.
//
// Called once from the constructor, after m_whatsNewBaselineVersion has been
// snapshotted: the per-entry "new to you" mark is computed against that
// baseline rather than against m_lastSeenWhatsNewVersion, because
// markWhatsNewSeen() moves the latter the moment the dialog opens and would
// wipe the marks out from under the user while they are still reading them.
// The snapshot never moves for the life of the process, which is why the
// three properties it feeds are CONSTANT.
void SettingsController::loadWhatsNew()
{
    QFile whatsNewFile(QStringLiteral(":/whatsnew.json"));
    if (!whatsNewFile.open(QIODevice::ReadOnly)) {
        // Fail loudly. Everything downstream degrades to the same empty
        // browser, so without this line a resource-wiring regression is
        // indistinguishable from a user who is simply up to date.
        qCWarning(PlasmaZones::lcCore) << "whatsnew.json resource missing at :/whatsnew.json; What's New will be empty";
        return;
    }

    // Parse errors are reported as parse errors. Without the out-param a
    // truncated file yields a null document, which then fails the schema's
    // required-property check and gets logged as a validation failure —
    // pointing the reader at the schema instead of at the broken file.
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(whatsNewFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(PlasmaZones::lcCore) << "whatsnew.json is not valid JSON at offset" << parseError.offset << ":"
                                       << parseError.errorString();
        return;
    }

    // Validate against the embedded schema before consuming. The same schema
    // CI validates the file against. fromResource fails closed if the
    // resource is missing (a build error), so malformed or unvalidatable data
    // is skipped rather than surfaced. Validation is all-or-nothing over the
    // whole document: one bad entry empties the feature, which is why the
    // dialog says the history could not be loaded rather than showing a blank
    // pane.
    const auto validator = PhosphorFsLoader::SchemaValidator::fromResource(
        QStringLiteral(":/schemas/whatsnew.schema.json"), PlasmaZones::lcCore());
    if (const auto errors = validator.validate(doc.object())) {
        qCWarning(PlasmaZones::lcCore) << "whatsnew.json failed schema validation; skipping What's New entries";
        PhosphorFsLoader::logSchemaErrors(PlasmaZones::lcCore(), *errors);
        return;
    }
    const auto releases = doc.object().value(QLatin1String("releases")).toArray();

    // Only entries for THIS build or older are consumed: whatsnew.json gains
    // the next release's entry while it is still unreleased, and without the
    // clamp a user opening What's New on the current build would both SEE the
    // unreleased entry and get it stamped as seen, so the badge never fires
    // when that release actually ships. An unparsable app version fails open
    // (no filtering).
    // VERSION_STRING, not applicationVersion(): the latter is only set by the
    // settings app's own main(); a host that skips setApplicationVersion
    // would fail the clamp open, let the unreleased entry through, AND let
    // markWhatsNewSeen stamp it — exactly the badge-never-fires bug the clamp
    // prevents. An unparsable VERSION_STRING (impossible for a release build)
    // fails open: no filtering, and the stamp risk returns with it.
    const QVersionNumber appVersion = QVersionNumber::fromString(PlasmaZones::VERSION_STRING);
    // Loop-invariant, so parsed once rather than per release.
    const QVersionNumber baseline = QVersionNumber::fromString(m_whatsNewBaselineVersion);
    if (!m_whatsNewBaselineVersion.isEmpty() && baseline.isNull()) {
        // Not fatal: an unreadable baseline simply marks nothing as new. Log
        // it, because the dialog still auto-pops and the missing marks would
        // otherwise look like a rendering bug.
        qCWarning(PlasmaZones::lcCore) << "stored last-seen What's New version is not a version number:"
                                       << m_whatsNewBaselineVersion;
    }

    for (const auto& entry : releases) {
        const auto obj = entry.toObject();
        const QString versionString = obj.value(QLatin1String("version")).toString();
        const QVersionNumber entryVersion = QVersionNumber::fromString(versionString);
        if (!appVersion.isNull() && !entryVersion.isNull() && entryVersion > appVersion) {
            continue;
        }
        QVariantMap release;
        release[QStringLiteral("version")] = versionString;
        release[QStringLiteral("date")] = obj.value(QLatin1String("date")).toString();
        // Version series ("3.4" for 3.4.13). The dialog's left rail groups
        // all 158 releases under these, so it is cheaper to cut the string
        // once here than per-frame in a QML delegate.
        const QStringList parts = versionString.split(QLatin1Char('.'));
        release[QStringLiteral("series")] = parts.size() >= 2 ? parts.mid(0, 2).join(QLatin1Char('.')) : versionString;
        // Split "Kind: text" so the page can badge and filter each highlight
        // without re-parsing prose in QML. Only the leading segment before
        // the first ": " is considered, so a colon inside the body can never
        // be mistaken for a prefix; a leading segment that is not one of the
        // three kinds falls through with the raw text intact.
        //
        // These three spellings are duplicated in two other places that must
        // change with them: the `pattern` in data/schemas/whatsnew.schema.json
        // and kindLabel()/kindColor() in src/settings/qml/WhatsNewPage.qml.
        QVariantList highlights;
        const auto arr = obj.value(QLatin1String("highlights")).toArray();
        for (const auto& h : arr) {
            const QString raw = h.toString();
            QVariantMap highlight;
            const qsizetype colon = raw.indexOf(QLatin1String(": "));
            const QString prefix = colon > 0 ? raw.left(colon) : QString();
            if (prefix == QLatin1String("New") || prefix == QLatin1String("Changed")
                || prefix == QLatin1String("Fixed")) {
                highlight[QStringLiteral("kind")] = prefix.toLower();
                highlight[QStringLiteral("text")] = raw.mid(colon + 2);
            } else {
                // Defensive: the bundled data always carries a prefix because
                // the schema enforces it, so this arm is only reachable if the
                // resource is swapped for one built against a looser schema.
                // Such a line still renders in every view, just without a
                // badge — the digest keeps an unkinded group for exactly this.
                highlight[QStringLiteral("kind")] = QString();
                highlight[QStringLiteral("text")] = raw;
            }
            highlights.append(highlight);
        }
        release[QStringLiteral("highlights")] = highlights;
        // "New to you" mark, against the session-start baseline. A first
        // launch has no baseline. Marking all 158 releases unseen there would
        // put the entire history behind the "since your last version" digest,
        // which is exactly what it is not for, so a user with no stored
        // baseline gets no marks.
        const bool unseen = !baseline.isNull() && !entryVersion.isNull() && baseline < entryVersion;
        release[QStringLiteral("unseen")] = unseen;
        if (unseen)
            ++m_unseenWhatsNewReleaseCount;
        m_whatsNewEntries.append(release);
    }

    // NEWEST FIRST, as a contract rather than as a property of the file. The
    // dialog reads entries[0] as the newest release, seeds the rail's
    // expanded series from it, and lists series in array order, so appending
    // a new release to the end of whatsnew.json — the natural edit — would
    // otherwise silently open the dialog on the oldest release. Entries whose
    // version does not parse sort last; they keep their relative order.
    std::stable_sort(m_whatsNewEntries.begin(), m_whatsNewEntries.end(), [](const QVariant& lhs, const QVariant& rhs) {
        const QVersionNumber l = QVersionNumber::fromString(lhs.toMap().value(QStringLiteral("version")).toString());
        const QVersionNumber r = QVersionNumber::fromString(rhs.toMap().value(QStringLiteral("version")).toString());
        if (l.isNull() != r.isNull())
            return r.isNull();
        return r < l;
    });
}

// Highest version among m_whatsNewEntries, using QVersionNumber so "1.10.0"
// sorts after "1.9.0" (plain string compare gets that wrong).
//
// This scans rather than reading m_whatsNewEntries.first(), even though
// loadWhatsNew() sorts newest first: the sort puts entries whose version does
// not parse at the END, but a file of nothing but unparsable versions would
// still leave one at the front. The scan skips those, and both callers need
// the highest version that actually parses.
QString SettingsController::latestWhatsNewVersion() const
{
    QVersionNumber best;
    QString bestStr;
    for (const QVariant& v : m_whatsNewEntries) {
        const QString ver = v.toMap().value(QStringLiteral("version")).toString();
        const QVersionNumber parsed = QVersionNumber::fromString(ver);
        if (parsed.isNull())
            continue;
        if (bestStr.isEmpty() || best < parsed) {
            best = parsed;
            bestStr = ver;
        }
    }
    return bestStr;
}

bool SettingsController::hasUnseenWhatsNew() const
{
    const QString latest = latestWhatsNewVersion();
    if (latest.isEmpty())
        return false;
    // Unseen iff the latest bundled entry is strictly newer than what the
    // user last marked seen. String compare after normalisation would still
    // mis-order "1.10" vs "1.9", so go through QVersionNumber.
    const QVersionNumber latestV = QVersionNumber::fromString(latest);
    // An absent stored version counts as unseen here, so a first launch does
    // pop the dialog, while loadWhatsNew() deliberately marks no individual
    // release as new to that same user. The two answers differ on purpose:
    // one decides whether to show the dialog, the other decides what to
    // highlight inside it.
    const QVersionNumber seenV = QVersionNumber::fromString(m_lastSeenWhatsNewVersion);
    // Belt-and-braces: loadWhatsNew() already clamps m_whatsNewEntries to
    // VERSION_STRING, so latestV can only exceed the running version if that
    // filter regresses. Same version source on both sides, so the two can
    // never disagree.
    const QVersionNumber appV = QVersionNumber::fromString(VERSION_STRING);
    if (!appV.isNull() && appV < latestV) {
        return false;
    }
    return seenV < latestV;
}

void SettingsController::markWhatsNewSeen()
{
    const QString latest = latestWhatsNewVersion();
    if (latest.isEmpty())
        return;
    if (m_lastSeenWhatsNewVersion != latest) {
        m_lastSeenWhatsNewVersion = latest;
        QSettings appSettings;
        appSettings.setValue(ConfigDefaults::settingsAppLastSeenWhatsNewVersionKey(), latest);
        Q_EMIT lastSeenWhatsNewVersionChanged();
    }
}

} // namespace PlasmaZones

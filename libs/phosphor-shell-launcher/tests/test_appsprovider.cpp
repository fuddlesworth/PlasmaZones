// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// AppsProvider over the fixture applications tree. Pins what the launcher
// experience depends on: nothing on an empty query, name matches beat
// keyword matches for the same text, the secondary-field penalty, the
// result cap, the rows' shape, and activation of an unknown id refusing.
// Launching itself starts a real process and is not exercised here.

#include <PhosphorShellLauncher/AppsProvider.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QtTest/QtTest>

using PhosphorRegistry::ILauncherProvider;
using PhosphorShellLauncher::AppsProvider;

namespace {
const QString kFixtures = QStringLiteral(PHOSPHOR_LAUNCHER_FIXTURES);

QStringList titles(const AppsProvider& p)
{
    QStringList out;
    for (const auto& r : p.results()) {
        out.append(r.title);
    }
    return out;
}
} // namespace

class TestAppsProvider : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void scansTheFixtureTreeOnConstruction();
    void anInstallIntoAVendorSubdirectoryIsNoticed();
    void nothingOnAnEmptyQuery();
    void nameMatchOutranksKeywordMatchForTheSameText();
    void keywordsAndGenericNameStillMatch();
    void capsTheResultCount();
    void activateRefusesAnIdThatWasNeverOffered();
    void rowsCarryTheEntryFields();
    void activateRefusesUnknownAndAlternate();
    void anInstalledApplicationAppearsWithoutARestart();
};

// The directory watcher is what makes an install visible without restarting
// the shell, and it was the one wiring in this provider with no coverage: a
// deleted connect, a watch never armed on a directory that did not exist at
// construction, or a debounce that never fires all look identical to a suite
// that only ever scans once.
void TestAppsProvider::anInstalledApplicationAppearsWithoutARestart()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Deliberately does not exist yet at construction: a fresh account has no
    // ~/.local/share/applications until the first install creates it, and a
    // watch armed only in the constructor would never cover it.
    const QString appsDir = QDir(dir.path()).filePath(QStringLiteral("applications"));

    AppsProvider provider({appsDir}, QString(), {QStringLiteral("KDE")});
    provider.setQuery(QStringLiteral("late"));
    QVERIFY(provider.results().isEmpty());

    QVERIFY(QDir().mkpath(appsDir));
    // The provider re-arms its watches on every rescan, so ask for one now
    // that the directory exists, the way an existing sibling directory's
    // change would.
    provider.rescan();

    QFile f(QDir(appsDir).filePath(QStringLiteral("late-arrival.desktop")));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("[Desktop Entry]\nType=Application\nName=Late Arrival\nExec=true\n");
    f.close();

    QSignalSpy spy(&provider, &AppsProvider::resultsChanged);
    // The watcher fires, the debounce coalesces, and the rescan follows.
    QVERIFY(spy.wait(5000));
    QCOMPARE(provider.results().size(), 1);
    QCOMPARE(provider.results().first().title, QStringLiteral("Late Arrival"));
}

// The scan is recursive, so the watch has to be too. A vendor subdirectory
// was scanned at startup and then never re-scanned, because the only watched
// path was the root and installing into the subdirectory does not touch it.
void TestAppsProvider::anInstallIntoAVendorSubdirectoryIsNoticed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString appsDir = QDir(dir.path()).filePath(QStringLiteral("applications"));
    const QString vendorDir = QDir(appsDir).filePath(QStringLiteral("kde4"));
    QVERIFY(QDir().mkpath(vendorDir));

    AppsProvider provider({appsDir}, QString(), {QStringLiteral("KDE")});
    provider.setQuery(QStringLiteral("vendored"));
    QVERIFY(provider.results().isEmpty());

    QFile f(QDir(vendorDir).filePath(QStringLiteral("vendored-app.desktop")));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("[Desktop Entry]\nType=Application\nName=Vendored App\nExec=true\n");
    f.close();

    QSignalSpy spy(&provider, &AppsProvider::resultsChanged);
    QVERIFY2(spy.wait(5000), "the subdirectory was watched, not just the root");
    QCOMPARE(provider.results().size(), 1);
    QCOMPARE(provider.results().first().title, QStringLiteral("Vendored App"));
}

void TestAppsProvider::scansTheFixtureTreeOnConstruction()
{
    AppsProvider provider({QDir(kFixtures).filePath(QStringLiteral("applications"))}, QString(),
                          {QStringLiteral("KDE")});
    QStringList ids;
    for (const auto& e : provider.entries()) {
        ids.append(e.id);
    }
    QVERIFY(ids.contains(QStringLiteral("firefox")));
    QVERIFY(ids.contains(QStringLiteral("kitty")));
    QVERIFY(ids.contains(QStringLiteral("vendor-nested-tool")));
    QVERIFY(!ids.contains(QStringLiteral("hidden-tool")));
    QVERIFY(!ids.contains(QStringLiteral("deleted-tool")));
}

void TestAppsProvider::nothingOnAnEmptyQuery()
{
    AppsProvider provider({QDir(kFixtures).filePath(QStringLiteral("applications"))}, QString(),
                          {QStringLiteral("KDE")});
    QVERIFY(!provider.listsOnEmptyQuery());

    // Go from rows to none, so there is a real change to announce. The
    // contract is "emit whenever results() would now answer differently",
    // not "emit on every setQuery": each emission costs the model a full
    // reset, which drops the surface's selected row.
    provider.setQuery(QStringLiteral("firefox"));
    QVERIFY(!provider.results().isEmpty());

    QSignalSpy changed(&provider, &ILauncherProvider::resultsChanged);
    provider.setQuery(QString());
    QCOMPARE(changed.count(), 1);
    QVERIFY(provider.results().isEmpty());

    // Already empty, and empty again: nothing to announce.
    provider.setQuery(QStringLiteral("zzqx"));
    QCOMPARE(changed.count(), 1);
    QVERIFY(provider.results().isEmpty());
}

void TestAppsProvider::nameMatchOutranksKeywordMatchForTheSameText()
{
    AppsProvider provider({QDir(kFixtures).filePath(QStringLiteral("applications"))}, QString(),
                          {QStringLiteral("KDE")});
    // "browser" is Firefox's KEYWORD ("Browser") and the fixture has no app
    // NAMED browser, so it must still match; but for "fire", which is in
    // the NAME, Firefox must come first even if some keyword elsewhere
    // matched equally well.
    provider.setQuery(QStringLiteral("browser"));
    QCOMPARE(titles(provider).first(), QStringLiteral("Firefox"));

    provider.setQuery(QStringLiteral("fire"));
    QCOMPARE(titles(provider).first(), QStringLiteral("Firefox"));

    // The case that actually needs the penalty: "files" is the NAME of
    // Files and a KEYWORD of Archive Tool, both perfect fuzzy matches on
    // the same text. Without SecondaryFieldPenalty they tie and the
    // alphabetical tie-break puts Archive Tool first. (A mutation run
    // with the penalty removed passed the two cases above, which is why
    // this one exists.)
    provider.setQuery(QStringLiteral("files"));
    QCOMPARE(titles(provider).first(), QStringLiteral("Files"));
    QCOMPARE(titles(provider).at(1), QStringLiteral("Archive Tool"));
}

void TestAppsProvider::keywordsAndGenericNameStillMatch()
{
    AppsProvider provider({QDir(kFixtures).filePath(QStringLiteral("applications"))}, QString(),
                          {QStringLiteral("KDE")});
    // "web" is only in Firefox's GenericName "Web Browser".
    provider.setQuery(QStringLiteral("web"));
    QVERIFY(titles(provider).contains(QStringLiteral("Firefox")));
    // "term" is kitty's keyword.
    provider.setQuery(QStringLiteral("term"));
    QVERIFY(titles(provider).contains(QStringLiteral("kitty")));
    // And a query matching nothing yields nothing.
    provider.setQuery(QStringLiteral("zzqx"));
    QVERIFY(provider.results().isEmpty());
}

void TestAppsProvider::capsTheResultCount()
{
    AppsProvider provider({QDir(kFixtures).filePath(QStringLiteral("applications"))}, QString(),
                          {QStringLiteral("KDE")});
    // Establish that the query really has more than one answer FIRST.
    // Without this the case passed whether the cap worked or the query
    // simply matched one entry, which is the shape a vacuous test takes.
    provider.setQuery(QStringLiteral("e"));
    QVERIFY2(provider.results().size() > 1, "the uncapped query yields several rows");

    provider.setMaximumResults(1);
    QCOMPARE(provider.maximumResults(), 1);
    provider.setQuery(QStringLiteral("e"));
    QCOMPARE(provider.results().size(), 1);
    // Clamped to at least one.
    provider.setMaximumResults(0);
    QCOMPARE(provider.maximumResults(), 1);
}

// The contract says an id the provider did not hand out is unknown. Resolving
// against every scanned entry meant a stale id, for an application filtered
// out of the current query or never listed at all, still launched it.
void TestAppsProvider::activateRefusesAnIdThatWasNeverOffered()
{
    AppsProvider provider({QDir(kFixtures).filePath(QStringLiteral("applications"))}, QString(),
                          {QStringLiteral("KDE")});
    // A query that matches Firefox and not kitty, so kitty is a real,
    // scanned, launchable entry that this query did not offer.
    provider.setQuery(QStringLiteral("firefox"));
    QStringList offered;
    for (const auto& r : provider.results()) {
        offered.append(r.id);
    }
    QVERIFY(offered.contains(QStringLiteral("firefox")));
    QVERIFY2(!offered.contains(QStringLiteral("kitty")), "kitty is scanned but not offered here");

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("unknown result")));
    QVERIFY(!provider.activate(QStringLiteral("kitty"), ILauncherProvider::Activation::Primary));
}

void TestAppsProvider::rowsCarryTheEntryFields()
{
    AppsProvider provider({QDir(kFixtures).filePath(QStringLiteral("applications"))}, QString(),
                          {QStringLiteral("KDE")});
    provider.setQuery(QStringLiteral("firefox"));
    const auto rows = provider.results();
    QVERIFY(!rows.isEmpty());
    const auto& r = rows.first();
    QCOMPARE(r.id, QStringLiteral("firefox"));
    QCOMPARE(r.title, QStringLiteral("Firefox"));
    QCOMPARE(r.subtitle, QStringLiteral("Web Browser"));
    QCOMPARE(r.iconName, QStringLiteral("firefox"));
    QVERIFY(r.score > 0);
    QVERIFY(!r.primaryActionLabel.isEmpty());
    QVERIFY(!r.hasAlternateAction());
}

void TestAppsProvider::activateRefusesUnknownAndAlternate()
{
    AppsProvider provider({QDir(kFixtures).filePath(QStringLiteral("applications"))}, QString(),
                          {QStringLiteral("KDE")});
    QVERIFY(!provider.activate(QStringLiteral("no-such-app"), ILauncherProvider::Activation::Primary));
    QVERIFY(!provider.activate(QStringLiteral("firefox"), ILauncherProvider::Activation::Alternate));
}

QTEST_GUILESS_MAIN(TestAppsProvider)

#include "test_appsprovider.moc"

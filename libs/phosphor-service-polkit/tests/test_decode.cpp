// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Decode unit test for phosphor-service-polkit (milestone 7). The agent's
// register / request / PAM lifecycle needs a live polkitd to exercise (covered
// by the CLI demo in milestone 6), but the pure transformation it applies to
// polkit's request data is testable in isolation: this constructs real
// PolkitQt1::Details + Identity values and asserts they decode into the plain Qt
// types AuthRequest exposes.

#include "polkitdecode.h"

#include <polkitqt1-details.h>
#include <polkitqt1-identity.h>

#include <QStringList>
#include <QTest>
#include <QVariantMap>

#include <sys/types.h>

using namespace PhosphorServicePolkit;

class PolkitDecodeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void detailsDecodeToVariantMap();
    void emptyDetailsDecodeToEmptyMap();
    void identitiesDecodeToDisplayStrings();
    void emptyIdentitiesDecodeToEmptyList();
};

void PolkitDecodeTest::detailsDecodeToVariantMap()
{
    PolkitQt1::Details details;
    details.insert(QStringLiteral("polkit.message"), QStringLiteral("Authentication is required"));
    details.insert(QStringLiteral("org.example.path"), QStringLiteral("/usr/bin/thing"));

    const QVariantMap map = detail::detailsToMap(details);
    QCOMPARE(map.size(), 2);
    QCOMPARE(map.value(QStringLiteral("polkit.message")).toString(), QStringLiteral("Authentication is required"));
    QCOMPARE(map.value(QStringLiteral("org.example.path")).toString(), QStringLiteral("/usr/bin/thing"));
}

void PolkitDecodeTest::emptyDetailsDecodeToEmptyMap()
{
    const PolkitQt1::Details details;
    QVERIFY(detail::detailsToMap(details).isEmpty());
}

void PolkitDecodeTest::identitiesDecodeToDisplayStrings()
{
    // uid 0 always exists, so its identity is valid and toString() is non-empty
    // (e.g. "unix-user:0" / "unix-user:root").
    PolkitQt1::Identity::List identities;
    identities << PolkitQt1::UnixUserIdentity(static_cast<uid_t>(0));

    const QStringList names = detail::identityNames(identities);
    QCOMPARE(names.size(), 1);
    // toString() is "unix-user:<uid>" for a UnixUserIdentity across polkit-qt6
    // versions; assert the stable prefix (not the NSS-dependent name form) plus a
    // non-empty uid suffix, so a bare "unix-user:" could not pass.
    QVERIFY(names.at(0).startsWith(QStringLiteral("unix-user:")));
    QVERIFY(names.at(0).size() > QStringLiteral("unix-user:").size());
}

void PolkitDecodeTest::emptyIdentitiesDecodeToEmptyList()
{
    const PolkitQt1::Identity::List identities;
    QVERIFY(detail::identityNames(identities).isEmpty());
}

QTEST_GUILESS_MAIN(PolkitDecodeTest)
#include "test_decode.moc"

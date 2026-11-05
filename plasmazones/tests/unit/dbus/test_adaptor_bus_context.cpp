// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_adaptor_bus_context.cpp
 * @brief A bus call's context reaches an adaptor through the object it is
 *        exported through, and OverlayAdaptor's thumbnail gate runs on it.
 *
 * Qt sets a call's QDBusContext on the exported object, never on a
 * QDBusAbstractAdaptor, so an adaptor that inherited QDBusContext read every
 * bus call as a direct one. Every adaptor here is exported through a BusHost,
 * as Daemon exports them, and called by a second connection on the private
 * test bus.
 *
 *  1. DbusHelpers::busCaller names the peer, and DbusHelpers::sendBusError
 *     answers a call to its own adaptor (or one naming no interface) and no
 *     other.
 *  2. OverlayAdaptor's two thumbnail entries accept the compositor and a
 *     direct child of a compositor that trusts its children, and refuse with
 *     AccessDenied a child of one that does not, a grandchild, an unrelated
 *     pid, a pid no process has, and everyone when there is no compositor
 *     (which warns once, not per call). The peer is this test process, and
 *     the compositor is named relative to it through
 *     setCompositorResolverForTesting, since the test runs on no Wayland
 *     display. The bus daemon's pid lookup, the PPid read and the trust
 *     cache all run for real.
 */

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusUnixFileDescriptor>
#include <QFile>
#include <QRegularExpression>
#include <QTest>
#include <QUuid>

#include <algorithm>
#include <memory>
#include <unistd.h>

#include <PhosphorZones/LayoutRegistry.h>

#include "core/types/dmabufthumbnail.h"
#include "dbus/dbushelpers.h"
#include "dbus/overlayadaptor.h"

#include "helpers/BusPeer.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubOverlayService.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::BusHost;
using PlasmaZones::TestHelpers::BusPeer;
using PlasmaZones::TestHelpers::isBusError;

// Named, not anonymous: the test class's Fixture holds these by value.
namespace BusContextTestSupport {

const QString OverlayInterface = QStringLiteral("org.plasmazones.Overlay");
const QString ProbeInterface = QStringLiteral("org.plasmazones.test.Probe");

/// Records the thumbnails that got past the adaptor, and stores every one.
class RecordingOverlay : public TestHelpers::StubOverlayService
{
public:
    bool setSnapAssistThumbnail(const QString& handle, int, int, const QByteArray&) override
    {
        pixelHandles.append(handle);
        return true;
    }
    bool setWindowThumbnailDmabuf(const QString& handle, const DmabufThumbnailDesc&) override
    {
        dmabufHandles.append(handle);
        return true;
    }
    QStringList pixelHandles;
    QStringList dmabufHandles;
};

/// A second adaptor on the host, whose slots run the helpers from inside a
/// bus call. @c other is another adaptor on the same host, which the call
/// being served is not addressed to.
class ProbeAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.test.Probe")

public:
    explicit ProbeAdaptor(QObject* parent)
        : QDBusAbstractAdaptor(parent)
    {
    }
    QObject* other = nullptr;

public Q_SLOTS:
    /// Errors through this adaptor when @p self, through @c other when not;
    /// answers 7 when no error went out.
    int refuse(bool self)
    {
        const bool sent = DbusHelpers::sendBusError(self ? static_cast<QObject*>(this) : other, QDBusError::InvalidArgs,
                                                    QStringLiteral("probe"));
        return sent ? -1 : 7;
    }
    QString caller()
    {
        return DbusHelpers::busCaller(this);
    }
};

/// This process, the pid the bus daemon reports for every BusPeer.
uint ownPid()
{
    return static_cast<uint>(QCoreApplication::applicationPid());
}

/// A pid that is neither this process nor its parent.
uint strangerPid()
{
    return static_cast<uint>(std::max<qint64>(QCoreApplication::applicationPid(), getppid())) + 1;
}

/// A pid no process can have: past PID_MAX_LIMIT, the kernel's ceiling on
/// pid_max.
constexpr uint NoSuchPid = 4 * 1024 * 1024 + 1;

/// The parent of @p pid as the test reads it for itself, 0 if unknown.
uint testParentOf(uint pid)
{
    QFile status(QStringLiteral("/proc/%1/status").arg(pid));
    if (!status.open(QIODevice::ReadOnly)) {
        return 0;
    }
    for (const QByteArray& line : status.readAll().split('\n')) {
        if (line.startsWith("PPid:")) {
            return line.mid(5).trimmed().toUInt();
        }
    }
    return 0;
}

OverlayAdaptor::Compositor compositorAt(uint pid, bool trustsChildren)
{
    OverlayAdaptor::Compositor compositor;
    compositor.pid = pid;
    compositor.trustsChildren = trustsChildren;
    return compositor;
}

QVariantList pixelArgs(const QString& handle)
{
    return {handle, 2, 2, QByteArray(16, '\0')};
}

} // namespace BusContextTestSupport

using namespace BusContextTestSupport;

class TestAdaptorBusContext : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // busCaller reads the host's context, so a bus call names its peer and a
    // direct call names nobody.
    void busCallerNamesThePeer()
    {
        Fixture f;
        BusPeer peer(&f.host, QStringLiteral("Caller"));
        QVERIFY(peer.ready());

        const QDBusMessage reply = peer.call(ProbeInterface, QStringLiteral("caller"));

        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(reply.arguments().value(0).toString(), peer.service());
        QVERIFY(f.probe->caller().isEmpty());
    }

    // sendBusError answers the call its own adaptor is serving. Through
    // another adaptor of the same host it sends nothing, so the slot's own
    // return is the answer, and a direct call sends nothing either.
    void busErrorAnswersOnlyACallToItsOwnAdaptor()
    {
        Fixture f;
        BusPeer peer(&f.host, QStringLiteral("Refuse"));
        QVERIFY(peer.ready());

        const QDBusMessage own = peer.call(ProbeInterface, QStringLiteral("refuse"), {true});
        QVERIFY2(isBusError(own, QDBusError::InvalidArgs), qPrintable(own.errorMessage()));

        const QDBusMessage other = peer.call(ProbeInterface, QStringLiteral("refuse"), {false});
        QCOMPARE(other.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(other.arguments().value(0).toInt(), 7);

        QCOMPARE(f.probe->refuse(true), 7);
    }

    // A call naming no interface is dispatched by member name, so it is the
    // adaptor's to answer.
    void busErrorAnswersACallNamingNoInterface()
    {
        Fixture f;
        BusPeer peer(&f.host, QStringLiteral("NoInterface"));
        QVERIFY(peer.ready());

        const QDBusMessage reply = peer.call(QString(), QStringLiteral("refuse"), {true});

        QVERIFY2(isBusError(reply, QDBusError::InvalidArgs), qPrintable(reply.errorMessage()));
    }

    // A peer unrelated to the compositor is refused on both thumbnail entries
    // with AccessDenied, and nothing reaches the overlay service.
    void thumbnailFromAnUnrelatedPeerIsRefused()
    {
        Fixture f;
        f.adaptor->setCompositorResolverForTesting([] {
            return compositorAt(strangerPid(), true);
        });
        BusPeer peer(&f.host, QStringLiteral("Stranger"));
        QVERIFY(peer.ready());
        const QString handle = QUuid::createUuid().toString();

        const QDBusMessage pixels =
            peer.call(OverlayInterface, QStringLiteral("setSnapAssistThumbnail"), pixelArgs(handle));
        QVERIFY2(isBusError(pixels, QDBusError::AccessDenied), qPrintable(pixels.errorMessage()));

        QFile null(QStringLiteral("/dev/null"));
        QVERIFY(null.open(QIODevice::ReadOnly));
        const QVariant fd = QVariant::fromValue(QDBusUnixFileDescriptor(null.handle()));
        const QDBusMessage dmabuf = peer.call(OverlayInterface, QStringLiteral("setWindowThumbnailDmabuf"),
                                              {handle, 2, 2, 0u, qulonglong(0), 8u, 0u, fd, fd});
        QVERIFY2(isBusError(dmabuf, QDBusError::AccessDenied), qPrintable(dmabuf.errorMessage()));

        QVERIFY(f.overlay.pixelHandles.isEmpty());
        QVERIFY(f.overlay.dmabufHandles.isEmpty());
    }

    // A compositor that trusts its children (kwin_wayland_wrapper, whose
    // child kwin_wayland runs the effect) lends that trust to a direct
    // child. Here the "compositor" is this test's parent.
    void thumbnailFromADirectChildOfATrustingCompositorIsAccepted()
    {
        Fixture f;
        f.adaptor->setCompositorResolverForTesting([] {
            return compositorAt(static_cast<uint>(getppid()), true);
        });

        const QDBusMessage reply = pushFrom(f, QStringLiteral("Child"));

        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(reply.arguments().value(0).toBool(), true);
        QCOMPARE(f.overlay.pixelHandles.size(), 1);
    }

    // A compositor that is not the wrapper lends its children nothing: kwin
    // itself starts Xwayland and the input method.
    void thumbnailFromAChildOfACompositorThatTrustsNoChildrenIsRefused()
    {
        Fixture f;
        f.adaptor->setCompositorResolverForTesting([] {
            return compositorAt(static_cast<uint>(getppid()), false);
        });

        const QDBusMessage reply = pushFrom(f, QStringLiteral("Untrusted"));

        QVERIFY2(isBusError(reply, QDBusError::AccessDenied), qPrintable(reply.errorMessage()));
        QVERIFY(f.overlay.pixelHandles.isEmpty());
    }

    // Only a direct child passes. Here the "compositor" is this test's
    // grandparent.
    void thumbnailFromAGrandchildIsRefused()
    {
        const uint grandparent = testParentOf(static_cast<uint>(getppid()));
        QVERIFY2(grandparent != 0, "this test's parent has no readable parent");
        Fixture f;
        f.adaptor->setCompositorResolverForTesting([grandparent] {
            return compositorAt(grandparent, true);
        });

        const QDBusMessage reply = pushFrom(f, QStringLiteral("Grandchild"));

        QVERIFY2(isBusError(reply, QDBusError::AccessDenied), qPrintable(reply.errorMessage()));
        QVERIFY(f.overlay.pixelHandles.isEmpty());
    }

    // A compositor pid no process has matches no sender and no parent.
    void thumbnailIsRefusedForACompositorPidNoProcessHas()
    {
        Fixture f;
        f.adaptor->setCompositorResolverForTesting([] {
            return compositorAt(NoSuchPid, true);
        });

        const QDBusMessage reply = pushFrom(f, QStringLiteral("Unknown"));

        QVERIFY2(isBusError(reply, QDBusError::AccessDenied), qPrintable(reply.errorMessage()));
        QVERIFY(f.overlay.pixelHandles.isEmpty());
    }

    // With no compositor nobody is trusted, and the daemon says so once
    // rather than per call. A refused sender is not cached, so the second
    // call runs the check again.
    void noCompositorTrustsNobodyAndWarnsOnce()
    {
        Fixture f;
        f.adaptor->setCompositorResolverForTesting([] {
            return compositorAt(0, true);
        });
        const QRegularExpression warning(QStringLiteral("no Wayland compositor to authenticate against"));
        BusPeer peer(&f.host, QStringLiteral("NoCompositor"));
        QVERIFY(peer.ready());

        QTest::ignoreMessage(QtWarningMsg, warning);
        const QDBusMessage first = peer.call(OverlayInterface, QStringLiteral("setSnapAssistThumbnail"),
                                             pixelArgs(QUuid::createUuid().toString()));
        QVERIFY2(isBusError(first, QDBusError::AccessDenied), qPrintable(first.errorMessage()));

        QTest::failOnWarning(warning);
        const QDBusMessage again = peer.call(OverlayInterface, QStringLiteral("setSnapAssistThumbnail"),
                                             pixelArgs(QUuid::createUuid().toString()));
        QVERIFY2(isBusError(again, QDBusError::AccessDenied), qPrintable(again.errorMessage()));
        QVERIFY(f.overlay.pixelHandles.isEmpty());
    }

    // With the production lookup, a test process is never the compositor:
    // offscreen has no Wayland connection, and on a real display the test is
    // no child of the compositor's wrapper.
    void productionCheckRefusesATestPeer()
    {
        Fixture f;
        BusPeer peer(&f.host, QStringLiteral("Production"));
        QVERIFY(peer.ready());

        const QDBusMessage reply = peer.call(OverlayInterface, QStringLiteral("setSnapAssistThumbnail"),
                                             pixelArgs(QUuid::createUuid().toString()));

        QVERIFY2(isBusError(reply, QDBusError::AccessDenied), qPrintable(reply.errorMessage()));
        QVERIFY(f.overlay.pixelHandles.isEmpty());
    }

    // The compositor is accepted on both entries, and the trust is its bus
    // name's: a second peer is checked on its own and refused once the
    // compositor is someone else, while the first stays trusted.
    void thumbnailFromTheCompositorIsAccepted()
    {
        Fixture f;
        uint compositor = ownPid();
        f.adaptor->setCompositorResolverForTesting([&compositor] {
            return compositorAt(compositor, false);
        });
        BusPeer peer(&f.host, QStringLiteral("Compositor"));
        QVERIFY(peer.ready());
        const QString handle = QUuid::createUuid().toString();

        const QDBusMessage pixels =
            peer.call(OverlayInterface, QStringLiteral("setSnapAssistThumbnail"), pixelArgs(handle));
        QCOMPARE(pixels.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(pixels.arguments().value(0).toBool(), true);
        QCOMPARE(f.overlay.pixelHandles, QStringList{handle});

        compositor = strangerPid();
        QFile null(QStringLiteral("/dev/null"));
        QVERIFY(null.open(QIODevice::ReadOnly));
        const QVariant fd = QVariant::fromValue(QDBusUnixFileDescriptor(null.handle()));
        const QDBusMessage dmabuf = peer.call(OverlayInterface, QStringLiteral("setWindowThumbnailDmabuf"),
                                              {handle, 2, 2, 0u, qulonglong(0), 8u, 0u, fd, fd});
        QCOMPARE(dmabuf.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(f.overlay.dmabufHandles, QStringList{handle});

        BusPeer second(&f.host, QStringLiteral("Second"));
        QVERIFY(second.ready());
        const QDBusMessage refused =
            second.call(OverlayInterface, QStringLiteral("setSnapAssistThumbnail"), pixelArgs(handle));
        QVERIFY2(isBusError(refused, QDBusError::AccessDenied), qPrintable(refused.errorMessage()));
        QCOMPARE(f.overlay.pixelHandles.size(), 1);
    }

    // A direct call has no peer to authorise and is not gated.
    void directThumbnailIsNotGated()
    {
        Fixture f;
        f.adaptor->setCompositorResolverForTesting([] {
            return compositorAt(strangerPid(), false);
        });
        const QString handle = QUuid::createUuid().toString();

        QVERIFY(f.adaptor->setSnapAssistThumbnail(handle, 2, 2, QByteArray(16, '\0')));

        QCOMPARE(f.overlay.pixelHandles, QStringList{handle});
    }

private:
    struct Fixture;

    /// One raw-pixel thumbnail pushed by a fresh peer named @p name, and the
    /// answer. An invalid message when the peer could not reach the bus.
    static QDBusMessage pushFrom(Fixture& f, const QString& name)
    {
        BusPeer peer(&f.host, name);
        if (!peer.ready()) {
            return QDBusMessage();
        }
        return peer.call(OverlayInterface, QStringLiteral("setSnapAssistThumbnail"),
                         pixelArgs(QUuid::createUuid().toString()));
    }

    /// An overlay adaptor and a probe, both exported through one host as
    /// Daemon exports its adaptors.
    struct Fixture
    {
        Fixture()
        {
            probe->other = adaptor;
        }

        TestHelpers::IsolatedConfigGuard guard;
        std::unique_ptr<PhosphorZones::LayoutRegistry> layouts{
            TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"))};
        RecordingOverlay overlay;
        StubZoneDetector detector;
        StubSettings settings;
        BusHost host; // destroyed first, with the adaptors it owns
        OverlayAdaptor* adaptor = new OverlayAdaptor(&overlay, &detector, layouts.get(), nullptr, &settings, &host);
        ProbeAdaptor* probe = new ProbeAdaptor(&host);
    };
};

QTEST_MAIN(TestAdaptorBusContext)
#include "test_adaptor_bus_context.moc"

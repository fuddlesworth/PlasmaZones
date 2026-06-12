// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_dbus_contract_sync.cpp
 * @brief XML ↔ adaptor contract tripwire for the hand-written D-Bus adaptors.
 *
 * The XML files under dbus/ are install-only artifacts ("Keep in sync with
 * the XMLs on disk" — root CMakeLists), while the adaptors are handwritten
 * with Q_CLASSINFO. Nothing enforced the symmetry mechanically, and the
 * PR-608 audit caught exactly the drift class that invites: a capability
 * documented against a deleted method, internal helpers exposed as bus slots,
 * and renamed args. This test pins, for each (XML, adaptor) pair:
 *
 *  1. Every XML method exists as a bus-exposed metaobject method with
 *     matching in/out argument types, names, and return mapping.
 *  2. Every bus-exposed metaobject method (public slots + Q_INVOKABLEs
 *     declared on the adaptor itself) appears in the XML — internal helpers
 *     must live in plain `public:` sections, never under Q_SLOTS.
 *  3. Signals match 1:1, modulo an explicit per-interface off-contract
 *     allowlist (e.g. WindowTracking's in-process windowClosedNotification).
 *  4. Q_PROPERTYs match the XML <property> entries (name, type, access).
 *
 * Bus-exposure model mirrors QDBusAbstractAdaptor's introspection: public
 * slots, public Q_INVOKABLEs, and signals declared on the adaptor subclass
 * are exported; plain public methods are not. Out-args follow QtDBus
 * convention: a non-void return is the FIRST out argument, then non-const
 * reference parameters in declaration order.
 */

#include <QFile>
#include <QHash>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QSet>
#include <QTest>
#include <QXmlStreamReader>

#include "../../../src/dbus/autotileadaptor.h"
#include "../../../src/dbus/compositorbridgeadaptor.h"
#include "../../../src/dbus/snapadaptor.h"
#include "../../../src/dbus/windowtrackingadaptor.h"

using namespace PlasmaZones;

namespace {

struct XmlArg
{
    QString name;
    QString dbusType;
    QString direction; // "in" / "out" (signals: treated as out)
    QString qtTypeName; // org.qtproject.QtDBus.QtTypeName.* annotation, if any
};

struct XmlMethod
{
    QString name;
    QList<XmlArg> args;
};

struct XmlProperty
{
    QString name;
    QString dbusType;
    QString access; // "read" / "readwrite"
};

struct XmlInterface
{
    QString name;
    QHash<QString, XmlMethod> methods;
    QHash<QString, XmlMethod> signalEntries;
    QHash<QString, XmlProperty> properties;
};

/// Map a (normalized) C++ parameter type to its D-Bus signature. Custom
/// marshalled types are matched through the XML's QtTypeName annotation
/// instead — see compareArg().
QString dbusTypeFor(const QString& cppTypeIn)
{
    QString cppType = cppTypeIn;
    cppType.remove(QLatin1Char('&'));
    if (cppType.startsWith(QLatin1String("const "))) {
        cppType = cppType.mid(6);
    }
    static const QHash<QString, QString> kMap = {
        {QStringLiteral("QString"), QStringLiteral("s")},
        {QStringLiteral("int"), QStringLiteral("i")},
        {QStringLiteral("bool"), QStringLiteral("b")},
        {QStringLiteral("uint"), QStringLiteral("u")},
        {QStringLiteral("double"), QStringLiteral("d")},
        {QStringLiteral("qlonglong"), QStringLiteral("x")},
        {QStringLiteral("qulonglong"), QStringLiteral("t")},
        {QStringLiteral("QStringList"), QStringLiteral("as")},
        {QStringLiteral("QVariantMap"), QStringLiteral("a{sv}")},
        {QStringLiteral("QByteArray"), QStringLiteral("ay")},
    };
    return kMap.value(cppType);
}

XmlInterface parseInterfaceXml(const QString& path)
{
    XmlInterface iface;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return iface;
    }
    QXmlStreamReader xml(&f);
    XmlMethod current;
    bool inMethod = false;
    bool inSignal = false;
    int lastArgIndex = -1;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto el = xml.name();
            const auto attrs = xml.attributes();
            if (el == QLatin1String("interface")) {
                iface.name = attrs.value(QLatin1String("name")).toString();
            } else if (el == QLatin1String("method") || el == QLatin1String("signal")) {
                current = XmlMethod{attrs.value(QLatin1String("name")).toString(), {}};
                inMethod = (el == QLatin1String("method"));
                inSignal = !inMethod;
                lastArgIndex = -1;
            } else if (el == QLatin1String("arg") && (inMethod || inSignal)) {
                XmlArg arg;
                arg.name = attrs.value(QLatin1String("name")).toString();
                arg.dbusType = attrs.value(QLatin1String("type")).toString();
                arg.direction = attrs.value(QLatin1String("direction")).toString();
                if (arg.direction.isEmpty()) {
                    // Method args default to "in" per the D-Bus spec; signal
                    // args are always out.
                    arg.direction = inSignal ? QStringLiteral("out") : QStringLiteral("in");
                }
                current.args.append(arg);
                lastArgIndex = current.args.size() - 1;
            } else if (el == QLatin1String("annotation") && lastArgIndex >= 0) {
                const QString annName = attrs.value(QLatin1String("name")).toString();
                if (annName.startsWith(QLatin1String("org.qtproject.QtDBus.QtTypeName"))) {
                    current.args[lastArgIndex].qtTypeName = attrs.value(QLatin1String("value")).toString();
                }
            } else if (el == QLatin1String("property")) {
                XmlProperty p;
                p.name = attrs.value(QLatin1String("name")).toString();
                p.dbusType = attrs.value(QLatin1String("type")).toString();
                p.access = attrs.value(QLatin1String("access")).toString();
                iface.properties.insert(p.name, p);
            }
        } else if (xml.isEndElement()) {
            const auto el = xml.name();
            if (el == QLatin1String("method") && inMethod) {
                iface.methods.insert(current.name, current);
                inMethod = false;
            } else if (el == QLatin1String("signal") && inSignal) {
                iface.signalEntries.insert(current.name, current);
                inSignal = false;
            } else if (el == QLatin1String("arg")) {
                // keep lastArgIndex until the next arg/method so trailing
                // annotations inside <arg>...</arg> are captured above
            }
        }
    }
    return iface;
}

/// Bus-exposed methods of the adaptor subclass itself: public slots and
/// public Q_INVOKABLEs from methodOffset() up, with default-argument clones
/// collapsed to the full-signature entry.
QHash<QString, QMetaMethod> busMethods(const QMetaObject& mo)
{
    QHash<QString, QMetaMethod> out;
    for (int i = mo.methodOffset(); i < mo.methodCount(); ++i) {
        const QMetaMethod m = mo.method(i);
        if (m.access() != QMetaMethod::Public) {
            continue;
        }
        if (m.methodType() != QMetaMethod::Slot && m.methodType() != QMetaMethod::Method) {
            continue;
        }
        const QString name = QString::fromLatin1(m.name());
        auto it = out.find(name);
        if (it == out.end() || it->parameterCount() < m.parameterCount()) {
            out.insert(name, m); // default-arg clones have fewer params
        }
    }
    return out;
}

QHash<QString, QMetaMethod> busSignals(const QMetaObject& mo)
{
    QHash<QString, QMetaMethod> out;
    for (int i = mo.methodOffset(); i < mo.methodCount(); ++i) {
        const QMetaMethod m = mo.method(i);
        if (m.methodType() == QMetaMethod::Signal) {
            out.insert(QString::fromLatin1(m.name()), m);
        }
    }
    return out;
}

QString normalizeCppType(QByteArray t)
{
    QString s = QString::fromLatin1(t);
    s.remove(QLatin1Char('&'));
    if (s.startsWith(QLatin1String("const "))) {
        s = s.mid(6);
    }
    return s;
}

/// Compare one XML arg against a C++ type. Custom marshalled types must
/// carry a QtTypeName annotation naming the exact C++ type; basic types map
/// through dbusTypeFor().
void compareArg(const QString& iface, const QString& method, const XmlArg& xmlArg, const QByteArray& cppType)
{
    const QString cpp = normalizeCppType(cppType);
    if (!xmlArg.qtTypeName.isEmpty()) {
        QVERIFY2(xmlArg.qtTypeName == cpp,
                 qPrintable(QStringLiteral("%1.%2 arg '%3': QtTypeName '%4' != C++ type '%5'")
                                .arg(iface, method, xmlArg.name, xmlArg.qtTypeName, cpp)));
        return;
    }
    const QString mapped = dbusTypeFor(cpp);
    QVERIFY2(!mapped.isEmpty(),
             qPrintable(QStringLiteral("%1.%2 arg '%3': C++ type '%4' has no basic D-Bus mapping "
                                       "and the XML carries no QtTypeName annotation")
                            .arg(iface, method, xmlArg.name, cpp)));
    QVERIFY2(mapped == xmlArg.dbusType,
             qPrintable(QStringLiteral("%1.%2 arg '%3': XML type '%4' != mapped '%5' "
                                       "(C++ '%6')")
                            .arg(iface, method, xmlArg.name, xmlArg.dbusType, mapped, cpp)));
}

} // namespace

class TestDBusContractSync : public QObject
{
    Q_OBJECT

    QString xmlPath(const QString& interfaceName) const
    {
        return QStringLiteral(PLASMAZONES_DBUS_XML_DIR "/") + interfaceName + QStringLiteral(".xml");
    }

    void verifyContract(const QMetaObject& mo, const QString& interfaceName,
                        const QSet<QString>& offContractSignals = {})
    {
        const XmlInterface iface = parseInterfaceXml(xmlPath(interfaceName));
        QVERIFY2(!iface.name.isEmpty(), qPrintable(QStringLiteral("could not parse %1").arg(xmlPath(interfaceName))));
        QCOMPARE(iface.name, interfaceName);

        // The adaptor's Q_CLASSINFO must name the same interface the XML does.
        const int ci = mo.indexOfClassInfo("D-Bus Interface");
        QVERIFY(ci >= 0);
        QCOMPARE(QString::fromLatin1(mo.classInfo(ci).value()), interfaceName);

        const QHash<QString, QMetaMethod> methods = busMethods(mo);
        const QHash<QString, QMetaMethod> signals_ = busSignals(mo);

        // 1 + 2. Method bijection with full argument verification.
        for (auto it = iface.methods.constBegin(); it != iface.methods.constEnd(); ++it) {
            QVERIFY2(methods.contains(it.key()),
                     qPrintable(QStringLiteral("%1: XML method '%2' has no bus-exposed adaptor method")
                                    .arg(interfaceName, it.key())));
            const QMetaMethod m = methods.value(it.key());

            QList<XmlArg> inArgs;
            QList<XmlArg> outArgs;
            for (const XmlArg& a : it->args) {
                (a.direction == QLatin1String("out") ? outArgs : inArgs).append(a);
            }

            // Split C++ parameters into ins (by-value / const-ref) and outs
            // (non-const refs), preserving order.
            QList<QByteArray> cppIns;
            QList<QByteArray> cppOuts;
            const QList<QByteArray> paramTypes = m.parameterTypes();
            for (const QByteArray& t : paramTypes) {
                if (t.endsWith('&') && !t.startsWith("const ")) {
                    cppOuts.append(t);
                } else {
                    cppIns.append(t);
                }
            }

            QVERIFY2(inArgs.size() == cppIns.size(),
                     qPrintable(QStringLiteral("%1.%2: %3 XML in-args vs %4 C++ in-params")
                                    .arg(interfaceName, it.key())
                                    .arg(inArgs.size())
                                    .arg(cppIns.size())));
            for (int i = 0; i < inArgs.size(); ++i) {
                compareArg(interfaceName, it.key(), inArgs[i], cppIns[i]);
            }

            // QtDBus out mapping: non-void return first, then ref params.
            const bool hasReturn = qstrcmp(m.typeName(), "void") != 0;
            const int expectedOuts = cppOuts.size() + (hasReturn ? 1 : 0);
            QVERIFY2(outArgs.size() == expectedOuts,
                     qPrintable(QStringLiteral("%1.%2: %3 XML out-args vs %4 expected (return %5 + %6 ref params)")
                                    .arg(interfaceName, it.key())
                                    .arg(outArgs.size())
                                    .arg(expectedOuts)
                                    .arg(hasReturn ? QStringLiteral("yes") : QStringLiteral("no"))
                                    .arg(cppOuts.size())));
            int outIdx = 0;
            if (hasReturn) {
                compareArg(interfaceName, it.key(), outArgs[outIdx++], QByteArray(m.typeName()));
            }
            for (const QByteArray& t : cppOuts) {
                compareArg(interfaceName, it.key(), outArgs[outIdx++], t);
            }

            // In-arg NAMES are part of the published contract (out-args may
            // use a synthetic name for the return value, so only the
            // ref-param tail is name-checked).
            const QList<QByteArray> paramNames = m.parameterNames();
            int nameIdx = 0;
            for (const QByteArray& t : paramTypes) {
                const QString cppName = QString::fromLatin1(paramNames.value(nameIdx));
                const bool isOut = t.endsWith('&') && !t.startsWith("const ");
                if (!isOut) {
                    int xmlIdx = 0;
                    // recompute position among in-args
                    for (int k = 0, ins = 0; k < paramTypes.size() && k < nameIdx; ++k) {
                        const QByteArray& pt = paramTypes[k];
                        if (!(pt.endsWith('&') && !pt.startsWith("const "))) {
                            ++ins;
                        }
                        xmlIdx = ins;
                    }
                    QVERIFY2(inArgs[xmlIdx].name == cppName,
                             qPrintable(QStringLiteral("%1.%2: XML in-arg name '%3' != C++ param name '%4'")
                                            .arg(interfaceName, it.key(), inArgs[xmlIdx].name, cppName)));
                }
                ++nameIdx;
            }
        }
        for (auto it = methods.constBegin(); it != methods.constEnd(); ++it) {
            QVERIFY2(iface.methods.contains(it.key()),
                     qPrintable(QStringLiteral("%1: bus-exposed adaptor method '%2' is missing from the contract "
                                               "XML — either add it to the XML or move it to a plain public: "
                                               "section (internal helpers must not sit under Q_SLOTS)")
                                    .arg(interfaceName, it.key())));
        }

        // 3. Signal bijection (modulo the documented off-contract set).
        for (auto it = iface.signalEntries.constBegin(); it != iface.signalEntries.constEnd(); ++it) {
            QVERIFY2(
                signals_.contains(it.key()),
                qPrintable(QStringLiteral("%1: XML signal '%2' has no adaptor signal").arg(interfaceName, it.key())));
            const QMetaMethod m = signals_.value(it.key());
            QVERIFY2(it->args.size() == m.parameterCount(),
                     qPrintable(QStringLiteral("%1 signal %2: %3 XML args vs %4 C++ params")
                                    .arg(interfaceName, it.key())
                                    .arg(it->args.size())
                                    .arg(m.parameterCount())));
            const QList<QByteArray> sigTypes = m.parameterTypes();
            for (int i = 0; i < it->args.size(); ++i) {
                compareArg(interfaceName, it.key(), it->args[i], sigTypes[i]);
            }
        }
        for (auto it = signals_.constBegin(); it != signals_.constEnd(); ++it) {
            if (offContractSignals.contains(it.key())) {
                continue;
            }
            QVERIFY2(iface.signalEntries.contains(it.key()),
                     qPrintable(QStringLiteral("%1: adaptor signal '%2' is missing from the contract XML (add it, "
                                               "or document it off-contract and extend this test's allowlist)")
                                    .arg(interfaceName, it.key())));
        }

        // 4. Property bijection.
        for (auto it = iface.properties.constBegin(); it != iface.properties.constEnd(); ++it) {
            const int pi = mo.indexOfProperty(it.key().toLatin1().constData());
            QVERIFY2(
                pi >= mo.propertyOffset(),
                qPrintable(
                    QStringLiteral("%1: XML property '%2' has no adaptor Q_PROPERTY").arg(interfaceName, it.key())));
            const QMetaProperty p = mo.property(pi);
            const QString mapped = dbusTypeFor(QString::fromLatin1(p.typeName()));
            QVERIFY2(mapped == it->dbusType,
                     qPrintable(QStringLiteral("%1 property %2: XML type '%3' != mapped '%4'")
                                    .arg(interfaceName, it.key(), it->dbusType, mapped)));
            const QString expectedAccess = p.isWritable() ? QStringLiteral("readwrite") : QStringLiteral("read");
            QCOMPARE(it->access, expectedAccess);
        }
        for (int i = mo.propertyOffset(); i < mo.propertyCount(); ++i) {
            const QString name = QString::fromLatin1(mo.property(i).name());
            QVERIFY2(iface.properties.contains(name),
                     qPrintable(QStringLiteral("%1: adaptor property '%2' is missing from the contract XML")
                                    .arg(interfaceName, name)));
        }
    }

private Q_SLOTS:

    void testCompositorBridgeContract()
    {
        verifyContract(CompositorBridgeAdaptor::staticMetaObject, QStringLiteral("org.plasmazones.CompositorBridge"));
    }

    void testWindowTrackingContract()
    {
        // windowClosedNotification is a documented in-process Qt signal —
        // bus-visible but deliberately off the published contract (see its
        // doc comment in windowtrackingadaptor.h).
        verifyContract(WindowTrackingAdaptor::staticMetaObject, QStringLiteral("org.plasmazones.WindowTracking"),
                       {QStringLiteral("windowClosedNotification")});
    }

    void testAutotileContract()
    {
        verifyContract(AutotileAdaptor::staticMetaObject, QStringLiteral("org.plasmazones.Autotile"));
    }

    void testSnapContract()
    {
        verifyContract(SnapAdaptor::staticMetaObject, QStringLiteral("org.plasmazones.Snap"));
    }
};

QTEST_GUILESS_MAIN(TestDBusContractSync)
#include "test_dbus_contract_sync.moc"

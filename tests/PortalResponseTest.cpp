#include "capture/PortalResponse.h"

#include <QDBusObjectPath>
#include <QtTest>

using namespace mistercast;

class PortalResponseTest final : public QObject {
    Q_OBJECT

private slots:
    void sourceOptionsTrackCapabilities();
    void sessionPathAcceptsPortalTypes();
    void streamSelectionUsesFirstValidStream();
    void malformedStreamResponseIsRejected();
};

void PortalResponseTest::sourceOptionsTrackCapabilities()
{
    const auto embedded = portalSourceOptions(4, 3);
    QCOMPARE(embedded.value(QStringLiteral("types")).toUInt(), std::uint32_t{3});
    QCOMPARE(embedded.value(QStringLiteral("multiple")).toBool(), false);
    QCOMPARE(embedded.value(QStringLiteral("cursor_mode")).toUInt(), std::uint32_t{2});
    QCOMPARE(embedded.value(QStringLiteral("persist_mode")).toUInt(), std::uint32_t{1});

    const auto hidden = portalSourceOptions(3, 1);
    QCOMPARE(hidden.value(QStringLiteral("cursor_mode")).toUInt(), std::uint32_t{1});
    QVERIFY(!hidden.contains(QStringLiteral("persist_mode")));

    const auto noCursor = portalSourceOptions(3, 0);
    QVERIFY(!noCursor.contains(QStringLiteral("cursor_mode")));
}

void PortalResponseTest::sessionPathAcceptsPortalTypes()
{
    const QString expected = QStringLiteral("/org/freedesktop/portal/desktop/session/1");
    QCOMPARE(
        portalSessionPath({
            {QStringLiteral("session_handle"),
             QVariant::fromValue(QDBusObjectPath(expected))},
        }),
        expected);
    QCOMPARE(
        portalSessionPath({{QStringLiteral("session_handle"), expected}}),
        expected);
    QVERIFY(portalSessionPath({}).isEmpty());
}

void PortalResponseTest::streamSelectionUsesFirstValidStream()
{
    const PortalStreams streams{
        PortalStream{
            42,
            {{QStringLiteral("pipewire-serial"), QVariant::fromValue(std::uint64_t{9001})}},
        },
        PortalStream{99, {}},
    };
    const auto selected = selectPortalStream(streams);
    QVERIFY(selected.has_value());
    QCOMPARE(selected->nodeId, std::uint32_t{42});
    QCOMPARE(selected->serial, std::uint64_t{9001});
}

void PortalResponseTest::malformedStreamResponseIsRejected()
{
    QVERIFY(!selectPortalStream({}).has_value());
    QVERIFY(!selectPortalStream({PortalStream{0, {}}}).has_value());
    QVERIFY(!portalStream({}).has_value());
    QVERIFY(!portalStream({
        {QStringLiteral("streams"), QStringLiteral("not a D-Bus array")},
    }).has_value());
}

QTEST_GUILESS_MAIN(PortalResponseTest)
#include "PortalResponseTest.moc"

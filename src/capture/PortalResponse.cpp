#include "capture/PortalResponse.h"

#include <QDBusMetaType>
#include <QDBusObjectPath>

namespace mistercast {

QDBusArgument& operator<<(QDBusArgument& argument, const PortalStream& stream)
{
    argument.beginStructure();
    argument << stream.nodeId << stream.properties;
    argument.endStructure();

    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, PortalStream& stream)
{
    argument.beginStructure();
    argument >> stream.nodeId >> stream.properties;
    argument.endStructure();
    
    return argument;
}

QVariantMap portalSourceOptions(
    std::uint32_t portalVersion,
    std::uint32_t availableCursorModes)
{
    QVariantMap options;
    options.insert(QStringLiteral("types"), 3u);
    options.insert(QStringLiteral("multiple"), false);
    
    if ((availableCursorModes & 2u) != 0) {
        options.insert(QStringLiteral("cursor_mode"), 2u);
    } else if ((availableCursorModes & 1u) != 0) {
        options.insert(QStringLiteral("cursor_mode"), 1u);
    }

    if (portalVersion >= 4) {
        options.insert(QStringLiteral("persist_mode"), 1u);
    }

    return options;
}

QString portalSessionPath(const QVariantMap& results)
{
    const auto path = results.value(QStringLiteral("session_handle"));

    return path.canConvert<QDBusObjectPath>()
        ? qvariant_cast<QDBusObjectPath>(path).path()
        : path.toString();
}

std::optional<PipeWireStream> selectPortalStream(const PortalStreams& streams)
{
    if (streams.isEmpty() || streams.front().nodeId == 0) {
        return std::nullopt;
    }

    return PipeWireStream{
        streams.front().nodeId,
        streams.front().properties.value(
            QStringLiteral("pipewire-serial")).toULongLong(),
    };
}

std::optional<PipeWireStream> portalStream(const QVariantMap& results)
{
    const auto value = results.value(QStringLiteral("streams"));
    
    if (!value.canConvert<QDBusArgument>()) {
        return std::nullopt;
    }

    static const bool registered = [] {
        qDBusRegisterMetaType<PortalStream>();
        qDBusRegisterMetaType<PortalStreams>();
        return true;
    }();

    Q_UNUSED(registered);
    
    return selectPortalStream(
        qdbus_cast<PortalStreams>(qvariant_cast<QDBusArgument>(value)));
}

} // namespace mistercast

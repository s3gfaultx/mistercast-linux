#pragma once

#include <QDBusArgument>
#include <QList>
#include <QVariantMap>

#include <cstdint>
#include <optional>

namespace mistercast {

struct PortalStream {
    std::uint32_t nodeId{};
    QVariantMap properties;
};

struct PipeWireStream {
    std::uint32_t nodeId{};
    std::uint64_t serial{};
};

using PortalStreams = QList<PortalStream>;

QDBusArgument& operator<<(QDBusArgument& argument, const PortalStream& stream);
const QDBusArgument& operator>>(const QDBusArgument& argument, PortalStream& stream);

[[nodiscard]] QVariantMap portalSourceOptions(
    std::uint32_t portalVersion,
    std::uint32_t availableCursorModes);
[[nodiscard]] QString portalSessionPath(const QVariantMap& results);
[[nodiscard]] std::optional<PipeWireStream> selectPortalStream(
    const PortalStreams& streams);
[[nodiscard]] std::optional<PipeWireStream> portalStream(
    const QVariantMap& results);

} // namespace mistercast

Q_DECLARE_METATYPE(mistercast::PortalStream)

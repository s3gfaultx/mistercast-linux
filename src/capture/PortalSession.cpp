#include "capture/PortalSession.h"

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QVariantMap>

#include <fcntl.h>

#include <utility>

namespace mistercast {
namespace {

constexpr auto kPortalService = "org.freedesktop.portal.Desktop";
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kScreenCastInterface = "org.freedesktop.portal.ScreenCast";
constexpr auto kSessionInterface = "org.freedesktop.portal.Session";

} // namespace

PortalSession::PortalSession(
    QDBusConnection bus,
    QString path,
    QObject* parent)
    : QObject(parent)
    , bus_(std::move(bus))
    , path_(std::move(path))
{
    subscribed_ = bus_.connect(
        kPortalService, path_, kSessionInterface, QStringLiteral("Closed"),
        this, SLOT(onClosed()));
}

PortalSession::~PortalSession()
{
    close();
}

void PortalSession::openPipeWireRemote(
    std::uint32_t nodeId,
    std::uint64_t pipeWireSerial)
{
    if (!active_) {
        return;
    }

    auto message = QDBusMessage::createMethodCall(
        kPortalService, kPortalPath, kScreenCastInterface,
        QStringLiteral("OpenPipeWireRemote"));

    message.setArguments({
        QVariant::fromValue(QDBusObjectPath(path_)),
        QVariantMap{},
    });

    auto* watcher = new QDBusPendingCallWatcher(bus_.asyncCall(message), this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
        [this, nodeId, pipeWireSerial](QDBusPendingCallWatcher* finished) {
            remoteCallFinished(finished, nodeId, pipeWireSerial);
        });
}

void PortalSession::close()
{
    active_ = false;

    if (path_.isEmpty()) {
        return;
    }

    if (subscribed_) {
        bus_.disconnect(
            kPortalService, path_, kSessionInterface, QStringLiteral("Closed"),
            this, SLOT(onClosed()));
        subscribed_ = false;
    }

    auto closeMessage = QDBusMessage::createMethodCall(
        kPortalService, path_, kSessionInterface, QStringLiteral("Close"));

    bus_.asyncCall(closeMessage);
    path_.clear();
}

void PortalSession::onClosed()
{
    if (!active_) {
        return;
    }

    active_ = false;
    subscribed_ = false;

    path_.clear();
    
    emit closed();
}

void PortalSession::remoteCallFinished(
    QDBusPendingCallWatcher* watcher,
    std::uint32_t nodeId,
    std::uint64_t pipeWireSerial)
{
    QDBusPendingReply<QDBusUnixFileDescriptor> reply = *watcher;
    watcher->deleteLater();

    if (!active_) {
        return;
    }

    if (reply.isError() || !reply.value().isValid()) {
        emit failed(QStringLiteral("Unable to open the portal PipeWire remote: %1")
                        .arg(reply.error().message()));
        return;
    }

    const int descriptor = fcntl(
        reply.value().fileDescriptor(), F_DUPFD_CLOEXEC, 3);

    if (descriptor < 0) {
        emit failed(QStringLiteral(
            "Unable to duplicate the PipeWire file descriptor"));
        return;
    }
    
    emit streamReady(descriptor, nodeId, pipeWireSerial);
}

} // namespace mistercast

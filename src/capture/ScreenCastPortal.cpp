#include "capture/ScreenCastPortal.h"

#include "capture/PortalRequest.h"
#include "capture/PortalResponse.h"
#include "capture/PortalSession.h"

#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>

namespace mistercast {
namespace {

constexpr auto kPortalService = "org.freedesktop.portal.Desktop";
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kScreenCastInterface = "org.freedesktop.portal.ScreenCast";

} // namespace

ScreenCastPortal::ScreenCastPortal(QObject* parent)
    : QObject(parent)
    , bus_(QDBusConnection::sessionBus())
{
    QDBusInterface properties(
        kPortalService, kPortalPath, QStringLiteral("org.freedesktop.DBus.Properties"), bus_);

    properties.setTimeout(1000);

    const QDBusReply<QVariantMap> reply = properties.call(
        QStringLiteral("GetAll"), QString::fromLatin1(kScreenCastInterface));
    
        if (reply.isValid()) {
        portalVersion_ = reply.value().value(QStringLiteral("version")).toUInt();
        availableCursorModes_ =
            reply.value().value(QStringLiteral("AvailableCursorModes")).toUInt();
    }
}

ScreenCastPortal::~ScreenCastPortal()
{
    stop();
}

void ScreenCastPortal::selectSource()
{
    if (!bus_.isConnected()) {
        fail(QStringLiteral("The D-Bus session bus is unavailable"));
        return;
    }

    stop();

    createSession();
}

void ScreenCastPortal::stop()
{
    releaseRequest();
    releaseSession();

    stage_ = Stage::Idle;
}

void ScreenCastPortal::createSession()
{
    stage_ = Stage::Creating;
    QVariantMap options;
    options.insert(QStringLiteral("session_handle_token"), nextToken(QStringLiteral("session")));
    beginRequest(QStringLiteral("CreateSession"), {}, std::move(options));
}

void ScreenCastPortal::selectSources()
{
    stage_ = Stage::Selecting;
    beginRequest(
        QStringLiteral("SelectSources"),
        {QVariant::fromValue(QDBusObjectPath(session_->path()))},
        portalSourceOptions(portalVersion_, availableCursorModes_));
}

void ScreenCastPortal::startSession()
{
    stage_ = Stage::Starting;
    beginRequest(
        QStringLiteral("Start"),
        {QVariant::fromValue(QDBusObjectPath(session_->path())), QString()},
        {});
}

void ScreenCastPortal::beginRequest(
    const QString& method,
    const QVariantList& arguments,
    QVariantMap options)
{
    releaseRequest();
    request_ = new PortalRequest(bus_, this);

    connect(request_, &PortalRequest::responded,
        this, &ScreenCastPortal::onRequestResponse);
    connect(request_, &PortalRequest::failed, this,
        [this, request = request_](const QString& message) {
            if (request == request_) {
                fail(message);
            }
        });

    if (!request_->start(
            method,
            arguments,
            std::move(options),
            nextToken(QStringLiteral("request")))) {
        fail(QStringLiteral("Unable to subscribe to the portal response"));
    }
}

void ScreenCastPortal::onRequestResponse(std::uint32_t response, const QVariantMap& results)
{
    if (sender() != request_) {
        return;
    }

    releaseRequest();

    if (response != 0) {
        const bool userCancelled = response == 1;

        stop();
        
        if (userCancelled) {
            emit cancelled();
        } else {
            emit failed(QStringLiteral("The ScreenCast portal rejected the request"));
        }

        return;
    }

    switch (stage_) {
    case Stage::Creating: {
        const QString sessionPath = portalSessionPath(results);

        if (sessionPath.isEmpty()) {
            fail(QStringLiteral("The portal returned no screencast session"));
            return;
        }

        session_ = new PortalSession(bus_, sessionPath, this);

        if (!session_->subscribed()) {
            fail(QStringLiteral("Unable to subscribe to the screencast session"));
            return;
        }

        connect(session_, &PortalSession::closed,
            this, &ScreenCastPortal::onSessionClosed);
        connect(session_, &PortalSession::failed, this,
            [this, session = session_](const QString& message) {
                if (session == session_) {
                    fail(message);
                }
            });
        connect(session_, &PortalSession::streamReady, this,
            [this, session = session_](
                int descriptor,
                std::uint32_t nodeId,
                std::uint64_t pipeWireSerial) {
                if (session != session_) {
                    return;
                }
                stage_ = Stage::Ready;
                emit streamReady(descriptor, nodeId, pipeWireSerial);
            });

        selectSources();

        break;
    }
    case Stage::Selecting:
        startSession();
        break;

    case Stage::Starting: {
        const auto streamsValue = results.value(QStringLiteral("streams"));
        
        if (!streamsValue.canConvert<QDBusArgument>()) {
            fail(QStringLiteral("The portal returned no PipeWire streams"));
            return;
        }

        const auto stream = portalStream(results);

        if (!stream.has_value()) {
            fail(QStringLiteral("The portal returned an invalid PipeWire node"));
            return;
        }

        stage_ = Stage::OpeningRemote;
        session_->openPipeWireRemote(stream->nodeId, stream->serial);

        break;
    }
    case Stage::Idle:
    case Stage::OpeningRemote:
    case Stage::Ready:
        break;
    }
}

void ScreenCastPortal::onSessionClosed()
{
    if (stage_ == Stage::Idle || sender() != session_) {
        return;
    }

    releaseRequest();
    releaseSession();

    stage_ = Stage::Idle;

    emit failed(QStringLiteral("The compositor closed the desktop capture session"));
}

void ScreenCastPortal::releaseRequest()
{
    if (request_ == nullptr) {
        return;
    }

    request_->close();
    request_->deleteLater();

    request_ = nullptr;
}

void ScreenCastPortal::releaseSession()
{
    if (session_ == nullptr) {
        return;
    }

    session_->close();
    session_->deleteLater();

    session_ = nullptr;
}

void ScreenCastPortal::fail(const QString& message)
{
    stop();

    emit failed(message);
}

QString ScreenCastPortal::nextToken(const QString& prefix)
{
    ++tokenCounter_;
    
    return QStringLiteral("mistercast_%1_%2").arg(prefix).arg(tokenCounter_);
}

} // namespace mistercast

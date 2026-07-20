#include "capture/PortalRequest.h"

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include <utility>

namespace mistercast {
namespace {

constexpr auto kPortalService = "org.freedesktop.portal.Desktop";
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kScreenCastInterface = "org.freedesktop.portal.ScreenCast";
constexpr auto kRequestInterface = "org.freedesktop.portal.Request";

} // namespace

PortalRequest::PortalRequest(QDBusConnection bus, QObject* parent)
    : QObject(parent)
    , bus_(std::move(bus))
{
}

PortalRequest::~PortalRequest()
{
    close();
}

bool PortalRequest::start(
    const QString& method,
    const QVariantList& arguments,
    QVariantMap options,
    const QString& handleToken)
{
    close();

    active_ = true;

    options.insert(QStringLiteral("handle_token"), handleToken);
    
    if (!subscribe(predictedPath(handleToken))) {
        active_ = false;
        return false;
    }

    auto message = QDBusMessage::createMethodCall(
        kPortalService, kPortalPath, kScreenCastInterface, method);

    QVariantList completeArguments = arguments;
    completeArguments.push_back(options);

    message.setArguments(completeArguments);

    auto* watcher = new QDBusPendingCallWatcher(bus_.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
        this, &PortalRequest::callFinished);

    return true;
}

void PortalRequest::close()
{
    active_ = false;

    if (path_.isEmpty()) {
        return;
    }

    auto closeRequest = QDBusMessage::createMethodCall(
        kPortalService, path_, kRequestInterface, QStringLiteral("Close"));

    bus_.asyncCall(closeRequest);

    unsubscribe();
}

void PortalRequest::onResponse(
    std::uint32_t response,
    const QVariantMap& results)
{
    if (!active_) {
        return;
    }

    unsubscribe();

    active_ = false;

    emit responded(response, results);
}

void PortalRequest::callFinished(QDBusPendingCallWatcher* watcher)
{
    QDBusPendingReply<QDBusObjectPath> reply = *watcher;

    watcher->deleteLater();

    if (!active_) {
        return;
    }

    if (reply.isError()) {
        emit failed(QStringLiteral("ScreenCast portal call failed: %1")
                        .arg(reply.error().message()));
        return;
    }

    const QString actualPath = reply.value().path();
    if (!actualPath.isEmpty() && actualPath != path_) {
        unsubscribe();
        if (!subscribe(actualPath)) {
            active_ = false;
            emit failed(QStringLiteral(
                "Unable to subscribe to the returned portal request"));
        }
    }
}

bool PortalRequest::subscribe(const QString& path)
{
    path_ = path;

    if (bus_.connect(
            kPortalService, path_, kRequestInterface, QStringLiteral("Response"),
            this, SLOT(onResponse(uint,QVariantMap)))) {
        return true;
    }

    path_.clear();

    return false;
}

void PortalRequest::unsubscribe()
{
    if (path_.isEmpty()) {
        return;
    }

    bus_.disconnect(
        kPortalService, path_, kRequestInterface, QStringLiteral("Response"),
        this, SLOT(onResponse(uint,QVariantMap)));

    path_.clear();
}

QString PortalRequest::predictedPath(const QString& token) const
{
    QString sender = bus_.baseService();
    
    sender.remove(QLatin1Char(':'));
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
        .arg(sender, token);
}

} // namespace mistercast

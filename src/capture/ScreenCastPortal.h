#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QVariantMap>

#include <cstdint>

namespace mistercast {

class PortalRequest;
class PortalSession;

class ScreenCastPortal final : public QObject {
    Q_OBJECT

public:
    explicit ScreenCastPortal(QObject* parent = nullptr);
    ~ScreenCastPortal() override;

    void selectSource();
    void stop();

signals:
    void streamReady(
        int pipeWireFileDescriptor,
        std::uint32_t nodeId,
        std::uint64_t pipeWireSerial);
    void cancelled();
    void failed(const QString& message);

private slots:
    void onRequestResponse(std::uint32_t response, const QVariantMap& results);
    void onSessionClosed();

private:
    enum class Stage {
        Idle,
        Creating,
        Selecting,
        Starting,
        OpeningRemote,
        Ready,
    };

    void createSession();
    void selectSources();
    void startSession();
    void beginRequest(const QString& method, const QVariantList& arguments, QVariantMap options);
    void releaseRequest();
    void releaseSession();
    void fail(const QString& message);
    QString nextToken(const QString& prefix);

    QDBusConnection bus_;
    Stage stage_{Stage::Idle};
    PortalRequest* request_{};
    PortalSession* session_{};
    std::uint64_t tokenCounter_{};
    std::uint32_t portalVersion_{};
    std::uint32_t availableCursorModes_{};
};

} // namespace mistercast

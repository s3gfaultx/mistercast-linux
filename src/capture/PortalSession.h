#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>

#include <cstdint>

class QDBusPendingCallWatcher;

namespace mistercast {

class PortalSession final : public QObject {
    Q_OBJECT

public:
    PortalSession(
        QDBusConnection bus,
        QString path,
        QObject* parent = nullptr);
    ~PortalSession() override;

    [[nodiscard]] bool subscribed() const { return subscribed_; }
    [[nodiscard]] const QString& path() const { return path_; }
    void openPipeWireRemote(std::uint32_t nodeId, std::uint64_t pipeWireSerial);
    void close();

signals:
    void streamReady(
        int pipeWireFileDescriptor,
        std::uint32_t nodeId,
        std::uint64_t pipeWireSerial);
    void closed();
    void failed(const QString& message);

private slots:
    void onClosed();

private:
    void remoteCallFinished(
        QDBusPendingCallWatcher* watcher,
        std::uint32_t nodeId,
        std::uint64_t pipeWireSerial);

    QDBusConnection bus_;
    QString path_;
    bool active_{true};
    bool subscribed_{};
};

} // namespace mistercast

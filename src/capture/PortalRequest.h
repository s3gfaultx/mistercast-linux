#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QVariantMap>

#include <cstdint>

class QDBusPendingCallWatcher;

namespace mistercast {

class PortalRequest final : public QObject {
    Q_OBJECT

public:
    explicit PortalRequest(QDBusConnection bus, QObject* parent = nullptr);
    ~PortalRequest() override;

    bool start(
        const QString& method,
        const QVariantList& arguments,
        QVariantMap options,
        const QString& handleToken);
    void close();

signals:
    void responded(std::uint32_t response, const QVariantMap& results);
    void failed(const QString& message);

private slots:
    void onResponse(std::uint32_t response, const QVariantMap& results);

private:
    void callFinished(QDBusPendingCallWatcher* watcher);
    bool subscribe(const QString& path);
    void unsubscribe();
    [[nodiscard]] QString predictedPath(const QString& token) const;

    QDBusConnection bus_;
    QString path_;
    bool active_{};
};

} // namespace mistercast

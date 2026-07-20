#include "stream/StreamController.h"

#include "stream/StreamSession.h"

namespace mistercast {

StreamController::StreamController(
    FrameMailbox& mailbox,
    PipeWireAudioCapture& audioCapture,
    QObject* parent)
    : QObject(parent)
    , mailbox_(mailbox)
    , audioCapture_(audioCapture)
{
    qRegisterMetaType<VideoDiagnostics>();
    qRegisterMetaType<AudioDiagnostics>();
    qRegisterMetaType<FpgaDiagnostics>();
}

StreamController::~StreamController()
{
    stop();
}

bool StreamController::start(StreamConfig config)
{
    if (config.host.empty() || running_.exchange(true)) {
        return false;
    }

    if (thread_ != nullptr) {
        thread_->join();
        thread_.reset();
        session_.reset();
    }

    const auto generation = ++sessionGeneration_;

    session_ = std::make_unique<StreamSession>(
        mailbox_, audioCapture_, std::move(config));

    connect(session_.get(), &StreamSession::started, this, [this, generation] {
        if (generation == sessionGeneration_) {
            emit started();
        }
    }, Qt::QueuedConnection);
    connect(session_.get(), &StreamSession::failed, this,
        [this, generation](const QString& message) {
            if (generation == sessionGeneration_) {
                emit failed(message);
            }
        }, Qt::QueuedConnection);
    connect(session_.get(), &StreamSession::warning, this,
        [this, generation](const QString& message) {
            if (generation == sessionGeneration_) {
                emit warning(message);
            }
        }, Qt::QueuedConnection);
    connect(session_.get(), &StreamSession::videoDiagnostics, this,
        [this, generation](const VideoDiagnostics& snapshot) {
            if (generation == sessionGeneration_) {
                emit videoDiagnostics(snapshot);
            }
        }, Qt::QueuedConnection);
    connect(session_.get(), &StreamSession::audioDiagnostics, this,
        [this, generation](const AudioDiagnostics& snapshot) {
            if (generation == sessionGeneration_) {
                emit audioDiagnostics(snapshot);
            }
        }, Qt::QueuedConnection);
    connect(session_.get(), &StreamSession::fpgaDiagnostics, this,
        [this, generation](const FpgaDiagnostics& snapshot) {
            if (generation == sessionGeneration_) {
                emit fpgaDiagnostics(snapshot);
            }
        }, Qt::QueuedConnection);
    connect(session_.get(), &StreamSession::stopped, this, [this, generation] {
        if (generation == sessionGeneration_) {
            running_ = false;
            emit stopped();
        }
    }, Qt::QueuedConnection);

    auto* session = session_.get();

    thread_ = std::make_unique<std::jthread>(
        [session](std::stop_token stopToken) {
            session->run(stopToken);
        });

    return true;
}

void StreamController::stop()
{
    ++sessionGeneration_;

    if (thread_ != nullptr) {
        thread_->request_stop();
        
        thread_->join();

        thread_.reset();
        session_.reset();
    }
    
    running_ = false;
}

} // namespace mistercast

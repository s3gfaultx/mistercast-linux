#include "ui/MainWindow.h"

#include "core/ModelineCatalog.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <filesystem>

namespace mistercast {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , streaming_(this)
{
    setWindowTitle(QStringLiteral("MiSTerCast"));
    resize(660, 620);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    auto* connectionGroup = new QGroupBox(QStringLiteral("Connection"), central);
    auto* form = new QFormLayout(connectionGroup);
    hostEdit_ = new QLineEdit(connectionGroup);
    hostEdit_->setPlaceholderText(QStringLiteral("192.168.1.50"));
    QSettings settings;
    hostEdit_->setText(settings.value(QStringLiteral("misterHost"), QStringLiteral("192.168.1.50")).toString());
    form->addRow(QStringLiteral("MiSTer address"), hostEdit_);
    audioCheck_ = new QCheckBox(QStringLiteral("Capture default output audio (48 kHz stereo)"), connectionGroup);
    audioCheck_->setChecked(settings.value(QStringLiteral("audio"), true).toBool());
    form->addRow(QString(), audioCheck_);
    diagnosticsCheck_ = new QCheckBox(QStringLiteral("Show diagnostics"), connectionGroup);
    diagnosticsCheck_->setChecked(settings.value(QStringLiteral("diagnostics"), false).toBool());
    form->addRow(QString(), diagnosticsCheck_);
    layout->addWidget(connectionGroup);

    auto* modeGroup = new QGroupBox(QStringLiteral("CRT Output"), central);
    auto* modeLayout = new QVBoxLayout(modeGroup);
    auto* modeRow = new QHBoxLayout;
    modelineSelect_ = new QComboBox(modeGroup);
    editModelinesButton_ = new QPushButton(QStringLiteral("Edit File"), modeGroup);
    reloadModelinesButton_ = new QPushButton(QStringLiteral("Reload"), modeGroup);
    modeRow->addWidget(modelineSelect_, 1);
    modeRow->addWidget(editModelinesButton_);
    modeRow->addWidget(reloadModelinesButton_);
    modeLayout->addLayout(modeRow);
    timingDetails_ = new QLabel(modeGroup);
    timingDetails_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    timingDetails_->setWordWrap(true);
    modeLayout->addWidget(timingDetails_);
    layout->addWidget(modeGroup);

    auto* framingGroup = new QGroupBox(QStringLiteral("Framing"), central);
    auto* framingForm = new QFormLayout(framingGroup);
    horizontalAlignment_ = new QComboBox(framingGroup);
    horizontalAlignment_->addItems({QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right")});
    horizontalAlignment_->setCurrentIndex(settings.value(QStringLiteral("horizontalAlignment"), 1).toInt());
    verticalAlignment_ = new QComboBox(framingGroup);
    verticalAlignment_->addItems({QStringLiteral("Top"), QStringLiteral("Center"), QStringLiteral("Bottom")});
    verticalAlignment_->setCurrentIndex(settings.value(QStringLiteral("verticalAlignment"), 1).toInt());
    rotation_ = new QComboBox(framingGroup);
    rotation_->addItems({QStringLiteral("None"), QStringLiteral("90 degrees clockwise"),
                         QStringLiteral("90 degrees counter-clockwise"), QStringLiteral("180 degrees")});
    rotation_->setCurrentIndex(settings.value(QStringLiteral("rotation"), 0).toInt());
    horizontalOffset_ = new QSpinBox(framingGroup);
    horizontalOffset_->setRange(-4096, 4096);
    horizontalOffset_->setValue(settings.value(QStringLiteral("horizontalOffset"), 0).toInt());
    verticalOffset_ = new QSpinBox(framingGroup);
    verticalOffset_->setRange(-4096, 4096);
    verticalOffset_->setValue(settings.value(QStringLiteral("verticalOffset"), 0).toInt());
    framingForm->addRow(QStringLiteral("Horizontal"), horizontalAlignment_);
    framingForm->addRow(QStringLiteral("Vertical"), verticalAlignment_);
    framingForm->addRow(QStringLiteral("Rotation"), rotation_);
    framingForm->addRow(QStringLiteral("Horizontal offset"), horizontalOffset_);
    framingForm->addRow(QStringLiteral("Vertical offset"), verticalOffset_);
    layout->addWidget(framingGroup);

    auto* actions = new QHBoxLayout;
    sourceButton_ = new QPushButton(QStringLiteral("Select Desktop Source"), central);
    startButton_ = new QPushButton(QStringLiteral("Start Streaming"), central);
    stopButton_ = new QPushButton(QStringLiteral("Stop"), central);
    actions->addWidget(sourceButton_);
    actions->addWidget(startButton_);
    actions->addWidget(stopButton_);
    layout->addLayout(actions);

    sourceStatus_ = new QLabel(QStringLiteral("Source: not selected"), central);
    streamStatus_ = new QLabel(QStringLiteral("Stream: idle"), central);
    latencyStatus_ = new QLabel(QStringLiteral("Latency metrics: waiting"), central);
    audioStatus_ = new QLabel(QStringLiteral("Audio diagnostics: waiting"), central);
    audioStatus_->setWordWrap(true);
    fpgaStatus_ = new QLabel(QStringLiteral("FPGA delivery: waiting"), central);
    fpgaStatus_->setWordWrap(true);
    layout->addWidget(sourceStatus_);
    layout->addWidget(streamStatus_);
    layout->addWidget(latencyStatus_);
    layout->addWidget(audioStatus_);
    layout->addWidget(fpgaStatus_);

    log_ = new QPlainTextEdit(central);
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(200);
    layout->addWidget(log_, 1);
    setCentralWidget(central);

    connect(sourceButton_, &QPushButton::clicked, this, &MainWindow::selectSource);
    connect(startButton_, &QPushButton::clicked, this, &MainWindow::startStreaming);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopStreaming);
    connect(diagnosticsCheck_, &QCheckBox::toggled, this, [this](bool enabled) {
        QSettings().setValue(QStringLiteral("diagnostics"), enabled);
        updateDiagnosticsVisibility();
    });
    connect(modelineSelect_, &QComboBox::currentIndexChanged, this,
        [this] { applySelectedModeline(true); });
    connect(editModelinesButton_, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(modelinesPath_));
    });
    connect(reloadModelinesButton_, &QPushButton::clicked, this,
        [this] { reloadModelines(true); });
    connect(horizontalAlignment_, &QComboBox::currentIndexChanged, this, &MainWindow::updateCropSettings);
    connect(verticalAlignment_, &QComboBox::currentIndexChanged, this, &MainWindow::updateCropSettings);
    connect(rotation_, &QComboBox::currentIndexChanged, this, &MainWindow::updateCropSettings);
    connect(horizontalOffset_, &QSpinBox::valueChanged, this, &MainWindow::updateCropSettings);
    connect(verticalOffset_, &QSpinBox::valueChanged, this, &MainWindow::updateCropSettings);

    connect(&streaming_, &StreamingCoordinator::sourceSelectionStarted, this,
        [this] {
            sourceStatus_->setText(QStringLiteral("Source: opening portal selector..."));
            updateControls();
        });
    connect(&streaming_, &StreamingCoordinator::sourceSelectionCancelled, this, [this] {
        sourceStatus_->setText(QStringLiteral("Source: not selected"));
        appendStatus(QStringLiteral("Desktop source selection cancelled"));
        updateControls();
    });
    connect(&streaming_, &StreamingCoordinator::sourceStarted, this,
        [this](std::uint32_t width, std::uint32_t height, const QString& format) {
            sourceStatus_->setText(QStringLiteral("Source: %1 x %2, %3")
                                       .arg(width).arg(height).arg(format));
            appendStatus(QStringLiteral("Direct PipeWire video capture started"));
            updateControls();
        });
    connect(&streaming_, &StreamingCoordinator::streamConnecting, this,
        [this](bool audioEnabled) {
            streamStatus_->setText(QStringLiteral("Stream: connecting..."));
            latencyStatus_->setText(QStringLiteral("Latency metrics: collecting..."));
            audioStatus_->setText(audioEnabled
                ? QStringLiteral("Audio diagnostics: collecting...")
                : QStringLiteral("Audio: disabled"));
            fpgaStatus_->setText(QStringLiteral("FPGA delivery: collecting..."));
        });
    connect(&streaming_, &StreamingCoordinator::streamStarted, this, [this] {
        streamStatus_->setText(QStringLiteral("Stream: connected"));
        appendStatus(QStringLiteral("Groovy_MiSTer acknowledged the stream"));
        updateControls();
    });
    connect(&streaming_, &StreamingCoordinator::streamStopped, this, [this] {
        streamStatus_->setText(QStringLiteral("Stream: idle"));
        updateControls();
    });
    connect(&streaming_, &StreamingCoordinator::stateChanged,
        this, &MainWindow::updateControls);
    connect(&streaming_, &StreamingCoordinator::failed, this,
        [this](const QString& message) { appendStatus(message, true); });
    connect(&streaming_, &StreamingCoordinator::warning, this,
        [this](const QString& message) { appendStatus(message); });
    connect(&streaming_, &StreamingCoordinator::videoDiagnostics, this,
        [this](const VideoDiagnostics& snapshot) {
            if (diagnosticsCheck_->isChecked()) {
                qInfo().nospace()
                    << "latency frame=" << snapshot.frameNumber
                    << " captured=" << snapshot.capturedFrames
                    << " reused=" << snapshot.reusedFrames
                    << " dequeue_age_ms_p50=" << snapshot.medianDequeueAgeMs
                    << " dequeue_age_ms_p95=" << snapshot.p95DequeueAgeMs
                    << " dequeue_age_ms_max=" << snapshot.maximumDequeueAgeMs
                    << " capture_process_us=" << snapshot.averageCaptureProcessingUs
                    << " ready_wait_us=" << snapshot.averageReadyWaitUs
                    << " lz4_us=" << snapshot.averageCompressionUs
                    << " capacity_wait_us=" << snapshot.averageCapacityWaitUs
                    << " submit_us=" << snapshot.averageSubmissionUs
                    << " payload_bytes=" << snapshot.averagePayloadBytes
                    << " reserve_lines=" << snapshot.deliveryReserveLines
                    << " drops=" << snapshot.intervalDroppedFrames;
            }
            latencyStatus_->setText(
                QStringLiteral("Frame %1  |  captured/reused %2/%3  |  new-frame dequeue age p50/p95/max %4/%5/%6 ms  |  capture process/ready %7/%8 us  |  LZ4/queue/submit %9/%10/%11 us  |  %12 B  |  reserve %13 lines  |  interval drops %14")
                    .arg(snapshot.frameNumber)
                    .arg(snapshot.capturedFrames)
                    .arg(snapshot.reusedFrames)
                    .arg(snapshot.medianDequeueAgeMs, 0, 'f', 2)
                    .arg(snapshot.p95DequeueAgeMs, 0, 'f', 2)
                    .arg(snapshot.maximumDequeueAgeMs, 0, 'f', 2)
                    .arg(snapshot.averageCaptureProcessingUs, 0, 'f', 1)
                    .arg(snapshot.averageReadyWaitUs, 0, 'f', 1)
                    .arg(snapshot.averageCompressionUs, 0, 'f', 1)
                    .arg(snapshot.averageCapacityWaitUs, 0, 'f', 1)
                    .arg(snapshot.averageSubmissionUs, 0, 'f', 1)
                    .arg(snapshot.averagePayloadBytes)
                    .arg(snapshot.deliveryReserveLines)
                    .arg(snapshot.intervalDroppedFrames));
        });
    connect(&streaming_, &StreamingCoordinator::audioDiagnostics, this,
        [this](const AudioDiagnostics& snapshot) {
            if (!audioCheck_->isChecked()) {
                audioStatus_->setText(QStringLiteral("Audio: disabled"));
                return;
            }
            if (diagnosticsCheck_->isChecked()) {
                qInfo().nospace()
                    << "audio requested=" << snapshot.requestedPackets
                    << " sent=" << snapshot.sentPackets
                    << " core_disabled=" << snapshot.coreDisabledPackets
                    << " underflow_packets=" << snapshot.underflowPackets
                    << " underflow_frames=" << snapshot.underflowFrames
                    << " stale_frames=" << snapshot.staleFramesDiscarded
                    << " overflow_frames=" << snapshot.overflowFrames
                    << " buffered_frames=" << snapshot.bufferedFrames
                    << " reserve_frames=" << snapshot.jitterReserveFrames
                    << " queue_peak_bytes=" << snapshot.socketQueueHighWaterBytes
                    << " pacing_overruns=" << snapshot.pacingOverruns
                    << " pacing_late_max_ms=" << snapshot.maximumPacingLatenessMs;
            }
            audioStatus_->setText(QStringLiteral(
                "Audio sent/requested %1/%2  |  core disabled %3  |  "
                "underflow packets/frames %4/%5  |  stale/overflow %6/%7 frames  |  "
                "buffered/reserve %8/%9 frames  |  UDP peak %10 KiB  |  pacing overruns %11, max %12 ms")
                .arg(snapshot.sentPackets)
                .arg(snapshot.requestedPackets)
                .arg(snapshot.coreDisabledPackets)
                .arg(snapshot.underflowPackets)
                .arg(snapshot.underflowFrames)
                .arg(snapshot.staleFramesDiscarded)
                .arg(snapshot.overflowFrames)
                .arg(snapshot.bufferedFrames)
                .arg(snapshot.jitterReserveFrames)
                .arg(snapshot.socketQueueHighWaterBytes / 1024.0, 0, 'f', 1)
                .arg(snapshot.pacingOverruns)
                .arg(snapshot.maximumPacingLatenessMs, 0, 'f', 2));
        });
    connect(&streaming_, &StreamingCoordinator::fpgaDiagnostics, this,
        [this](const FpgaDiagnostics& snapshot) {
            if (diagnosticsCheck_->isChecked()) {
                qInfo().nospace()
                    << "fpga samples=" << snapshot.statusSamples
                    << " fallback=" << snapshot.fallbackSamples
                    << " unsynced=" << snapshot.unsyncedSamples
                    << " queue_empty=" << snapshot.queueEmptySamples;
            }
            fpgaStatus_->setText(QStringLiteral(
                "FPGA samples %1  |  fallback %2  |  unsynced %3  |  queue empty %4")
                .arg(snapshot.statusSamples)
                .arg(snapshot.fallbackSamples)
                .arg(snapshot.unsyncedSamples)
                .arg(snapshot.queueEmptySamples));
        });

    reloadModelines(false);
    updateCropSettings();
    updateDiagnosticsVisibility();
    updateControls();
}

MainWindow::~MainWindow()
{
    streaming_.shutdown();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    QSettings settings;
    settings.setValue(QStringLiteral("misterHost"), hostEdit_->text().trimmed());
    settings.setValue(QStringLiteral("audio"), audioCheck_->isChecked());
    settings.setValue(QStringLiteral("horizontalAlignment"), horizontalAlignment_->currentIndex());
    settings.setValue(QStringLiteral("verticalAlignment"), verticalAlignment_->currentIndex());
    settings.setValue(QStringLiteral("rotation"), rotation_->currentIndex());
    settings.setValue(QStringLiteral("horizontalOffset"), horizontalOffset_->value());
    settings.setValue(QStringLiteral("verticalOffset"), verticalOffset_->value());
    streaming_.shutdown();
    event->accept();
}

void MainWindow::selectSource()
{
    streaming_.selectSource();
}

void MainWindow::startStreaming()
{
    if (!streaming_.sourceReady() || streaming_.streaming()) {
        return;
    }

    streaming_.startStreaming(
        hostEdit_->text().trimmed().toStdString(),
        audioCheck_->isChecked());

    updateControls();
}

void MainWindow::stopStreaming()
{
    streaming_.stopStreaming();

    updateControls();
}

void MainWindow::appendStatus(const QString& message, bool error)
{
    log_->appendPlainText((error ? QStringLiteral("Error: ") : QString()) + message);
    
    if (error) {
        qWarning().noquote() << message;
    } else {
        qInfo().noquote() << message;
    }
}

void MainWindow::updateControls()
{
    const bool streaming = streaming_.streaming();
    sourceButton_->setEnabled(!streaming);
    startButton_->setEnabled(
        streaming_.sourceReady() && !streaming &&
        !hostEdit_->text().trimmed().isEmpty());
    stopButton_->setEnabled(streaming);
    hostEdit_->setEnabled(!streaming);
    audioCheck_->setEnabled(!streaming);
}

void MainWindow::updateDiagnosticsVisibility()
{
    const bool visible = diagnosticsCheck_->isChecked();

    latencyStatus_->setVisible(visible);
    audioStatus_->setVisible(visible);
    fpgaStatus_->setVisible(visible);
}

void MainWindow::reloadModelines(bool restartStream)
{
    const bool wasStreaming = restartStream && streaming_.streaming();
    
    if (wasStreaming) {
        stopStreaming();
    }

    const QString configDirectory =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);

    QDir().mkpath(configDirectory);
    modelinesPath_ = QDir(configDirectory).filePath(QStringLiteral("modelines.dat"));
    
    if (!QFileInfo::exists(modelinesPath_) &&
        !QFile::copy(QStringLiteral(":/mistercast/resources/modelines.dat"), modelinesPath_)) {
        appendStatus(QStringLiteral("Unable to create editable modelines file at %1")
                         .arg(modelinesPath_), true);
    }

    QFile::setPermissions(
        modelinesPath_, QFile::permissions(modelinesPath_) | QFileDevice::WriteUser);

    const auto encodedPath = QFile::encodeName(modelinesPath_);
    auto result = ModelineCatalog::load(
        std::filesystem::path(encodedPath.constData(), encodedPath.constData() + encodedPath.size()));
    
    for (const auto& warning : result.warnings) {
        appendStatus(QStringLiteral("Modeline warning: %1").arg(QString::fromStdString(warning)));
    }

    if (result.modelines.empty()) {
        appendStatus(QStringLiteral("No valid modelines found; using the built-in 320x240p60 mode"), true);
        result.modelines.push_back(kDefaultModeline);
    }

    QSettings settings;
    QString selectedName = settings.value(
        QStringLiteral("modeline"), QString::fromStdString(kDefaultModeline.name)).toString();
    if (modelineSelect_->currentIndex() >= 0 && !modelines_.empty()) {
        selectedName = QString::fromStdString(selectedModeline().name);
    }

    const QSignalBlocker blocker(modelineSelect_);

    modelines_ = std::move(result.modelines);
    modelineSelect_->clear();

    int selectedIndex = 0;

    for (std::size_t i = 0; i < modelines_.size(); ++i) {
        modelineSelect_->addItem(QString::fromStdString(modelines_[i].name));
        if (QString::fromStdString(modelines_[i].name) == selectedName) {
            selectedIndex = static_cast<int>(i);
        }
    }

    modelineSelect_->setCurrentIndex(selectedIndex);

    applySelectedModeline(false);
    appendStatus(QStringLiteral("Loaded %1 CRT modes from %2")
                     .arg(modelines_.size()).arg(modelinesPath_));

    if (wasStreaming) {
        startStreaming();
    }
}

void MainWindow::applySelectedModeline(bool restartStream)
{
    if (modelines_.empty() || modelineSelect_->currentIndex() < 0) {
        return;
    }

    const bool wasStreaming = restartStream && streaming_.streaming();

    if (wasStreaming) {
        stopStreaming();
    }

    const auto& mode = selectedModeline();
    streaming_.setOutputMode(mode);
    timingDetails_->setText(QStringLiteral(
        "%1 MHz  |  H %2 %3 %4 %5  |  V %6 %7 %8 %9  |  payload %10 x %11")
        .arg(mode.pixelClockMHz, 0, 'f', 3)
        .arg(mode.hActive).arg(mode.hBegin).arg(mode.hEnd).arg(mode.hTotal)
        .arg(mode.vActive).arg(mode.vBegin).arg(mode.vEnd).arg(mode.vTotal)
        .arg(mode.hActive).arg(mode.payloadHeight()));
    QSettings().setValue(QStringLiteral("modeline"), QString::fromStdString(mode.name));

    if (wasStreaming) {
        startStreaming();
    }
}

const Modeline& MainWindow::selectedModeline() const
{
    const int index = modelineSelect_ != nullptr ? modelineSelect_->currentIndex() : -1;

    if (index < 0 || static_cast<std::size_t>(index) >= modelines_.size()) {
        return kDefaultModeline;
    }
    
    return modelines_[static_cast<std::size_t>(index)];
}

void MainWindow::updateCropSettings()
{
    CropSettings settings;
    settings.horizontal = static_cast<HorizontalAlignment>(horizontalAlignment_->currentIndex());
    settings.vertical = static_cast<VerticalAlignment>(verticalAlignment_->currentIndex());
    settings.rotation = static_cast<Rotation>(rotation_->currentIndex());
    settings.offsetX = horizontalOffset_->value();
    settings.offsetY = verticalOffset_->value();
    streaming_.setCropSettings(settings);
}

} // namespace mistercast

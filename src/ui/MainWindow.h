#pragma once

#include "app/StreamingCoordinator.h"
#include "core/Modeline.h"

#include <QMainWindow>

#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QPlainTextEdit;
class QSpinBox;

namespace mistercast {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void selectSource();
    void startStreaming();
    void stopStreaming();
    void appendStatus(const QString& message, bool error = false);
    void updateControls();
    void updateCropSettings();
    void updateDiagnosticsVisibility();
    void reloadModelines(bool restartStream);
    void applySelectedModeline(bool restartStream);
    [[nodiscard]] const Modeline& selectedModeline() const;

    StreamingCoordinator streaming_;

    QLineEdit* hostEdit_{};
    QCheckBox* audioCheck_{};
    QCheckBox* diagnosticsCheck_{};
    QComboBox* modelineSelect_{};
    QLabel* timingDetails_{};
    QPushButton* editModelinesButton_{};
    QPushButton* reloadModelinesButton_{};
    QComboBox* horizontalAlignment_{};
    QComboBox* verticalAlignment_{};
    QComboBox* rotation_{};
    QSpinBox* horizontalOffset_{};
    QSpinBox* verticalOffset_{};
    QPushButton* sourceButton_{};
    QPushButton* startButton_{};
    QPushButton* stopButton_{};
    QLabel* sourceStatus_{};
    QLabel* streamStatus_{};
    QLabel* latencyStatus_{};
    QLabel* audioStatus_{};
    QLabel* fpgaStatus_{};
    QPlainTextEdit* log_{};
    std::vector<Modeline> modelines_;
    QString modelinesPath_;
};

} // namespace mistercast

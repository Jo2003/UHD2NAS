#pragma once

/**
 * @file mainwindow.h
 * @brief Main application window for UHD2NAS.
 *
 * Provides the user interface for selecting source/target files,
 * choosing the encoder variant, monitoring encoding progress,
 * and viewing tool output logs.
 */

#include <QMainWindow>
#include <QCloseEvent>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QTextEdit>
#include <QSlider>
#include <QLabel>
#include "encoderengine.h"

/**
 * @class MainWindow
 * @brief Primary GUI window for the UHD2NAS encoding application.
 *
 * The window contains:
 * - File selection (source MKV and target output path)
 * - Encoder selection (Software/QuickSync/NVEnc/AMF)
 * - Quality slider (CRF 18-25)
 * - Step progress bar (overall workflow)
 * - Encode progress bar (current encoding percentage)
 * - Start/Abort/Quit buttons with appropriate enable/disable logic
 * - Collapsible log output area showing raw tool output
 * - Settings menu for configuring tool paths
 *
 * On startup, the application attempts to auto-detect ffmpeg, ffprobe,
 * and dovi_tool in the system PATH. If not found, the user is prompted
 * to configure paths via the Settings dialog.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    /**
     * @brief Constructs the main window and initializes all UI elements.
     * @param parent Optional parent widget.
     */
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    /** @brief Abort running processes before closing. */
    void closeEvent(QCloseEvent *event) override;

private slots:
    /** @brief Validate inputs and start the encoding pipeline. */
    void onStart();
    /** @brief Abort the running encoding process. */
    void onAbort();
    /** @brief Open the tool paths settings dialog. */
    void onSettings();
    /** @brief Show the help dialog. */
    void onHelp();
    /** @brief Save log content to a file. */
    void onSaveLog();
    /** @brief Open file dialog for source MKV selection. */
    void onBrowseSource();
    /** @brief Open file dialog for target file selection. */
    void onBrowseTarget();
    /**
     * @brief Handle encoding completion.
     * @param success True if encoding finished without errors.
     * @param message Result description for the user.
     */
    void onFinished(bool success, const QString &message);

private:
    /** @brief Load tool paths from QSettings. */
    void loadSettings();
    void saveSettings();
    void detectEncoders();
    QString findTool(const QString &name);

    QLineEdit *m_sourceEdit;       ///< Source file path input.
    QLineEdit *m_targetEdit;       ///< Target file path input.
    QComboBox *m_encoderCombo;     ///< Encoder selection dropdown.
    QSlider *m_crfSlider;          ///< Quality factor slider (18-25).
    QLabel *m_crfLabel;            ///< Current slider value display.
    QPushButton *m_startBtn;       ///< Start encoding button.
    QPushButton *m_abortBtn;       ///< Abort button (enabled during encoding).
    QPushButton *m_quitBtn;        ///< Quit application button.
    QProgressBar *m_stepProgress;  ///< Overall workflow step progress.
    QProgressBar *m_encodeProgress;///< Encoding percentage progress.
    QTextEdit *m_logView;          ///< Always-visible log output area.
    QLabel *m_stepLabel;           ///< Current step description label.

    EncoderEngine *m_engine;       ///< The encoding workflow engine.

    QString m_ffmpegPath;          ///< Configured ffmpeg path.
    QString m_ffprobePath;         ///< Configured ffprobe path.
    QString m_doviToolPath;        ///< Configured dovi_tool path.
    QString m_mkvmergePath;        ///< Configured mkvmerge path.
};

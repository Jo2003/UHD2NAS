#pragma once

/**
 * @file encoderengine.h
 * @brief Orchestrates the complete video encoding workflow.
 *
 * The EncoderEngine manages the full pipeline from source analysis to
 * final output, automatically choosing the correct workflow based on
 * the input content:
 *
 * **4K UHD with Dolby Vision (Profile 7):**
 * 1. Probe for Dolby Vision metadata
 * 2. Detect crop values (black bars)
 * 3. Extract RPU and convert to Profile 8.1
 * 4. Adjust RPU for cropped dimensions
 * 5. Encode video (HEVC 10-bit, video-only)
 * 6. Inject RPU into encoded stream
 * 7. Final mux with original audio and subtitles
 *
 * **4K UHD without Dolby Vision / Full HD (SDR):**
 * 1. Detect crop values
 * 2. Encode directly to output (with audio/subtitles copied)
 *
 * All commands use configurable templates (TemplateManager) and
 * cross-platform process execution (ProcessRunner).
 */

#include <QObject>
#include <QProcess>
#include "templatemanager.h"
#include "cropdetector.h"
#include "doviprocessor.h"

class ProcessRunner;

/**
 * @class EncoderEngine
 * @brief Main encoding orchestrator with progress reporting.
 *
 * Provides a high-level interface for the UI: set input/output files,
 * choose encoder, and call start(). Progress is reported via signals
 * for both individual step progress and encoding percentage.
 *
 * The engine automatically:
 * - Detects whether the source is 4K or Full HD (based on crop dimensions)
 * - Probes for Dolby Vision Profile 7 metadata
 * - Selects appropriate templates (HDR vs SDR, with/without DV)
 * - Manages temporary files for intermediate processing steps
 * - Cleans up temp files on completion or abort
 */
class EncoderEngine : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Constructs the EncoderEngine.
     * @param parent Optional parent QObject.
     */
    explicit EncoderEngine(QObject *parent = nullptr);
    ~EncoderEngine() override;

    /**
     * @brief Set the source MKV file path.
     * @param path Absolute path to the input file.
     */
    void setInputFile(const QString &path) { m_inputFile = path; }

    /**
     * @brief Set the target output file path.
     * @param path Absolute path for the encoded output.
     */
    void setOutputFile(const QString &path) { m_outputFile = path; }

    /**
     * @brief Select the hardware encoder to use.
     * @param enc One of Software, QuickSync, NVEnc, or AMF.
     */
    void setEncoder(TemplateManager::Encoder enc) { m_encoder = enc; }

    /**
     * @brief Set the quality factor (CRF / global_quality / cq).
     * @param crf Quality value (18 = best quality, 25 = smaller files).
     */
    void setCrf(int crf) { m_crf = crf; }

    /**
     * @brief Configure paths to external tools.
     * @param ffmpeg Path to ffmpeg executable.
     * @param ffprobe Path to ffprobe executable.
     * @param doviTool Path to dovi_tool executable.
     */
    void setToolPaths(const QString &ffmpeg, const QString &ffprobe, const QString &doviTool, const QString &mkvmerge);

    /**
     * @brief Start the encoding workflow.
     *
     * This initiates the asynchronous pipeline. Progress and completion
     * are reported via signals.
     */
    void start();

    /**
     * @brief Access the template manager for editing templates.
     * @return Pointer to the internal TemplateManager.
     */
    TemplateManager* templateManager() { return &m_templates; }

    void abort();

    /**
     * @brief Continue the pipeline after user confirms/edits crop values.
     * @param info The (possibly modified) crop info to use.
     */
    void confirmCrop(const CropInfo &info);

    /**
     * @brief Returns the total number of steps in the current workflow.
     * @return Step count (2 for simple encode, 6 for full DV pipeline).
     */
    int totalSteps() const;

signals:
    /**
     * @brief Reports progress through workflow steps.
     * @param current Current step number (1-based).
     * @param total Total number of steps.
     * @param description Human-readable description of the current step.
     */
    void stepProgress(int current, int total, const QString &description);

    /**
     * @brief Reports encoding progress as percentage (0-100).
     *
     * Parsed from ffmpeg's `time=` output relative to total duration.
     *
     * @param percent Completion percentage of the encode step.
     */
    void encodeProgress(double percent);

    /**
     * @brief Raw log output from all tools for display in the UI.
     * @param line Output text (may contain partial lines or ANSI codes).
     */
    void logOutput(const QString &line);

    /**
     * @brief Emitted when the entire workflow completes or fails.
     * @param success True if all steps completed without errors.
     * @param message Human-readable result description.
     */
    void finished(bool success, const QString &message);

    /**
     * @brief Emitted after crop detection so the UI can confirm/edit values.
     * @param info Detected crop values.
     */
    void cropReady(const CropInfo &info);

private slots:
    void onCropDetected(bool success);
    void onDoviStepDone(bool success);
    void onEncodeFinished(int exitCode, QProcess::ExitStatus status);

private:
    /** @brief Internal workflow step identifiers. */
    enum Step { CropDetect, ExtractRpu, Encode, InjectDovi, MuxFinal, Done };

    void advanceStep();
    void startEncode();
    void startDoviExtract();
    void startDoviInject();
    void startFinalMux();
    void probeForDovi();
    void probeCodec();
    void logCmd(const QString &cmd) const;
    QMap<QString, QString> buildVars() const;

    QString m_inputFile;    ///< Source MKV path.
    QString m_outputFile;   ///< Target output path.
    TemplateManager::Encoder m_encoder = TemplateManager::Software; ///< Selected encoder.
    int m_crf = 20;         ///< Quality factor (18-25).

    QString m_ffmpegPath;   ///< Path to ffmpeg.
    QString m_ffprobePath;  ///< Path to ffprobe.
    QString m_doviToolPath; ///< Path to dovi_tool.
    QString m_mkvmergePath; ///< Path to mkvmerge.

    TemplateManager m_templates;       ///< Command template storage.
    CropDetector *m_cropDetector = nullptr;   ///< Crop detection handler.
    DoviProcessor *m_doviProcessor = nullptr; ///< Dolby Vision processing handler.
    ProcessRunner *m_encodeProcess = nullptr; ///< Main encode process.
    ProcessRunner *m_activeProcess = nullptr; ///< Currently running process (for abort).

    CropInfo m_cropInfo;    ///< Detected crop values.
    bool m_hasDovi = false; ///< True if source has Dolby Vision Profile 7.
    bool m_isFullHD = false;///< True if source is 1080p (not 4K).
    bool m_isVC1 = false;   ///< True if source codec is VC-1 (no HW decode).
    Step m_currentStep = CropDetect; ///< Current pipeline step.
    int m_totalSteps = 3;   ///< Total steps for progress reporting.
    double m_duration = 0;  ///< Video duration in seconds.
    double m_fps = 0;       ///< Video frame rate (detected from stream info).
    double m_totalFrames = 0; ///< Total frames (duration * fps) for progress calc.
    bool m_running = false;  ///< True while a workflow is active.

    QString m_tempDir;       ///< Temporary working directory.
    QString m_rpuFile;       ///< Path to extracted RPU (Profile 8.1).
    QString m_encodedFile;   ///< Path to intermediate encoded HEVC.
    QString m_injectedFile;  ///< Path to HEVC with injected RPU.
};

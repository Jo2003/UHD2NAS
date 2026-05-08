#pragma once

/**
 * @file doviprocessor.h
 * @brief Dolby Vision metadata processing (Profile 7 → 8.1 conversion).
 *
 * Manages the three-step Dolby Vision workflow:
 * 1. Extract RPU from source HEVC stream (with Profile 7→8.1 conversion via `-m 2`)
 * 2. Adjust RPU crop coordinates using `dovi_tool editor` with a crop.json file
 * 3. Inject the converted/cropped RPU into the newly encoded HEVC stream
 *
 * Each step is executed as a separate command via ProcessRunner,
 * supporting pipe chains for zero-copy stream processing.
 */

#include <QObject>

class ProcessRunner;

/**
 * @class DoviProcessor
 * @brief Executes dovi_tool commands for Dolby Vision RPU handling.
 *
 * This class provides a step-by-step interface for the DV conversion
 * pipeline. Each method takes a pre-resolved command string and executes
 * it asynchronously. The stepFinished() signal indicates completion of
 * each step.
 *
 * Typical workflow:
 * @code
 * processor->extractRpu(extractCmd);  // Waits for stepFinished
 * processor->convertRpu(editCmd);     // Waits for stepFinished
 * processor->injectRpu(injectCmd);    // Waits for stepFinished
 * @endcode
 */
class DoviProcessor : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Constructs a DoviProcessor.
     * @param parent Optional parent QObject.
     */
    explicit DoviProcessor(QObject *parent = nullptr);

    /**
     * @brief Extract RPU from source and convert Profile 7 → 8.1.
     *
     * Typically pipes ffmpeg HEVC output to `dovi_tool -m 2 extract-rpu`.
     *
     * @param cmd Fully resolved command string.
     */
    void extractRpu(const QString &cmd);

    /**
     * @brief Adjust RPU metadata for cropped video dimensions.
     *
     * Runs `dovi_tool editor -j crop.json` to update RPU coordinates.
     *
     * @param cmd Fully resolved command string.
     */
    void convertRpu(const QString &cmd);

    /**
     * @brief Inject processed RPU into the re-encoded HEVC stream.
     *
     * Typically pipes the encoded HEVC through `dovi_tool inject-rpu`.
     *
     * @param cmd Fully resolved command string.
     */
    void injectRpu(const QString &cmd);

    /**
     * @brief Abort the currently running dovi_tool process.
     */
    void abort();

signals:
    /**
     * @brief Emitted when the current step completes.
     * @param success True if the process exited with code 0.
     */
    void stepFinished(bool success);

    /**
     * @brief Emitted with process output for logging.
     * @param line Output text from the dovi_tool process.
     */
    void logOutput(const QString &line);

private:
    ProcessRunner *m_runner = nullptr; ///< Current process runner instance.

    /**
     * @brief Execute a command and emit stepFinished on completion.
     * @param cmd The command string to run.
     */
    void runCommand(const QString &cmd);
};

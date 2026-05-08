#pragma once

/**
 * @file cropdetector.h
 * @brief Automatic black bar detection using ffmpeg's cropdetect filter.
 *
 * Analyzes a video file to determine the optimal crop rectangle by running
 * ffmpeg's cropdetect filter and selecting the most frequently reported
 * crop values. Also computes top/bottom crop offsets needed by dovi_tool
 * for RPU metadata adjustment.
 */

#include <QObject>

class ProcessRunner;

/**
 * @struct CropInfo
 * @brief Holds the detected crop rectangle and derived metadata offsets.
 */
struct CropInfo {
    int w = 0;          ///< Crop width in pixels (visible area).
    int h = 0;          ///< Crop height in pixels (visible area).
    int x = 0;          ///< Horizontal offset from left edge.
    int y = 0;          ///< Vertical offset from top edge.
    int cropTop = 0;    ///< Pixels removed from top (for dovi_tool crop.json).
    int cropBottom = 0; ///< Pixels removed from bottom (for dovi_tool crop.json).
    bool valid = false; ///< True if detection succeeded and values are usable.
};

/**
 * @class CropDetector
 * @brief Detects optimal crop values for a video file.
 *
 * Runs the ffmpeg cropdetect filter (configurable via template) over a
 * portion of the input video and parses the output to find the most
 * common `crop=W:H:X:Y` value.
 *
 * The detection analyzes frames from 2 to 5 minutes into the video to
 * avoid false positives from opening credits or black screens.
 *
 * After detection, cropTop/cropBottom are calculated assuming:
 * - Original height 2160 for 4K content (crop height > 1080)
 * - Original height 1080 for Full HD content
 *
 * Usage:
 * @code
 * auto *detector = new CropDetector(this);
 * connect(detector, &CropDetector::finished, this, [](bool ok) { ... });
 * detector->detect(ffmpegPath, inputFile, templateCmd);
 * CropInfo info = detector->result();
 * @endcode
 */
class CropDetector : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Constructs a CropDetector.
     * @param parent Optional parent QObject.
     */
    explicit CropDetector(QObject *parent = nullptr);

    /**
     * @brief Start crop detection on a video file.
     * @param ffmpegPath Path to the ffmpeg executable.
     * @param inputFile Path to the source MKV file.
     * @param templateCmd The cropdetect command template (from TemplateManager).
     */
    void detect(const QString &ffmpegPath, const QString &inputFile, const QString &templateCmd);

    /**
     * @brief Abort a running detection.
     */
    void abort();

    /**
     * @brief Get the detection result.
     * @return CropInfo with detected values (check `valid` field).
     */
    CropInfo result() const { return m_result; }

signals:
    /**
     * @brief Emitted when detection completes.
     * @param success True if a valid crop value was found.
     */
    void finished(bool success);

    /**
     * @brief Emitted with raw ffmpeg output for logging purposes.
     * @param line Output text from ffmpeg process.
     */
    void logOutput(const QString &line);

private:
    ProcessRunner *m_runner = nullptr; ///< The process executing cropdetect.
    CropInfo m_result;                 ///< Final parsed crop values.
    QString m_rawOutput;               ///< Accumulated ffmpeg stderr output.

    /** @brief Parse m_rawOutput to find the most common crop value. */
    void parseOutput();
};

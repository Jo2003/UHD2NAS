#include "encoderengine.h"
#include "processrunner.h"
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

EncoderEngine::EncoderEngine(QObject *parent) : QObject(parent)
{
    m_cropDetector = new CropDetector(this);
    m_doviProcessor = new DoviProcessor(this);

    connect(m_cropDetector, &CropDetector::finished, this, &EncoderEngine::onCropDetected);
    connect(m_cropDetector, &CropDetector::logOutput, this, &EncoderEngine::logOutput);
    connect(m_doviProcessor, &DoviProcessor::stepFinished, this, &EncoderEngine::onDoviStepDone);
    connect(m_doviProcessor, &DoviProcessor::logOutput, this, [this](const QString &data) {
        emit logOutput(data);
        // Parse ffmpeg frame progress during RPU extraction
        if (m_currentStep == ExtractRpu && m_totalFrames > 0) {
            QRegularExpression frameRe(R"(frame=\s*(\d+))");
            auto match = frameRe.match(data);
            if (match.hasMatch()) {
                double currentFrame = match.captured(1).toDouble();
                emit encodeProgress(qMin(100.0, (currentFrame / m_totalFrames) * 100.0));
            }
        }
    });
}

void EncoderEngine::setToolPaths(const QString &ffmpeg, const QString &ffprobe, const QString &doviTool, const QString &mkvmerge)
{
    m_ffmpegPath = ffmpeg;
    m_ffprobePath = ffprobe;
    m_doviToolPath = doviTool;
    m_mkvmergePath = mkvmerge;
}

EncoderEngine::~EncoderEngine()
{
    if (!m_tempDir.isEmpty())
        QDir(m_tempDir).removeRecursively();
}

int EncoderEngine::totalSteps() const { return m_totalSteps; }

void EncoderEngine::start()
{
    // Setup temp directory
    m_running = true;
    m_tempDir = QDir::temp().filePath("uhd2nas_" + QString::number(QCoreApplication::applicationPid()));
    QDir().mkpath(m_tempDir);

    m_rpuFile = m_tempDir + "/rpu_p81.bin";
    m_encodedFile = m_tempDir + "/encoded.hevc";
    m_injectedFile = m_tempDir + "/injected.hevc";

    // First, probe for Dolby Vision
    probeForDovi();
}

void EncoderEngine::probeForDovi()
{
    emit stepProgress(0, m_totalSteps, "Probing for Dolby Vision...");
    emit logOutput("Probing input for Dolby Vision metadata...\n");

    auto *probe = new ProcessRunner(this);
    auto *probeOutput = new QString();

    connect(probe, &ProcessRunner::outputReady, this, [probeOutput](const QString &data) {
        *probeOutput += data;
    });
    connect(probe, &ProcessRunner::finished, this, [this, probe, probeOutput](int) {
        QString output = probeOutput->trimmed();
        delete probeOutput;
        emit logOutput("DV probe result: " + output + "\n");

        // Check if profile 7 is present
        m_hasDovi = output.contains("7") || output.contains("dv_profile=7");

        if (m_hasDovi) {
            m_totalSteps = 5; // crop, extract rpu, encode, inject, mux
            emit logOutput("Dolby Vision Profile 7 detected - will convert to 8.1\n");
        } else {
            m_totalSteps = 2; // crop, encode (direct to output with audio/subs)
            emit logOutput("No Dolby Vision detected - HDR10 metadata will be passed through\n");
        }

        // Now probe codec to detect VC-1 (no HW decode support)
        probe->deleteLater();
        probeCodec();
    });

    QMap<QString, QString> vars = buildVars();
    QString cmd = TemplateManager::resolve(m_templates.getTemplate(TemplateManager::KEY_PROBE_DOVI), vars);
    logCmd(cmd);
    probe->run(cmd);
}

void EncoderEngine::probeCodec()
{
    auto *probe = new ProcessRunner(this);
    auto *probeOutput = new QString();

    connect(probe, &ProcessRunner::outputReady, this, [probeOutput](const QString &data) {
        *probeOutput += data;
    });
    connect(probe, &ProcessRunner::finished, this, [this, probe, probeOutput](int) {
        QString codec = probeOutput->trimmed().toLower();
        delete probeOutput;

        m_isVC1 = codec.contains("vc1");
        if (m_isVC1)
            emit logOutput("Source codec: VC-1 (HW decode not supported, using SW decode)\n");

        // Start crop detection
        m_currentStep = CropDetect;
        emit stepProgress(1, m_totalSteps, "Detecting crop...");
        m_cropDetector->detect(m_ffmpegPath, m_inputFile,
                               m_templates.getTemplate(TemplateManager::KEY_CROPDETECT));

        probe->deleteLater();
    });

    QMap<QString, QString> vars = buildVars();
    QString cmd = QString("%1 -v quiet -select_streams v:0 -show_entries stream=codec_name -of default=nw=1:nk=1 %2")
                      .arg(vars["ffprobe"], vars["input"]);
    logCmd(cmd);
    probe->run(cmd);
}

void EncoderEngine::onCropDetected(bool success)
{
    if (!m_running)
        return; // aborted

    if (!success) {
        // Even on failure, let the user enter values manually
        CropInfo info;
        info.valid = false;
        emit cropReady(info);
        return;
    }

    m_cropInfo = m_cropDetector->result();
    emit logOutput(QString("Crop detected: %1:%2:%3:%4\n")
                       .arg(m_cropInfo.w).arg(m_cropInfo.h).arg(m_cropInfo.x).arg(m_cropInfo.y));

    emit cropReady(m_cropInfo);
}

void EncoderEngine::confirmCrop(const CropInfo &info)
{
    m_cropInfo = info;
    m_cropInfo.valid = true;

    emit logOutput(QString("Crop confirmed: %1:%2:%3:%4\n")
                       .arg(m_cropInfo.w).arg(m_cropInfo.h).arg(m_cropInfo.x).arg(m_cropInfo.y));

    // Determine if this is FullHD or 4K based on crop dimensions
    m_isFullHD = (m_cropInfo.w <= 1920);
    if (m_isFullHD) {
        emit logOutput("Source type: Full HD (SDR)\n");
    } else if (m_hasDovi) {
        emit logOutput("Source type: 4K UHD (HDR + Dolby Vision)\n");
    } else {
        emit logOutput("Source type: 4K UHD (HDR10 only, no Dolby Vision)\n");
    }

    if (m_hasDovi && !m_isFullHD) {
        startDoviExtract();
    } else {
        startEncode();
    }
}

void EncoderEngine::startDoviExtract()
{
    m_currentStep = ExtractRpu;
    emit stepProgress(2, m_totalSteps, "Extracting RPU (Profile 7 -> 8.1)...");
    emit encodeProgress(0);

    QMap<QString, QString> vars = buildVars();
    QString cmd = TemplateManager::resolve(m_templates.getTemplate(TemplateManager::KEY_EXTRACT_RPU), vars);
    logCmd(cmd);
    m_doviProcessor->extractRpu(cmd);
}

void EncoderEngine::startEncode()
{
    m_currentStep = Encode;
    int stepNum = m_hasDovi ? 3 : 2;
    emit stepProgress(stepNum, m_totalSteps, "Encoding video...");

    QString templateKey;

    if (m_isFullHD || !m_hasDovi) {
        // FullHD or 4K without DV: direct encode to output with audio/subs
        // HDR10 metadata is automatically passed through by ffmpeg
        if (m_isVC1 && m_encoder != TemplateManager::Software) {
            // VC-1 sources need SW decode + HW encode
            switch (m_encoder) {
            case TemplateManager::Software: break; // unreachable
            case TemplateManager::QuickSync: templateKey = TemplateManager::KEY_ENCODE_QSV_SWDEC; break;
            case TemplateManager::NVEnc: templateKey = TemplateManager::KEY_ENCODE_NVENC_SWDEC; break;
            case TemplateManager::AMF: templateKey = TemplateManager::KEY_ENCODE_AMF_SWDEC; break;
            }
        } else {
            switch (m_encoder) {
            case TemplateManager::Software: templateKey = TemplateManager::KEY_ENCODE_SW_FHD; break;
            case TemplateManager::QuickSync: templateKey = TemplateManager::KEY_ENCODE_QSV_FHD; break;
            case TemplateManager::NVEnc: templateKey = TemplateManager::KEY_ENCODE_NVENC_FHD; break;
            case TemplateManager::AMF: templateKey = TemplateManager::KEY_ENCODE_AMF_FHD; break;
            }
        }
    } else {
        // 4K UHD with DV: video-only encode (audio/subs added in final mux)
        switch (m_encoder) {
        case TemplateManager::Software: templateKey = TemplateManager::KEY_ENCODE_SW; break;
        case TemplateManager::QuickSync: templateKey = TemplateManager::KEY_ENCODE_QSV; break;
        case TemplateManager::NVEnc: templateKey = TemplateManager::KEY_ENCODE_NVENC; break;
        case TemplateManager::AMF: templateKey = TemplateManager::KEY_ENCODE_AMF; break;
        }
    }

    QMap<QString, QString> vars = buildVars();
    // For DV: encode video-only to temp file; for simple: encode directly to output with audio/subs
    // Note: buildVars() already provides quoted "output" and "encoded_hevc",
    // so we just override with the correct (already quoted) value
    if (m_hasDovi) {
        vars["output"] = vars["encoded_hevc"];
    }
    // else: vars["output"] already set correctly by buildVars()

    QString cmd = TemplateManager::resolve(m_templates.getTemplate(templateKey), vars);
    emit logOutput("Encode command:\n");
    logCmd(cmd);

    m_encodeProcess = new ProcessRunner(this);
    connect(m_encodeProcess, &ProcessRunner::outputReady, this, [this](const QString &data) {
        emit logOutput(data);

        // Parse ffmpeg progress based on frame count
        QRegularExpression frameRe(R"(frame=\s*(\d+))");
        auto match = frameRe.match(data);
        if (match.hasMatch() && m_totalFrames > 0) {
            double currentFrame = match.captured(1).toDouble();
            emit encodeProgress(qMin(100.0, (currentFrame / m_totalFrames) * 100.0));
        }

        // Try to get total frames from duration + fps
        if (m_totalFrames == 0) {
            QRegularExpression durRe(R"(Duration:\s*(\d+):(\d+):(\d+)\.(\d+))");
            auto durMatch = durRe.match(data);
            if (durMatch.hasMatch()) {
                m_duration = durMatch.captured(1).toDouble() * 3600 +
                             durMatch.captured(2).toDouble() * 60 +
                             durMatch.captured(3).toDouble() +
                             durMatch.captured(4).toDouble() / 100.0;
            }

            // Look for fps in stream info (e.g. "23.98 fps")
            QRegularExpression fpsRe(R"((\d+(?:\.\d+)?)\s*fps)");
            auto fpsMatch = fpsRe.match(data);
            if (fpsMatch.hasMatch()) {
                m_fps = fpsMatch.captured(1).toDouble();
            }

            if (m_duration > 0 && m_fps > 0) {
                m_totalFrames = m_duration * m_fps;
            }
        }
    });
    connect(m_encodeProcess, &ProcessRunner::finished, this, [this](int exitCode) {
        onEncodeFinished(exitCode, QProcess::NormalExit);
    });

    m_encodeProcess->run(cmd);
}

void EncoderEngine::onEncodeFinished(int exitCode, QProcess::ExitStatus)
{
    m_encodeProcess->deleteLater();
    m_encodeProcess = nullptr;

    if (exitCode != 0) {
        m_running = false;
        emit finished(false, "Encoding failed with exit code " + QString::number(exitCode));
        return;
    }

    emit encodeProgress(100.0);

    if (m_hasDovi) {
        startDoviInject();
    } else {
        m_running = false;
        emit finished(true, "Encoding completed successfully");
    }
}

void EncoderEngine::startDoviInject()
{
    m_currentStep = InjectDovi;
    emit stepProgress(4, m_totalSteps, "Injecting Dolby Vision RPU...");

    QMap<QString, QString> vars = buildVars();
    QString cmd = TemplateManager::resolve(m_templates.getTemplate(TemplateManager::KEY_INJECT_DOVI), vars);
    logCmd(cmd);
    m_doviProcessor->injectRpu(cmd);
}

void EncoderEngine::startFinalMux()
{
    m_currentStep = MuxFinal;
    emit stepProgress(5, m_totalSteps, "Final muxing (video + audio + subtitles)...");
    emit encodeProgress(0);

    QMap<QString, QString> vars = buildVars();
    QString cmd = TemplateManager::resolve(m_templates.getTemplate(TemplateManager::KEY_MUXFINAL), vars);
    logCmd(cmd);

    auto *mux = new ProcessRunner(this);
    connect(mux, &ProcessRunner::outputReady, this, [this](const QString &data) {
        emit logOutput(data);
        // mkvmerge outputs "Progress: 45%" or localized "Fortschritt: 45%"
        QRegularExpression progressRe(R"([^:]+:\s*(\d+)%)");
        auto match = progressRe.match(data);
        if (match.hasMatch()) {
            emit encodeProgress(match.captured(1).toDouble());
        }
    });
    connect(mux, &ProcessRunner::finished, this, [this, mux](int exitCode) {
        mux->deleteLater();
        m_running = false;
        if (exitCode == 0) {
            emit encodeProgress(100.0);
            emit finished(true, "Dolby Vision 8.1 encode completed successfully");
        } else {
            emit finished(false, "Final muxing failed");
        }
    });

    mux->run(cmd);
}

void EncoderEngine::onDoviStepDone(bool success)
{
    if (!success) {
        m_running = false;
        emit finished(false, "Dolby Vision processing step failed at: " +
                      QString::number(static_cast<int>(m_currentStep)));
        return;
    }

    switch (m_currentStep) {
    case ExtractRpu: startEncode(); break;
    case InjectDovi: startFinalMux(); break;
    default: break;
    }
}

void EncoderEngine::logCmd(const QString &cmd) const
{
    // Replace full tool paths with just the executable name for readability
    QString display = cmd;
    if (!m_ffmpegPath.isEmpty())
        display.replace(m_ffmpegPath, QFileInfo(m_ffmpegPath).fileName());
    if (!m_ffprobePath.isEmpty())
        display.replace(m_ffprobePath, QFileInfo(m_ffprobePath).fileName());
    if (!m_doviToolPath.isEmpty())
        display.replace(m_doviToolPath, QFileInfo(m_doviToolPath).fileName());
    // Also handle quoted variants
    display.replace("\"" + QFileInfo(m_ffmpegPath).fileName() + "\"", QFileInfo(m_ffmpegPath).fileName());
    display.replace("\"" + QFileInfo(m_ffprobePath).fileName() + "\"", QFileInfo(m_ffprobePath).fileName());
    display.replace("\"" + QFileInfo(m_doviToolPath).fileName() + "\"", QFileInfo(m_doviToolPath).fileName());

    emit const_cast<EncoderEngine*>(this)->logOutput("[CMD] " + display + "\n");
}

void EncoderEngine::abort()
{
    if (!m_running)
        return;
    m_running = false;
    m_cropDetector->abort();
    m_doviProcessor->abort();
    if (m_encodeProcess) {
        m_encodeProcess->abort();
    }
    emit finished(false, "Aborted by user");
}

QMap<QString, QString> EncoderEngine::buildVars() const
{
    // Helper to quote paths that may contain spaces
    auto q = [](const QString &path) -> QString {
        if (path.contains(' '))
            return "\"" + path + "\"";
        return path;
    };

    QMap<QString, QString> vars;
    vars["ffmpeg"] = q(m_ffmpegPath);
    vars["ffprobe"] = q(m_ffprobePath);
    vars["dovi_tool"] = q(m_doviToolPath);
    vars["mkvmerge"] = q(m_mkvmergePath);
    vars["input"] = q(m_inputFile);
    vars["output"] = q(m_outputFile);
    vars["crop_w"] = QString::number(m_cropInfo.w);
    vars["crop_h"] = QString::number(m_cropInfo.h);
    vars["crop_x"] = QString::number(m_cropInfo.x);
    vars["crop_y"] = QString::number(m_cropInfo.y);
    vars["crop_top"] = QString::number(m_cropInfo.cropTop);
    vars["crop_bottom"] = QString::number(m_cropInfo.cropBottom);
    vars["rpu_file"] = q(m_rpuFile);
    vars["encoded_hevc"] = q(m_encodedFile);
    vars["injected_hevc"] = q(m_injectedFile);
    // NVEnc CQ scale differs from QSV/x265: offset +5 for comparable file sizes
    int crf = m_crf;
    if (m_encoder == TemplateManager::NVEnc)
        crf += 5;
    vars["crf"] = QString::number(crf);
    vars["fps"] = QString::number(m_fps, 'f', 3);
    return vars;
}

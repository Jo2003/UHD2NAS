#include "cropdetector.h"
#include "templatemanager.h"
#include "processrunner.h"
#include <QRegularExpression>

CropDetector::CropDetector(QObject *parent) : QObject(parent) {}

void CropDetector::detect(const QString &ffmpegPath, const QString &inputFile, const QString &templateCmd)
{
    // Quote paths that contain spaces
    auto q = [](const QString &path) -> QString {
        if (path.contains(' '))
            return "\"" + path + "\"";
        return path;
    };

    QMap<QString, QString> vars;
    vars["ffmpeg"] = q(ffmpegPath);
    vars["input"] = q(inputFile);

    QString cmd = TemplateManager::resolve(templateCmd, vars);
    emit logOutput("[CMD] " + cmd + "\n");

    m_rawOutput.clear();
    m_result = CropInfo();

    m_runner = new ProcessRunner(this);
    connect(m_runner, &ProcessRunner::outputReady, this, [this](const QString &data) {
        m_rawOutput += data;
        emit logOutput(data);
    });
    connect(m_runner, &ProcessRunner::finished, this, [this](int) {
        parseOutput();
        emit finished(m_result.valid);
        m_runner->deleteLater();
        m_runner = nullptr;
    });

    m_runner->run(cmd);
}

void CropDetector::abort()
{
    if (m_runner) {
        m_runner->abort();
    }
}

void CropDetector::parseOutput()
{
    QRegularExpression re(R"(crop=(\d+):(\d+):(\d+):(\d+))");
    QMap<QString, int> counts;
    QString bestCrop;
    int bestCount = 0;

    auto it = re.globalMatch(m_rawOutput);
    while (it.hasNext()) {
        auto match = it.next();
        QString crop = match.captured(0);
        counts[crop]++;
        if (counts[crop] > bestCount) {
            bestCount = counts[crop];
            bestCrop = crop;
        }
    }

    if (!bestCrop.isEmpty()) {
        auto match = re.match(bestCrop);
        m_result.w = match.captured(1).toInt();
        m_result.h = match.captured(2).toInt();
        m_result.x = match.captured(3).toInt();
        m_result.y = match.captured(4).toInt();
        m_result.valid = true;

        // Calculate crop top/bottom for dovi_tool
        int originalH = (m_result.h > 1080) ? 2160 : 1080;
        m_result.cropTop = m_result.y;
        m_result.cropBottom = originalH - m_result.h - m_result.y;
    }
}

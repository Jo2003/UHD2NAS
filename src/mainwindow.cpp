#include "mainwindow.h"
#include "settingsdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QLabel>
#include <QMenuBar>
#include <QProcess>
#include <QFontDatabase>
#include <QFile>
#include <QDesktopServices>
#include <QUrl>
#include <QDateTime>
#include <QDir>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("UHD2NAS - Blu-ray to NAS Encoder");
    setMinimumSize(700, 500);

    auto *central = new QWidget();
    setCentralWidget(central);
    auto *mainLayout = new QVBoxLayout(central);

    // Pastel gradient inspired by icon colors (dark blue + warm gold)
    central->setStyleSheet(
        "QWidget#centralWidget {"
        "  background: qlineargradient(x1:1, y1:0, x2:0, y2:1,"
        "    stop:0 #a3c8ed, stop:0.4 #c8d8ed, stop:0.7 #eddcc3, stop:1 #edcfa3);"
        "}"
    );
    central->setObjectName("centralWidget");

    // File selection with icon
    auto *fileGroup = new QGroupBox("Files");
    auto *fileOuterLayout = new QHBoxLayout(fileGroup);
    auto *fileLayout = new QFormLayout();

    auto *sourceRow = new QHBoxLayout();
    m_sourceEdit = new QLineEdit();
    auto *sourceBrowse = new QPushButton("...");
    sourceBrowse->setMaximumWidth(30);
    connect(sourceBrowse, &QPushButton::clicked, this, &MainWindow::onBrowseSource);
    sourceRow->addWidget(m_sourceEdit);
    sourceRow->addWidget(sourceBrowse);
    auto *srcWidget = new QWidget(); srcWidget->setLayout(sourceRow);
    fileLayout->addRow("Source MKV:", srcWidget);

    auto *targetRow = new QHBoxLayout();
    m_targetEdit = new QLineEdit();
    auto *targetBrowse = new QPushButton("...");
    targetBrowse->setMaximumWidth(30);
    connect(targetBrowse, &QPushButton::clicked, this, &MainWindow::onBrowseTarget);
    targetRow->addWidget(m_targetEdit);
    targetRow->addWidget(targetBrowse);
    auto *tgtWidget = new QWidget(); tgtWidget->setLayout(targetRow);
    fileLayout->addRow("Target MKV:", tgtWidget);

    fileOuterLayout->addLayout(fileLayout, 1);

    // App icon on the right
    auto *iconLabel = new QLabel();
    QPixmap iconPix(":/uhd2nas.ico");
    iconLabel->setPixmap(iconPix.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    iconLabel->setAlignment(Qt::AlignTop | Qt::AlignRight);
    fileOuterLayout->addWidget(iconLabel);

    mainLayout->addWidget(fileGroup);

    // Encoder settings
    auto *encGroup = new QGroupBox("Encoder Settings");
    auto *encLayout = new QHBoxLayout(encGroup);

    m_encoderCombo = new QComboBox();
    encLayout->addWidget(new QLabel("Encoder:"));
    encLayout->addWidget(m_encoderCombo);

    encLayout->addWidget(new QLabel("Quality:"));
    m_crfSlider = new QSlider(Qt::Horizontal);
    m_crfSlider->setRange(15, 30);
    m_crfSlider->setValue(20);
    m_crfSlider->setTickPosition(QSlider::TicksBelow);
    m_crfSlider->setTickInterval(1);
    m_crfSlider->setToolTip("15 = highest quality (larger file), 30 = lower quality (smaller file)");
    m_crfLabel = new QLabel("20");
    m_crfLabel->setMinimumWidth(20);
    connect(m_crfSlider, &QSlider::valueChanged, this, [this](int val) {
        m_crfLabel->setText(QString::number(val));
    });
    encLayout->addWidget(m_crfSlider, 1);
    encLayout->addWidget(m_crfLabel);
    encLayout->addStretch();

    mainLayout->addWidget(encGroup);

    // Progress
    auto *progressGroup = new QGroupBox("Progress");
    auto *progressLayout = new QVBoxLayout(progressGroup);

    m_stepLabel = new QLabel("Ready");
    progressLayout->addWidget(m_stepLabel);

    progressLayout->addWidget(new QLabel("Step Progress:"));
    m_stepProgress = new QProgressBar();
    m_stepProgress->setRange(0, 100);
    progressLayout->addWidget(m_stepProgress);

    progressLayout->addWidget(new QLabel("Encode Progress:"));
    m_encodeProgress = new QProgressBar();
    m_encodeProgress->setRange(0, 100);
    progressLayout->addWidget(m_encodeProgress);

    mainLayout->addWidget(progressGroup);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    m_startBtn = new QPushButton("Start");
    m_abortBtn = new QPushButton("Abort");
    m_abortBtn->setEnabled(false);
    m_quitBtn = new QPushButton("Quit");
    auto *saveLogBtn = new QPushButton("Save Log");
    btnLayout->addWidget(m_startBtn);
    btnLayout->addWidget(m_abortBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(saveLogBtn);
    btnLayout->addWidget(m_quitBtn);
    mainLayout->addLayout(btnLayout);

    // Log area (always visible)
    auto *logGroup = new QGroupBox("Log Output");
    auto *logLayout = new QVBoxLayout(logGroup);
    m_logView = new QTextEdit();
    m_logView->setReadOnly(true);
    m_logView->setLineWrapMode(QTextEdit::NoWrap);
    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monoFont.setPointSize(9);
    m_logView->setFont(monoFont);
    m_logView->setMinimumHeight(QFontMetrics(monoFont).lineSpacing() * 30 + 10);
    m_logView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    logLayout->addWidget(m_logView);
    logGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(logGroup, 1); // stretch factor 1 so it grows

    // Menu
    menuBar()->addAction("Settings", this, &MainWindow::onSettings);
    menuBar()->addAction("Help", this, &MainWindow::onHelp);

    // Engine
    m_engine = new EncoderEngine(this);
    connect(m_engine, &EncoderEngine::stepProgress, this, [this](int current, int total, const QString &desc) {
        m_stepProgress->setMaximum(total);
        m_stepProgress->setValue(current);
        m_stepLabel->setText(desc);
    });
    connect(m_engine, &EncoderEngine::encodeProgress, this, [this](double pct) {
        m_encodeProgress->setValue(static_cast<int>(pct));
    });
    connect(m_engine, &EncoderEngine::logOutput, this, [this](const QString &line) {
        m_logView->append(line.trimmed());
    });
    connect(m_engine, &EncoderEngine::finished, this, &MainWindow::onFinished);

    // Connections
    connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::onStart);
    connect(m_abortBtn, &QPushButton::clicked, this, &MainWindow::onAbort);
    connect(saveLogBtn, &QPushButton::clicked, this, &MainWindow::onSaveLog);
    connect(m_quitBtn, &QPushButton::clicked, this, &QWidget::close);

    loadSettings();
    detectEncoders();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_engine->abort();
    QMainWindow::closeEvent(event);
}

void MainWindow::onBrowseSource()
{
    QString path = QFileDialog::getOpenFileName(this, "Select Source MKV", QString(), "MKV Files (*.mkv);;All Files (*)");
    if (!path.isEmpty()) {
        m_sourceEdit->setText(path);
        if (m_targetEdit->text().isEmpty()) {
            QFileInfo fi(path);
            m_targetEdit->setText(fi.absolutePath() + "/" + fi.completeBaseName() + "_encoded.mkv");
        }
    }
}

void MainWindow::onBrowseTarget()
{
    QString path = QFileDialog::getSaveFileName(this, "Select Target MKV", QString(),
        "MKV Files (*.mkv)", nullptr, QFileDialog::DontConfirmOverwrite);
    if (!path.isEmpty()) m_targetEdit->setText(path);
}

void MainWindow::onStart()
{
    if (m_sourceEdit->text().isEmpty() || m_targetEdit->text().isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select source and target files.");
        return;
    }
    if (m_ffmpegPath.isEmpty()) {
        QMessageBox::warning(this, "Error", "ffmpeg not found. Please configure in Settings.");
        onSettings();
        return;
    }

    // Check if target file already exists
    QFileInfo targetInfo(m_targetEdit->text());
    if (targetInfo.exists()) {
        auto reply = QMessageBox::question(this, "File exists",
            QString("The file \"%1\" already exists.\nOverwrite?").arg(targetInfo.fileName()),
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No)
            return;
        // Delete existing file so ffmpeg doesn't prompt
        QFile::remove(m_targetEdit->text());
    }

    m_startBtn->setEnabled(false);
    m_abortBtn->setEnabled(true);
    m_sourceEdit->setEnabled(false);
    m_targetEdit->setEnabled(false);
    m_crfSlider->setEnabled(false);
    m_encoderCombo->setEnabled(false);
    m_encodeProgress->setValue(0);
    m_stepProgress->setValue(0);
    m_logView->clear();

    m_engine->setInputFile(m_sourceEdit->text());
    m_engine->setOutputFile(m_targetEdit->text());
    m_engine->setEncoder(static_cast<TemplateManager::Encoder>(m_encoderCombo->currentData().toInt()));
    m_engine->setCrf(m_crfSlider->value());
    m_engine->setToolPaths(m_ffmpegPath, m_ffprobePath, m_doviToolPath, m_mkvmergePath);
    m_engine->start();
}

void MainWindow::onAbort()
{
    m_engine->abort();
}

void MainWindow::onFinished(bool success, const QString &message)
{
    m_startBtn->setEnabled(true);
    m_abortBtn->setEnabled(false);
    m_sourceEdit->setEnabled(true);
    m_targetEdit->setEnabled(true);
    m_crfSlider->setEnabled(true);
    m_encoderCombo->setEnabled(true);

    if (success) {
        QMessageBox::information(this, "Done", message);
    } else {
        QMessageBox::critical(this, "Error", message);
    }
}

void MainWindow::onSettings()
{
    SettingsDialog dlg(m_engine->templateManager(), this);
    dlg.setFfmpegPath(m_ffmpegPath);
    dlg.setFfprobePath(m_ffprobePath);
    dlg.setDoviToolPath(m_doviToolPath);
    dlg.setMkvmergePath(m_mkvmergePath);

    if (dlg.exec() == QDialog::Accepted) {
        m_ffmpegPath = dlg.ffmpegPath();
        m_ffprobePath = dlg.ffprobePath();
        m_doviToolPath = dlg.doviToolPath();
        m_mkvmergePath = dlg.mkvmergePath();
        saveSettings();
        detectEncoders();
    }
}

void MainWindow::loadSettings()
{
    QSettings s;
    m_ffmpegPath = s.value("tools/ffmpeg", findTool("ffmpeg")).toString();
    m_ffprobePath = s.value("tools/ffprobe", findTool("ffprobe")).toString();
    m_doviToolPath = s.value("tools/dovi_tool", findTool("dovi_tool")).toString();
    m_mkvmergePath = s.value("tools/mkvmerge", findTool("mkvmerge")).toString();
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue("tools/ffmpeg", m_ffmpegPath);
    s.setValue("tools/ffprobe", m_ffprobePath);
    s.setValue("tools/dovi_tool", m_doviToolPath);
    s.setValue("tools/mkvmerge", m_mkvmergePath);
}

QString MainWindow::findTool(const QString &name)
{
    QString path = QStandardPaths::findExecutable(name);
    return path;
}

void MainWindow::detectEncoders()
{
    m_encoderCombo->clear();

    // TODO: Auto-detection of hardware encoders via test-encode is unreliable
    // and too slow. For now, list all encoder options and let the user choose.
    m_encoderCombo->addItem("Software (libx265)", TemplateManager::Software);
    m_encoderCombo->addItem("Intel QuickSync", TemplateManager::QuickSync);
    m_encoderCombo->addItem("NVIDIA NVEnc", TemplateManager::NVEnc);
    m_encoderCombo->addItem("AMD AMF", TemplateManager::AMF);

#if 0
    // Disabled: Test-encoding approach to detect available HW encoders.
    // Problems: Too slow (up to 30s), false negatives on some driver configs.
    if (m_ffmpegPath.isEmpty())
        return;

    struct HwEncoder {
        const char *name;
        const char *label;
        TemplateManager::Encoder type;
    };
    static const HwEncoder candidates[] = {
        { "hevc_qsv",   "Intel QuickSync", TemplateManager::QuickSync },
        { "hevc_nvenc", "NVIDIA NVEnc",    TemplateManager::NVEnc },
        { "hevc_amf",   "AMD AMF",         TemplateManager::AMF },
    };

    for (const auto &enc : candidates) {
        QProcess proc;
        QString cmd = QString("\"%1\" -hide_banner -loglevel error "
                              "-f lavfi -i nullsrc=s=64x64:d=0.04 "
                              "-c:v %2 -frames:v 1 -f null -")
                          .arg(m_ffmpegPath, enc.name);
#ifdef Q_OS_WIN
        proc.setProgram("cmd.exe");
        proc.setNativeArguments("/c \"" + cmd + "\"");
#else
        proc.setProgram("sh");
        proc.setArguments(QStringList() << "-c" << cmd);
#endif
        proc.start();
        proc.waitForFinished(10000);

        if (proc.exitCode() == 0) {
            m_encoderCombo->addItem(enc.label, enc.type);
        }
    }
#endif
}

void MainWindow::onHelp()
{
    // Write help.html and screenshot to temp dir, then open in browser
    QString helpDir = QDir::temp().filePath("uhd2nas_help");
    QDir().mkpath(helpDir);

    // Copy HTML with cache-busted screenshot reference
    QFile htmlSrc(":/help.html");
    QString htmlPath = helpDir + "/help.html";
    QFile::remove(htmlPath);
    if (!htmlSrc.open(QIODevice::ReadOnly))
        return;
    QString html = QString::fromUtf8(htmlSrc.readAll());
    htmlSrc.close();
    // Append timestamp to screenshot URL to defeat browser cache
    html.replace("src=\"screenshot.png\"",
                 QString("src=\"screenshot.png?t=%1\"").arg(QDateTime::currentMSecsSinceEpoch()));
    QFile htmlOut(htmlPath);
    if (!htmlOut.open(QIODevice::WriteOnly))
        return;
    htmlOut.write(html.toUtf8());
    htmlOut.close();

    // Copy screenshot
    QFile imgSrc(":/screenshot.png");
    QString imgPath = helpDir + "/screenshot.png";
    QFile::remove(imgPath);
    imgSrc.copy(imgPath);

    QFile::setPermissions(imgPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    QDesktopServices::openUrl(QUrl::fromLocalFile(htmlPath));
}

void MainWindow::onSaveLog()
{
    if (m_logView->toPlainText().isEmpty()) {
        QMessageBox::information(this, "Save Log", "Log is empty.");
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, "Save Log",
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/uhd2nas.log",
        "Log Files (*.log *.txt);;All Files (*)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Save Log", "Could not write to:\n" + path);
        return;
    }
    file.write(m_logView->toPlainText().toUtf8());
    file.close();
}

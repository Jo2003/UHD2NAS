#include "settingsdialog.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QLabel>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QDesktopServices>
#include <QUrl>
#include <QTabWidget>
#include <QHeaderView>
#include <QMessageBox>

SettingsDialog::SettingsDialog(TemplateManager *templates, QWidget *parent)
    : QDialog(parent), m_templates(templates)
{
    setWindowTitle("Settings");
    setMinimumSize(700, 500);

    auto *layout = new QVBoxLayout(this);

    auto *tabs = new QTabWidget();
    layout->addWidget(tabs);

    // --- Tab 1: Tool Paths ---
    auto *pathPage = new QWidget();
    auto *pathLayout = new QVBoxLayout(pathPage);

    auto *pathGroup = new QGroupBox("Tool Paths");
    auto *formLayout = new QFormLayout(pathGroup);

    auto createRow = [this](QLineEdit *&edit, const QString &label, QFormLayout *form) {
        auto *row = new QHBoxLayout();
        edit = new QLineEdit();
        auto *browseBtn = new QPushButton("...");
        browseBtn->setMaximumWidth(30);
        connect(browseBtn, &QPushButton::clicked, this, [this, edit]() { browse(edit); });
        row->addWidget(edit);
        row->addWidget(browseBtn);

        QWidget *w = new QWidget();
        w->setLayout(row);
        form->addRow(label, w);
    };

    createRow(m_ffmpegEdit, "ffmpeg:", formLayout);
    createRow(m_ffprobeEdit, "ffprobe:", formLayout);
    createRow(m_doviToolEdit, "dovi_tool:", formLayout);
    createRow(m_mkvmergeEdit, "mkvmerge:", formLayout);

    pathLayout->addWidget(pathGroup);

    // Download links
    auto *dlGroup = new QGroupBox("Download Tools");
    auto *dlLayout = new QVBoxLayout(dlGroup);

    auto addLink = [dlLayout](const QString &name, const QString &url) {
        auto *linkBtn = new QPushButton("Download " + name);
        QObject::connect(linkBtn, &QPushButton::clicked, [url]() {
            QDesktopServices::openUrl(QUrl(url));
        });
        dlLayout->addWidget(linkBtn);
    };

    addLink("ffmpeg", "https://ffmpeg.org/download.html");
    addLink("dovi_tool", "https://github.com/quietvoid/dovi_tool/releases");
    addLink("mkvtoolnix", "https://mkvtoolnix.download/downloads.html");

    pathLayout->addWidget(dlGroup);
    pathLayout->addStretch();
    tabs->addTab(pathPage, "Tool Paths");

    // --- Tab 2: Command Templates ---
    auto *tmplPage = new QWidget();
    auto *tmplLayout = new QVBoxLayout(tmplPage);

    tmplLayout->addWidget(new QLabel("Edit command templates. Use {placeholders} for variables."));

    m_templateTable = new QTableWidget();
    m_templateTable->setColumnCount(2);
    m_templateTable->setHorizontalHeaderLabels({"Key", "Command Template"});
    m_templateTable->horizontalHeader()->setStretchLastSection(true);
    m_templateTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_templateTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    tmplLayout->addWidget(m_templateTable, 1);

    auto *tmplBtnLayout = new QHBoxLayout();
    auto *resetBtn = new QPushButton("Reset to Defaults");
    connect(resetBtn, &QPushButton::clicked, this, &SettingsDialog::resetTemplates);
    tmplBtnLayout->addWidget(resetBtn);
    tmplBtnLayout->addStretch();
    tmplLayout->addLayout(tmplBtnLayout);

    tabs->addTab(tmplPage, "Command Templates");

    // --- Dialog Buttons ---
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        applyTemplates();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    populateTemplates();
}

void SettingsDialog::browse(QLineEdit *edit)
{
    QString path = QFileDialog::getOpenFileName(this, "Select Tool");
    if (!path.isEmpty()) edit->setText(path);
}

void SettingsDialog::populateTemplates()
{
    // Get all template keys and values
    struct Entry { const char* key; const char* desc; };
    static const Entry entries[] = {
        { TemplateManager::KEY_CROPDETECT,     "Crop Detection" },
        { TemplateManager::KEY_PROBE_DOVI,     "Dolby Vision Probe" },
        { TemplateManager::KEY_EXTRACT_RPU,    "Extract RPU (DV7->8.1)" },
        { TemplateManager::KEY_ENCODE_SW,      "Encode 4K (Software)" },
        { TemplateManager::KEY_ENCODE_QSV,     "Encode 4K (QuickSync)" },
        { TemplateManager::KEY_ENCODE_NVENC,   "Encode 4K (NVEnc)" },
        { TemplateManager::KEY_ENCODE_AMF,     "Encode 4K (AMF)" },
        { TemplateManager::KEY_ENCODE_SW_FHD,  "Encode FHD (Software)" },
        { TemplateManager::KEY_ENCODE_QSV_FHD, "Encode FHD (QuickSync)" },
        { TemplateManager::KEY_ENCODE_NVENC_FHD, "Encode FHD (NVEnc)" },
        { TemplateManager::KEY_ENCODE_AMF_FHD, "Encode FHD (AMF)" },
        { TemplateManager::KEY_INJECT_DOVI,    "Inject RPU" },
        { TemplateManager::KEY_MUXFINAL,       "Final Mux" },
    };

    int count = sizeof(entries) / sizeof(entries[0]);
    m_templateTable->setRowCount(count);

    for (int i = 0; i < count; ++i) {
        auto *keyItem = new QTableWidgetItem(QString("%1 (%2)").arg(entries[i].desc, entries[i].key));
        keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
        keyItem->setData(Qt::UserRole, QString(entries[i].key));
        m_templateTable->setItem(i, 0, keyItem);

        auto *valItem = new QTableWidgetItem(m_templates->getTemplate(entries[i].key));
        m_templateTable->setItem(i, 1, valItem);
    }
}

void SettingsDialog::resetTemplates()
{
    if (QMessageBox::question(this, "Reset Templates",
            "Reset all command templates to their defaults?",
            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        // Create a fresh TemplateManager to get defaults
        TemplateManager defaults;
        // Clear saved settings so defaults are used
        QSettings settings;
        settings.beginGroup("Templates");
        settings.remove("");
        settings.endGroup();

        // Re-create with defaults
        TemplateManager fresh;

        for (int i = 0; i < m_templateTable->rowCount(); ++i) {
            QString key = m_templateTable->item(i, 0)->data(Qt::UserRole).toString();
            m_templateTable->item(i, 1)->setText(fresh.getTemplate(key));
        }
    }
}

void SettingsDialog::applyTemplates()
{
    for (int i = 0; i < m_templateTable->rowCount(); ++i) {
        QString key = m_templateTable->item(i, 0)->data(Qt::UserRole).toString();
        QString val = m_templateTable->item(i, 1)->text();
        m_templates->setTemplate(key, val);
    }
    m_templates->save();
}

QString SettingsDialog::ffmpegPath() const { return m_ffmpegEdit->text(); }
QString SettingsDialog::ffprobePath() const { return m_ffprobeEdit->text(); }
QString SettingsDialog::doviToolPath() const { return m_doviToolEdit->text(); }
QString SettingsDialog::mkvmergePath() const { return m_mkvmergeEdit->text(); }

void SettingsDialog::setFfmpegPath(const QString &p) { m_ffmpegEdit->setText(p); }
void SettingsDialog::setFfprobePath(const QString &p) { m_ffprobeEdit->setText(p); }
void SettingsDialog::setDoviToolPath(const QString &p) { m_doviToolEdit->setText(p); }
void SettingsDialog::setMkvmergePath(const QString &p) { m_mkvmergeEdit->setText(p); }

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
#include <QRegularExpression>
#include <QFont>
#include <QFontDatabase>

// --- PlaceholderHighlighter ---

PlaceholderHighlighter::PlaceholderHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent) {}

void PlaceholderHighlighter::highlightBlock(const QString &text)
{
    QTextCharFormat fmt;
    fmt.setForeground(QColor(128, 0, 128)); // violet
    fmt.setFontWeight(QFont::Bold);

    QRegularExpression re(R"(\{[^}]+\})");
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        auto match = it.next();
        setFormat(match.capturedStart(), match.capturedLength(), fmt);
    }
}

// --- SettingsDialog ---

SettingsDialog::SettingsDialog(TemplateManager *templates, QWidget *parent)
    : QDialog(parent), m_templates(templates)
{
    setWindowTitle("Settings");
    setMinimumSize(800, 600);

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

    tmplLayout->addWidget(new QLabel("Select a template to edit:"));

    m_templateTable = new QTableWidget();
    m_templateTable->setColumnCount(1);
    m_templateTable->setHorizontalHeaderLabels({"Template"});
    m_templateTable->horizontalHeader()->setStretchLastSection(true);
    m_templateTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_templateTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_templateTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tmplLayout->addWidget(m_templateTable, 1);

    // Placeholder info
    m_placeholderLabel = new QLabel(
        "Placeholders: {ffmpeg} {ffprobe} {dovi_tool} {mkvmerge} {input} {output} "
        "{crf} {crop_w} {crop_h} {crop_x} {crop_y} {rpu_file} {encoded_hevc} {injected_hevc}");
    m_placeholderLabel->setWordWrap(true);
    m_placeholderLabel->setStyleSheet("color: purple; font-style: italic;");
    tmplLayout->addWidget(m_placeholderLabel);

    // Template editor
    m_templateEdit = new QTextEdit();
    m_templateEdit->setMinimumHeight(100);
    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monoFont.setPointSize(9);
    m_templateEdit->setFont(monoFont);
    new PlaceholderHighlighter(m_templateEdit->document());
    tmplLayout->addWidget(m_templateEdit, 1);

    // Template buttons
    auto *tmplBtnLayout = new QHBoxLayout();
    auto *saveTemplateBtn = new QPushButton("Save Template");
    auto *resetBtn = new QPushButton("Reset All to Defaults");
    connect(saveTemplateBtn, &QPushButton::clicked, this, &SettingsDialog::saveCurrentTemplate);
    connect(resetBtn, &QPushButton::clicked, this, &SettingsDialog::resetTemplates);
    tmplBtnLayout->addWidget(saveTemplateBtn);
    tmplBtnLayout->addStretch();
    tmplBtnLayout->addWidget(resetBtn);
    tmplLayout->addLayout(tmplBtnLayout);

    tabs->addTab(tmplPage, "Command Templates");

    // --- Dialog Buttons ---
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        saveCurrentTemplate(); // save any pending edit
        applyTemplates();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    populateTemplates();

    // Connect table selection
    connect(m_templateTable, &QTableWidget::currentCellChanged,
            this, [this](int row, int, int, int) { onTemplateSelected(row); });

    // Select first row
    if (m_templateTable->rowCount() > 0) {
        m_templateTable->selectRow(0);
        onTemplateSelected(0);
    }
}

void SettingsDialog::onTemplateSelected(int row)
{
    // Save previous template before switching
    saveCurrentTemplate();

    m_currentTemplateRow = row;
    if (row >= 0 && row < m_templateTable->rowCount()) {
        QString key = m_templateTable->item(row, 0)->data(Qt::UserRole).toString();
        m_templateEdit->setPlainText(m_templates->getTemplate(key));
    }
}

void SettingsDialog::saveCurrentTemplate()
{
    if (m_currentTemplateRow < 0 || m_currentTemplateRow >= m_templateTable->rowCount())
        return;

    QString key = m_templateTable->item(m_currentTemplateRow, 0)->data(Qt::UserRole).toString();
    QString val = m_templateEdit->toPlainText().trimmed();
    // Store in templates (will be persisted on dialog OK)
    m_templates->setTemplate(key, val);
}

void SettingsDialog::browse(QLineEdit *edit)
{
    QString path = QFileDialog::getOpenFileName(this, "Select Tool");
    if (!path.isEmpty()) edit->setText(path);
}

void SettingsDialog::populateTemplates()
{
    struct Entry { const char* key; const char* desc; };
    static const Entry entries[] = {
        { TemplateManager::KEY_CROPDETECT,       "Crop Detection" },
        { TemplateManager::KEY_PROBE_DOVI,       "Dolby Vision Probe" },
        { TemplateManager::KEY_EXTRACT_RPU,      "Extract RPU (DV7->8.1)" },
        { TemplateManager::KEY_ENCODE_SW,        "Encode 4K (Software)" },
        { TemplateManager::KEY_ENCODE_QSV,       "Encode 4K (QuickSync)" },
        { TemplateManager::KEY_ENCODE_NVENC,     "Encode 4K (NVEnc)" },
        { TemplateManager::KEY_ENCODE_AMF,       "Encode 4K (AMF)" },
        { TemplateManager::KEY_ENCODE_SW_FHD,    "Encode FHD (Software)" },
        { TemplateManager::KEY_ENCODE_QSV_FHD,   "Encode FHD (QuickSync)" },
        { TemplateManager::KEY_ENCODE_NVENC_FHD, "Encode FHD (NVEnc)" },
        { TemplateManager::KEY_ENCODE_AMF_FHD,   "Encode FHD (AMF)" },
        { TemplateManager::KEY_ENCODE_QSV_SWDEC, "Encode FHD QSV (SW Decode)" },
        { TemplateManager::KEY_ENCODE_NVENC_SWDEC, "Encode FHD NVEnc (SW Decode)" },
        { TemplateManager::KEY_ENCODE_AMF_SWDEC, "Encode FHD AMF (SW Decode)" },
        { TemplateManager::KEY_INJECT_DOVI,      "Inject RPU" },
        { TemplateManager::KEY_MUXFINAL,         "Final Mux" },
    };

    int count = sizeof(entries) / sizeof(entries[0]);
    m_templateTable->setRowCount(count);

    for (int i = 0; i < count; ++i) {
        auto *item = new QTableWidgetItem(QString("%1 (%2)").arg(entries[i].desc, entries[i].key));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        item->setData(Qt::UserRole, QString(entries[i].key));
        m_templateTable->setItem(i, 0, item);
    }
}

void SettingsDialog::resetTemplates()
{
    if (QMessageBox::question(this, "Reset Templates",
            "Reset all command templates to their defaults?",
            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        QSettings settings;
        settings.beginGroup("Templates");
        settings.remove("");
        settings.endGroup();

        TemplateManager fresh;
        // Update the live template manager
        for (int i = 0; i < m_templateTable->rowCount(); ++i) {
            QString key = m_templateTable->item(i, 0)->data(Qt::UserRole).toString();
            m_templates->setTemplate(key, fresh.getTemplate(key));
        }
        // Refresh editor
        m_currentTemplateRow = -1;
        int row = m_templateTable->currentRow();
        if (row >= 0) onTemplateSelected(row);
    }
}

void SettingsDialog::applyTemplates()
{
    saveCurrentTemplate();
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

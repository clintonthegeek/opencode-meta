#include "ProfileCompareDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QTextEdit>
#include <QVBoxLayout>

#include "generation.h"
#include "models/Profile.h"
#include "models/Template.h"
#include "storage/StorageManager.h"

ProfileCompareDialog::ProfileCompareDialog(StorageManager &storageManager, QWidget *parent)
    : QDialog(parent)
    , m_storageManager(storageManager)
{
    setupUi();
    populateProfiles();
    updateViews();
}

void ProfileCompareDialog::setupUi()
{
    setWindowTitle(tr("Compare Profiles"));

    auto *mainLayout = new QVBoxLayout(this);

    auto *selectorRow = new QHBoxLayout();
    auto *leftLabel = new QLabel(tr("Left profile:"), this);
    auto *rightLabel = new QLabel(tr("Right profile:"), this);
    m_leftCombo = new QComboBox(this);
    m_rightCombo = new QComboBox(this);

    selectorRow->addWidget(leftLabel);
    selectorRow->addWidget(m_leftCombo, 1);
    selectorRow->addSpacing(12);
    selectorRow->addWidget(rightLabel);
    selectorRow->addWidget(m_rightCombo, 1);
    mainLayout->addLayout(selectorRow);

    auto *previewsRow = new QHBoxLayout();

    auto *leftColumn = new QVBoxLayout();
    auto *leftPreviewLabel = new QLabel(tr("Left: rendered opencode.json"), this);
    m_leftText = new QTextEdit(this);
    m_leftText->setReadOnly(true);
    leftColumn->addWidget(leftPreviewLabel);
    leftColumn->addWidget(m_leftText, 1);

    auto *rightColumn = new QVBoxLayout();
    auto *rightPreviewLabel = new QLabel(tr("Right: rendered opencode.json"), this);
    m_rightText = new QTextEdit(this);
    m_rightText->setReadOnly(true);
    rightColumn->addWidget(rightPreviewLabel);
    rightColumn->addWidget(m_rightText, 1);

    previewsRow->addLayout(leftColumn, 1);
    previewsRow->addLayout(rightColumn, 1);
    mainLayout->addLayout(previewsRow, 1);

    auto *diffLabel = new QLabel(tr("Top-level differences"), this);
    m_diffSummary = new QTextEdit(this);
    m_diffSummary->setReadOnly(true);
    mainLayout->addWidget(diffLabel);
    mainLayout->addWidget(m_diffSummary, 0);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &ProfileCompareDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &ProfileCompareDialog::accept);
    mainLayout->addWidget(buttons);

    connect(m_leftCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProfileCompareDialog::onSelectionChanged);
    connect(m_rightCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProfileCompareDialog::onSelectionChanged);
}

void ProfileCompareDialog::populateProfiles()
{
    m_leftCombo->clear();
    m_rightCombo->clear();

    const QList<Profile> profiles = m_storageManager.listProfiles();
    for (const Profile &p : profiles) {
        const QString label = p.name.isEmpty() ? p.id : p.name;
        m_leftCombo->addItem(label, p.id);
        m_rightCombo->addItem(label, p.id);
    }

    if (m_leftCombo->count() > 0 && m_rightCombo->count() > 1) {
        m_leftCombo->setCurrentIndex(0);
        m_rightCombo->setCurrentIndex(1);
    }
}

void ProfileCompareDialog::onSelectionChanged()
{
    updateViews();
}

void ProfileCompareDialog::updateViews()
{
    const QString leftId = m_leftCombo->currentData().toString();
    const QString rightId = m_rightCombo->currentData().toString();

    QJsonObject leftConfig;
    QJsonObject rightConfig;

    auto renderConfigForProfile = [this](const QString &profileId, QJsonObject &outConfig) {
        outConfig = QJsonObject();
        if (profileId.isEmpty()) {
            return;
        }

        const Profile p = m_storageManager.loadProfile(profileId);
        if (p.id.isEmpty() || p.templateId.isEmpty()) {
            return;
        }

        const Template t = m_storageManager.loadTemplate(p.templateId);
        if (t.id.isEmpty()) {
            return;
        }

        outConfig = renderProfileToConfig(t, p);
    };

    renderConfigForProfile(leftId, leftConfig);
    renderConfigForProfile(rightId, rightConfig);

    auto configToPrettyJson = [](const QJsonObject &obj) -> QString {
        if (obj.isEmpty()) {
            return QString();
        }
        const QJsonDocument doc(obj);
        return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    };

    m_leftText->setPlainText(configToPrettyJson(leftConfig));
    m_rightText->setPlainText(configToPrettyJson(rightConfig));

    if (!leftConfig.isEmpty() && !rightConfig.isEmpty()) {
        const QStringList summary = summarizeTopLevelConfigDiff(leftConfig, rightConfig);
        m_diffSummary->setPlainText(summary.join(QLatin1Char('\n')));
    } else {
        m_diffSummary->clear();
    }
}

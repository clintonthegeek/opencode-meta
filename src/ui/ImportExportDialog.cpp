#include "ui/ImportExportDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

#include "storage/ImportExportManager.h"
#include "storage/StorageManager.h"

namespace {

constexpr const char *kObjRoleList        = "bundleDialog.roleList";
constexpr const char *kObjTeamList        = "bundleDialog.teamList";
constexpr const char *kObjPathEdit        = "bundleDialog.pathEdit";
constexpr const char *kObjBrowseButton    = "bundleDialog.browseButton";
constexpr const char *kObjNotesEdit       = "bundleDialog.notesEdit";
constexpr const char *kObjImportPreview   = "bundleDialog.importPreview";
constexpr const char *kObjSummaryLabel    = "bundleDialog.summaryLabel";

QString normalizedPath(const QString &raw)
{
    if (raw.startsWith(QStringLiteral("file://"))) {
        return QUrl(raw).toLocalFile();
    }
    return raw;
}

} // namespace

QString ImportExportDialog::suggestExportFilename(const QList<QString> &teamIds)
{
    if (!teamIds.isEmpty()) {
        return QStringLiteral("bundle-") + teamIds.first() + QStringLiteral(".zip");
    }
    return QStringLiteral("bundle.zip");
}

ImportExportDialog::ImportExportDialog(Mode mode,
                                       const QList<Role> &availableRoles,
                                       const QList<Team> &availableTeams,
                                       QWidget *parent)
    : QDialog(parent),
      m_mode(mode),
      m_availableRoles(availableRoles),
      m_availableTeams(availableTeams)
{
    setWindowTitle(mode == Mode::Export ? tr("Export Bundle") : tr("Import Bundle"));
    resize(720, 540);
    buildUi();
    populateRoleList();
    populateTeamList();
    updateAcceptEnabled();
}

void ImportExportDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    auto *intro = new QLabel(this);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    if (m_mode == Mode::Export) {
        intro->setText(tr(
            "<b>Export Bundle</b> &mdash; select the Roles and Teams "
            "you want to package into a portable .zip file. Any "
            "Specialists referenced by the selected Teams are auto-"
            "included so the bundle is self-contained."));
    } else {
        intro->setText(tr(
            "<b>Import Bundle</b> &mdash; choose a previously exported "
            "<code>.zip</code> bundle. A preview of its manifest is "
            "shown after you pick a file. Importing will overwrite "
            "any Roles, Teams, or Specialists whose id already "
            "exists in your storage."));
    }
    intro->setToolTip(m_mode == Mode::Export
        ? tr("Checked entries are written to the bundle.")
        : tr("The bundle's manifest is shown after a file is chosen."));
    root->addWidget(intro);

    auto *grid = new QHBoxLayout();
    grid->setContentsMargins(0, 0, 0, 0);

    // Roles column.
    auto *roleColumn = new QVBoxLayout();
    auto *roleHeader = new QLabel(tr("<b>Roles</b>"), this);
    roleColumn->addWidget(roleHeader);
    m_roleList = new QListWidget(this);
    m_roleList->setObjectName(QString::fromLatin1(kObjRoleList));
    m_roleList->setSelectionMode(QAbstractItemView::MultiSelection);
    if (m_mode == Mode::Import) {
        m_roleList->setEnabled(false);
    }
    m_roleList->setToolTip(tr(
        "Toggle the Roles you want to include. Multiple selection is OK."));
    roleColumn->addWidget(m_roleList, 1);

    // Teams column.
    auto *teamColumn = new QVBoxLayout();
    auto *teamHeader = new QLabel(tr("<b>Teams</b>"), this);
    teamColumn->addWidget(teamHeader);
    m_teamList = new QListWidget(this);
    m_teamList->setObjectName(QString::fromLatin1(kObjTeamList));
    m_teamList->setSelectionMode(QAbstractItemView::MultiSelection);
    if (m_mode == Mode::Import) {
        m_teamList->setEnabled(false);
    }
    m_teamList->setToolTip(tr(
        "Toggle the Teams you want to include. Specialists referenced "
        "by these Teams are pulled in automatically."));
    teamColumn->addWidget(m_teamList, 1);

    grid->addLayout(roleColumn, 1);
    grid->addLayout(teamColumn, 1);
    root->addLayout(grid, 1);

    // Path row.
    auto *pathRow = new QHBoxLayout();
    auto *pathLabel = new QLabel(tr("&Bundle file:"), this);
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setObjectName(QString::fromLatin1(kObjPathEdit));
    m_pathEdit->setPlaceholderText(m_mode == Mode::Export
        ? tr("Choose a destination .zip file")
        : tr("Choose a bundle .zip file to import"));
    m_browseButton = new QPushButton(tr("Browse..."), this);
    m_browseButton->setObjectName(QString::fromLatin1(kObjBrowseButton));

    pathLabel->setBuddy(m_pathEdit);
    pathRow->addWidget(pathLabel);
    pathRow->addWidget(m_pathEdit, 1);
    pathRow->addWidget(m_browseButton);

    auto *pathGroup = new QVBoxLayout();
    pathGroup->addLayout(pathRow);

    // Notes (only meaningful for Export; show disabled for Import).
    auto *notesRow = new QHBoxLayout();
    auto *notesLabel = new QLabel(tr("&Notes:"), this);
    m_notesEdit = new QLineEdit(this);
    m_notesEdit->setObjectName(QString::fromLatin1(kObjNotesEdit));
    m_notesEdit->setPlaceholderText(
        tr("Optional: tag this bundle (\"Q3 demo set\", etc.)"));
    m_notesEdit->setToolTip(tr(
        "Saved into the bundle manifest under 'notes'. Free-form; "
        "leave empty to skip."));
    if (m_mode == Mode::Import) {
        m_notesEdit->setEnabled(false);
        m_notesEdit->setPlaceholderText(
            tr("(notes from the bundle manifest are read-only here)"));
    }
    notesLabel->setBuddy(m_notesEdit);
    notesRow->addWidget(notesLabel);
    notesRow->addWidget(m_notesEdit, 1);
    pathGroup->addLayout(notesRow);
    root->addLayout(pathGroup);

    // Import preview / Export summary label.
    if (m_mode == Mode::Import) {
        m_importPreview = new QTextEdit(this);
        m_importPreview->setObjectName(QString::fromLatin1(kObjImportPreview));
        m_importPreview->setReadOnly(true);
        m_importPreview->setPlaceholderText(
            tr("Pick a bundle .zip file to see its manifest preview."));
        m_importPreview->setToolTip(tr(
            "Read-only preview of the bundle's manifest. The import "
            "operation will only happen after you click OK."));
        root->addWidget(m_importPreview, 1);
    } else {
        m_summaryLabel = new QLabel(this);
        m_summaryLabel->setObjectName(QString::fromLatin1(kObjSummaryLabel));
        m_summaryLabel->setWordWrap(true);
        m_summaryLabel->setText(tr(
            "No Roles or Teams selected yet. Toggle at least one "
            "entry above to enable Export."));
        root->addWidget(m_summaryLabel);
    }

    // OK/Cancel.
    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        Qt::Horizontal, this);
    m_okButton = buttonBox->button(QDialogButtonBox::Ok);
    m_okButton->setText(m_mode == Mode::Export ? tr("Export") : tr("Import"));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttonBox);

    // Wire up browse + path change.
    connect(m_browseButton, &QPushButton::clicked, this, &ImportExportDialog::onBrowse);
    connect(m_pathEdit, &QLineEdit::textChanged, this,
            [this](const QString &) { updateAcceptEnabled(); });
    if (m_mode == Mode::Import) {
        connect(m_pathEdit, &QLineEdit::textChanged, this,
                &ImportExportDialog::onAutoRefreshImportPreview);
    }

    // Selection-changed wiring for export-mode validation. We connect
    // AFTER populating so the very first paint has the right state.
    if (m_mode == Mode::Export) {
        connect(m_roleList, &QListWidget::itemSelectionChanged, this,
                [this]() { updateAcceptEnabled(); });
        connect(m_teamList, &QListWidget::itemSelectionChanged, this,
                [this]() { updateAcceptEnabled(); });
    }
}

void ImportExportDialog::populateRoleList()
{
    if (!m_roleList) {
        return;
    }
    m_roleList->clear();
    m_roleList->addItem(tr("(none -- no Roles in storage)"));
    m_roleList->item(0)->setFlags(Qt::NoItemFlags);

    for (const Role &role : std::as_const(m_availableRoles)) {
        if (role.id.isEmpty()) {
            continue;
        }
        auto *item = new QListWidgetItem(role.name.isEmpty() ? role.id : role.name,
                                         m_roleList);
        item->setData(Qt::UserRole, role.id);
        if (m_mode == Mode::Export) {
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
        }
    }
    if (m_availableRoles.isEmpty()) {
        m_roleList->item(0)->setHidden(false);
    } else {
        delete m_roleList->takeItem(0); // remove "(none)" placeholder
    }
}

void ImportExportDialog::populateTeamList()
{
    if (!m_teamList) {
        return;
    }
    m_teamList->clear();
    m_teamList->addItem(tr("(none -- no Teams in storage)"));
    m_teamList->item(0)->setFlags(Qt::NoItemFlags);

    for (const Team &team : std::as_const(m_availableTeams)) {
        if (team.id.isEmpty()) {
            continue;
        }
        auto *item = new QListWidgetItem(team.name.isEmpty() ? team.id : team.name,
                                         m_teamList);
        item->setData(Qt::UserRole, team.id);
        if (m_mode == Mode::Export) {
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
        }
    }
    if (m_availableTeams.isEmpty()) {
        m_teamList->item(0)->setHidden(false);
    } else {
        delete m_teamList->takeItem(0); // remove "(none)" placeholder
    }
}

void ImportExportDialog::onBrowse()
{
    if (!m_pathEdit) {
        return;
    }
    QString chosen;
    const QString start = m_pathEdit->text();
    if (m_mode == Mode::Export) {
        const QString suggested = suggestExportFilename({});
        const QString startDir = start.isEmpty()
            ? QDir::homePath()
            : QFileInfo(start).absolutePath();
        chosen = QFileDialog::getSaveFileName(
            this,
            tr("Choose bundle destination"),
            startDir + QStringLiteral("/") + suggested,
            tr("Zip Bundles (*.zip);;All Files (*)"));
    } else {
        const QString startDir = start.isEmpty()
            ? QDir::homePath()
            : QFileInfo(start).absolutePath();
        chosen = QFileDialog::getOpenFileName(
            this,
            tr("Choose bundle file"),
            startDir,
            tr("Zip Bundles (*.zip);;All Files (*)"));
    }
    if (chosen.isEmpty()) {
        return;
    }
    m_pathEdit->setText(chosen);
    if (m_mode == Mode::Import) {
        onAutoRefreshImportPreview();
    }
}

void ImportExportDialog::onAutoRefreshImportPreview()
{
    if (m_mode != Mode::Import) {
        return;
    }
    refreshImportPreview();
}

void ImportExportDialog::refreshImportPreview()
{
    if (!m_importPreview) {
        return;
    }
    const QString path = normalizedPath(m_pathEdit ? m_pathEdit->text() : QString());
    if (path.isEmpty() || !QFileInfo(path).isFile()) {
        m_importPreview->setPlainText(
            tr("Pick a bundle .zip file to see its manifest preview."));
        return;
    }

    // We need a StorageManager for readManifest (it does not write,
    // but the import-export manager takes a reference at construction
    // time so we fabricate a temp one). The dialog does not own
    // storage; we borrow via the file's parent dir as a transient
    // stand-in. The readManifest path never touches storage so any
    // rootPath() is safe.
    const QFileInfo info(path);
    StorageManager transient(info.absolutePath());
    ImportExportManager manager(transient);
    const ImportExportManager::Manifest manifest = manager.readManifest(path);

    if (!manifest.valid) {
        m_importPreview->setPlainText(
            tr("Could not read this bundle:\n%1").arg(manifest.errorString));
        updateAcceptEnabled();
        return;
    }

    QString preview;
    preview += tr("Format: %1\n").arg(QString::fromLatin1(ImportExportManager::kFormatTag));
    preview += tr("Format version: %1\n").arg(manifest.formatVersion);
    preview += tr("Created (UTC): %1\n").arg(
        manifest.createdUtc.toUTC().toString(Qt::ISODateWithMs));
    preview += tr("Source: %1 %2\n").arg(manifest.source, manifest.sourceVersion);
    if (!manifest.notes.isEmpty()) {
        preview += tr("Notes: %1\n").arg(manifest.notes);
    }
    preview += tr("\nIncluded:\n");
    preview += tr("  Roles (%1): %2\n").arg(manifest.includedRoleIds.size())
                                      .arg(manifest.includedRoleIds.join(QStringLiteral(", ")));
    preview += tr("  Teams (%1): %2\n").arg(manifest.includedTeamIds.size())
                                      .arg(manifest.includedTeamIds.join(QStringLiteral(", ")));
    preview += tr("  Specialists (%1): %2\n").arg(manifest.includedSpecialistIds.size())
                                            .arg(manifest.includedSpecialistIds.join(QStringLiteral(", ")));
    m_importPreview->setPlainText(preview);

    updateAcceptEnabled();
}

void ImportExportDialog::updateAcceptEnabled()
{
    if (!m_okButton) {
        return;
    }
    const QString path = m_pathEdit ? m_pathEdit->text().trimmed() : QString();
    if (path.isEmpty()) {
        m_okButton->setEnabled(false);
        if (m_summaryLabel) {
            m_summaryLabel->setText(
                tr("Destination path is required."));
        }
        return;
    }
    if (m_mode == Mode::Import) {
        // For Import: enable OK only if the file looks readable AND
        // the most recent preview read succeeded. We re-read here
        // every time -- cheap, and consistent with the onAutoRefresh
        // path.
        if (!QFileInfo(path).isFile()) {
            m_okButton->setEnabled(false);
            return;
        }
        // We do not call readManifest here because the dialog's
        // preview slot already does; instead we read the state of
        // the preview text: if it starts with "Could not read", we
        // refuse to enable.
        if (m_importPreview) {
            const QString text = m_importPreview->toPlainText();
            if (text.startsWith(QStringLiteral("Could not read"))) {
                m_okButton->setEnabled(false);
                return;
            }
        }
        m_okButton->setEnabled(true);
        return;
    }

    // Export: enable iff at least one Role or Team is checked and a
    // path is set.
    int selectedCount = 0;
    if (m_roleList) {
        for (int i = 0; i < m_roleList->count(); ++i) {
            QListWidgetItem *item = m_roleList->item(i);
            if (item && item->checkState() == Qt::Checked) {
                ++selectedCount;
            }
        }
    }
    if (m_teamList) {
        for (int i = 0; i < m_teamList->count(); ++i) {
            QListWidgetItem *item = m_teamList->item(i);
            if (item && item->checkState() == Qt::Checked) {
                ++selectedCount;
            }
        }
    }
    const bool hasSelection = selectedCount > 0;
    m_okButton->setEnabled(hasSelection);
    if (m_summaryLabel) {
        m_summaryLabel->setText(hasSelection
            ? tr("%1 entries selected for export. Click Export to write them into the bundle.")
                .arg(selectedCount)
            : tr("No Roles or Teams selected yet. Toggle at least one "
                 "entry above to enable Export."));
    }
}

ImportExportDialog::Result ImportExportDialog::result() const
{
    Result out;
    out.mode = m_mode;
    out.zipPath = normalizedPath(m_pathEdit ? m_pathEdit->text() : QString());
    out.notes = m_notesEdit ? m_notesEdit->text() : QString();
    if (m_roleList) {
        for (int i = 0; i < m_roleList->count(); ++i) {
            const QListWidgetItem *item = m_roleList->item(i);
            if (item && m_roleList->isEnabled()
                && item->checkState() == Qt::Checked) {
                out.roleIds.append(item->data(Qt::UserRole).toString());
            }
        }
    }
    if (m_teamList) {
        for (int i = 0; i < m_teamList->count(); ++i) {
            const QListWidgetItem *item = m_teamList->item(i);
            if (item && m_teamList->isEnabled()
                && item->checkState() == Qt::Checked) {
                out.teamIds.append(item->data(Qt::UserRole).toString());
            }
        }
    }
    return out;
}

#include "Editor/App/MainWindow.h"

#include <cstdio>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include "Editor/Core/ExplorerModel.h"
#include "Editor/Core/InspectorModel.h"
#include "Editor/Core/PlayDemo.h"
#include "Engine/Serialization/SceneSerializer.h"

namespace {

std::filesystem::path to_path(const QString& s) { return std::filesystem::path(s.toStdU16String()); }
QString from_path(const std::filesystem::path& p) { return QString::fromStdU16String(p.u16string()); }
QString q(const std::string& s) { return QString::fromStdString(s); }

constexpr k::TransformField kFields[3] = {k::TransformField::Position, k::TransformField::Rotation, k::TransformField::Scale};
const char* const kFieldNames[3] = {"Position", "Rotation (deg)", "Scale"};
const k::ObjectKind kCreateKinds[4] = {k::ObjectKind::Empty, k::ObjectKind::Cube, k::ObjectKind::Sphere, k::ObjectKind::Plane};
const char* const kCreateNames[4] = {"Empty", "Cube", "Sphere", "Plane"};

}  // namespace

MainWindow::MainWindow(k::EditorDocument* doc) : doc_(doc) {
    viewport_ = new EditorViewport(doc_, this);
    setCentralWidget(viewport_);
    setDockNestingEnabled(true);

    build_docks();
    build_actions_and_menus();

    gpu_label_ = new QLabel(tr("GPU: starting..."), this);
    selection_label_ = new QLabel(this);
    statusBar()->addWidget(selection_label_, 1);
    statusBar()->addPermanentWidget(gpu_label_);

    viewport_->on_status = [this](const QString& text) { gpu_label_->setText(text); };
    viewport_->on_selection_changed = [this] {
        refresh_explorer_selection();
        refresh_inspector();
        refresh_actions();
        refresh_status();
    };

    play_timer_ = new QTimer(this);
    play_timer_->setInterval(16);
    play_clock_ = new QElapsedTimer();
    connect(play_timer_, &QTimer::timeout, this, [this] {
        const double dt = static_cast<double>(play_clock_->restart()) / 1000.0;
        tick_play(dt);
    });

    resize(1280, 800);
    refresh_all();
    log(tr("K Engine Editor ready. Create objects from the Create menu, click them in the viewport, edit them in the Inspector."));
}

MainWindow::~MainWindow() { delete play_clock_; }

// ------------------------------------------------------------------ building ---

void MainWindow::build_docks() {
    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    auto* explorer = new QDockWidget(tr("Explorer"), this);
    explorer->setWidget(tree_);
    addDockWidget(Qt::LeftDockWidgetArea, explorer);
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        if (!current) {
            doc_->clear_selection();
        } else {
            const auto id = k::Uuid::parse(current->data(0, Qt::UserRole).toString().toStdString());
            if (id) (void)doc_->select(*id);
        }
        refresh_inspector();
        refresh_actions();
        refresh_status();
        viewport_->update();
    });

    auto* inspector = new QDockWidget(tr("Inspector"), this);
    inspector->setWidget(build_inspector());
    addDockWidget(Qt::RightDockWidgetArea, inspector);

    console_ = new QPlainTextEdit(this);
    console_->setReadOnly(true);
    console_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    console_->setMaximumBlockCount(2000);
    auto* console_dock = new QDockWidget(tr("Console"), this);
    console_dock->setWidget(console_);
    addDockWidget(Qt::BottomDockWidgetArea, console_dock);
}

QWidget* MainWindow::build_inspector() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    name_edit_ = new QLineEdit(page);
    form->addRow(tr("Name"), name_edit_);
    connect(name_edit_, &QLineEdit::editingFinished, this, [this] { apply_name(); });

    kind_label_ = new QLabel("-", page);
    form->addRow(tr("Kind"), kind_label_);
    uuid_label_ = new QLabel("-", page);
    uuid_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("UUID"), uuid_label_);

    for (int f = 0; f < 3; ++f) {
        auto* row = new QWidget(page);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        for (int a = 0; a < 3; ++a) {
            auto* spin = new QDoubleSpinBox(row);
            spin->setRange(f == 2 ? k::kInspectorMinScale : -k::kInspectorMaxAbs, k::kInspectorMaxAbs);
            spin->setDecimals(4);
            spin->setSingleStep(f == 1 ? 5.0 : 0.1);
            spin->setKeyboardTracking(false);   // typed values commit on Enter / focus loss, not on every keystroke
            spin_[f][a] = spin;
            layout->addWidget(spin);
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, f, a](double v) { apply_axis(f, a, v); });
            connect(spin, &QDoubleSpinBox::editingFinished, this, [this] { doc_->end_edit_gesture(); });
        }
        form->addRow(tr(kFieldNames[f]), row);
    }
    return page;
}

void MainWindow::build_actions_and_menus() {
    auto make_action = [this](const QString& text, const QKeySequence& key, auto handler) {
        auto* a = new QAction(text, this);
        if (!key.isEmpty()) a->setShortcut(key);
        connect(a, &QAction::triggered, this, [handler] { handler(); });
        return a;
    };

    act_new_ = make_action(tr("&New"), QKeySequence::New, [this] { do_new(); });
    act_open_ = make_action(tr("&Open..."), QKeySequence::Open, [this] { do_open(); });
    act_save_ = make_action(tr("&Save"), QKeySequence::Save, [this] { (void)do_save(); });
    act_save_as_ = make_action(tr("Save &As..."), QKeySequence::SaveAs, [this] { (void)do_save_as(); });
    act_quit_ = make_action(tr("&Quit"), QKeySequence::Quit, [this] { close(); });

    act_undo_ = make_action(tr("Undo"), QKeySequence::Undo, [this] { do_undo(); });
    act_redo_ = make_action(tr("Redo"), QKeySequence::Redo, [this] { do_redo(); });
    act_delete_ = make_action(tr("&Delete"), QKeySequence::Delete, [this] { do_delete(); });

    for (int i = 0; i < 4; ++i) {
        const k::ObjectKind kind = kCreateKinds[i];
        act_create_[i] = make_action(tr(kCreateNames[i]), QKeySequence(), [this, kind] { do_create(kind); });
    }

    act_frame_ = make_action(tr("&Frame All"), QKeySequence(Qt::Key_F), [this] { do_frame_all(); });
    act_play_ = make_action(tr("&Play"), QKeySequence(Qt::Key_F5), [this] { do_play(); });
    act_stop_ = make_action(tr("&Stop"), QKeySequence(Qt::SHIFT | Qt::Key_F5), [this] { do_stop(); });

    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(act_new_);
    file->addAction(act_open_);
    file->addSeparator();
    file->addAction(act_save_);
    file->addAction(act_save_as_);
    file->addSeparator();
    file->addAction(act_quit_);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(act_undo_);
    edit->addAction(act_redo_);
    edit->addSeparator();
    edit->addAction(act_delete_);

    QMenu* create = menuBar()->addMenu(tr("&Create"));
    for (int i = 0; i < 4; ++i) create->addAction(act_create_[i]);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addAction(act_frame_);

    QMenu* play = menuBar()->addMenu(tr("&Play"));
    play->addAction(act_play_);
    play->addAction(act_stop_);

    QToolBar* bar = addToolBar(tr("Main"));
    bar->setMovable(false);
    bar->addAction(act_new_);
    bar->addAction(act_open_);
    bar->addAction(act_save_);
    bar->addSeparator();
    bar->addAction(act_undo_);
    bar->addAction(act_redo_);
    bar->addSeparator();
    for (int i = 0; i < 4; ++i) bar->addAction(act_create_[i]);
    bar->addSeparator();
    bar->addAction(act_play_);
    bar->addAction(act_stop_);
}

// ------------------------------------------------------------------- refresh ---

void MainWindow::refresh_all() {
    refresh_explorer();
    refresh_inspector();
    refresh_actions();
    refresh_status();
    viewport_->update();
}

void MainWindow::refresh_explorer() {
    QSignalBlocker blocker(tree_);
    items_.clear();
    tree_->clear();
    for (const k::ExplorerRow& row : k::build_explorer_rows(doc_->scene())) {
        auto* item = new QTreeWidgetItem();
        item->setText(0, q(k::explorer_label(row)));
        item->setData(0, Qt::UserRole, q(row.id.to_string()));
        const auto parent = items_.find(row.parent);
        if (row.parent.is_nil() || parent == items_.end()) {
            tree_->addTopLevelItem(item);
        } else {
            parent->second->addChild(item);   // rows are in preorder, so the parent item exists already
        }
        items_.emplace(row.id, item);
    }
    tree_->expandAll();
    const auto sel = items_.find(doc_->selected());
    tree_->setCurrentItem(sel == items_.end() ? nullptr : sel->second);
}

void MainWindow::refresh_explorer_selection() {
    QSignalBlocker blocker(tree_);
    const auto sel = items_.find(doc_->selected());
    tree_->setCurrentItem(sel == items_.end() ? nullptr : sel->second);
}

void MainWindow::refresh_inspector() {
    const k::SceneObject* o = doc_->selected_object();
    const bool editable = o != nullptr && !doc_->playing();   // while playing the values are shown read-only
    name_edit_->setEnabled(editable);
    {
        QSignalBlocker b(name_edit_);
        name_edit_->setText(o ? q(o->name) : QString());
    }
    kind_label_->setText(o ? QString::fromUtf8(k::to_string(o->kind)) : QString("-"));
    uuid_label_->setText(o ? q(o->id.to_string()) : QString("-"));
    for (int f = 0; f < 3; ++f) {
        for (int a = 0; a < 3; ++a) {
            spin_[f][a]->setEnabled(editable);
            QSignalBlocker b(spin_[f][a]);
            const double fallback = (f == 2) ? 1.0 : 0.0;
            spin_[f][a]->setValue(o ? k::axis_value(o->transform, kFields[f], a) : fallback);
        }
    }
}

void MainWindow::refresh_actions() {
    const bool playing = doc_->playing();
    act_new_->setEnabled(!playing);
    act_open_->setEnabled(!playing);
    act_undo_->setText(q(doc_->undo_label()));
    act_undo_->setEnabled(doc_->can_undo());
    act_redo_->setText(q(doc_->redo_label()));
    act_redo_->setEnabled(doc_->can_redo());
    act_delete_->setEnabled(!playing && !doc_->selected().is_nil());
    for (int i = 0; i < 4; ++i) act_create_[i]->setEnabled(!playing);
    act_play_->setEnabled(!playing);
    act_stop_->setEnabled(playing);
    setWindowTitle(q(doc_->title()) + " - K Engine");
}

void MainWindow::refresh_status() {
    const k::SceneObject* o = doc_->selected_object();
    selection_label_->setText(o ? tr("Selected: %1").arg(q(o->name)) : tr("Nothing selected"));
}

// ------------------------------------------------------------------- actions ---

void MainWindow::log(const QString& line) {
    console_->appendPlainText(line);
    std::printf("%s\n", line.toLocal8Bit().constData());
    std::fflush(stdout);
}

void MainWindow::report(const k::Status& st, const QString& what) {
    if (st) return;
    const QString text = what + ": " + q(st.error().message) + "  [" + q(st.error().code) + "]";
    log("error: " + text);
    statusBar()->showMessage(text, 6000);
}

bool MainWindow::confirm_discard_changes() {
    if (doc_->playing()) do_stop();
    if (!doc_->is_dirty()) return true;
    const auto answer = QMessageBox::question(this, tr("Unsaved changes"),
                                              tr("\"%1\" has unsaved changes. Save them first?").arg(q(doc_->display_name())),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return do_save();
    return true;
}

void MainWindow::do_new() {
    if (!confirm_discard_changes()) return;
    report(doc_->new_scene(), tr("New scene"));
    viewport_->frame_all();
    refresh_all();
    log(tr("New scene."));
}

bool MainWindow::open_path(const std::filesystem::path& path) {
    const k::Status st = doc_->open(path);
    if (!st) {
        report(st, tr("Open %1").arg(from_path(path)));
        QMessageBox::warning(this, tr("Cannot open scene"), q(st.error().message));
        return false;
    }
    log(tr("Opened %1 (%2 objects).").arg(from_path(path)).arg(static_cast<qulonglong>(doc_->scene().object_count())));
    for (const std::string& w : doc_->warnings()) log("warning: " + q(w));
    viewport_->frame_all();
    refresh_all();
    return true;
}

void MainWindow::do_open() {
    if (!confirm_discard_changes()) return;
    const QString file = QFileDialog::getOpenFileName(this, tr("Open scene"), QString(),
                                                      tr("K Engine scene (*.kscene);;All files (*)"));
    if (file.isEmpty()) return;
    (void)open_path(to_path(file));
}

bool MainWindow::do_save() {
    if (!doc_->has_path()) return do_save_as();
    const k::Status st = doc_->save();
    report(st, tr("Save"));
    if (st) log(tr("Saved %1.").arg(from_path(doc_->path())));
    refresh_actions();
    return static_cast<bool>(st);
}

bool MainWindow::do_save_as() {
    QString file = QFileDialog::getSaveFileName(this, tr("Save scene as"), QString(),
                                                tr("K Engine scene (*.kscene);;All files (*)"));
    if (file.isEmpty()) return false;
    if (QFileInfo(file).suffix().isEmpty()) file += ".kscene";
    const k::Status st = doc_->save_as(to_path(file));
    report(st, tr("Save As"));
    if (st) log(tr("Saved %1.").arg(file));
    refresh_actions();
    return static_cast<bool>(st);
}

void MainWindow::do_create(k::ObjectKind kind) {
    auto created = doc_->create(kind);
    if (!created) {
        report(k::Status(created.error()), tr("Create"));
        return;
    }
    const k::SceneObject* o = doc_->selected_object();
    log(tr("Created %1.").arg(o ? q(o->name) : QString()));
    refresh_all();
}

void MainWindow::do_delete() {
    const k::SceneObject* o = doc_->selected_object();
    const QString name = o ? q(o->name) : QString();
    const k::Status st = doc_->delete_selected();
    report(st, tr("Delete"));
    if (st) log(tr("Deleted %1.").arg(name));
    refresh_all();
}

void MainWindow::do_undo() {
    const QString label = q(doc_->undo_label());
    const k::Status st = doc_->undo();
    report(st, label);
    if (st) log(label + ".");
    refresh_all();
}

void MainWindow::do_redo() {
    const QString label = q(doc_->redo_label());
    const k::Status st = doc_->redo();
    report(st, label);
    if (st) log(label + ".");
    refresh_all();
}

void MainWindow::do_frame_all() { viewport_->frame_all(); }

void MainWindow::do_play() {
    const k::Status st = doc_->play();
    report(st, tr("Play"));
    if (!st) return;
    play_clock_->start();
    play_timer_->start();
    log(tr("Play started (editing is locked; the scene is a copy)."));
    refresh_all();
}

void MainWindow::do_stop() {
    if (!doc_->playing()) return;
    play_timer_->stop();
    const k::Status st = doc_->stop();
    report(st, tr("Stop"));
    log(tr("Play stopped; the authored scene is unchanged."));
    refresh_all();
}

void MainWindow::tick_play(double dt_seconds) {
    if (!doc_->playing()) return;
    k::advance_play_demo(*doc_->play_scene(), dt_seconds);
    refresh_inspector();   // read-only view of the selection follows the running scene
    viewport_->update();
}

void MainWindow::apply_axis(int field, int axis, double value) {
    const k::SceneObject* o = doc_->selected_object();
    if (!o || doc_->playing()) return;
    // Replace exactly one axis of the CURRENT transform: other axes (shown rounded) are never written back.
    const k::Transform t = k::with_axis(o->transform, kFields[field], axis, value);
    if (t == o->transform) return;
    const k::Status st = doc_->set_selected_transform(t, /*merge=*/true);
    report(st, tr("Transform"));
    refresh_actions();
    viewport_->update();
}

void MainWindow::apply_name() {
    const k::SceneObject* o = doc_->selected_object();
    if (!o || doc_->playing()) return;
    const std::string text = name_edit_->text().toStdString();
    if (text == o->name) return;
    if (text.empty()) {   // an empty name is not useful in the Explorer: revert
        refresh_inspector();
        return;
    }
    const k::Status st = doc_->rename_selected(text);
    report(st, tr("Rename"));
    refresh_all();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!confirm_discard_changes()) {
        e->ignore();
        return;
    }
    e->accept();
}

// --------------------------------------------------------------------- smoke ---

int MainWindow::run_smoke_test(const QString& temp_dir) {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const QString& what) {
        ++checks;
        if (!ok) ++failures;
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what.toLocal8Bit().constData());
        std::fflush(stdout);
    };
    auto explorer_count = [this] {
        int n = 0;
        for (QTreeWidgetItemIterator it(tree_); *it; ++it) ++n;
        return n;
    };
    auto select_in_explorer = [this](const k::Uuid& id) {
        const auto it = items_.find(id);
        if (it != items_.end()) tree_->setCurrentItem(it->second);   // fires the same handler a user click does
    };
    auto authored_text = [this] { return k::save_scene_to_string(doc_->editor().scene()).value(); };

    QApplication::processEvents();
    check(viewport_->init_error().empty(), tr("GPU initialized (%1)").arg(q(viewport_->init_error())));
    check(doc_->scene().object_count() == 0 && explorer_count() == 0, tr("starts with an empty scene and an empty Explorer"));
    check(windowTitle().startsWith("Untitled - "), tr("window title is 'Untitled' and clean: %1").arg(windowTitle()));
    check(!act_undo_->isEnabled() && !act_redo_->isEnabled() && !act_delete_->isEnabled(), tr("Undo/Redo/Delete disabled when there is nothing to do"));
    check(!name_edit_->isEnabled() && !spin_[0][0]->isEnabled(), tr("Inspector is disabled without a selection"));

    // Create the three primitives through the same handlers the Create menu uses.
    do_create(k::ObjectKind::Cube);
    do_create(k::ObjectKind::Sphere);
    do_create(k::ObjectKind::Plane);
    check(explorer_count() == 3 && doc_->scene().object_count() == 3, tr("Explorer lists the 3 created objects"));
    check(windowTitle().startsWith("Untitled*"), tr("title shows the unsaved marker: %1").arg(windowTitle()));
    check(name_edit_->text() == "Plane" && name_edit_->isEnabled(), tr("the newly created object is selected in the Inspector"));
    check(act_undo_->isEnabled() && act_undo_->text() == "Undo Create 'Plane'", tr("Undo action reads '%1'").arg(act_undo_->text()));

    // Select the cube in the Explorer; edit it through the Inspector widgets.
    const k::Uuid cube = k::build_explorer_rows(doc_->scene())[0].id;
    select_in_explorer(cube);
    check(doc_->selected() == cube && name_edit_->text() == "Cube" && kind_label_->text() == "cube", tr("Explorer click selects the Cube"));
    spin_[0][0]->setValue(2.5);
    spin_[0][1]->setValue(1.0);
    emit spin_[0][1]->editingFinished();
    check(doc_->selected_object() != nullptr && doc_->selected_object()->transform.position[0] == 2.5 &&
              doc_->selected_object()->transform.position[1] == 1.0,
          tr("Inspector position edit reaches the scene"));
    check(act_undo_->text() == "Undo Transform 'Cube'", tr("Undo action reads '%1'").arg(act_undo_->text()));
    spin_[2][0]->setValue(0.0);   // the scale spin box refuses 0 (its minimum is 0.001)
    check(doc_->selected_object()->transform.scale[0] >= k::kInspectorMinScale, tr("scale can never become 0 from the UI"));
    spin_[2][0]->setValue(1.0);   // restore a visible size
    name_edit_->setText("Hero");
    emit name_edit_->editingFinished();
    check(doc_->selected_object()->name == "Hero", tr("Inspector rename reaches the scene"));
    check(items_.count(cube) != 0 && items_.at(cube)->text(0).startsWith("Hero"), tr("Explorer shows the new name"));

    // Undo the rename and redo it.
    do_undo();
    check(doc_->selected_object()->name == "Cube" && name_edit_->text() == "Cube", tr("Undo reverts the rename in scene, Explorer and Inspector"));
    do_redo();
    check(doc_->selected_object()->name == "Hero", tr("Redo restores it"));

    // Save, change, reopen.
    const std::filesystem::path file = to_path(temp_dir + "/smoke.kscene");
    const std::string saved_text = authored_text();
    report(doc_->save_as(file), tr("Save"));
    refresh_all();
    check(windowTitle().startsWith("smoke.kscene - ") && !doc_->is_dirty(), tr("title shows the file name, clean after save: %1").arg(windowTitle()));
    do_delete();
    check(explorer_count() == 2 && doc_->scene().object_count() == 2, tr("Delete removes the selected object from the Explorer"));
    check(open_path(file), tr("Open reloads the saved file"));
    check(authored_text() == saved_text && explorer_count() == 3, tr("reopened scene equals the saved scene (3 objects)"));
    check(!act_undo_->isEnabled(), tr("a freshly opened document has no undo history"));

    // Play: editing locked, scene copy runs, Stop restores.
    select_in_explorer(cube);
    do_play();
    check(doc_->playing() && windowTitle().contains("[Playing]"), tr("title shows [Playing]"));
    check(!act_new_->isEnabled() && !act_create_[1]->isEnabled() && !act_delete_->isEnabled() && !act_undo_->isEnabled(),
          tr("editing actions are disabled while playing"));
    check(!name_edit_->isEnabled() && !spin_[0][0]->isEnabled(), tr("Inspector is read-only while playing"));
    check(act_stop_->isEnabled() && !act_play_->isEnabled(), tr("Stop enabled, Play disabled"));
    tick_play(1.0);
    check(spin_[1][1]->value() == 45.0, tr("Inspector shows the running scene's rotation (%1)").arg(spin_[1][1]->value()));
    check(authored_text() == saved_text, tr("authored scene is untouched while playing"));
    do_stop();
    check(!doc_->playing() && !windowTitle().contains("[Playing]"), tr("Stop ends Play"));
    check(authored_text() == saved_text && spin_[1][1]->value() == 0.0, tr("after Stop the scene and Inspector are back to the authored values"));
    check(act_new_->isEnabled() && act_create_[1]->isEnabled() && !act_stop_->isEnabled(), tr("editing actions are enabled again"));
    check(!doc_->is_dirty(), tr("Play did not dirty the document"));

    do_frame_all();
    QApplication::processEvents();
    check(viewport_->init_error().empty(), tr("viewport still healthy after the workflow"));

    std::printf("SMOKE: %d checks, %d failed\n", checks, failures);
    std::fflush(stdout);
    return failures;
}

// K Engine — Editor/App/MainWindow.h
//
// The Editor window (M0.5): menus + toolbar, Explorer / Inspector / Console docks and the
// Direct3D viewport. Every action calls EditorDocument (Editor/Core, unit-tested, Qt-free); this
// class only binds widgets to it. No Q_OBJECT / moc is used: connections use lambdas.
#pragma once

#include <filesystem>
#include <unordered_map>

#include <QMainWindow>
#include <QString>

#include "Editor/App/EditorViewport.h"
#include "Editor/Core/EditorDocument.h"

class QAction;
class QCloseEvent;
class QDoubleSpinBox;
class QElapsedTimer;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(k::EditorDocument* doc);
    ~MainWindow() override;

    EditorViewport* viewport() { return viewport_; }

    // Loads a scene file (reports failure in a dialog and the console). Returns success.
    bool open_path(const std::filesystem::path& path);

    // Automation (`k_editor --smoke`): drives the real widgets through the Hello Cube workflow and checks that
    // the UI reflects the document at every step. Prints one line per check; returns the number of failed checks.
    int run_smoke_test(const QString& temp_dir);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void build_actions_and_menus();
    void build_docks();
    QWidget* build_inspector();

    // ---- refresh: document -> widgets ----
    void refresh_all();
    void refresh_explorer();
    void refresh_explorer_selection();
    void refresh_inspector();
    void refresh_actions();
    void refresh_status();

    // ---- user actions (menu / toolbar / shortcuts) ----
    void do_new();
    void do_open();
    bool do_save();
    bool do_save_as();
    void do_create(k::ObjectKind kind);
    void do_delete();
    void do_undo();
    void do_redo();
    void do_play();
    void do_stop();
    void do_frame_all();
    void tick_play(double dt_seconds);
    bool confirm_discard_changes();

    // Inspector edits.
    void apply_axis(int field, int axis, double value);
    void apply_name();

    void log(const QString& line);
    void report(const k::Status& st, const QString& what);

    k::EditorDocument* doc_;
    EditorViewport* viewport_ = nullptr;

    QTreeWidget* tree_ = nullptr;
    std::unordered_map<k::Uuid, QTreeWidgetItem*, k::Uuid::Hash> items_;
    QPlainTextEdit* console_ = nullptr;
    QLineEdit* name_edit_ = nullptr;
    QLabel* kind_label_ = nullptr;
    QLabel* uuid_label_ = nullptr;
    QDoubleSpinBox* spin_[3][3] = {};   // [position|rotation|scale][x|y|z]
    QLabel* gpu_label_ = nullptr;
    QLabel* selection_label_ = nullptr;

    QAction* act_new_ = nullptr;
    QAction* act_open_ = nullptr;
    QAction* act_save_ = nullptr;
    QAction* act_save_as_ = nullptr;
    QAction* act_quit_ = nullptr;
    QAction* act_undo_ = nullptr;
    QAction* act_redo_ = nullptr;
    QAction* act_delete_ = nullptr;
    QAction* act_create_[4] = {};       // Empty, Cube, Sphere, Plane
    QAction* act_frame_ = nullptr;
    QAction* act_play_ = nullptr;
    QAction* act_stop_ = nullptr;

    QTimer* play_timer_ = nullptr;
    QElapsedTimer* play_clock_ = nullptr;
};

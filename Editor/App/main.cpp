// k_editor — the K Engine Editor (M0.5): Qt 6 Widgets UI + Direct3D 11 viewport.
//
//   k_editor [file.kscene]               start the Editor, optionally opening a scene
//   k_editor --demo                      start with the demo scene (ground, cube, sphere, child cube)
//   k_editor --screenshot out.png [--demo]   render once, save the viewport as PNG, exit (automation)
//   k_editor --smoke                     drive the UI through the Hello Cube workflow and exit (automation)
//   k_editor --no-warp                   fail instead of falling back to the WARP software rasterizer
//
// Exit codes: 0 ok, 1 smoke checks failed / screenshot could not be saved, 2 GPU initialization failed
// (automation modes), 3 timed out, 64 bad command line.
#include <cstdio>
#include <cstring>

#include <QApplication>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>

#include "Editor/App/MainWindow.h"
#include "Editor/Core/DemoScene.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QString screenshot_path, file;
    bool demo = false, smoke = false, allow_warp = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshot_path = QString::fromLocal8Bit(argv[++i]);
        else if (std::strcmp(argv[i], "--demo") == 0) demo = true;
        else if (std::strcmp(argv[i], "--smoke") == 0) smoke = true;
        else if (std::strcmp(argv[i], "--no-warp") == 0) allow_warp = false;
        else if (argv[i][0] != '-' && file.isEmpty()) file = QString::fromLocal8Bit(argv[i]);
        else { std::fprintf(stderr, "k_editor: unknown argument '%s'\n", argv[i]); return 64; }
    }

    k::EditorDocument doc;
    MainWindow window(&doc);
    window.viewport()->set_allow_warp(allow_warp);

    if (!smoke) {   // the smoke test needs the empty initial state
        if (!file.isEmpty()) {
            if (!window.open_path(std::filesystem::path(file.toStdU16String()))) return 1;
        } else if (demo) {
            k::Scene scene;
            k::build_demo_scene(scene);
            (void)doc.adopt_scene(std::move(scene));
        }
    }

    window.show();
    QApplication::processEvents();
    window.viewport()->frame_all();
    QApplication::processEvents();

    if (smoke) {
        QTemporaryDir tmp;
        if (!tmp.isValid()) { std::fprintf(stderr, "k_editor: cannot create a temporary directory\n"); return 1; }
        return window.run_smoke_test(tmp.path()) == 0 ? 0 : 1;
    }

    if (!screenshot_path.isEmpty()) {
        if (!window.viewport()->init_error().empty()) {
            std::fprintf(stderr, "k_editor: GPU initialization failed: %s\n", window.viewport()->init_error().c_str());
            return 2;
        }
        window.viewport()->on_capture_finished = [&](bool ok) {
            std::printf(ok ? "screenshot saved: %s\n" : "screenshot FAILED: %s\n", screenshot_path.toLocal8Bit().constData());
            std::fflush(stdout);
            QApplication::exit(ok ? 0 : 1);
        };
        QTimer::singleShot(300, [&] { window.viewport()->request_capture(screenshot_path); });
        QTimer::singleShot(15000, [] { std::fprintf(stderr, "k_editor: timed out\n"); QApplication::exit(3); });
    }
    return app.exec();
}

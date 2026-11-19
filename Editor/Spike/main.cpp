// k_viewport_spike — Qt 6 window with a Direct3D 11 viewport (ADR-0004 spike).
// STATUS: UNVERIFIED — never compiled or run by its author. See README.md.
//
//   k_viewport_spike                      interactive
//   k_viewport_spike --screenshot out.png render once, save the frame, exit
//   k_viewport_spike --no-warp ...        fail instead of falling back to the WARP software rasterizer
//
// Exit codes: 0 ok, 1 screenshot could not be saved, 2 GPU initialization failed,
// 3 timed out waiting for the first frame.
#include <cstdio>
#include <cstring>

#include <QApplication>
#include <QFontDatabase>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "Editor/Spike/ViewportWidget.h"
#include "Engine/Scene/Scene.h"

namespace {

// Demo content built with the verified Scene API.
void build_demo_scene(k::Scene& scene) {
    auto place = [&](k::ObjectHandle h, k::Vec3 p, k::Vec3 sc = {1, 1, 1}, k::Vec3 rot = {0, 0, 0}) {
        k::Transform t;
        t.position = p;
        t.scale = sc;
        t.rotation_euler_deg = rot;
        (void)scene.set_transform(h, t);
    };
    place(scene.create(k::ObjectKind::Plane, "Ground").value(), {0, -0.5, 0}, {8, 1, 8});
    place(scene.create(k::ObjectKind::Cube, "Cube").value(), {-1.5, 0, 0}, {1, 1, 1}, {0, 25, 0});
    place(scene.create(k::ObjectKind::Sphere, "Sphere").value(), {1.5, 0, 0});
    auto group = scene.create(k::ObjectKind::Empty, "Group").value();
    place(group, {0, 0, -2.5});
    place(scene.create(k::ObjectKind::Cube, "Child Cube", group).value(), {0, 0.25, 0}, {0.5, 1.5, 0.5});
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QString screenshot_path;
    bool allow_warp = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshot_path = QString::fromLocal8Bit(argv[++i]);
        else if (std::strcmp(argv[i], "--no-warp") == 0) allow_warp = false;
        else { std::fprintf(stderr, "k_viewport_spike: unknown argument '%s'\n", argv[i]); return 64; }
    }

    k::Scene scene;
    build_demo_scene(scene);

    QWidget window;
    window.setWindowTitle("K Engine viewport spike (Direct3D 11)");
    auto* layout = new QVBoxLayout(&window);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* info = new QLabel("starting...");
    info->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    info->setWordWrap(true);
    auto* viewport = new ViewportWidget(&scene);
    viewport->set_allow_warp(allow_warp);
    layout->addWidget(info);
    layout->addWidget(viewport, 1);

    viewport->on_status = [&](const QString& text) {
        info->setText(text);
        std::printf("%s\n", text.toLocal8Bit().constData());
        std::fflush(stdout);
    };
    viewport->on_capture_finished = [&](bool ok) {
        std::printf(ok ? "screenshot saved: %s\n" : "screenshot FAILED: %s\n", screenshot_path.toLocal8Bit().constData());
        std::fflush(stdout);
        QApplication::exit(ok ? 0 : 1);
    };

    window.resize(960, 640);
    window.show();
    QApplication::processEvents();

    if (!viewport->init_error().empty()) {
        std::fprintf(stderr, "k_viewport_spike: GPU initialization failed: %s\n", viewport->init_error().c_str());
        if (!screenshot_path.isEmpty()) return 2;   // automated mode: fail loudly
    }
    if (!screenshot_path.isEmpty()) {
        QTimer::singleShot(300, [&] { viewport->request_capture(screenshot_path); });
        QTimer::singleShot(15000, [] { std::fprintf(stderr, "k_viewport_spike: timed out\n"); QApplication::exit(3); });
    }
    return app.exec();
}

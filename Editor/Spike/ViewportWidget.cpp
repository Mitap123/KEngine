// STATUS: UNVERIFIED — see ViewportWidget.h.
#include "Editor/Spike/ViewportWidget.h"

#include <algorithm>
#include <cmath>

#include <QImage>
#include <QMouseEvent>
#include <QPaintEngine>
#include <QWheelEvent>

#include "Engine/Rendering/DrawList.h"
#include "Engine/Rendering/Picking.h"

ViewportWidget::ViewportWidget(k::Scene* scene, QWidget* parent) : QWidget(parent), scene_(scene) {
    setAttribute(Qt::WA_NativeWindow);       // own HWND for the swap chain
    setAttribute(Qt::WA_PaintOnScreen);      // Qt must not paint over D3D output
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
}

int ViewportWidget::pixel_width() const { return static_cast<int>(std::lround(width() * devicePixelRatioF())); }
int ViewportWidget::pixel_height() const { return static_cast<int>(std::lround(height() * devicePixelRatioF())); }

void ViewportWidget::set_status(const QString& text) {
    if (on_status) on_status(text);
}

void ViewportWidget::request_capture(const QString& png_path) {
    capture_path_ = png_path;
    update();
}

void ViewportWidget::ensure_initialized() {
    if (init_attempted_) return;
    init_attempted_ = true;
    const k::Status st = renderer_.initialize(reinterpret_cast<void*>(winId()), pixel_width(), pixel_height(),
                                              allow_warp_);
    if (!st) {
        init_error_ = st.error().code + ": " + st.error().message;
        set_status(QString::fromStdString("GPU initialization FAILED: " + init_error_));
        return;
    }
    const k::GpuInfo& g = renderer_.info();
    set_status(QString::fromStdString(g.adapter_name + " | feature level " + g.feature_level + " | " + g.driver_type));
}

void ViewportWidget::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    ensure_initialized();
}

void ViewportWidget::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (renderer_.initialized()) {
        const k::Status st = renderer_.resize(pixel_width(), pixel_height());
        if (!st) set_status(QString::fromStdString("resize failed: " + st.error().message));
        update();
    }
}

void ViewportWidget::paintEvent(QPaintEvent*) {
    ensure_initialized();
    if (renderer_.initialized()) render_frame();
}

void ViewportWidget::render_frame() {
    const int w = pixel_width(), h = pixel_height();
    if (w <= 0 || h <= 0) return;
    const auto items = k::build_draw_list(*scene_, selected_);
    const bool want_capture = !capture_path_.isEmpty();
    k::CapturedImage image;

    QElapsedTimer timer;
    timer.start();
    const k::Status st = renderer_.render(items, camera_.view(), camera_.projection(static_cast<double>(w) / h),
                                          want_capture ? &image : nullptr);
    const double ms = static_cast<double>(timer.nsecsElapsed()) / 1e6;

    if (!st) {
        set_status(QString::fromStdString("render failed: " + st.error().code + ": " + st.error().message));
        if (want_capture) { capture_path_.clear(); if (on_capture_finished) on_capture_finished(false); }
        return;
    }
    const k::GpuInfo& g = renderer_.info();
    QString text = QString::fromStdString(g.adapter_name + " | FL " + g.feature_level + " | " + g.driver_type) +
                   QString(" | frame %1 ms (includes vsync wait)").arg(ms, 0, 'f', 1);
    if (!selected_.is_null()) {
        if (const k::SceneObject* o = scene_->get(selected_)) text += " | selected: " + QString::fromStdString(o->name);
    }
    set_status(text);

    if (want_capture) {
        bool ok = false;
        if (image.width > 0 && image.height > 0) {
            QImage qimg(image.rgba.data(), image.width, image.height, image.width * 4, QImage::Format_RGBA8888);
            ok = qimg.copy().save(capture_path_, "PNG");   // copy(): qimg only borrows image.rgba
        }
        capture_path_.clear();
        if (on_capture_finished) on_capture_finished(ok);
    }
}

void ViewportWidget::mousePressEvent(QMouseEvent* e) {
    last_pos_ = press_pos_ = e->position().toPoint();
    dragged_ = false;
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* e) {
    const QPoint p = e->position().toPoint();
    const QPoint d = p - last_pos_;
    last_pos_ = p;
    if ((p - press_pos_).manhattanLength() > 4) dragged_ = true;
    if (!dragged_ || !renderer_.initialized()) return;
    const double dpr = devicePixelRatioF();
    const bool pan = e->buttons().testFlag(Qt::RightButton) || e->buttons().testFlag(Qt::MiddleButton) ||
                     e->modifiers().testFlag(Qt::ShiftModifier);
    if (pan) {
        const double h = std::max(1, pixel_height());
        camera_.pan(d.x() * dpr / h, d.y() * dpr / h);
    } else if (e->buttons().testFlag(Qt::LeftButton)) {
        camera_.orbit(-d.x() * 0.4, d.y() * 0.4);
    }
    update();
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || dragged_ || !renderer_.initialized()) return;
    // A click without dragging: select the object under the cursor (or clear the selection).
    const double dpr = devicePixelRatioF();
    const auto ray = camera_.screen_ray(e->position().x() * dpr, e->position().y() * dpr, pixel_width(), pixel_height());
    if (!ray.valid) return;
    const auto hit = k::pick(*scene_, ray.ray);
    selected_ = hit ? hit->object : k::kNullObjectHandle;
    update();
}

void ViewportWidget::wheelEvent(QWheelEvent* e) {
    camera_.zoom(std::pow(0.9, e->angleDelta().y() / 120.0));
    update();
}

// STATUS: UNVERIFIED — Qt 6 + Direct3D 11 code never compiled or run by its author.
//
// A QWidget that owns a native window and lets D3D11Renderer draw into it.
// No Q_OBJECT / moc is used on purpose (fewer moving parts for a spike).
// Interaction: left-drag orbit, right/middle-drag or Shift+left-drag pan,
// wheel zoom, left click selects the object under the cursor (CPU picking,
// Engine/Rendering/Picking). Rendering is on demand (only when something changes).
#pragma once

#include <functional>
#include <string>

#include <QElapsedTimer>
#include <QPoint>
#include <QString>
#include <QWidget>

#include "Editor/Spike/D3D11Renderer.h"
#include "Engine/Rendering/OrbitCamera.h"
#include "Engine/Scene/Scene.h"

class QPaintEngine;

class ViewportWidget : public QWidget {
public:
    explicit ViewportWidget(k::Scene* scene, QWidget* parent = nullptr);

    // Null until the renderer initialized successfully.
    const k::GpuInfo* gpu_info() const { return renderer_.initialized() ? &renderer_.info() : nullptr; }
    const std::string& init_error() const { return init_error_; }
    k::ObjectHandle selected() const { return selected_; }

    // Must be called before the widget is first shown. Default: true (WARP software fallback allowed).
    void set_allow_warp(bool allow) { allow_warp_ = allow; }

    // Capture the next rendered frame to a PNG file; on_capture_finished(ok) is called afterwards.
    void request_capture(const QString& png_path);

    std::function<void(const QString&)> on_status;       // one-line status text for the UI
    std::function<void(bool)> on_capture_finished;

    QPaintEngine* paintEngine() const override { return nullptr; }   // we paint natively

protected:
    void showEvent(QShowEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    void ensure_initialized();
    void render_frame();
    void set_status(const QString& text);
    int pixel_width() const;
    int pixel_height() const;

    k::Scene* scene_;
    k::D3D11Renderer renderer_;
    k::OrbitCamera camera_;
    k::ObjectHandle selected_ = k::kNullObjectHandle;
    std::string init_error_;
    bool init_attempted_ = false;
    bool allow_warp_ = true;
    QString capture_path_;
    QPoint last_pos_, press_pos_;
    bool dragged_ = false;
};

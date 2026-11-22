// K Engine — Editor/App/EditorViewport.h
//
// The Editor's 3D viewport: a QWidget with its own native window that D3D11Renderer draws into.
// It shows EditorDocument::scene() (the play copy while playing), highlights the document's
// selection, and turns mouse input into camera moves and CPU picking.
// No Q_OBJECT / moc is used on purpose. Interaction: left-drag orbit, right/middle-drag or
// Shift+left-drag pan, wheel zoom, left click selects (click on empty space clears).
#pragma once

#include <functional>
#include <string>

#include <QPoint>
#include <QString>
#include <QWidget>

#include "Editor/Core/EditorDocument.h"
#include "Editor/Spike/D3D11Renderer.h"
#include "Engine/Rendering/OrbitCamera.h"

class QPaintEngine;

class EditorViewport : public QWidget {
public:
    explicit EditorViewport(k::EditorDocument* doc, QWidget* parent = nullptr);

    // Call before the first show. Default: true (the WARP software rasterizer may be used if no GPU works).
    void set_allow_warp(bool allow) { allow_warp_ = allow; }
    const k::GpuInfo* gpu_info() const { return renderer_.initialized() ? &renderer_.info() : nullptr; }
    const std::string& init_error() const { return init_error_; }

    // Aim the camera at everything in the scene.
    void frame_all();
    // Capture the next rendered frame as PNG; on_capture_finished(ok) is called afterwards.
    void request_capture(const QString& png_path);

    std::function<void(const QString&)> on_status;   // GPU / frame-time line for the status bar
    std::function<void()> on_selection_changed;      // the user picked (or cleared) an object by clicking
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
    int pixel_width() const;
    int pixel_height() const;

    k::EditorDocument* doc_;
    k::D3D11Renderer renderer_;
    k::OrbitCamera camera_;
    std::string init_error_;
    bool init_attempted_ = false;
    bool allow_warp_ = true;
    QString capture_path_;
    QPoint last_pos_, press_pos_;
    bool dragged_ = false;
};

#include "ww/renderer.hpp"

#include <cairo.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace ww {

struct Renderer::Impl {
    cairo_surface_t* surface = nullptr;
    cairo_t* cr = nullptr;
    PangoLayout* layout = nullptr;
    std::unordered_map<std::string, PangoFontDescription*> fonts;
    int widthPx = 0;
    int heightPx = 0;
    float scale = 1.0f;

    ~Impl() {
        for (auto& [key, desc] : fonts) pango_font_description_free(desc);
        if (layout) g_object_unref(layout);
        if (cr) cairo_destroy(cr);
        if (surface) cairo_surface_destroy(surface);
    }

    void setColor(Color c) { cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a); }

    void roundRectPath(Rect r, float radius) {
        const double x = r.x, y = r.y, w = r.w, h = r.h;
        // A rectangle can be empty or even negative (a card on its first frame, an inset() of
        // something tiny). std::clamp(v, lo, hi) with hi < lo is undefined, and libstdc++
        // asserts on it, so keep the upper bound at 0 or more.
        const double maxRadius = std::max(std::min(w, h) / 2.0, 0.0);
        const double rad = std::clamp(static_cast<double>(radius), 0.0, maxRadius);
        if (rad <= 0.0) {
            cairo_rectangle(cr, x, y, w, h);
            return;
        }
        constexpr double pi = 3.14159265358979323846;
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + w - rad, y + rad, rad, -pi / 2, 0);
        cairo_arc(cr, x + w - rad, y + h - rad, rad, 0, pi / 2);
        cairo_arc(cr, x + rad, y + h - rad, rad, pi / 2, pi);
        cairo_arc(cr, x + rad, y + rad, rad, pi, 3 * pi / 2);
        cairo_close_path(cr);
    }

    // Prepares the shared layout with the given text and font.
    void prepareText(std::string_view text, const Font& font) {
        char key[160];
        std::snprintf(key, sizeof key, "%s|%.2f|%d|%d", font.family.c_str(), font.size, font.bold, font.italic);
        auto it = fonts.find(key);
        if (it == fonts.end()) {
            PangoFontDescription* desc = pango_font_description_new();
            pango_font_description_set_family(desc, font.family.c_str());
            pango_font_description_set_absolute_size(desc, static_cast<double>(font.size) * PANGO_SCALE);
            pango_font_description_set_weight(desc, font.bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
            pango_font_description_set_style(desc, font.italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
            it = fonts.emplace(key, desc).first;
        }
        pango_layout_set_font_description(layout, it->second);
        pango_layout_set_text(layout, text.data(), static_cast<int>(text.size()));
    }
};

Renderer::Renderer(const Framebuffer& fb) : impl_(std::make_unique<Impl>()) {
    impl_->widthPx = fb.width;
    impl_->heightPx = fb.height;
    impl_->scale = fb.scale;
    impl_->surface = cairo_image_surface_create_for_data(reinterpret_cast<unsigned char*>(fb.pixels),
                                                         CAIRO_FORMAT_ARGB32, fb.width, fb.height, fb.stride);
    cairo_surface_set_device_scale(impl_->surface, fb.scale, fb.scale);
    impl_->cr = cairo_create(impl_->surface);

    impl_->layout = pango_cairo_create_layout(impl_->cr);
    // Grayscale antialiasing with light hinting: consistent on ARGB buffers.
    cairo_font_options_t* options = cairo_font_options_create();
    cairo_font_options_set_antialias(options, CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_SLIGHT);
    cairo_font_options_set_hint_metrics(options, CAIRO_HINT_METRICS_ON);
    pango_cairo_context_set_font_options(pango_layout_get_context(impl_->layout), options);
    cairo_font_options_destroy(options);
}

Renderer::~Renderer() = default;

float Renderer::width() const { return static_cast<float>(impl_->widthPx) / impl_->scale; }
float Renderer::height() const { return static_cast<float>(impl_->heightPx) / impl_->scale; }
float Renderer::scale() const { return impl_->scale; }

void Renderer::clear(Color color) {
    cairo_save(impl_->cr);
    cairo_set_operator(impl_->cr, CAIRO_OPERATOR_SOURCE);
    impl_->setColor(color);
    cairo_paint(impl_->cr);
    cairo_restore(impl_->cr);
}

void Renderer::fillRect(Rect r, Color color) {
    impl_->setColor(color);
    cairo_rectangle(impl_->cr, r.x, r.y, r.w, r.h);
    cairo_fill(impl_->cr);
}

void Renderer::strokeRect(Rect r, Color color, float lineWidth) {
    impl_->setColor(color);
    cairo_set_line_width(impl_->cr, lineWidth);
    const Rect inner = r.inset(lineWidth / 2.0f);   // stroke is centred on the path: keep it inside r
    cairo_rectangle(impl_->cr, inner.x, inner.y, inner.w, inner.h);
    cairo_stroke(impl_->cr);
}

void Renderer::fillRoundRect(Rect r, float radius, Color color) {
    impl_->setColor(color);
    impl_->roundRectPath(r, radius);
    cairo_fill(impl_->cr);
}

void Renderer::strokeRoundRect(Rect r, float radius, Color color, float lineWidth) {
    impl_->setColor(color);
    cairo_set_line_width(impl_->cr, lineWidth);
    impl_->roundRectPath(r.inset(lineWidth / 2.0f), radius - lineWidth / 2.0f);
    cairo_stroke(impl_->cr);
}

void Renderer::fillRoundRectGradient(Rect r, float radius, Color top, Color bottom) {
    cairo_pattern_t* gradient = cairo_pattern_create_linear(0, r.y, 0, r.y + r.h);
    cairo_pattern_add_color_stop_rgba(gradient, 0, top.r, top.g, top.b, top.a);
    cairo_pattern_add_color_stop_rgba(gradient, 1, bottom.r, bottom.g, bottom.b, bottom.a);
    cairo_set_source(impl_->cr, gradient);
    impl_->roundRectPath(r, radius);
    cairo_fill(impl_->cr);
    cairo_pattern_destroy(gradient);
}

void Renderer::fillCircle(float cx, float cy, float radius, Color color) {
    impl_->setColor(color);
    cairo_new_path(impl_->cr);
    cairo_arc(impl_->cr, cx, cy, radius, 0, 2 * 3.14159265358979323846);
    cairo_fill(impl_->cr);
}

void Renderer::strokeCircle(float cx, float cy, float radius, Color color, float lineWidth) {
    impl_->setColor(color);
    cairo_set_line_width(impl_->cr, lineWidth);
    cairo_new_path(impl_->cr);
    cairo_arc(impl_->cr, cx, cy, radius - lineWidth / 2.0f, 0, 2 * 3.14159265358979323846);
    cairo_stroke(impl_->cr);
}

void Renderer::fillTriangle(float x1, float y1, float x2, float y2, float x3, float y3, Color color) {
    impl_->setColor(color);
    cairo_new_path(impl_->cr);
    cairo_move_to(impl_->cr, x1, y1);
    cairo_line_to(impl_->cr, x2, y2);
    cairo_line_to(impl_->cr, x3, y3);
    cairo_close_path(impl_->cr);
    cairo_fill(impl_->cr);
}

void Renderer::line(float x1, float y1, float x2, float y2, Color color, float lineWidth) {
    impl_->setColor(color);
    cairo_set_line_width(impl_->cr, lineWidth);
    cairo_set_line_cap(impl_->cr, CAIRO_LINE_CAP_ROUND);
    cairo_new_path(impl_->cr);
    cairo_move_to(impl_->cr, x1, y1);
    cairo_line_to(impl_->cr, x2, y2);
    cairo_stroke(impl_->cr);
}

void Renderer::shadow(Rect r, float radius, float spread, Color color) {
    constexpr int layers = 8;
    for (int i = layers; i >= 1; --i) {
        const float t = static_cast<float>(i) / layers;   // 1 = outermost
        const float grow = spread * t;
        const float alpha = color.a * (1.0f - t) * 0.35f;
        fillRoundRect({r.x - grow, r.y - grow + spread * 0.35f, r.w + 2 * grow, r.h + 2 * grow}, radius + grow,
                      color.withAlpha(alpha));
    }
}

void Renderer::drawText(float x, float y, std::string_view text, const Font& font, Color color) {
    if (text.empty()) return;
    impl_->prepareText(text, font);
    impl_->setColor(color);
    cairo_move_to(impl_->cr, x, y);
    pango_cairo_show_layout(impl_->cr, impl_->layout);
}

TextSize Renderer::measureText(std::string_view text, const Font& font) {
    impl_->prepareText(text, font);
    PangoRectangle logical;
    pango_layout_get_extents(impl_->layout, nullptr, &logical);
    return {static_cast<float>(logical.width) / PANGO_SCALE, static_cast<float>(logical.height) / PANGO_SCALE};
}

float Renderer::caretX(std::string_view text, const Font& font, size_t byteIndex) {
    impl_->prepareText(text, font);
    PangoRectangle pos;
    pango_layout_index_to_pos(impl_->layout, static_cast<int>(std::min(byteIndex, text.size())), &pos);
    return static_cast<float>(pos.x) / PANGO_SCALE;
}

size_t Renderer::indexAtX(std::string_view text, const Font& font, float x) {
    if (text.empty()) return 0;
    impl_->prepareText(text, font);
    int index = 0, trailing = 0;
    pango_layout_xy_to_index(impl_->layout, static_cast<int>(x * PANGO_SCALE), 0, &index, &trailing);
    // `trailing` > 0 means the click was on the right half of the character at `index`.
    const char* base = pango_layout_get_text(impl_->layout);
    const char* p = g_utf8_offset_to_pointer(base + index, trailing);
    return static_cast<size_t>(p - base);
}

void Renderer::pushClip(Rect r) {
    cairo_save(impl_->cr);
    cairo_rectangle(impl_->cr, r.x, r.y, r.w, r.h);
    cairo_clip(impl_->cr);
}

void Renderer::popClip() { cairo_restore(impl_->cr); }

OffscreenSurface::OffscreenSurface(int logicalWidth, int logicalHeight, float scale) {
    fb_.width = static_cast<int>(std::lround(static_cast<float>(logicalWidth) * scale));
    fb_.height = static_cast<int>(std::lround(static_cast<float>(logicalHeight) * scale));
    fb_.stride = fb_.width * 4;
    fb_.scale = scale;
    pixels_.assign(static_cast<size_t>(fb_.width) * static_cast<size_t>(fb_.height), 0xff000000u);
    fb_.pixels = pixels_.data();
}

Framebuffer OffscreenSurface::framebuffer() { return fb_; }

bool Renderer::savePng(const std::string& path) const {
    cairo_surface_flush(impl_->surface);
    return cairo_surface_write_to_png(impl_->surface, path.c_str()) == CAIRO_STATUS_SUCCESS;
}

} // namespace ww

#include "codeos_litehtml.h"
#include "litehtml.h"
#include "jengine/jengine.h"

#include <memory>
#include <cstdio>
#include <string>
#include <vector>
#include <map>

class CodeOSContainer : public litehtml::document_container {
public:
CodeOSContainer(const codeos_render_callbacks_t *callbacks)
    : m_callbacks(callbacks), m_script_exec(0), m_width(0), m_height(0) {

    ~CodeOSContainer() {
        for (auto &font : m_fonts) {
            if (font.second && m_callbacks.font_free) {
                m_callbacks.font_free(m_callbacks.user_data, font.second);
            }
        }
    }

    int execute_script(const char *js_source, char *output, int max_out) {
        if (m_script_exec) {
            return m_script_exec(m_callbacks->user_data, js_source, output, max_out);
        }
        if (!output || max_out <= 0) return -1;
        jengine_value result = jengine_eval(js_source);
        if (result.type == JENGINE_NUMBER) {
            return std::snprintf(output, (size_t)max_out, "%lld", (long long)result.num_val);
        }
        if (result.type == JENGINE_UNDEFINED) {
            return std::snprintf(output, (size_t)max_out, "undefined");
        }
        output[0] = 0;
        return -1;
    }

    void set_dimensions(int width, int height) {
        m_width = width;
        m_height = height;
    }

    virtual std::shared_ptr<litehtml::font> create_font(const char *faceName, int size, int weight, litehtml::font_style italic, unsigned int decoration, litehtml::font_metrics *fm) override {
        const char *family = faceName ? faceName : "Liberation Sans";
        int weight_val = weight;
        int style = (italic == litehtml::font_style_italic) ? 1 : 0;

        void *font = nullptr;
        if (m_callbacks.font_load) {
            font = m_callbacks.font_load(m_callbacks.user_data, family, size, weight_val, style);
        }

        if (font) {
            m_fonts[font] = font;
            if (fm) {
                fm->ascent = size;
                fm->descent = size / 4;
                fm->height = fm->ascent + fm->descent;
                fm->x_height = size / 2;
            }
        }

        auto result = std::make_shared<FontWrapper>(font, size);
        return result;
    }

    virtual void draw_text(const char *text, const position &pos, const std::shared_ptr<litehtml::font> &font, web_color color, const std::shared_ptr<litehtml::css_borders> &borders) override {
        if (!m_callbacks.text_draw || !font) return;

        FontWrapper *fw = static_cast<FontWrapper*>(font.get());
        if (!fw->handle) return;

        uint32_t color_val = (color.red << 24) | (color.green << 16) | (color.blue << 8) | color.alpha;
        m_callbacks.text_draw(m_callbacks.user_data, fw->handle, pos.x, pos.y, text, (int)strlen(text), color_val);
    }

    virtual void draw_rect(const position &pos, const std::shared_ptr<litehtml::css_borders> &borders, const litehtml::web_color &color) override {
        if (!m_callbacks.draw_rect) return;
        uint32_t color_val = (color.red << 24) | (color.green << 16) | (color.blue << 8) | color.alpha;
        m_callbacks.draw_rect(m_callbacks.user_data, pos.x, pos.y, pos.width, pos.height, color_val);
    }

    virtual void draw_background(const background_paint &bg) override {
        if (!m_callbacks.draw_rect) return;

        if (bg.color.alpha > 0) {
            uint32_t color_val = (bg.color.red << 24) | (bg.color.green << 16) | (bg.color.blue << 8) | bg.color.alpha;
            m_callbacks.draw_rect(m_callbacks.user_data, (int)bg.border_box.x, (int)bg.border_box.y,
                                  (int)bg.border_box.width, (int)bg.border_box.height, color_val);
        }

        if (!bg.image.empty() && m_callbacks.image_load) {
            int w = 0, h = 0;
            m_callbacks.image_load(m_callbacks.user_data, bg.image.c_str(), &w, &h);
            if (w > 0 && h > 0 && m_callbacks.image_draw) {
                m_callbacks.image_draw(m_callbacks.user_data, nullptr,
                                     (int)bg.border_box.x, (int)bg.border_box.y,
                                     (int)bg.border_box.width, (int)bg.border_box.height);
            }
        }
    }

    virtual void draw_borders(const litehtml::border_box &borders, const litehtml::borders &brd) override {
        if (!m_callbacks.draw_rect) return;

        struct Side { int x, y, w, h; uint32_t color; };

        std::vector<Side> sides;

        if (brd.top.width > 0) {
            sides.push_back({borders.left, borders.top, (int)borders.width, (int)brd.top.width,
                           (brd.top.color.red << 24) | (brd.top.color.green << 16) | (brd.top.color.blue << 8) | brd.top.color.alpha});
        }
        if (brd.bottom.width > 0) {
            sides.push_back({borders.left, borders.bottom - (int)brd.bottom.width, (int)borders.width, (int)brd.bottom.width,
                           (brd.bottom.color.red << 24) | (brd.bottom.color.green << 16) | (brd.bottom.color.blue << 8) | brd.bottom.color.alpha});
        }
        if (brd.left.width > 0) {
            sides.push_back({borders.left, borders.top, (int)brd.left.width, (int)borders.height,
                           (brd.left.color.red << 24) | (brd.left.color.green << 16) | (brd.left.color.blue << 8) | brd.left.color.alpha});
        }
        if (brd.right.width > 0) {
            sides.push_back({borders.right - (int)brd.right.width, borders.top, (int)brd.right.width, (int)borders.height,
                           (brd.right.color.red << 24) | (brd.right.color.green << 16) | (brd.right.color.blue << 8) | brd.right.color.alpha});
        }

        for (const auto &s : sides) {
            m_callbacks.draw_rect(m_callbacks.user_data, s.x, s.y, s.w, s.h, s.color);
        }
    }

    virtual void get_media_features(litehtml::media_features &features) const override {
        features.width = m_width;
        features.height = m_height;
        features.device_width = m_width;
        features.device_height = m_height;
        features.color_bits = 32;
        features.monochrome = 0;
    }

    virtual void get_client_rect(litehtml::position &client) const override {
        client.x = 0;
        client.y = 0;
        client.width = m_width;
        client.height = m_height;
    }

    virtual std::string get_default_font_name() const override {
        return "Liberation Sans";
    }

    virtual int get_default_font_size() const override {
        return 16;
    }

    virtual void link(const std::string &url, const std::string &rel) override {
        (void)url;
        (void)rel;
    }

    virtual void import_stylesheet(const std::string &url, const std::string &baseurl, std::string &out) override {
        out.clear();
    }

    virtual void on_anchor_click(const std::string &url, const std::shared_ptr<litehtml::element> &el) override {
        (void)url;
        (void)el;
    }

    virtual void set_cursor(const std::string &cursor) override {
        (void)cursor;
    }

    virtual void set_clip(const position &pos, const std::shared_ptr<litehtml::element> &el) override {
        (void)pos;
        (void)el;
    }

    virtual void del_clip() override {}

    virtual void get_image_size(const std::string &src, litehtml::size &sz) override {
        if (m_callbacks.image_load) {
            int w = 0, h = 0;
            m_callbacks.image_load(m_callbacks.user_data, src.c_str(), &w, &h);
            sz.width = w;
            sz.height = h;
        }
    }

    virtual void draw_list_marker(const position &pos, const std::string &marker_type) override {
        (void)pos;
        (void)marker_type;
    }

    void draw_element(litehtml::element *el) {
        if (!el) return;
        el->draw(0, 0, 0, 0);
    }

    int get_height() const { return m_height; }
    int get_width() const { return m_width; }

private:
    struct FontWrapper {
        void *handle;
        int size;
        FontWrapper(void *h, int s) : handle(h), size(s) {}
    };

    const codeos_render_callbacks_t *m_callbacks;
    int m_script_exec;
    int m_width;
    int m_height;
    std::map<void*, void*> m_fonts;
};

struct codeos_litehtml_context {
    std::unique_ptr<CodeOSContainer> container;
    std::shared_ptr<litehtml::document> document;
};

codeos_litehtml_context_t *codeos_litehtml_create(const codeos_render_callbacks_t *callbacks) {
    if (!callbacks) return nullptr;

    auto ctx = new codeos_litehtml_context_t();
    ctx->container = std::make_unique<CodeOSContainer>(callbacks);
    return ctx;
}

void codeos_litehtml_destroy(codeos_litehtml_context_t *ctx) {
    if (ctx) {
        ctx->document.reset();
        ctx->container.reset();
        delete ctx;
    }
}

int codeos_litehtml_load_html(codeos_litehtml_context_t *ctx, const char *html, int len, int width) {
    if (!ctx || !ctx->container || !html || len <= 0) return -1;

    ctx->container->set_dimensions(width, 0);
    ctx->document = litehtml::document::createFromString(
        std::string(html, len),
        ctx->container.get()
    );

    if (!ctx->document) return -1;

    ctx->document->render(width);
    return 0;
}

void codeos_litehtml_render(codeos_litehtml_context_t *ctx, int x, int y) {
    if (!ctx || !ctx->document) return;

    ctx->document->draw((int)x, (int)y, 0, 0, ctx->container.get());
}

int codeos_litehtml_get_height(codeos_litehtml_context_t *ctx) {
    if (!ctx || !ctx->container) return 0;
    return ctx->container->get_height();
}

int codeos_litehtml_get_width(codeos_litehtml_context_t *ctx) {
    if (!ctx || !ctx->container) return 0;
    return ctx->container->get_width();
}

void codeos_litehtml_scroll(codeos_litehtml_context_t *ctx, int y) {
    if (!ctx || !ctx->document) return;
    ctx->document->media_changed();
}

int codeos_litehtml_on_click(codeos_litehtml_context_t *ctx, int x, int y) {
    if (!ctx || !ctx->document) return 0;
    return ctx->document->on_mouse_down(x, y, 1) ? 1 : 0;
}

int codeos_litehtml_on_scroll(codeos_litehtml_context_t *ctx, int dy) {
    if (!ctx || !ctx->document) return 0;
    return 1;
}

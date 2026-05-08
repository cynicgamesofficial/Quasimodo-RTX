#include "ui_rmlui.h"

#include <cstddef>

extern "C" {
typedef unsigned char byte;
typedef int qhandle_t;

typedef struct cvar_s {
    char *name;
    char *string;
    char *latched_string;
    int flags;
    int modified;
    float value;
    struct cvar_s *next;
    int integer;
} cvar_t;

qhandle_t R_RegisterImage(const char *name, int type, int flags);
qhandle_t R_RegisterRawImage(const char *name, int width, int height, byte *pic, int type, int flags);
void R_UnregisterImage(qhandle_t handle);
bool R_GetPicSize(int *w, int *h, qhandle_t pic);
void R_DrawStretchPicUV_RTX(int x, int y, int w, int h, float s1, float t1, float s2, float t2, unsigned color, qhandle_t pic);
void *Z_Malloc(size_t size);
void Z_Free(void *ptr);
void Com_LPrintf(int type, const char *fmt, ...);
}

#define IT_PIC       0
#define IF_PERMANENT 1
#define IF_SRGB      512
#define PRINT_DEVELOPER 2
#define PRINT_WARNING 3

#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/Types.h>
#include <RmlUi/Core/Vertex.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// TODO(rmlui): Replace this compatibility path with a refresh-side triangle
// overlay pipeline once the UI is stable. It is deliberately isolated here so
// menu/HUD work can proceed while the exact Vulkan mesh path is added.
#define Q2RML_USE_STRETCH_PIC_APPROXIMATION 1

namespace {

struct Q2RmlGeometry {
    std::vector<Rml::Vertex> vertices;
    std::vector<int> indices;
};

struct DrawBounds {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    float s1 = 0.0f;
    float t1 = 0.0f;
    float s2 = 1.0f;
    float t2 = 1.0f;
    unsigned color = 0xffffffffu;
};

static bool rewrite_coverage_texture_to_straight_alpha(byte *rgba, Rml::Vector2i dimensions)
{
    const size_t pixel_count = static_cast<size_t>(dimensions.x) * static_cast<size_t>(dimensions.y);
    if (!rgba || pixel_count == 0) {
        return false;
    }

    for (size_t i = 0; i < pixel_count; ++i) {
        const size_t base = i * 4;
        const byte red = rgba[base + 0];
        const byte green = rgba[base + 1];
        const byte blue = rgba[base + 2];
        const byte alpha = rgba[base + 3];
        if (red != green || red != blue || red != alpha) {
            return false;
        }
    }

    for (size_t i = 0; i < pixel_count; ++i) {
        const size_t base = i * 4;
        const byte coverage = rgba[base + 3];
        rgba[base + 0] = 255;
        rgba[base + 1] = 255;
        rgba[base + 2] = 255;
        rgba[base + 3] = coverage;
    }

    return true;
}

static unsigned pack_color(const Rml::ColourbPremultiplied& color)
{
    unsigned red = static_cast<unsigned>(color.red);
    unsigned green = static_cast<unsigned>(color.green);
    unsigned blue = static_cast<unsigned>(color.blue);
    const unsigned alpha = static_cast<unsigned>(color.alpha);

    if (alpha > 0 && alpha < 255) {
        red = std::min(255u, (red * 255u + alpha / 2u) / alpha);
        green = std::min(255u, (green * 255u + alpha / 2u) / alpha);
        blue = std::min(255u, (blue * 255u + alpha / 2u) / alpha);
    }

    return red
        | (green << 8)
        | (blue << 16)
        | (alpha << 24);
}

static int iround(float value)
{
    return static_cast<int>(std::lround(value));
}

static void debug_log(const char *fmt, ...)
{
    if (!ui_rmlui_debug || !ui_rmlui_debug->integer) {
        return;
    }

    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Com_LPrintf(PRINT_DEVELOPER, "%s", buffer);
}

static bool add_vertex_bounds(DrawBounds& bounds, const Rml::Vertex& vertex, bool first, const Rml::Vector2f& translation)
{
    const float x = vertex.position.x + translation.x;
    const float y = vertex.position.y + translation.y;

    if (first) {
        bounds.x1 = bounds.x2 = x;
        bounds.y1 = bounds.y2 = y;
        bounds.s1 = bounds.s2 = vertex.tex_coord.x;
        bounds.t1 = bounds.t2 = vertex.tex_coord.y;
        bounds.color = pack_color(vertex.colour);
        return true;
    }

    bounds.x1 = std::min(bounds.x1, x);
    bounds.y1 = std::min(bounds.y1, y);
    bounds.x2 = std::max(bounds.x2, x);
    bounds.y2 = std::max(bounds.y2, y);
    bounds.s1 = std::min(bounds.s1, vertex.tex_coord.x);
    bounds.t1 = std::min(bounds.t1, vertex.tex_coord.y);
    bounds.s2 = std::max(bounds.s2, vertex.tex_coord.x);
    bounds.t2 = std::max(bounds.t2, vertex.tex_coord.y);
    return true;
}

class Q2RmlRenderInterface final : public Rml::RenderInterface {
public:
    Q2RmlRenderInterface()
    {
        byte *white = static_cast<byte *>(Z_Malloc(4));
        white[0] = 255;
        white[1] = 255;
        white[2] = 255;
        white[3] = 255;
        white_texture = R_RegisterRawImage("ui/rml/white", 1, 1, white, IT_PIC, IF_PERMANENT | IF_SRGB);
        if (!white_texture) {
            Z_Free(white);
        }
    }

    ~Q2RmlRenderInterface() override
    {
        for (qhandle_t texture : owned_textures) {
            if (texture && texture != white_texture) {
                R_UnregisterImage(texture);
            }
        }
        owned_textures.clear();
        if (white_texture) {
            R_UnregisterImage(white_texture);
            white_texture = 0;
        }
    }

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override
    {
        auto *geometry = new Q2RmlGeometry;
        geometry->vertices.assign(vertices.begin(), vertices.end());
        geometry->indices.assign(indices.begin(), indices.end());
        return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
    }

    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override
    {
#if Q2RML_USE_STRETCH_PIC_APPROXIMATION
        auto *geometry = reinterpret_cast<Q2RmlGeometry *>(handle);
        if (!geometry || geometry->vertices.empty() || geometry->indices.empty()) {
            return;
        }

        const qhandle_t pic = texture ? static_cast<qhandle_t>(texture) : white_texture;
        size_t i = 0;
        while (i + 2 < geometry->indices.size()) {
            if (i + 5 < geometry->indices.size() && render_quad_candidate(*geometry, i, translation, pic)) {
                i += 6;
                continue;
            }

            DrawBounds bounds;
            if (bounds_for_range(*geometry, i, 3, translation, bounds)) {
                draw_bounds(bounds, pic);
            }
            i += 3;
        }
#endif
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override
    {
        delete reinterpret_cast<Q2RmlGeometry *>(handle);
    }

    void EnableScissorRegion(bool enable) override
    {
        clip_enabled = enable;
    }

    void SetScissorRegion(Rml::Rectanglei region) override
    {
        clip_x = region.Left();
        clip_y = region.Top();
        clip_w = region.Width();
        clip_h = region.Height();
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override
    {
        qhandle_t texture = R_RegisterImage(source.c_str(), IT_PIC, IF_SRGB);
        int width = 0;
        int height = 0;
        if (!texture) {
            Com_LPrintf(PRINT_WARNING, "RmlUi: failed to load texture %s\n", source.c_str());
            dimensions = Rml::Vector2i(1, 1);
            return static_cast<Rml::TextureHandle>(white_texture);
        }

        R_GetPicSize(&width, &height, texture);
        dimensions = Rml::Vector2i(std::max(1, width), std::max(1, height));
        owned_textures.push_back(texture);
        debug_log("RmlUi: loaded texture %s (%dx%d)\n", source.c_str(), dimensions.x, dimensions.y);
        return static_cast<Rml::TextureHandle>(texture);
    }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override
    {
        if (dimensions.x <= 0 || dimensions.y <= 0 || source.size() == 0) {
            dimensions = Rml::Vector2i(1, 1);
            return static_cast<Rml::TextureHandle>(white_texture);
        }

        std::string name = "ui/rml/generated_";
        name += std::to_string(next_texture++);

        byte *rgba = static_cast<byte *>(Z_Malloc(static_cast<size_t>(dimensions.x) * static_cast<size_t>(dimensions.y) * 4));
        memcpy(rgba, source.data(), static_cast<size_t>(source.size()));
    const bool converted_coverage = rewrite_coverage_texture_to_straight_alpha(rgba, dimensions);
        // Font atlases contain linear coverage data, NOT sRGB-encoded colour.
        // Registering as IF_SRGB causes the hardware sampler to apply the sRGB
        // gamma-decode curve (pow(x, 2.4)) to the coverage values, which
        // compresses partial-alpha edges toward 0 and makes anti-aliased text
        // nearly invisible at rest.  Do NOT use IF_SRGB here.
        qhandle_t texture = R_RegisterRawImage(name.c_str(), dimensions.x, dimensions.y, rgba, IT_PIC, IF_PERMANENT);
        if (!texture) {
            Z_Free(rgba);
            Com_LPrintf(PRINT_WARNING, "RmlUi: failed to generate texture %s\n", name.c_str());
            return static_cast<Rml::TextureHandle>(white_texture);
        }

        owned_textures.push_back(texture);
        debug_log("RmlUi: generated texture %s (%dx%d, %zu bytes, format=%s)\n",
                  name.c_str(),
                  dimensions.x,
                  dimensions.y,
                  static_cast<size_t>(source.size()),
                  converted_coverage ? "coverage-alpha" : "rgba8");
        return static_cast<Rml::TextureHandle>(texture);
    }

    void ReleaseTexture(Rml::TextureHandle texture) override
    {
        const qhandle_t handle = static_cast<qhandle_t>(texture);
        if (!handle || handle == white_texture) {
            return;
        }

        auto it = std::find(owned_textures.begin(), owned_textures.end(), handle);
        if (it != owned_textures.end()) {
            owned_textures.erase(it);
        }
        R_UnregisterImage(handle);
    }

private:
    bool bounds_for_range(const Q2RmlGeometry& geometry, size_t first_index, size_t count, const Rml::Vector2f& translation, DrawBounds& bounds) const
    {
        bool first = true;
        for (size_t n = 0; n < count; ++n) {
            const size_t index_pos = first_index + n;
            if (index_pos >= geometry.indices.size()) {
                return false;
            }

            const int vertex_index = geometry.indices[index_pos];
            if (vertex_index < 0 || static_cast<size_t>(vertex_index) >= geometry.vertices.size()) {
                return false;
            }

            add_vertex_bounds(bounds, geometry.vertices[vertex_index], first, translation);
            first = false;
        }
        return true;
    }

    bool render_quad_candidate(const Q2RmlGeometry& geometry, size_t first_index, const Rml::Vector2f& translation, qhandle_t pic) const
    {
        int unique[6] = {};
        int unique_count = 0;
        for (size_t n = 0; n < 6; ++n) {
            const int vertex_index = geometry.indices[first_index + n];
            if (vertex_index < 0 || static_cast<size_t>(vertex_index) >= geometry.vertices.size()) {
                return false;
            }

            bool seen = false;
            for (int u = 0; u < unique_count; ++u) {
                seen = seen || unique[u] == vertex_index;
            }
            if (!seen) {
                unique[unique_count++] = vertex_index;
            }
        }

        if (unique_count != 4) {
            return false;
        }

        DrawBounds bounds;
        bool first = true;
        for (int n = 0; n < unique_count; ++n) {
            add_vertex_bounds(bounds, geometry.vertices[unique[n]], first, translation);
            first = false;
        }
        draw_bounds(bounds, pic);
        return true;
    }

    void draw_bounds(DrawBounds bounds, qhandle_t pic) const
    {
        if (((bounds.color >> 24) & 0xffu) == 0) {
            return;
        }

        float x1 = bounds.x1;
        float y1 = bounds.y1;
        float x2 = bounds.x2;
        float y2 = bounds.y2;
        float s1 = bounds.s1;
        float t1 = bounds.t1;
        float s2 = bounds.s2;
        float t2 = bounds.t2;

        if (clip_enabled) {
            const float cx1 = static_cast<float>(clip_x);
            const float cy1 = static_cast<float>(clip_y);
            const float cx2 = static_cast<float>(clip_x + clip_w);
            const float cy2 = static_cast<float>(clip_y + clip_h);

            if (x1 >= cx2 || x2 <= cx1 || y1 >= cy2 || y2 <= cy1) {
                return;
            }

            const float width = std::max(0.0001f, x2 - x1);
            const float height = std::max(0.0001f, y2 - y1);

            if (x1 < cx1) {
                const float f = (cx1 - x1) / width;
                s1 += f * (s2 - s1);
                x1 = cx1;
            }
            if (x2 > cx2) {
                const float f = (x2 - cx2) / width;
                s2 -= f * (s2 - s1);
                x2 = cx2;
            }
            if (y1 < cy1) {
                const float f = (cy1 - y1) / height;
                t1 += f * (t2 - t1);
                y1 = cy1;
            }
            if (y2 > cy2) {
                const float f = (y2 - cy2) / height;
                t2 -= f * (t2 - t1);
                y2 = cy2;
            }
        }

        const int x = iround(x1);
        const int y = iround(y1);
        const int w = std::max(1, iround(x2 - x1));
        const int h = std::max(1, iround(y2 - y1));
        R_DrawStretchPicUV_RTX(x, y, w, h, s1, t1, s2, t2, bounds.color, pic);
    }

    qhandle_t white_texture = 0;
    std::vector<qhandle_t> owned_textures;
    int next_texture = 1;
    bool clip_enabled = false;
    int clip_x = 0;
    int clip_y = 0;
    int clip_w = 0;
    int clip_h = 0;
};

} // namespace

Rml::RenderInterface *UI_Rml_CreateRenderInterface(void)
{
    return new Q2RmlRenderInterface;
}

void UI_Rml_DestroyRenderInterface(Rml::RenderInterface *renderer)
{
    delete renderer;
}

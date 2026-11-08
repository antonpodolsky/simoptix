// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_raster.h"

#include <windows.h>
#include <dwrite_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

#include "shared/log.h"

namespace {

    // DirectWrite's shared factory, created once for the process (null if this Windows lacks DirectWrite 2,
    // Windows 8.1, which the grayscale glyph analysis needs).
    IDWriteFactory2* Factory() {
        static IDWriteFactory2* factory = [] {
            IDWriteFactory2* f = nullptr;
            if (FAILED(DWriteCreateFactory(
                    DWRITE_FACTORY_TYPE_SHARED,
                    __uuidof(IDWriteFactory2),
                    reinterpret_cast<IUnknown**>(&f)
                ))) {
                CoreLog("G-Meter Overlay: DirectWrite 2 isn't available: meter labels won't be drawn");
                f = nullptr;
            }

            return f;
        }();

        return factory;
    }

    // Collects the glyph runs a text layout draws, with their baseline origins, for rasterising ourselves.
    class RunCollector : public IDWriteTextRenderer {
    public:
        struct Run {
            IDWriteFontFace* face;
            float emSize;
            std::vector<UINT16> glyphs;
            std::vector<FLOAT> advances;
            std::vector<DWRITE_GLYPH_OFFSET> offsets;
            BOOL sideways;
            UINT32 bidiLevel;
            float x, y;  // baseline origin in the layout, pixels
        };

        std::vector<Run> runs;

        ~RunCollector() {
            for (Run& r : runs) {
                r.face->Release();
            }
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
            if (id == __uuidof(IUnknown) || id == __uuidof(IDWritePixelSnapping)
                || id == __uuidof(IDWriteTextRenderer)) {
                *out = static_cast<IDWriteTextRenderer*>(this);
                return S_OK;
            }

            *out = nullptr;
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override { return 1; }  // on the stack: never freed through COM
        ULONG STDMETHODCALLTYPE Release() override { return 1; }
        HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* disabled) override {
            *disabled = TRUE;  // exact positions: the label code places text to a fraction of a pixel
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* m) override {
            *m = {1, 0, 0, 1, 0, 0};
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* ppd) override {
            *ppd = 1;  // sizes are in pixels
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE DrawGlyphRun(
            void*,
            FLOAT x,
            FLOAT y,
            DWRITE_MEASURING_MODE,
            const DWRITE_GLYPH_RUN* g,
            const DWRITE_GLYPH_RUN_DESCRIPTION*,
            IUnknown*
        ) override {
            Run r;
            r.face = g->fontFace;
            r.face->AddRef();
            r.emSize = g->fontEmSize;
            r.glyphs.assign(g->glyphIndices, g->glyphIndices + g->glyphCount);
            r.advances.assign(g->glyphAdvances, g->glyphAdvances + g->glyphCount);
            if (g->glyphOffsets) {
                r.offsets.assign(g->glyphOffsets, g->glyphOffsets + g->glyphCount);
            } else {
                r.offsets.assign(g->glyphCount, DWRITE_GLYPH_OFFSET{0, 0});
            }

            r.sideways = g->isSideways;
            r.bidiLevel = g->bidiLevel;
            r.x = x, r.y = y;
            runs.push_back(std::move(r));
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE
        DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) override {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE
        DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE
        DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override {
            return S_OK;
        }
    };

    // What one glyph run rasterised to: its pixel rectangle and coverage.
    struct Piece {
        RECT r;
        std::vector<uint8_t> a;
    };

    // Lays the text out in one line and collects its glyph runs. Returns the baseline (pixels below the
    // layout's top) and the advance (pixels, trailing whitespace included).
    bool LayOut(
        IDWriteFactory2* f,
        const wchar_t* text,
        UINT32 length,
        const TextStyle& style,
        RunCollector* collector,
        float* baseline,
        float* advance
    ) {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        Microsoft::WRL::ComPtr<IDWriteTypography> typography;
        Microsoft::WRL::ComPtr<IDWriteTextLayout1> layout1;
        bool ok = SUCCEEDED(f->CreateTextFormat(
                      style.family,
                      nullptr,
                      DWRITE_FONT_WEIGHT(style.weight),
                      DWRITE_FONT_STYLE_NORMAL,
                      DWRITE_FONT_STRETCH_NORMAL,
                      style.size,
                      L"en-us",
                      &format
                  ))
                  && SUCCEEDED(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP))
                  && SUCCEEDED(f->CreateTextLayout(text, length, format.Get(), 100000, 100000, &layout));

        if (ok && style.tabular && SUCCEEDED(f->CreateTypography(&typography))) {
            typography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1});
            layout->SetTypography(typography.Get(), {0, length});
        }

        if (ok && style.tracking != 0 && SUCCEEDED(layout.As(&layout1))) {
            layout1->SetCharacterSpacing(0, style.tracking * style.size, 0, {0, length});
        }

        if (ok) {
            DWRITE_LINE_METRICS line;
            UINT32 lines = 0;
            DWRITE_TEXT_METRICS metrics;
            ok = SUCCEEDED(layout->GetLineMetrics(&line, 1, &lines)) && lines >= 1
                 && SUCCEEDED(layout->GetMetrics(&metrics))
                 && SUCCEEDED(layout->Draw(nullptr, collector, 0, 0));

            *baseline = line.baseline;
            *advance = metrics.widthIncludingTrailingWhitespace;
        }

        return ok;
    }

    // Each run's coverage, in the pen's pixel space: the pen is at (ox, oy) and the text turned by `angle`.
    std::vector<Piece> RasterRuns(
        IDWriteFactory2* f,
        const RunCollector& collector,
        float baseline,
        float ox,
        float oy,
        float angle
    ) {
        const float cs = std::cos(angle), sn = std::sin(angle);
        const bool upright = std::fabs(sn) < 1e-6f && cs > 0;
        std::vector<Piece> pieces;
        for (const RunCollector::Run& run : collector.runs) {
            DWRITE_GLYPH_RUN g = {
                run.face,
                run.emSize,
                UINT32(run.glyphs.size()),
                run.glyphs.data(),
                run.advances.data(),
                run.offsets.data(),
                run.sideways,
                run.bidiLevel
            };

            // The run's baseline origin from the pen, turned with the text.
            const float rx = run.x, ry = run.y - baseline;
            const float px = ox + rx * cs - ry * sn, py = oy + rx * sn + ry * cs;

            // x' = x m11 + y m21 + dx; y' = x m12 + y m22 + dy
            const DWRITE_MATRIX turn = {cs, sn, -sn, cs, px, py};
            Microsoft::WRL::ComPtr<IDWriteGlyphRunAnalysis> analysis;
            if (FAILED(f->CreateGlyphRunAnalysis(
                    &g,
                    upright ? nullptr : &turn,
                    DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                    DWRITE_MEASURING_MODE_NATURAL,
                    upright ? DWRITE_GRID_FIT_MODE_ENABLED : DWRITE_GRID_FIT_MODE_DISABLED,
                    DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
                    upright ? px : 0,
                    upright ? py : 0,
                    &analysis
                ))) {
                continue;
            }

            Piece p;
            if (SUCCEEDED(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &p.r))
                && p.r.right > p.r.left && p.r.bottom > p.r.top) {
                p.a.resize(size_t(p.r.right - p.r.left) * (p.r.bottom - p.r.top));
                if (SUCCEEDED(analysis->CreateAlphaTexture(
                        DWRITE_TEXTURE_ALIASED_1x1,
                        &p.r,
                        p.a.data(),
                        UINT32(p.a.size())
                    ))) {
                    pieces.push_back(std::move(p));
                }
            }
        }

        return pieces;
    }

    void Compose(const std::vector<Piece>& pieces, TextRaster* out) {
        RECT all = pieces[0].r;
        for (const Piece& p : pieces) {
            all.left = std::min(all.left, p.r.left), all.top = std::min(all.top, p.r.top);
            all.right = std::max(all.right, p.r.right), all.bottom = std::max(all.bottom, p.r.bottom);
        }

        out->x0 = all.left, out->y0 = all.top;
        out->w = all.right - all.left, out->h = all.bottom - all.top;
        out->coverage.assign(size_t(out->w) * out->h, 0);
        for (const Piece& p : pieces) {
            const int pw = p.r.right - p.r.left;
            for (int y = p.r.top; y < p.r.bottom; y++) {
                for (int x = p.r.left; x < p.r.right; x++) {
                    uint8_t& d = out->coverage[size_t(y - all.top) * out->w + (x - all.left)];
                    d = uint8_t(std::min(255, d + p.a[size_t(y - p.r.top) * pw + (x - p.r.left)]));
                }
            }
        }
    }

}

bool RasterText(
    const wchar_t* text,
    const TextStyle& style,
    float ox,
    float oy,
    float angle,
    TextRaster* out
) {
    IDWriteFactory2* f = Factory();
    const UINT32 length = UINT32(wcslen(text));
    if (!f || length == 0) {
        return false;
    }

    RunCollector collector;
    float baseline = 0;
    if (!LayOut(f, text, length, style, &collector, &baseline, &out->advance)) {
        return false;
    }

    // Each run's coverage, in the pen's pixel space; then all of them in one bitmap.
    const std::vector<Piece> pieces = RasterRuns(f, collector, baseline, ox, oy, angle);
    if (pieces.empty()) {
        return false;
    }

    Compose(pieces, out);
    return true;
}

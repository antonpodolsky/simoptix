// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// grid_shift: frame-to-frame shift of a grid of patches in a raw greyscale video read from stdin
// (ffmpeg ... -vf scale=960:540,format=gray -f rawvideo -). Knows nothing about cockpits, poses or
// traces: shake.py fits the cockpit's rotation to these shifts and tells cockpit from scenery.
//
// usage: ffmpeg ... | grid_shift <width> <height> <out.f32>
// Writes per frame (from the second) float32: whole-frame mean abs difference, then per patch
// dx, dy (pixels; b(x, y) ~ a(x - dx, y - dy), +x right, +y down), the mean squared difference at
// the best shift and the patch's variance. Patch k is at kGridX + (k % kCols) * kStepX,
// kGridY + (k / kCols) * kStepY, kPatchW x kPatchH.
#include <emmintrin.h>
#include <fcntl.h>
#include <io.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

    constexpr int kPatchW = 48, kPatchH = 32;  // kPatchW a multiple of 16 (SSE rows)
    constexpr int kCols = 11, kRows = 8, kGridX = 32, kGridY = 32, kStepX = 80, kStepY = 60;
    constexpr int kNear = 5;  // search around the patch's previous shift first, pixels each way
    constexpr int kFar = 24;  // the full search when the near one ends at its edge

    int Sad(const unsigned char* a, const unsigned char* b, int width) {
        __m128i acc = _mm_setzero_si128();
        for (int y = 0; y < kPatchH; y++, a += width, b += width) {
            for (int x = 0; x < kPatchW; x += 16) {
                acc = _mm_add_epi64(
                    acc,
                    _mm_sad_epu8(
                        _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + x)),
                        _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + x))
                    )
                );
            }
        }

        return _mm_cvtsi128_si32(acc) + _mm_extract_epi16(acc, 4);
    }

    double Ssd(const unsigned char* a, const unsigned char* b, int width) {
        double c = 0;
        for (int y = 0; y < kPatchH; y++, a += width, b += width) {
            for (int x = 0; x < kPatchW; x++) {
                const int d = int(a[x]) - int(b[x]);
                c += d * d;
            }
        }

        return c;
    }

    struct Result {
        float dx, dy, residual, variance;
    };

    // Best shift of the patch at (px, py) from frame a to frame b, searched within `range` of (cx, cy).
    // Returns false if the best lies on the search window's edge.
    bool Search(
        const unsigned char* a,
        const unsigned char* b,
        int width,
        int px,
        int py,
        int cx,
        int cy,
        int range,
        int* bx,
        int* by
    ) {
        int best = 1 << 30;
        for (int sy = cy - range; sy <= cy + range; sy++) {
            for (int sx = cx - range; sx <= cx + range; sx++) {
                if (sx < -kFar || sx > kFar || sy < -kFar || sy > kFar) {
                    continue;
                }
                const int c = Sad(a + size_t(py - sy) * width + px - sx, b + size_t(py) * width + px, width);
                if (c < best) {
                    best = c, *bx = sx, *by = sy;
                }
            }
        }

        return std::abs(*bx - cx) < range && std::abs(*by - cy) < range;
    }

    Result Measure(
        const unsigned char* a,
        const unsigned char* b,
        int width,
        int px,
        int py,
        int* lastX,
        int* lastY
    ) {
        int bx = 0, by = 0;
        if (!Search(a, b, width, px, py, *lastX, *lastY, kNear, &bx, &by)) {
            Search(a, b, width, px, py, 0, 0, kFar, &bx, &by);
        }

        *lastX = bx, *lastY = by;
        const unsigned char* pb = b + size_t(py) * width + px;
        double c[3][3];
        for (int j = -1; j <= 1; j++) {
            for (int i = -1; i <= 1; i++) {
                const int sx = std::max(-kFar - 1, std::min(kFar + 1, bx + i)),
                          sy = std::max(-kFar - 1, std::min(kFar + 1, by + j));

                c[j + 1][i + 1] = Ssd(a + size_t(py - sy) * width + px - sx, pb, width);
            }
        }

        double dx = bx, dy = by;
        const double cx = c[1][0] - 2 * c[1][1] + c[1][2], cy = c[0][1] - 2 * c[1][1] + c[2][1];
        if (cx > 0) {
            dx += std::max(-0.5, std::min(0.5, 0.5 * (c[1][0] - c[1][2]) / cx));
        }
        if (cy > 0) {
            dy += std::max(-0.5, std::min(0.5, 0.5 * (c[0][1] - c[2][1]) / cy));
        }
        double sum = 0, sum2 = 0;
        for (int y = 0; y < kPatchH; y++) {
            for (int x = 0; x < kPatchW; x++) {
                const double v = pb[size_t(y) * width + x];
                sum += v, sum2 += v * v;
            }
        }

        const double n = kPatchW * kPatchH;
        return {float(dx), float(dy), float(c[1][1] / n), float(sum2 / n - (sum / n) * (sum / n))};
    }

}

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: ffmpeg ... | grid_shift <width> <height> <out.f32>\n");
        return 1;
    }

    const int width = atoi(argv[1]), height = atoi(argv[2]);
    if (kGridX - kFar - 1 < 0 || kGridX + (kCols - 1) * kStepX + kPatchW + kFar + 1 > width
        || kGridY - kFar - 1 < 0 || kGridY + (kRows - 1) * kStepY + kPatchH + kFar + 1 > height) {
        fprintf(stderr, "the grid doesn't fit a %dx%d frame\n", width, height);
        return 1;
    }

    _setmode(_fileno(stdin), _O_BINARY);
    FILE* out = nullptr;
    if (fopen_s(&out, argv[3], "wb") != 0 || !out) {
        return 1;
    }

    const size_t size = size_t(width) * height;
    const int count = kCols * kRows;
    std::vector<unsigned char> a(size), b(size);
    std::vector<int> lastX(count, 0), lastY(count, 0);
    std::vector<Result> results(count);
    std::vector<float> row(1 + 4 * count);

    if (fread(a.data(), 1, size, stdin) != size) {
        return 1;
    }
    long frames = 0;

    while (fread(b.data(), 1, size, stdin) == size) {
        double mad = 0;
        for (size_t i = 0; i < size; i += 7) {
            mad += std::abs(int(a[i]) - int(b[i]));
        }
        row[0] = float(mad / double(size / 7));
#pragma omp parallel for schedule(dynamic)
        for (int k = 0; k < count; k++) {
            results[k] = Measure(
                a.data(),
                b.data(),
                width,
                kGridX + (k % kCols) * kStepX,
                kGridY + (k / kCols) * kStepY,
                &lastX[k],
                &lastY[k]
            );
        }

        for (int k = 0; k < count; k++) {
            row[1 + 4 * k] = results[k].dx;
            row[2 + 4 * k] = results[k].dy;
            row[3 + 4 * k] = results[k].residual;
            row[4 + 4 * k] = results[k].variance;
        }

        fwrite(row.data(), sizeof(float), row.size(), out);
        a.swap(b);
        if (++frames % 9000 == 0) {
            fprintf(stderr, "%ld frames\n", frames);
        }
    }

    fclose(out);
    return 0;
}

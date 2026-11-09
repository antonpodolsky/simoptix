// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// patch_shift: frame-to-frame image shift of a raw greyscale clip, measured on patches of the
// image (for a stabilized recording: cockpit patches, which are fixed in head-tracking space, so
// they move only with the view and with pose errors). Knows nothing about traces or poses:
// window.py compares its output with the motion the poses predict.
//
// usage: patch_shift <clip.gray> <width> <height> x y w h [x y w h ...]
// Prints one line per frame from the second: index, time (s at 90 fps), median dx, median dy
// (clip pixels; +x right, +y down), mean absolute difference of the whole frame, then per patch
// dx, dy and its residual (mean squared difference at the best shift).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

    constexpr int kRange = 20;  // largest shift searched, pixels each way

    struct Patch {
        int x, y, w, h;
    };

    // Best shift of `p` from frame a to frame b (b(x, y) ~ a(x - sx, y - sy)), sub-pixel by a parabola
    // through the neighbouring costs. Returns the residual per pixel.
    double BestShift(
        const std::vector<unsigned char>& a,
        const std::vector<unsigned char>& b,
        int width,
        const Patch& p,
        double* dx,
        double* dy
    ) {
        static double cost[2 * kRange + 1][2 * kRange + 1];
        for (int sy = -kRange; sy <= kRange; sy++) {
            for (int sx = -kRange; sx <= kRange; sx++) {
                double c = 0;
                for (int y = p.y; y < p.y + p.h; y++) {
                    const unsigned char* rb = &b[size_t(y) * width + p.x];
                    const unsigned char* ra = &a[size_t(y - sy) * width + p.x - sx];
                    for (int x = 0; x < p.w; x++) {
                        const int d = int(rb[x]) - int(ra[x]);
                        c += d * d;
                    }
                }

                cost[sy + kRange][sx + kRange] = c;
            }
        }

        int bx = 0, by = 0;
        for (int sy = -kRange; sy <= kRange; sy++) {
            for (int sx = -kRange; sx <= kRange; sx++) {
                if (cost[sy + kRange][sx + kRange] < cost[by + kRange][bx + kRange]) {
                    bx = sx, by = sy;
                }
            }
        }

        *dx = bx;
        *dy = by;
        const double c = cost[by + kRange][bx + kRange];
        if (bx > -kRange && bx < kRange) {
            const double l = cost[by + kRange][bx + kRange - 1], r = cost[by + kRange][bx + kRange + 1];
            if (l - 2 * c + r > 0) {
                *dx += 0.5 * (l - r) / (l - 2 * c + r);
            }
        }

        if (by > -kRange && by < kRange) {
            const double l = cost[by + kRange - 1][bx + kRange], r = cost[by + kRange + 1][bx + kRange];
            if (l - 2 * c + r > 0) {
                *dy += 0.5 * (l - r) / (l - 2 * c + r);
            }
        }

        return c / (p.w * p.h);
    }

}

int main(int argc, char** argv) {
    if (argc < 8 || (argc - 4) % 4 != 0) {
        fprintf(stderr, "usage: patch_shift <clip.gray> <width> <height> x y w h [x y w h ...]\n");
        return 1;
    }

    const int width = atoi(argv[2]), height = atoi(argv[3]);
    std::vector<Patch> patches;
    for (int i = 4; i + 3 < argc; i += 4) {
        const Patch p = {atoi(argv[i]), atoi(argv[i + 1]), atoi(argv[i + 2]), atoi(argv[i + 3])};
        if (p.w <= 0 || p.h <= 0 || p.x < kRange || p.y < kRange || p.x + p.w + kRange > width
            || p.y + p.h + kRange > height) {
            fprintf(
                stderr,
                "patch %d,%d,%d,%d must stay %d px inside the %dx%d frame\n",
                p.x,
                p.y,
                p.w,
                p.h,
                kRange,
                width,
                height
            );

            return 1;
        }

        patches.push_back(p);
    }

    FILE* f = nullptr;
    if (fopen_s(&f, argv[1], "rb") != 0 || !f) {
        fprintf(stderr, "can't open %s\n", argv[1]);
        return 1;
    }

    const size_t size = size_t(width) * height;
    std::vector<unsigned char> a(size), b(size);
    if (fread(a.data(), 1, size, f) != size) {
        return 1;
    }

    for (int index = 1; fread(b.data(), 1, size, f) == size; index++) {
        double mad = 0;
        for (size_t i = 0; i < size; i += 7) {
            mad += std::abs(int(a[i]) - int(b[i]));
        }
        mad /= double(size / 7);

        std::vector<double> dxs, dys, per;
        for (const Patch& p : patches) {
            double dx, dy;
            const double residual = BestShift(a, b, width, p, &dx, &dy);
            dxs.push_back(dx);
            dys.push_back(dy);
            per.insert(per.end(), {dx, dy, residual});
        }

        std::sort(dxs.begin(), dxs.end());
        std::sort(dys.begin(), dys.end());
        printf("%d,%.4f,%.3f,%.3f,%.2f", index, index / 90.0, dxs[dxs.size() / 2], dys[dys.size() / 2], mad);
        for (size_t k = 0; k < per.size(); k += 3) {
            printf(",%.2f,%.2f,%.1f", per[k], per[k + 1], per[k + 2]);
        }
        printf("\n");
        a.swap(b);
    }

    fclose(f);
    return 0;
}

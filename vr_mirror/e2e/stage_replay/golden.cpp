// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "golden.h"

#include <cstring>

#include "runs.h"

namespace stage_replay {

    namespace {

        void Write(FILE* out, int index, const WarpParams& p, const Pipeline& st) {
            float w[28];
            static_assert(sizeof(w) == sizeof(WarpParams), "28 floats");
            std::memcpy(w, &p, sizeof(w));
            char line[2048];
            int n = snprintf(line, sizeof(line), "%d", index);
            for (float v : w) {
                n += snprintf(line + n, sizeof(line) - n, " %.6a", double(v));
            }
            for (const Quat* q : {&st.LastRaw(), &st.LastView(), &st.LastImagePose()}) {
                n += snprintf(
                    line + n,
                    sizeof(line) - n,
                    " %.6a %.6a %.6a %.6a",
                    double(q->x),
                    double(q->y),
                    double(q->z),
                    double(q->w)
                );
            }

            snprintf(line + n, sizeof(line) - n, " %u %u\n", st.EdgeStops(), st.EdgePlanned());
            fputs(line, out);
        }

    }

    void RunGolden(const GoldenRun& run, FILE* out) {
        fprintf(out, "# %s\n", run.label.c_str());
        HostRun host;
        host.input = run.input;
        host.settings = run.settings;
        host.config = ConfigFor(run.settings);  // as the hosts build it
        host.ticks = run.ticks;
        host.tickHz = 60;
        host.stabilizeOff = run.stabilizeOff, host.stabilizeOn = run.stabilizeOn;
        RunHost(host, [&](const HostOutput& o) { Write(out, o.index, o.warp, *o.pipeline); });
    }

}

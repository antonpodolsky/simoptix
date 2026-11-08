// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// stage_replay: runs core's frame pipeline offline, without the tool, the fake runtime or the GPU,
// on e2e's replays of real sessions, on synthetic motion and on OBS frame traces: the whole pipeline
// with any mix of stages, or one stage alone. Deterministic and quick (vr_mirror\e2e\README.md, "Stage
// replay").
//
//   --golden FILE [--replays DIR]: the golden dump (golden.h) over a fixed matrix: the replays in
//     kReplays (frames drawn with their true slots' poses) and edge_test+heartbeat and racing+heartbeat, at
//     smoothing 1/2/3 and zoom 1.2 and 1.5, as the window and as OBS ticks; then single runs with
//     stabilization switched off and on. A refactor that must not change behaviour reproduces the
//     file byte for byte.
//   --pipeline [stage flags] [--zoom Z] [--ticks HZ] [--input NAME ...]: the pipeline with the
//     settings' configuration (Smoothest, the slot check), each stage overridden by its flag:
//     --arrival_timing grid, --drawn_pose slot_check|arrival_slot|cockpit,
//     --frame_picker fixed_delay|newest_frame|held_frame, --smoothing instant|smooth|smoothest,
//     --warp rotation (PipelineConfig's fields and values). As the mirror app's window, or as OBS ticks
//     at HZ picked by the frame picker. Inputs: four of the five replays (kReplays) and the two motions.
//   --stage drawn_pose <replay .csv> [--show FIRST-LAST] [--dump FILE] [--delays A,B,...]
//     [--drawn_pose K]: the drawn pose alone on a replay's real thumbnails, its poses scored
// against the true ones as each host asks for them (runs_drawn_pose.cpp; the findings call it lag_sim, its
// old name).
//   --stage frame_picker [--frame_picker K] [--trace TICKS PIMAX ...]: the frame picker alone
//     (runs_frame_picker.cpp).
//   --stage arrival_timing [--arrival_timing K] [--trace TICKS PIMAX ...]: the arrival timing alone
//     over the traces' frame arrivals (runs_arrival.cpp).

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "golden.h"
#include "pipeline/pipeline_config.h"
#include "replay_poses.h"
#include "runs.h"

using namespace stage_replay;

namespace {

    const char* const kReplays[] = {
        "queued_late_frames",
        "false_retiming_head_turns",
        "false_retiming_short_hiccup",
        "false_retiming_featureless_cockpit"
    };

    const char* const kMotions[] = {"edge_test+heartbeat", "racing+heartbeat"};

    std::string DefaultReplays() {
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);

        std::string folder = path;
        folder = folder.substr(0, folder.find_last_of("\\/"));             // vr_mirror\e2e\bin
        return folder.substr(0, folder.find_last_of("\\/")) + "\\replay";  // vr_mirror\e2e\replay
    }

    // The replays in kReplays, then the two motions, in that order.
    bool LoadInputs(const std::string& replays, std::vector<std::unique_ptr<Input>>* inputs) {
        std::string error;

        for (const char* name : kReplays) {
            inputs->push_back(std::make_unique<Input>());

            if (!LoadReplay(replays + "\\" + name + ".csv", name, inputs->back().get(), &error)) {
                printf("%s\n", error.c_str());
                return false;
            }
        }

        for (const char* spec : kMotions) {
            inputs->push_back(std::make_unique<Input>());

            if (!MakeSynthetic(spec, 12.0, inputs->back().get(), &error)) {
                printf("%s\n", error.c_str());
                return false;
            }
        }

        return true;
    }

    std::string LabelOf(const GoldenRun& run, const char* extra) {
        char label[256];

        snprintf(
            label,
            sizeof(label),
            "%s smoothing=%d zoom=%.2f host=%s%s",
            run.input->name.c_str(),
            run.settings.smoothing,
            run.settings.zoom,
            run.ticks ? "ticks" : "window",
            extra
        );

        return std::string(label);
    }

    std::vector<GoldenRun> GoldenRuns(const std::vector<std::unique_ptr<Input>>& inputs) {
        std::vector<GoldenRun> runs;

        for (const auto& input : inputs) {
            for (int smoothing = 1; smoothing <= 3; smoothing++) {
                for (float zoom : {1.2f, 1.5f}) {
                    for (bool ticks : {false, true}) {
                        GoldenRun run;
                        run.input = input.get();
                        run.settings.smoothing = smoothing;
                        run.settings.zoom = zoom;
                        run.ticks = ticks;
                        run.label = LabelOf(run, "");
                        runs.push_back(run);
                    }
                }
            }
        }

        // Stabilization switched off and on, at every smoothing and both host styles.
        for (int smoothing = 1; smoothing <= 3; smoothing++) {
            for (bool ticks : {false, true}) {
                GoldenRun run;
                run.input = inputs[4].get();
                run.settings.smoothing = smoothing;
                run.settings.zoom = 1.2f;
                run.ticks = ticks;
                run.stabilizeOff = 200;
                run.stabilizeOn = 260;
                run.label = LabelOf(run, " stabilize off 200-260");
                runs.push_back(run);
            }
        }

        return runs;
    }

    int Golden(const std::string& outPath, const std::string& replays) {
        std::vector<std::unique_ptr<Input>> inputs;

        if (!LoadInputs(replays, &inputs)) {
            return 1;
        }

        const std::vector<GoldenRun> runs = GoldenRuns(inputs);
        FILE* out = nullptr;

        if (fopen_s(&out, outPath.c_str(), "wb") != 0 || !out) {
            printf("cannot write %s\n", outPath.c_str());
            return 1;
        }

        for (const GoldenRun& run : runs) {
            RunGolden(run, out);
        }

        fclose(out);
        printf("%zu runs written to %s\n", runs.size(), outPath.c_str());
        return 0;
    }

    // A stage flag (--<field> <value>) onto its config field; false if `flag` is no stage flag.
    template <class Kind>
    bool StageFlag(const char* flag, const char* field, const char* value, Kind* kind, bool* bad) {
        if (strcmp(flag + 2, field) != 0) {
            return false;
        }

        if (!Parse(value, kind)) {
            printf("unknown %s: %s\n", field, value);
            *bad = true;
        }

        return true;
    }

    // lag_sim's own options follow the replay; --drawn_pose picks the implementation.
    int DrawnPoseStage(int argc, char** argv) {
        PipelineConfig config;

        for (int i = 3; i + 1 < argc; i++) {
            if (!strcmp(argv[i], "--drawn_pose") && !Parse(argv[i + 1], &config.drawnPose)) {
                printf("unknown drawn_pose: %s\n", argv[i + 1]);
                return 2;
            }
        }

        return RunDrawnPose(argc - 2, argv + 2, config);
    }

    struct Options {
        std::string golden;
        std::string replays = DefaultReplays();
        std::string stage;
        bool pipeline = false;
        bool bad = false;
        bool overridden = false;
        double tickHz = 0;
        MirrorSettings settings;
        PipelineConfig config = ConfigFor(settings);
        PipelineConfig flags = config;
        std::vector<std::string> names;
        std::vector<std::pair<std::string, std::string>> traces;
    };

    bool IsStageFlag(const char* option, const char* value, Options* options) {
        PipelineConfig& flags = options->flags;
        bool* bad = &options->bad;

        return StageFlag(option, "arrival_timing", value, &flags.arrivalTiming, bad)
               || StageFlag(option, "frame_picker", value, &flags.framePicker, bad)
               || StageFlag(option, "drawn_pose", value, &flags.drawnPose, bad)
               || StageFlag(option, "smoothing", value, &flags.smoothing, bad)
               || StageFlag(option, "warp", value, &flags.warp, bad)
               || StageFlag(option, "observer", value, &flags.observer, bad);
    }

    void ParseOptions(int argc, char** argv, Options* options) {
        for (int i = 1; i < argc; i++) {
            const char* option = argv[i];
            const char* value = i + 1 < argc ? argv[i + 1] : "";

            if (!strcmp(option, "--pipeline")) {
                options->pipeline = true;
            } else if (!strcmp(option, "--golden")) {
                options->golden = value;
                i++;
            } else if (!strcmp(option, "--replays")) {
                options->replays = value;
                i++;
            } else if (!strcmp(option, "--stage")) {
                options->stage = value;
                i++;
            } else if (!strcmp(option, "--zoom")) {
                options->settings.zoom = float(atof(value));
                i++;
            } else if (!strcmp(option, "--ticks")) {
                options->tickHz = atof(value);
                i++;
            } else if (!strcmp(option, "--input")) {
                options->names.push_back(value);
                i++;
            } else if (!strcmp(option, "--trace") && i + 2 < argc) {
                options->traces.push_back({argv[i + 1], argv[i + 2]});
                i += 2;
            } else if (IsStageFlag(option, value, options)) {
                options->overridden = true;
                i++;
            } else {
                printf("unknown option %s\n", option);
                options->bad = true;
            }
        }
    }

    int ArrivalTimingStage(const Options& options) {
        std::vector<std::string> pimax;

        for (const auto& trace : options.traces) {
            pimax.push_back(trace.second);
        }

        return RunArrivalTiming(options.config, pimax) == 0 ? 0 : 1;
    }

    int PipelineRun(const Options& options) {
        std::vector<std::unique_ptr<Input>> inputs;

        if (!LoadInputs(options.replays, &inputs)) {
            return 1;
        }

        std::vector<const Input*> chosen;

        for (const auto& input : inputs) {
            bool wanted = options.names.empty();

            for (const std::string& name : options.names) {
                wanted |= name == input->name;
            }

            if (wanted) {
                chosen.push_back(input.get());
            }
        }

        return RunPipeline(options.config, options.settings, chosen, options.tickHz) == 0 ? 0 : 1;
    }

    void PrintUsage() {
        printf(
            "usage: stage_replay.exe --golden FILE [--replays DIR]\n"
            "       stage_replay.exe --pipeline [--<stage> <kind> ...] [--zoom Z] [--ticks HZ] "
            "[--input NAME ...]\n"
            "       stage_replay.exe --stage drawn_pose <replay .csv> [--show FIRST-LAST] [--dump FILE] "
            "[--delays A,B,...] [--drawn_pose K]\n"
            "       stage_replay.exe --stage frame_picker [--trace TICKS PIMAX ...]\n"
            "       stage_replay.exe --stage arrival_timing [--arrival_timing K] [--trace TICKS PIMAX ...]\n"
        );
    }

}

int main(int argc, char** argv) {
    if (argc >= 3 && !strcmp(argv[1], "--stage") && !strcmp(argv[2], "drawn_pose")) {
        return DrawnPoseStage(argc, argv);
    }

    Options options;
    ParseOptions(argc, argv, &options);

    if (options.bad) {
        return 2;
    }

    if (options.overridden) {
        options.config = options.flags;
    }

    if (!options.golden.empty()) {
        return Golden(options.golden, options.replays);
    }

    if (options.stage == "frame_picker") {
        return RunFramePicker(options.traces, options.config.framePicker) == 0 ? 0 : 1;
    }

    if (options.stage == "arrival_timing") {
        return ArrivalTimingStage(options);
    }

    if (options.pipeline) {
        return PipelineRun(options);
    }

    PrintUsage();
    return 2;
}

---
paths:
  - "engine/tools/perf/**/*"
  - "tests/SearchSliceTest.cpp"
  - "tests/ChessUiAllocTest.cpp"
---

# The benchmarks of this repository

Read by the skills `perf` and `perf-investigate` and by the sentinel step of `todo-merge` (`.claude/skills/perf/SKILL.md`; the runner and the format of the set: `engine/tools/perf/CLAUDE.md`). The skills are the engine's, linked here; this file is the game's own set: the own engine's search speed and the headless frames of the shipped scene, run with `{engine}/tools/perf/headless_frames.py` on `BagiChess`.

## Benchmark set

```json
{
  "area": "chess",
  "preset": "windows-clang-release",
  "profilePreset": "windows-clang-profile-editor",
  "targets": ["BagiChess", "ChessTests"],
  "repetitions": 5,
  "thresholdPercent": 3,
  "benchmarks": [
    {
      "name": "search-nodes",
      "about": "The own engine's search speed: the disabled SearchSliceBenchmark.DISABLED_NodesPerSecond (fixed positions and depths, the search in one call and sliced), one JSON line of Mnodes/s per column. own_one_call_mnps and own_slices_mnps are about 7.5 on this machine in the profile build (Release is faster); a run is about 2 s",
      "fast": false,
      "command": ["{bin}/ChessTests", "--gtest_also_run_disabled_tests", "--gtest_filter=SearchSliceBenchmark.DISABLED_NodesPerSecond"],
      "metrics": {
        "own_one_call_mnps": {"source": "json", "key": "ownOneCall", "unit": "Mnodes/s", "better": "higher"},
        "own_slices_mnps": {"source": "json", "key": "ownSlices", "unit": "Mnodes/s", "better": "higher"}
      }
    },
    {
      "name": "scene-idle",
      "about": "BagiChess headless, the opening position with nobody playing: 600 frames after 120 of warm-up. work_ms is about 0.03 ms of a 1.9 ms frame (the rest is the software renderer's present), 134 draw calls, a run about 2 s. allocs_per_frame is 0 in the profile build and always 0 in Release, so it is not gated; ChessUiAllocTest holds it",
      "fast": true,
      "command": ["{python}", "{engine}/tools/perf/headless_frames.py", "--exe", "{bin}/BagiChess", "--frames", "600", "--warmup", "120"],
      "traceCommand": ["{python}", "{engine}/tools/perf/headless_frames.py", "--exe", "{bin}/BagiChess", "--frames", "600", "--warmup", "120", "--trace", "{trace}"],
      "metrics": {
        "work_ms": {"source": "json", "key": "workMs", "unit": "ms/frame", "better": "lower"},
        "cpu_ms": {"source": "json", "key": "cpuMs", "unit": "ms/frame", "better": "lower", "gate": false},
        "draw_calls": {"source": "json", "key": "drawCalls", "unit": "per frame", "better": "lower", "gate": false},
        "allocs_per_frame": {"source": "json", "key": "allocsPerFrame", "unit": "per frame", "better": "lower", "gate": false}
      }
    },
    {
      "name": "scene-thinking",
      "about": "BagiChess headless with two own engines playing each other (level max, seed 7), 600 frames after 120 of warm-up: the scene while moves are chosen and played. It sees the main thread only: the search runs on a worker, and its cost shows as the frames in which a move arrives (the update of the opponent, the animation, the analysis). work_ms is about 0.05 ms of a 3 ms frame, 180 draw calls, a run about 3 s. The moves played depend on the timing of the search, so draw_calls is not gated; allocs_per_frame is process-wide here (the worker's search allocates, about 8 to 17 per frame in the profile build), not the main thread's, and 0 in Release",
      "fast": false,
      "command": ["{python}", "{engine}/tools/perf/headless_frames.py", "--exe", "{bin}/BagiChess", "--frames", "600", "--warmup", "120", "--call", "set_state", "{\"opponent\":{\"plays\":\"both\",\"engine\":\"own\",\"level\":\"max\",\"seed\":7}}"],
      "traceCommand": ["{python}", "{engine}/tools/perf/headless_frames.py", "--exe", "{bin}/BagiChess", "--frames", "600", "--warmup", "120", "--call", "set_state", "{\"opponent\":{\"plays\":\"both\",\"engine\":\"own\",\"level\":\"max\",\"seed\":7}}", "--trace", "{trace}"],
      "metrics": {
        "work_ms": {"source": "json", "key": "workMs", "unit": "ms/frame", "better": "lower"},
        "cpu_ms": {"source": "json", "key": "cpuMs", "unit": "ms/frame", "better": "lower", "gate": false},
        "draw_calls": {"source": "json", "key": "drawCalls", "unit": "per frame", "better": "lower", "gate": false},
        "allocs_per_frame": {"source": "json", "key": "allocsPerFrame", "unit": "per frame", "better": "lower", "gate": false}
      }
    }
  ]
}
```

## Notes

- Headless, the present of the SDL software renderer is most of a frame whatever the scene draws; `work_ms` (the frame without the present) is the number that moves with scene and UI code.
- Windows: the runner builds with plain `cmake`, so it runs as `python engine/CI/scripts/vs_env.py python engine/tools/perf/perf.py ...`; a Release build is `cmake --preset windows-clang-release`. The allocation counts, `ChessUiAllocTest` and the zones of a trace need the profile preset.
- The zones a trace shows are listed in `src/Chess/Engine/CLAUDE.md` ("Profiler zones").
- The sentinel (`todo-merge`): `ask`.

# vkt

Vulkan smoke tests and benchmarks for Android GPU drivers, made for
[malisx2](https://github.com/bmdhacks/malisx2) (a Vulkan driver for Arm Mali
GPUs) and the features the [Eden](https://git.eden-emu.dev/eden-emu/eden)
Switch emulator needs.

vkt loads either the phone's own driver (`-sys`) or a driver library
directly, without installing it, so a driver build can be tested and
compared with the system driver on the same phone in one go. Each test
renders offscreen or writes a buffer, reads the result back and checks it,
and prints one `PASS` or `FAIL` line per check, with the values it read.

    $ vkt ./libvulkan_malisx2.so eden/logic-op stages/multi-viewport
    device: Mali-G720 MC7
    features: geometryShader 1 tessellationShader 1 multiViewport 1 (maxViewports 16) ...
    == eden/logic-op
    PASS logic op: XOR of (55,0f,f0,ff) and (ff,ff,00,00) = aa,f0,f0,ff (want aa,f0,f0,ff)
    == stages/multi-viewport
    PASS multi viewport: viewport 0 red at (16,16) = 255,0,0
    ...
    ALL PASSED: 0 failure(s)

## Tests

| Group | Tests |
|---|---|
| `basic` | `info` (device, limits, which of the 33 features and extensions Eden needs are missing), `compute`, `draw` |
| `eden` | `variable-pointers`, `draw-parameters`, `logic-op`, `wide-lines`, `host-query-reset` |
| `stages` | `cull-distance`, `multi-viewport`, `polygon-mode`, `geometry-shader`, `tessellation`, `pipeline-cache` |
| `poly` | `tess-patch-size`, `relaxed-precision`, `tess-front-face`, `viewport-index`, `compute-in-pass`: geometry and tessellation corner cases |
| `blend` | `dual-source`, `framebuffer-fetch`: blending that emulators use for blend modes Vulkan cannot express |
| `bench` | `shared-memory`, `chain-barrier`, `draws`, `geometry`, `tessellation` (run only when selected) |

[docs/tests.md](docs/tests.md) describes each test: what it draws, what it
checks, what the driver must offer.

## Quick start

Build (Android NDK, Vulkan-Headers, glslang and SPIRV-Tools needed; see
[docs/development.md](docs/development.md)):

    ANDROID_NDK_HOME=/path/to/android-ndk ./build.sh

Run on a phone:

    adb push build/vkt configs/*.yaml libvulkan_malisx2.so /data/local/tmp/vkt/
    adb shell
    cd /data/local/tmp/vkt
    ./vkt -sys                              # every test on the system driver
    ./vkt -c malisx2.yaml                   # every test on a malisx2 build, strict
    ./vkt -c malisx2.yaml 'stages/*'        # some tests
    ./vkt -c malisx2.yaml bench             # the benchmarks
    ./vkt -l                                # list the tests

With malisx2: [docs/malisx2.md](docs/malisx2.md) covers building the
driver, the configuration (`configs/malisx2.yaml`), and what to expect on
each Mali GPU.

## Usage

    vkt [<driver>] [-c <config.yaml>] [--strict] [<selector>...]
    vkt -l [-c <config.yaml>] [<selector>...]

| Option | |
|---|---|
| `<driver>` | `-sys`, or a driver library (`*.so`; a bare name is in the current directory) |
| `<selector>` | a group, a test (`stages/tessellation`) or a pattern (`'stages/geo*'`); none: every test but the benchmarks |
| `-c <file>` | a YAML configuration file |
| `--strict` | a feature the driver does not offer fails the test instead of skipping it |
| `-l` | list what would run |

Exit status: 0 all checks passed, 1 a check failed, 2 bad arguments or
configuration. Details: [docs/usage.md](docs/usage.md).

## Configuration

```yaml
driver: ./libvulkan_malisx2.so   # or -sys
strict: true                      # as --strict
tests: [eden, stages/tess*]      # selectors
bench:
  n: 5000                         # batch size of the benchmarks
  warm_up_ms: 3000                # GPU warm-up before the first one
  runs: 6                         # runs per measurement, the first not counted
  delay_s: 0                      # sleep before each measurement
```

Every key is optional, and the command line wins over the file. Each key,
its type, default and range: [docs/configuration.md](docs/configuration.md).
Examples: [`configs/`](configs/).

## Documentation

- [docs/usage.md](docs/usage.md): command line, selectors, output, exit status
- [docs/configuration.md](docs/configuration.md): every configuration key
- [docs/tests.md](docs/tests.md): every test
- [docs/benchmarks.md](docs/benchmarks.md): how the benchmarks measure, their output
- [docs/malisx2.md](docs/malisx2.md): testing a malisx2 build: building it, the configuration, what to expect per GPU
- [docs/development.md](docs/development.md): building, the code, adding a test

## License

[MIT No Attribution](LICENSE) (MIT-0).

# Using vkt with malisx2

[malisx2](https://github.com/bmdhacks/malisx2) is a Vulkan driver for Arm
Mali GPUs on Arm's kbase kernel driver. vkt loads it the way an application
that brings its own driver does: `dlopen` on the library and its
`vk_icdGetInstanceProcAddr`, nothing installed. So a build can be tested on
a stock phone, next to the phone's own driver.

## 1. Build the driver

Follow malisx2's README ("Building", Android). In short, with the Mesa
sources checked out at `mesa/` and the NDK r28c:

    meson setup build-android --cross-file cross/android-aarch64.ini
    ninja -C build-android
    llvm-strip -o libvulkan_malisx2.so build-android/src/libvulkan_malisx2.so

Geometry and tessellation shaders (v11 and v12, in malisx2 versions that
have them: the `poly` meson option) need Mesa's `mesa_clc` and
`vtn_bindgen2` on the build machine. They come from a Mesa build configured
with `-Dmesa-clc=enabled -Dinstall-mesa-clc=true` (LLVM, libclc and
SPIRV-LLVM-Translator needed); put them on `PATH`. The `poly` option is
`auto`, so without them the driver builds without both and says so only in
meson's output. To be sure they are in, configure with `-Dpoly=enabled`,
which fails when the tools are missing. A build without them reports `geometryShader 0` and
`tessellationShader 0`, and vkt skips (or with strict fails)
`stages/geometry-shader`, `stages/tessellation`, `stages/pipeline-cache`
and the `bench` geometry and tessellation tests.

## 2. Put both on the phone

    adb shell mkdir -p /data/local/tmp/vkt
    adb push build/vkt configs/malisx2.yaml /data/local/tmp/vkt/
    adb push /path/to/malisx2/libvulkan_malisx2.so /data/local/tmp/vkt/

`/data/local/tmp` is where `adb shell` may run programs. vkt takes a bare
library name from the current directory, so run it from there.

## 3. Configuration

[`configs/malisx2.yaml`](../configs/malisx2.yaml):

```yaml
driver: ./libvulkan_malisx2.so
strict: true
bench:
  n: 5000
  warm_up_ms: 3000
  runs: 6
  delay_s: 0
```

- `driver`: the library pushed above. `-sys` on the command line replaces
  it, to run the same tests on the phone's driver.
- `strict: true`: on malisx2, a feature that should be there and is not is
  a bug, so it fails the run instead of being skipped. See the table below
  for what each GPU should offer; on a GPU where malisx2 does not offer a
  feature by design, turn strict off or leave those tests out with
  `tests`.
- `bench`: the settings the published malisx2 measurements use; they apply
  only when a benchmark is selected.
- No `tests`: every test but the benchmarks. To run some, list them, for
  example the features Eden needs:

  ```yaml
  tests: [eden, stages]
  ```

Every key: [configuration.md](configuration.md).

## 4. Run

    adb shell
    cd /data/local/tmp/vkt
    ./vkt -c malisx2.yaml                 # every test on malisx2
    ./vkt -sys                            # the same on the phone's driver (not strict)
    ./vkt -c malisx2.yaml 'stages/*'      # some tests
    ./vkt -c malisx2.yaml bench           # the benchmarks

The two driver runs print the same check lines, so they can be compared
side by side (`diff` of the two outputs, after the `device:` and
`features:` lines).

### Comparing performance

A phone's clocks and temperature change from run to run, so alternate the
drivers over several rounds and compare medians:

```sh
for r in 1 2 3; do
   for d in -sys ./libvulkan_malisx2.so; do
      echo "##### round $r $d"
      ./vkt $d -c malisx2.yaml bench
   done
done > bench.txt
```

Compare builds of malisx2 the same way: push each under its own name
(`lib_before.so`, `lib_after.so`) and alternate them in the loop.

## What to expect per GPU

malisx2 picks its code by the GPU's Mesa architecture. On the
command-stream GPUs (v10, v11, v12) it offers the Eden features vkt tests,
in versions that have them (geometry and tessellation shaders and the rest
came with bmdhacks/malisx2#51); on v7 and v9 it does not offer some of
them, by design, and vkt skips those tests (strict: fails them).

| Architecture | GPUs (examples) | Expected |
|---|---|---|
| v12 | Mali-G720, G620 | every test passes, with a `-Dpoly` build |
| v11 | Mali-G615, G715 | every test passes, with a `-Dpoly` build |
| v10 | Mali-G310, G510, G610, G710 | every test passes, with a `-Dpoly` build |
| v9 | Mali-G57, G68, G77, G78 | `basic` passes; most Eden features not offered |
| v7 | Mali-G31, G51, G52, G76 | `basic` passes; most Eden features not offered |

`basic/info` lists what is missing of what Eden needs on the device at
hand; the `features:` line at the top shows what the tests use. These
reports, not this table, are what a run is judged against: malisx2 changes,
and the table may lag behind it.

## When there is no device

    FAIL no physical device (0, 0)

means malisx2 found no GPU it serves: not a Mali, an architecture it does
not build for (Midgard, arch 6, arch 13 on), or a kbase kernel interface it
does not speak (for CSF GPUs it needs UK 1.20 or newer; older kernels, such
as UK 1.18, are refused). The driver says why in the system log:

    adb logcat -d | grep malisx2

    malisx2: skipping /dev/mali0: ... (refusing device: ...)

That is a property of the phone, not a test failure; `-sys` still works on
it.

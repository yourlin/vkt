# Development

## Build

Needs:

- the Android NDK (r28c tested), at `ANDROID_NDK_HOME`;
- Vulkan-Headers 1.3 or later (`vulkan/vulkan.h`): found in
  `$VULKAN_SDK/include`, `/opt/homebrew/include`, `/usr/local/include` or
  `/usr/include`, or set `VULKAN_INCLUDE`;
- `glslangValidator`, and `spirv-as` and `spirv-val` from SPIRV-Tools;
- Python 3 (to turn the SPIR-V into C arrays).

Then:

    ANDROID_NDK_HOME=/path/to/ndk ./build.sh

`build/vkt` is the program; `build/` also holds the SPIR-V and the generated
`shaders.h`. `CC=...` replaces the NDK's compiler (only the NDK build is
tested).

## Layout

| Path | What |
|---|---|
| `vkt.c` | the table of tests, the command line, the configuration file |
| `vkt.h` | the harness: driver loading, device creation, buffers, render targets and readback, graphics pipelines, PASS/FAIL/SKIP |
| `yaml.h` | the YAML reader |
| `tests/<group>.c` | the tests of a group |
| `shaders/` | the shaders: GLSL, and SPIR-V assembly (`.spvasm`) where GLSL cannot express a test |
| `configs/` | example configuration files |
| `docs/` | this documentation |

`vkt.c` includes the test files and the harness into one translation unit;
everything is `static`.

## Adding a test

1. Write a `static void test_<name>(void)` in the group's file in `tests/`.
2. Check what the driver must offer first; if it does not, call
   `NOT_OFFERED("<what>")` and return.
3. Render or dispatch, read back, and report each check with
   `expect(<condition>, "<what is checked> (<values seen>)", ...)`. Say what
   was expected in the message and print what was read, so a `FAIL` line is
   enough to start from.
4. Add a line to `tests[]` in `vkt.c`: `{"<group>/<name>", test_<name>, "<one line>"}`.
5. Describe it in [tests.md](tests.md).

A new group is a new file in `tests/`, included from `vkt.c`. A group whose
tests should run only when selected is listed in `opt_in()` in `vkt.c`.

Shaders go in `shaders/`, named `<prefix>_<name>.<stage>`; `build.sh` makes
each a `static const uint32_t <prefix>_<name>_<stage>[]` in `shaders.h`
(`shaders/t3_cull.vert`: `t3_cull_vert`). In C: `SHADER(t3_cull_vert)`, or
`GFX_STAGE(vs, t3_cull_vert)` in a `struct gfx_desc`.

## Harness

The main parts of `vkt.h`:

| Function | Does |
|---|---|
| `FEAT`, `vkt_features` | the features the driver reports and the device has |
| `has_ext(name)` | whether the driver has a device extension |
| `mkbuf(size, usage)`, `mkbuf_data(data, size, usage)` | a mapped, host-coherent buffer, zeroed or filled |
| `mktarget(w, h)`, `mktarget_fmt(w, h, format, bpp)` | a colour target with a render pass and a readback buffer |
| `pass_begin(t, clear)`, `pass_begin_cv(t, value)` | a command buffer in the target's render pass |
| `pass_end_readback(t, cb)` | ends the pass, copies the image to the readback buffer, submits, waits |
| `pix(t, x, y)`, `lit(t, x, y)`, `same_image(a, b)`, `write_ppm(t, path)` | read the result |
| `mkgfx(t, desc)` | a graphics pipeline from a `struct gfx_desc` |
| `mklayout(set_layout, push_size)`, `mkdsl(...)`, `mkset(...)` | pipeline layouts and descriptor sets |
| `begin()`, `submit(cb)` | a command buffer; end, submit and wait |
| `expect(ok, fmt, ...)`, `NOT_OFFERED(what)` | report a check, or a test that cannot run |
| `D(Name)`, `I(Name)`, `CHECK(call)` | device and instance entry points; a call that must succeed |

## Code style

`.clang-format` holds it: 3-space indents, 100 columns, a function's return
type on its own line. Format before committing:

    clang-format -i vkt.c vkt.h yaml.h tests/*.c

Comments say why, in full sentences.

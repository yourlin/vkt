# Command line

    vkt [<driver>] [-c <config.yaml>] [--strict] [<selector>...]
    vkt -l [-c <config.yaml>] [<selector>...]
    vkt -h

## Arguments

| Argument | Meaning |
|---|---|
| `<driver>` | The Vulkan driver to test. `-sys`: the system's driver, through `libvulkan.so` and `vkGetInstanceProcAddr`. Otherwise a driver library, recognised as a word ending in `.so`, loaded with `dlopen` and entered through `vk_icdGetInstanceProcAddr` (after `vk_icdNegotiateLoaderICDInterfaceVersion`, if it has one, with version 5). A name without `/` is taken from the current directory. At most one. Default: the configuration's [`driver`](configuration.md#driver). |
| `<selector>` | Which tests run; see [Selectors](#selectors). Any number. Default: the configuration's [`tests`](configuration.md#tests). |
| `-c <file>` | Read a [configuration file](configuration.md). |
| `--strict` | A test of a feature the driver does not offer fails instead of being skipped. Same as [`strict: true`](configuration.md#strict). |
| `-l` | List the tests that would run, with a line about each, and exit without loading a driver. |
| `-h`, `--help` | Print the usage and exit 2. |

Any other word starting with `-` is an error.

## Selectors

A selector picks tests by name. Test names are `<group>/<test>`; `vkt -l`
lists them, and [tests.md](tests.md) describes them.

| Selector | Picks |
|---|---|
| a group: `eden` | every test of the group |
| a name: `stages/tessellation` | that test |
| a pattern: `'stages/geo*'`, `'*-lines'`, `'eden/?ogic-op'` | the tests it matches, as `fnmatch(3)` (`*`, `?`, `[...]`) |

Quote patterns in a shell, so it does not expand them.

- Tests run in the order of `vkt -l`, whatever the order of the selectors,
  and each runs once even if several selectors pick it.
- Without selectors, every test runs but the `bench` group's: benchmarks
  take long and measure rather than check, so they run only when a
  selector picks them (`bench`, `bench/draws`, `'*'`).
- A selector that picks no test is an error (exit 2), so a typo does not
  pass as an empty run.

## Precedence

The command line wins over the configuration file:

- a `<driver>` replaces `driver`;
- any `<selector>` replaces the whole `tests` list;
- `--strict` turns strict on whatever `strict` says (there is no way to turn
  it off from the command line once the file turns it on).

The `bench` settings exist only in the file.

## Output

    device: <name>
    features: geometryShader 1 tessellationShader 1 multiViewport 1 (maxViewports 16) ...
    == <group>/<test>
    PASS <what was checked> (<values seen>)
    FAIL <what was checked> (<values seen>)
    SKIP <feature>: not offered
    ...
    ALL PASSED: 0 failure(s)[, <n> skipped]
    FAILED: <n> failure(s)[, <n> skipped]

- `features:` is what the driver reports for the features the tests use.
- Each `== ` line starts a test; its checks follow, one line each.
- A `FAIL` line names what was expected and shows what was read, so a
  failure can be read without running anything else.
- `SKIP` (or with strict a `FAIL ...: not offered`): the driver does not
  offer a feature the test needs; the test does not run.
- A Vulkan call that fails where it must not prints
  `FAIL <file>:<line> <call> -> <VkResult>` and exits 1 at once.
- Benchmarks print their own lines; see [benchmarks.md](benchmarks.md).

Some tests also write images (`.ppm`, binary PPM) of what they rendered
into the current directory; [tests.md](tests.md) lists them.

## Exit status

| Status | Meaning |
|---|---|
| 0 | Every check passed (skipped tests do not count). |
| 1 | A check failed, or a Vulkan call failed. |
| 2 | Bad arguments or configuration, a selector that matches no test, or no driver given. |

## Examples

    vkt -l                                  # every test but the benchmarks
    vkt -l bench                            # the benchmarks
    vkt -sys                                # every test on the system driver
    vkt ./libvulkan_malisx2.so --strict     # every test on a driver build
    vkt libvulkan_malisx2.so eden 'stages/tess*'
    vkt -c configs/all.yaml                 # what the file says
    vkt -sys -c configs/bench.yaml          # the file's tests, on -sys

# Configuration file

`vkt -c <file>` reads a YAML file. Every key is optional; an empty file is
valid. The [command line](usage.md#precedence) wins over the file.

```yaml
driver: ./libvulkan_malisx2.so
strict: true
tests:
  - eden
  - stages/tess*
bench:
  n: 5000
  warm_up_ms: 3000
  runs: 6
  delay_s: 0
```

## Keys

### `driver`

| | |
|---|---|
| Type | string |
| Default | none: the command line must give one |
| Values | `-sys`, or the path of a driver library |
| Command line | `<driver>`, which replaces it |

The driver to test, as the [`<driver>` argument](usage.md#arguments): `-sys`
for the system's driver, otherwise a library loaded directly. A path
without `/` is in the current directory. Unlike the command line, the
file's value does not have to end in `.so`.

### `strict`

| | |
|---|---|
| Type | boolean: `true`, `false`, `yes`, `no`, `on`, `off` |
| Default | `false` |
| Command line | `--strict`, which turns it on |

`false`: a test of a feature the driver does not offer is skipped
(`SKIP ...: not offered`) and the run can still pass. `true`: it is a
`FAIL`. Use `true` on driver builds that should offer every feature, so
one turned off by mistake fails the run; `false` to compare with a driver
that lacks some.

### `tests`

| | |
|---|---|
| Type | a list of strings, or one string |
| Default | every test but the `bench` group's |
| Values | [selectors](usage.md#selectors): groups, names, patterns |
| Command line | `<selector>...`, which replace the whole list |

Block or flow style:

```yaml
tests:
  - basic
  - stages/geometry-shader

tests: [basic, stages/geometry-shader]

tests: eden
```

Each selector must match a test. Patterns need no quotes in YAML unless
they start with `*` (an alias in YAML): write `'*-lines'`.

### `bench`

Settings of the `bench` group's tests. A mapping; every key is optional.
They only matter when a benchmark runs; see
[benchmarks.md](benchmarks.md).

#### `bench.n`

| | |
|---|---|
| Type | whole number, 50 to 10000000 |
| Default | 5000 |

The size of the measurements: `n` plain draws, `n / 5` dispatches, `n / 10`
geometry and tessellation draws, `n / 50` of the 64-instance ones. 5000 is
what the published measurements use; smaller is faster and noisier.

#### `bench.warm_up_ms`

| | |
|---|---|
| Type | whole number of milliseconds, 0 to 600000 |
| Default | 0: no warm-up |

How long to keep the GPU busy before the first measurement of the run, so
its clocks are up: batches of 2000 shared-memory dispatches of 512
workgroups, submitted one after another until the time is up. 3000 is what
the published measurements use.

#### `bench.runs`

| | |
|---|---|
| Type | whole number, 2 to 1000 |
| Default | 6 |

How many times each measurement runs. The first run is a warm-up and not
counted; the printed record and GPU times are each the smallest of the
others.

#### `bench.delay_s`

| | |
|---|---|
| Type | whole number of seconds, 0 to 3600 |
| Default | 0 |

Seconds to sleep before each measurement, to let the device cool down
between them.

## Syntax

The reader takes the part of YAML these files need:

- mappings (`key: value`, or `key:` and an indented block);
- lists (`- item` lines, or `[a, b]` on one line) of scalars;
- scalars: plain, `'single quoted'` or `"double quoted"` (with `\"` and
  `\\`);
- comments: `#` at the start of a line or after a blank;
- a `---` first line.

Not taken: anchors and aliases, tags, multi-line scalars (`|`, `>`), flow
mappings (`{...}`), lists of mappings, tabs for indentation.

## Errors

Every mistake stops vkt before it loads a driver, with the file, the line
and what was wrong, and exit status 2:

    bench.yaml:3: bench.n: expected a whole number from 50 to 10000000, not '10'
    bench.yaml:4: unknown key bench.warmup (n, warm_up_ms, runs, delay_s)
    all.yaml:1: a flow sequence must end with ] on the same line
    all.yaml:3: tabs are not allowed for indentation
    cannot open nothere.yaml

Unknown keys are errors rather than ignored, so a misspelt setting does not
silently keep its default.

## Examples

`configs/` has three:

| File | Runs |
|---|---|
| [`all.yaml`](../configs/all.yaml) | every test but the benchmarks on `./libvulkan_malisx2.so`, strict |
| [`eden.yaml`](../configs/eden.yaml) | the features Eden needs, without the pipeline cache rounds, strict |
| [`bench.yaml`](../configs/bench.yaml) | the benchmarks with the published settings; the driver comes from the command line |

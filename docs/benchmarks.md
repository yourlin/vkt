# Benchmarks

The `bench` group measures how long a driver takes to record and run
batches of dispatches and draws. Its tests run only when selected:

    vkt -sys bench
    vkt ./libvulkan_malisx2.so -c configs/bench.yaml

Settings: [`bench`](configuration.md#bench) in the configuration file.

## What is measured

For each measurement, a command buffer with a batch of the same dispatch or
draw is recorded and submitted alone, [`runs`](configuration.md#benchruns)
times. Two times are taken per run:

- **record**: CPU time from allocating and beginning the command buffer to
  ending it;
- **gpu**: time from `vkQueueSubmit` to the return of `vkQueueWaitIdle`. It
  is measured on the CPU, so it includes the submission and the wait, not
  only the GPU's work; with large batches that overhead is small next to
  the work.

The first run is a warm-up and not counted; each printed time is the
smallest of the other runs, record and gpu taken separately. Per-item times
divide by the batch size.

With [`warm_up_ms`](configuration.md#benchwarm_up_ms), the GPU is kept busy
before the first measurement, so the first ones do not run at low clocks;
[`delay_s`](configuration.md#benchdelay_s) sleeps before each measurement.

## Measurements

`n` is [`bench.n`](configuration.md#benchn) (default 5000).

| Test | Line | Batch | What one item is |
|---|---|---|---|
| `bench/shared-memory` | `shared-memory dispatch` | n/5 | 64 x 1 workgroups of 64 invocations, each reversing its ids through shared memory |
| | `shared-memory 1x64 dispatch` | n/5 | the same, 1 x 64 workgroups |
| | `shared-memory 8x8 dispatch` | n/5 | the same, 8 x 8 workgroups |
| `bench/chain-barrier` | `chain+barrier 64x1` ... `16x32` | n/5 | a dispatch of 64x1, 1x64, 8x8, 2x32, 1x512 or 16x32 workgroups, each invocation following a chain of 16 dependent loads through a 4096-word table; a compute-to-compute barrier between each two |
| `bench/draws` | `plain draw` | n | a triangle, colour pushed once |
| | `plain draw + push constants` | n | a triangle, colour pushed before each draw |
| `bench/geometry` | `geometry shader draw` | n/10 | a triangle through a geometry shader that emits it twice |
| | `geometry shader, 64 inst.` | n/50 | the same, 64 instances |
| `bench/tessellation` | `tessellation draw` | n/10 | a quad patch, constant levels of 4 |
| | `tessellation, 64 patches` | n/50 | the same, 64 instances |
| | `tess dynamic levels` | n/10 | a quad patch, levels computed by the control shader |
| | `tess dynamic, 64 patches` | n/50 | the same, 64 instances |

Draws go to one render pass on a 256 x 256 RGBA8 target. A benchmark of a
feature the driver does not offer is skipped like a test.

## Output

    warm-up <ms> ms, <n> submits
    <name>  <count> disp.  record <ms> ms (<us> us/disp)  gpu <ms> ms (<us> us/disp) ok
    <name>  pipeline create <ms> ms
    <name>  <count> draws  record <ms> ms (<us> us/draw)  gpu <ms> ms (<us> us/draw)

- Dispatch lines end with `ok`, or `WRONG RESULT` when the output buffer is
  not what the shaders should have written (every output word is checked
  after the last run); a wrong result is also a failure of the run.
- `pipeline create` is the time of the `vkCreateGraphicsPipelines` call for
  the measurement's pipeline, without a pipeline cache.

The line format is meant to be parsed; keep it when changing the code.

## Comparing drivers

- Run the drivers alternately, several rounds, and compare medians: a
  phone's clocks and temperature change from one run to the next.
- Compare numbers measured with the same vkt version and settings only.
- The device is created without `robustBufferAccess`, on every driver.

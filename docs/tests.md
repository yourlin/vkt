# Tests

Every test renders offscreen or writes a buffer, reads the result back and
checks it on the CPU. A test whose "Needs" the driver does not offer is
skipped (`SKIP`), or fails with [`--strict`](usage.md#arguments).

The device is created with every feature the driver reports, except
`robustBufferAccess` (no test needs it, and its bounds checks would weigh
on the benchmarks), and with these extensions when the driver has them:
`VK_EXT_host_query_reset`, `VK_EXT_shader_viewport_index_layer`,
`VK_KHR_shader_float_controls`, `VK_KHR_sampler_mirror_clamp_to_edge`,
`VK_KHR_driver_properties`, `VK_KHR_draw_indirect_count`,
`VK_EXT_shader_demote_to_helper_invocation`.

Render targets are `VK_FORMAT_R8G8B8A8_UNORM` unless said otherwise; "lit"
means a channel above 64 on a black clear.

## basic

### basic/info

Prints, without checking:

- `device:` name, Vulkan version, device ID, and the driver's name and info
  (`VK_KHR_driver_properties`);
- `limits:` `lineWidthRange`, `maxViewports`, `maxDrawIndirectCount`;
- `eden mandatory:` what is missing of the 33 features and extensions the
  Eden emulator requires ([eden-emu/eden#4597](https://git.eden-emu.dev/eden-emu/eden/issues/4597)),
  each as `-name`, and their count. The 33: 24 core features
  (`robustBufferAccess`, `imageCubeArray`, `independentBlend`,
  `geometryShader`, `tessellationShader`, `sampleRateShading`,
  `dualSrcBlend`, `logicOp`, `multiDrawIndirect`,
  `drawIndirectFirstInstance`, `depthClamp`, `depthBiasClamp`,
  `fillModeNonSolid`, `wideLines`, `largePoints`, `multiViewport`,
  `samplerAnisotropy`, `occlusionQueryPrecise`,
  `vertexPipelineStoresAndAtomics`, `fragmentStoresAndAtomics`,
  `shaderImageGatherExtended`, `shaderStorageImageWriteWithoutFormat`,
  `shaderClipDistance`, `shaderCullDistance`); `shaderDrawParameters`,
  `variablePointers`, `variablePointersStorageBuffer`, `hostQueryReset`,
  `shaderDemoteToHelperInvocation`; and the extensions
  `VK_KHR_shader_float_controls`, `VK_EXT_host_query_reset`,
  `VK_KHR_sampler_mirror_clamp_to_edge`, `VK_KHR_driver_properties`.

### basic/compute

Needs: nothing.

A compute shader of 64 invocations per workgroup, 1024 workgroups, writes
`3i + 1` into word `i` of a 65536-word storage buffer. Checks every word.

### basic/draw

Needs: nothing. Image: `basic_draw.ppm`.

On a 256 x 256 target cleared blue: an indexed quad (16-bit indices) over
the left half, red at its four vertices, and a triangle on the right, green
at its three; the fragment shader multiplies the interpolated colour by a
uniform-buffer tint of 1. Checks five pixels, each channel within 2: two in
the quad (red), one in the triangle (green), one beside it and one corner
(blue).

## eden

### eden/variable-pointers

Needs: `variablePointers`, `variablePointersStorageBuffer`.

A compute shader in SPIR-V assembly (`shaders/t2_vptr.spvasm`; GLSL cannot
express it) selects, per invocation, a pointer to one of two storage buffers
with `OpSelect` (odd ids: the second, even ids: the first) and stores
`id + 7` through it. Checks all 4096 words of both buffers: the chosen
buffer's word is `id + 7`, the other's is still 0.

### eden/draw-parameters

Needs: `shaderDrawParameters`, and `vkCmdDrawIndirectCountKHR` or
`vkCmdDrawIndirectCount`.

An indirect-count draw on a 64 x 16 `R32G32B32A32_UINT` target, with room
for 4 draw commands and a count buffer of 3. Command `i` draws a strip over
column `i` of 4, with first vertex `10i` and first instance `5 + i`; the
fragment shader writes `gl_DrawID`, `gl_BaseVertex`, `gl_BaseInstance`,
`gl_InstanceIndex`. Checks columns 0 to 2 hold `i, 10i, 5+i, 5+i` and
column 3 still holds the clear value `0xdead`.

### eden/logic-op

Needs: `logicOp`.

A full-target triangle with logic op `XOR`, colour `(55, 0f, f0, ff)`, onto
a clear of `(ff, ff, 00, 00)`. Checks the result is `(aa, f0, f0, ff)`
exactly.

### eden/wide-lines

Needs: `wideLines`.

A horizontal line through the middle of a 64 x 64 target, at line width 1
and 5 (dynamic state). Counts the lit rows in the middle column: 1 or 2 at
width 1, 4 to 6 at width 5.

### eden/host-query-reset

Needs: `hostQueryReset` (`VK_EXT_host_query_reset`).

Twice: resets an occlusion query from the host (`vkResetQueryPool`), checks
`vkGetQueryPoolResults` returns `VK_NOT_READY` with availability 0, then
counts a full-target draw on 64 x 64. Checks 4096 samples
(`occlusionQueryPrecise`), or a nonzero count without precise queries.

## stages

### stages/cull-distance

Needs: `shaderCullDistance`, `shaderClipDistance`. Image: `stages_cull.ppm`.

Three triangles side by side on 192 x 64:

1. cull distance −1 at every vertex: culled, nothing lit;
2. cull distance −1 at the apex only: drawn whole;
3. clip distance −1 at the apex: the half nearer the apex clipped.

Checks a pixel near each triangle's base and near its apex.

### stages/multi-viewport

Needs: `multiViewport`, `maxViewports` ≥ 4,
`VK_EXT_shader_viewport_index_layer`. Image: `stages_mvp.ppm`.

On 64 x 64, four viewports (the quadrants); the last one's scissor ends at
x = 52. A full-target triangle drawn with 4 instances, each sent to viewport
`gl_InstanceIndex` from the vertex shader; the fragment shader colours by
`gl_ViewportIndex` (red, green, blue, yellow). Checks one pixel per
quadrant, and that the scissored-off part of the last is black.

### stages/polygon-mode

Needs: `fillModeNonSolid`. Images: `stages_point.ppm`, `stages_line.ppm`,
`stages_strip.ppm`.

On 64 x 64, a triangle with corners at pixels (16,48), (48,48), (32,16):

- polygon mode point: the three corners lit (point size 3), the edges'
  middles and the inside not;
- polygon mode line, not indexed and 16-bit indexed: the three edges lit,
  the inside not;
- plain line lists over the same edges (16-bit indexed, 32-bit indexed,
  not indexed): the three lines lit;
- polygon mode line on a strip of two triangles: the square's outline and
  its diagonal lit, the middle of each half not.

"Lit" for an edge or corner: anywhere within 1 pixel of the point.

### stages/geometry-shader

Needs: `geometryShader`. Images: `stages_gs_pass.ppm`, `stages_gs_quad.ppm`,
`stages_gs_static.ppm`.

- A triangle on the left half of 128 x 64, coloured red, green, blue at its
  vertices; the geometry shader emits it and a copy shifted right by half
  the target. Checks the copy has the same colour (within 1) at the same
  place, and black outside.
- Four points on 128 x 32; the geometry shader turns even primitive IDs
  into quads coloured `(1, id/4, 0)` and emits nothing for odd ones. Checks
  quads 0 and 2 with their colours, nothing at 1 and 3, black at the edge.
- A geometry shader with a fixed output (no loop) that moves one vertex,
  drawn with 2 instances of the same triangle. Checks the image is the same
  as with 1 instance, and lit.

### stages/tessellation

Needs: `tessellationShader`. Images: `stages_ts_quad.ppm`,
`stages_ts_tri.ppm`, `stages_ts_frac.ppm`, `stages_ts_frac_tri.ppm`; and,
only when the image differs, `stages_ts_dyn0.ppm` to `stages_ts_dyn2.ppm`.

On 256 x 256, a quad patch with corners at ±0.9, coloured with its
tessellation coordinates:

- levels computed by the control shader (from `gl_PrimitiveID`, 0 here)
  give the same image as constant levels of 4;
- 4 instances of the patch give the same image as 1;
- the colour is about 0.5 at the centre, about 0 and 1 at opposite corners,
  black outside.

A triangle patch, level 3, coloured with its barycentric coordinates:
checks they sum to 1 (255 within 5) at the centre, black outside.

Fractional spacing with uneven levels (quads: fractional odd, outer
3.3, 9.7, 5.1, 13.4, inner 7.6, 4.2; triangles: fractional even, outer
6.5, 2.2, 11.8, inner 8.9): computed levels give the same images as
constant ones, and both patches are drawn.

### stages/pipeline-cache

Needs: `geometryShader` or `tessellationShader`.

Runs `stages/geometry-shader` and `stages/tessellation` twice more: first
with their pipelines created through an empty pipeline cache, then through
a second cache created from the first one's `vkGetPipelineCacheData`. Their
checks print again in each round. Then checks the serialized data is not
empty and the second round failed nothing, and prints the data size and the
pipeline creation time of both rounds.

## poly

Corner cases of geometry and tessellation shaders that a driver
implementing them in software (vertex shaders fused into the next stage,
compute jobs before the draw, as Mesa's libpoly does) can get wrong.

### poly/tess-patch-size

Needs: `tessellationShader`.

Twelve vertices on a line, drawn as patches of 4 (3 patches) and then of 3
(4 patches) with the same shaders, both pipelines created through one
pipeline cache; then the other way round, with a new cache. The control
shader puts a small square at the mean of each patch's vertices, red for
patches of 3 and green for 4. Checks every square is in place and coloured
right, and nothing is where the other size would put one. A driver that
caches a control shader compiled for one patch size and reuses it for
another fails the second pipeline of each pair.

### poly/relaxed-precision

Needs: `geometryShader`, `tessellationShader` (each part is skipped on its
own).

A vertex shader writes `2, 0.5, -3, 100` to a `mediump` output, exact in 16
bits; it is read by the fragment shader directly, by a geometry shader, and
by a tessellation control shader, and written to an
`R32G32B32A32_SFLOAT` target. Checks the values within 0.1 %. A stage that
reads a 16-bit output back as 32 bits gets something else (0, or NaN).

### poly/tess-front-face

Needs: `tessellationShader`.

One triangle patch, level 1, with vertex order `cw` and `ccw` and the domain
origin upper and lower left (`VkPipelineTessellationDomainOriginStateCreateInfo`).
The fragment shader is green on front faces and red on back faces; the draw
is repeated with back faces culled. Checks each combination against the
spec's winding rule: for this patch, `ccw` with an upper-left origin is
front-facing (`VK_FRONT_FACE_COUNTER_CLOCKWISE`), and `cw` and a lower-left
origin each turn it over.

### poly/viewport-index

Needs: `multiViewport` and `maxViewports` ≥ 4; `geometryShader` for the
first part; `tessellationShader` and `VK_EXT_shader_viewport_index_layer`
for the second. Images: `poly_vp_gs.ppm`, `poly_vp_tes.ppm`.

On 64 x 64, four viewports (the quadrants). A geometry shader with 4
invocations sends invocation k's copy of a full-target triangle to
viewport k; then a tessellation evaluation shader sends patch k of four
full-target quads to viewport k. The fragment shader colours by
`gl_ViewportIndex`. Checks each quadrant has its viewport's colour.

### poly/compute-in-pass

Needs: `tessellationShader` or `fillModeNonSolid`. Image (only when it
fails): `poly_compute_in_pass.ppm`.

A render pass with a slow draw over every pixel of 64 x 64 (a vertex shader
with a long loop, one instance per pixel), then a draw that makes the
driver run a compute job on the vertex/tiler stream first (tessellation
with levels from the control shader, else line polygon mode), drawn
off-screen, then the end of the pass. Repeated 20 times; checks no pixel is
ever missing. It is a race: a pass is enough to show a missing wait, not to
prove there is none.

## bench

Benchmarks; they run only when selected. See [benchmarks.md](benchmarks.md).

| Test | Measures |
|---|---|
| `bench/shared-memory` | dispatches using shared memory, three workgroup grids |
| `bench/chain-barrier` | dependent dispatches with barriers, six grids |
| `bench/draws` | plain draws, with and without push constants per draw |
| `bench/geometry` | geometry shader draws, 1 and 64 instances |
| `bench/tessellation` | tessellation draws, constant and computed levels, 1 and 64 instances |

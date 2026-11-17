/*
 * SPDX-FileCopyrightText: 2026 yourlin
 * SPDX-License-Identifier: MIT-0
 *
 * Group poly: corner cases of geometry and tessellation shaders that a
 * driver implementing them in software (Mesa's libpoly: vertex shaders
 * fused into the next stage, compute jobs before the draw) can get wrong:
 * pipelines that differ only in patch size, relaxed-precision outputs into
 * the next stage, the front face of tessellated triangles, viewport
 * indices from the last pre-rasterization stage, and draws before a
 * compute job in the same render pass.
 */

static int
patch_draw(struct target *t, struct gfx_desc *d, const float *verts, uint32_t n, uint32_t instances,
           VkPipelineLayout pl, const char *ppm)
{
   d->layout = pl;
   VkPipeline p = mkgfx(t, d);
   if (!p)
      return 0;
   struct buf vb = {0};
   VkCommandBuffer cb = pass_begin(t, black);
   D(CmdBindPipeline)(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
   if (verts) {
      vb = mkbuf_data(verts, n * 8, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
      VkDeviceSize off = 0;
      D(CmdBindVertexBuffers)(cb, 0, 1, &vb.b, &off);
   }
   D(CmdDraw)(cb, n, instances, 0, 0);
   pass_end_readback(t, cb);
   if (ppm)
      write_ppm(t, ppm);
   if (verts)
      freebuf(&vb);
   D(DestroyPipeline)(dev, p, NULL);
   return 1;
}

/* ------------------------------------------------------------------------ */
/* Patch size                                                                */

/* Twelve vertices on a line, drawn as patches of 4 (3 patches) and then of
 * 3 (4 patches) with the same shaders, both pipelines created through one
 * pipeline cache: the control shader puts a square at the mean of each
 * patch's vertices, red for 3 and green for 4. A driver that compiles the
 * vertex shader into the control shader for one patch size, and finds that
 * in the cache for another, draws the second wrong. */
static void
test_tess_patch_size(void)
{
   if (!FEAT.tessellationShader) {
      NOT_OFFERED("tessellation");
      return;
   }
   float v[24];
   for (int i = 0; i < 12; i++) {
      v[2 * i] = -0.9f + 0.15f * i;
      v[2 * i + 1] = 0;
   }
   /* The squares' centres in pixels: (mean x + 1) * 128, row 32. */
   static const int c3[4] = {32, 90, 147, 205}, c4[3] = {42, 118, 195};
   VkPipelineLayout pl = mklayout(NULL, 0);
   struct target t = mktarget(256, 64);
   for (int order = 0; order < 2; order++) {
      /* Order 0: 4 then 3; order 1: 3 then 4; a new cache for each. */
      VkPipelineCacheCreateInfo cci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
      VkPipelineCache cache;
      CHECK(D(CreatePipelineCache)(dev, &cci, NULL, &cache));
      for (int k = 0; k < 2; k++) {
         const int size = (order == 0) == (k == 0) ? 4 : 3;
         struct gfx_desc d = {
            GFX_STAGE(vs, ts_vert),     GFX_STAGE(tcs, t4_psize_tesc), GFX_STAGE(tes, t4_pcol_tese),
            GFX_STAGE(fs, gs_col_frag), .patch_points = size,          .cache = cache,
         };
         if (!patch_draw(&t, &d, v, 12, 1, pl, NULL))
            continue;
         const int *want = size == 3 ? c3 : c4, *other = size == 3 ? c4 : c3;
         const int nw = size == 3 ? 4 : 3, no = size == 3 ? 3 : 4;
         int right = 0, stray = 0;
         for (int i = 0; i < nw; i++) {
            const uint8_t *p = pix(&t, want[i], 32);
            right += size == 3 ? (p[0] > 200 && p[1] < 50) : (p[1] > 200 && p[0] < 50);
         }
         for (int i = 0; i < no; i++)
            stray += lit(&t, other[i], 32);
         expect(right == nw && !stray,
                "tessellation patch size: patches of %d %s, %d of %d squares in place and "
                "coloured, %d where the other size puts them",
                size, k ? "after the other size" : "first", right, nw, stray);
      }
      D(DestroyPipelineCache)(dev, cache, NULL);
   }
   freetarget(&t);
   D(DestroyPipelineLayout)(dev, pl, NULL);
}

/* ------------------------------------------------------------------------ */
/* Relaxed precision                                                         */

/* A vertex shader's mediump output read by a geometry shader, by a
 * tessellation control shader, and (for comparison) by the fragment
 * shader. The values are exact in 16 bits, so only a stage that reads 16
 * bits back as 32 gets them wrong. */
static void
test_relaxed_precision(void)
{
   static const float want[4] = {2.0f, 0.5f, -3.0f, 100.0f};
   VkPipelineLayout pl = mklayout(NULL, 0);
   struct target t = mktarget_fmt(16, 16, VK_FORMAT_R32G32B32A32_SFLOAT, 16);
   for (int path = 0; path < 3; path++) {
      static const char *const name[3] = {"fragment shader", "geometry shader",
                                          "tessellation control shader"};
      if ((path == 1 && !FEAT.geometryShader) || (path == 2 && !FEAT.tessellationShader)) {
         NOT_OFFERED(path == 1 ? "geometry shader" : "tessellation");
         continue;
      }
      struct gfx_desc d = {
         GFX_STAGE(vs, t4_mp_vert),
         GFX_STAGE(fs, t4_val_frag),
         .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
         .no_vertex_input = 1,
      };
      if (path == 0) {
         d.fs = t4_mp_direct_frag;
         d.fs_size = sizeof(t4_mp_direct_frag);
      } else if (path == 1) {
         d.gs = t4_mp_geom;
         d.gs_size = sizeof(t4_mp_geom);
      } else {
         d.tcs = t4_mp_tesc;
         d.tcs_size = sizeof(t4_mp_tesc);
         d.tes = t4_mp_tese;
         d.tes_size = sizeof(t4_mp_tese);
         d.patch_points = 3;
      }
      if (!patch_draw(&t, &d, NULL, 3, 1, pl, NULL))
         continue;
      const float *got = (const float *)pix(&t, 8, 8);
      int ok = 1;
      for (int c = 0; c < 4; c++)
         ok &= got[c] == got[c] &&
               (got[c] - want[c]) * (got[c] - want[c]) <= (want[c] * 1e-3f) * (want[c] * 1e-3f);
      expect(ok,
             "relaxed precision: a vertex shader's mediump output read by the %s = "
             "%g, %g, %g, %g (want 2, 0.5, -3, 100)",
             name[path], got[0], got[1], got[2], got[3]);
   }
   freetarget(&t);
   D(DestroyPipelineLayout)(dev, pl, NULL);
}

/* ------------------------------------------------------------------------ */
/* Front face of tessellated triangles                                       */

/* One triangle patch, level 1, with vertex order cw or ccw and the domain
 * origin upper or lower left. The fragment shader shows gl_FrontFacing;
 * with back faces culled, only front faces are drawn.
 *
 * Expected, from the spec's winding rule ("Tessellator Vertex Winding
 * Order"): the patch (-0.8,-0.8), (0.8,-0.8), (0,0.8) maps (u,v) space to
 * the framebuffer with a positive determinant, so ccw with an upper-left
 * origin is counter-clockwise in the framebuffer too, front-facing for
 * VK_FRONT_FACE_COUNTER_CLOCKWISE; cw, and a lower-left origin, each turn
 * it over. */
static void
test_tess_front_face(void)
{
   if (!FEAT.tessellationShader) {
      NOT_OFFERED("tessellation");
      return;
   }
   static const float tri[] = {-0.8f, -0.8f, 0.8f, -0.8f, 0.0f, 0.8f};
   VkPipelineLayout pl = mklayout(NULL, 0);
   struct target t = mktarget(64, 64);
   for (int ll = 0; ll < 2; ll++) {
      for (int ccw = 0; ccw < 2; ccw++) {
         const int want_front = ccw != ll;
         int front = -1, drawn_culled = -1;
         for (int cull = 0; cull < 2; cull++) {
            struct gfx_desc d = {
               GFX_STAGE(vs, ts_vert),
               GFX_STAGE(tcs, t4_lvl1_tesc),
               GFX_STAGE(fs, t4_face_frag),
               .patch_points = 3,
               .lower_left = ll,
               .cull_mode = cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE,
            };
            d.tes = ccw ? t4_face_ccw_tese : t4_face_cw_tese;
            d.tes_size = ccw ? sizeof(t4_face_ccw_tese) : sizeof(t4_face_cw_tese);
            if (!patch_draw(&t, &d, tri, 3, 1, pl, NULL))
               continue;
            const uint8_t *p = pix(&t, 32, 36);
            if (!cull)
               front = p[1] > 200 ? 1 : p[0] > 200 ? 0 : -1;
            else
               drawn_culled = lit(&t, 32, 36);
         }
         expect(front == want_front && drawn_culled == want_front,
                "tessellation front face: %s, origin %s: %s (want %s), %s with back faces "
                "culled",
                ccw ? "ccw" : "cw", ll ? "lower left" : "upper left",
                front == 1   ? "front"
                : front == 0 ? "back"
                             : "not drawn",
                want_front ? "front" : "back", drawn_culled ? "drawn" : "culled");
      }
   }
   freetarget(&t);
   D(DestroyPipelineLayout)(dev, pl, NULL);
}

/* ------------------------------------------------------------------------ */
/* Viewport index from geometry and tessellation evaluation shaders         */

static void
test_viewport_index(void)
{
   if (!FEAT.multiViewport || props.limits.maxViewports < 4) {
      NOT_OFFERED("multi viewport");
      return;
   }
   static const VkViewport vp[4] = {
      {0, 0, 32, 32, 0, 1},
      {32, 0, 32, 32, 0, 1},
      {0, 32, 32, 32, 0, 1},
      {32, 32, 32, 32, 0, 1},
   };
   static const VkRect2D sc[4] = {
      {{0, 0}, {32, 32}},
      {{32, 0}, {32, 32}},
      {{0, 32}, {32, 32}},
      {{32, 32}, {32, 32}},
   };
   static const int at[4][2] = {{16, 16}, {48, 16}, {16, 48}, {48, 48}};
   static const uint8_t col[4][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}};
   float quads[32];
   for (int k = 0; k < 4; k++) {
      const float q[8] = {-1, -1, 1, -1, 1, 1, -1, 1};
      memcpy(quads + 8 * k, q, sizeof(q));
   }
   VkPipelineLayout pl = mklayout(NULL, 0);
   struct target t = mktarget(64, 64);
   for (int stage = 0; stage < 2; stage++) {
      const char *what = stage ? "tessellation evaluation shader" : "geometry shader";
      if (stage == 0 && !FEAT.geometryShader) {
         NOT_OFFERED("geometry shader");
         continue;
      }
      if (stage == 1 && (!FEAT.tessellationShader ||
                         !has_ext(VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME))) {
         NOT_OFFERED("viewport index from a tessellation evaluation shader");
         continue;
      }
      struct gfx_desc d = {
         GFX_STAGE(fs, t3_mvp_frag),
         .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
         .viewports = 4,
         .vp = vp,
         .sc = sc,
      };
      int drawn;
      if (stage == 0) {
         d.vs = t2_fsq_vert;
         d.vs_size = sizeof(t2_fsq_vert);
         d.gs = t4_vp_geom;
         d.gs_size = sizeof(t4_vp_geom);
         d.no_vertex_input = 1;
         drawn = patch_draw(&t, &d, NULL, 3, 1, pl, "poly_vp_gs.ppm");
      } else {
         d.vs = ts_vert;
         d.vs_size = sizeof(ts_vert);
         d.tcs = t4_lvl1q_tesc;
         d.tcs_size = sizeof(t4_lvl1q_tesc);
         d.tes = t4_vp_tese;
         d.tes_size = sizeof(t4_vp_tese);
         d.patch_points = 4;
         drawn = patch_draw(&t, &d, quads, 16, 1, pl, "poly_vp_tes.ppm");
      }
      if (!drawn)
         continue;
      int right = 0;
      char seen[96] = "";
      for (int k = 0; k < 4; k++) {
         const uint8_t *p = pix(&t, at[k][0], at[k][1]);
         right += p[0] == col[k][0] && p[1] == col[k][1] && p[2] == col[k][2];
         size_t l = strlen(seen);
         snprintf(seen + l, sizeof(seen) - l, "%s%u,%u,%u", k ? " / " : "", p[0], p[1], p[2]);
      }
      expect(right == 4, "viewport index from the %s: %d of 4 viewports drawn (%s)", what, right,
             seen);
   }
   freetarget(&t);
   D(DestroyPipelineLayout)(dev, pl, NULL);
}

/* ------------------------------------------------------------------------ */
/* Draws before a compute job on the vertex/tiler stream                     */

/* A render pass with a slow draw over every pixel, then a draw that runs a
 * compute job on the vertex/tiler stream first (tessellation with levels
 * from the control shader, else a line polygon mode; drawn off-screen),
 * then the end of the pass. If the end of the pass waits only for the work
 * after the compute job, fragments can run before the slow draw is tiled:
 * pixels missing. Repeated, as it is a race. */
static void
test_compute_in_pass(void)
{
   const int tess = FEAT.tessellationShader;
   if (!tess && !FEAT.fillModeNonSolid) {
      NOT_OFFERED("a draw with a vertex/tiler compute job (tessellation or line polygon mode)");
      return;
   }
   VkPipelineLayout pl = mklayout(NULL, 32);
   struct target t = mktarget(64, 64);
   struct gfx_desc slow = {
      GFX_STAGE(vs, t4_heavy_vert),
      GFX_STAGE(fs, t2_solid_frag),
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .no_vertex_input = 1,
      .layout = pl,
   };
   struct gfx_desc job = {
      GFX_STAGE(fs, t2_solid_frag),
      .layout = pl,
   };
   if (tess) {
      job.vs = ts_vert;
      job.vs_size = sizeof(ts_vert);
      job.tcs = ts_dyn_tesc;
      job.tcs_size = sizeof(ts_dyn_tesc);
      job.tes = ts_quad_tese;
      job.tes_size = sizeof(ts_quad_tese);
      job.patch_points = 4;
   } else {
      job.vs = pos2_vert;
      job.vs_size = sizeof(pos2_vert);
      job.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      job.polygon = VK_POLYGON_MODE_LINE;
   }
   VkPipeline ps = mkgfx(&t, &slow), pj = mkgfx(&t, &job);
   /* Off-screen: the job runs, nothing of it is drawn. */
   static const float off[] = {2.0f, 2.0f, 2.5f, 2.0f, 2.5f, 2.5f, 2.0f, 2.5f};
   struct buf vb = mkbuf_data(off, sizeof(off), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
   int worst = 0, bad_runs = 0;
   const int runs = 20;
   for (int run = 0; ps && pj && run < runs; run++) {
      VkCommandBuffer cb = pass_begin(&t, black);
      struct {
         float col[4];
         int32_t spin;
      } pc = {{1, 1, 1, 1}, 3000};
      D(CmdBindPipeline)(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, ps);
      D(CmdPushConstants)(cb, pl, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(pc), &pc);
      D(CmdDraw)(cb, 6, 64 * 64, 0, 0);
      D(CmdBindPipeline)(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pj);
      VkDeviceSize o = 0;
      D(CmdBindVertexBuffers)(cb, 0, 1, &vb.b, &o);
      D(CmdDraw)(cb, tess ? 4 : 3, 1, 0, 0);
      pass_end_readback(&t, cb);
      int missing = 0;
      for (uint32_t y = 0; y < t.h; y++) {
         for (uint32_t x = 0; x < t.w; x++)
            missing += !lit(&t, x, y);
      }
      if (missing) {
         bad_runs++;
         if (missing > worst) {
            worst = missing;
            write_ppm(&t, "poly_compute_in_pass.ppm");
         }
      }
   }
   if (ps && pj)
      expect(!bad_runs,
             "draws before a vertex/tiler compute job (%s) in the pass: %d of %d runs with "
             "pixels missing (at most %d of 4096)",
             tess ? "tessellation, computed levels" : "line polygon mode", bad_runs, runs, worst);
   freebuf(&vb);
   if (ps)
      D(DestroyPipeline)(dev, ps, NULL);
   if (pj)
      D(DestroyPipeline)(dev, pj, NULL);
   freetarget(&t);
   D(DestroyPipelineLayout)(dev, pl, NULL);
}

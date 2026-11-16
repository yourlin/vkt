/*
 * SPDX-FileCopyrightText: 2026 yourlin
 * SPDX-License-Identifier: MIT-0
 *
 * Group stages: the rasterization and shader-stage features Eden needs,
 * each checked by what it renders: cull and clip distances, multiple
 * viewports, point and line polygon modes, geometry and tessellation
 * shaders, and those pipelines from serialized pipeline cache data.
 */

static const float black[4] = {0, 0, 0, 1};
static const float white[4] = {1, 1, 1, 1};

/* Some lit pixel within r of (x, y). */
static int
lit_near(const struct target *t, int x, int y, int r)
{
   for (int dy = -r; dy <= r; dy++) {
      for (int dx = -r; dx <= r; dx++) {
         const int px = x + dx, py = y + dy;
         if (px >= 0 && py >= 0 && px < (int)t->w && py < (int)t->h && lit(t, px, py))
            return 1;
      }
   }
   return 0;
}

/* ------------------------------------------------------------------------ */
/* Cull and clip distances                                                   */

static void
test_cull_distance(void)
{
   if (!FEAT.shaderCullDistance || !FEAT.shaderClipDistance) {
      NOT_OFFERED("cull distance");
      return;
   }
   struct target t = mktarget(192, 64);
   VkPipelineLayout pl = mklayout(NULL, 16);
   struct gfx_desc gd = {
      GFX_STAGE(vs, t3_cull_vert),
      GFX_STAGE(fs, t2_solid_frag),
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .no_vertex_input = 1,
      .layout = pl,
   };
   VkPipeline p = mkgfx(&t, &gd);
   if (p) {
      VkCommandBuffer cb = pass_begin(&t, black);
      D(CmdBindPipeline)(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
      D(CmdPushConstants)(cb, pl, VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16, white);
      D(CmdDraw)(cb, 9, 1, 0, 0);
      pass_end_readback(&t, cb);
      write_ppm(&t, "stages_cull.ppm");
      /* Each triangle's centre line, near its base and near its apex. */
      unsigned base[3], apex[3];
      for (int i = 0; i < 3; i++) {
         base[i] = lit(&t, 32 + 64 * i, 13);
         apex[i] = lit(&t, 32 + 64 * i, 51);
      }
      expect(!base[0] && !apex[0], "cull distance: negative at every vertex culls (lit %u %u)",
             base[0], apex[0]);
      expect(base[1] && apex[1],
             "cull distance: negative at one vertex keeps the whole triangle (lit %u %u)", base[1],
             apex[1]);
      expect(base[2] && !apex[2],
             "clip distance: negative at the apex clips the apex half (lit %u %u)", base[2],
             apex[2]);
      D(DestroyPipeline)(dev, p, NULL);
   }
   D(DestroyPipelineLayout)(dev, pl, NULL);
   freetarget(&t);
}

/* ------------------------------------------------------------------------ */
/* Multiple viewports                                                        */

static void
test_multi_viewport(void)
{
   if (!FEAT.multiViewport || props.limits.maxViewports < 4 ||
       !has_ext(VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME)) {
      NOT_OFFERED("multi viewport");
      return;
   }
   struct target t = mktarget(64, 64);
   /* The four quadrants; the last one's scissor ends at x = 52. */
   const VkViewport vp[4] = {
      {0, 0, 32, 32, 0, 1},
      {32, 0, 32, 32, 0, 1},
      {0, 32, 32, 32, 0, 1},
      {32, 32, 32, 32, 0, 1},
   };
   const VkRect2D sc[4] = {
      {{0, 0}, {32, 32}},
      {{32, 0}, {32, 32}},
      {{0, 32}, {32, 32}},
      {{32, 32}, {20, 32}},
   };
   VkPipelineLayout pl = mklayout(NULL, 0);
   struct gfx_desc gd = {
      GFX_STAGE(vs, t3_mvp_vert),
      GFX_STAGE(fs, t3_mvp_frag),
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .no_vertex_input = 1,
      .viewports = 4,
      .vp = vp,
      .sc = sc,
      .layout = pl,
   };
   VkPipeline p = mkgfx(&t, &gd);
   if (p) {
      VkCommandBuffer cb = pass_begin(&t, black);
      D(CmdBindPipeline)(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
      D(CmdDraw)(cb, 3, 4, 0, 0);
      pass_end_readback(&t, cb);
      write_ppm(&t, "stages_mvp.ppm");
      const struct {
         const char *what;
         int x, y;
         uint8_t r, g, b;
      } c[] = {
         {"viewport 0 red", 16, 16, 255, 0, 0},         {"viewport 1 green", 48, 16, 0, 255, 0},
         {"viewport 2 blue", 16, 48, 0, 0, 255},        {"viewport 3 yellow", 40, 48, 255, 255, 0},
         {"viewport 3 scissored off", 56, 48, 0, 0, 0},
      };
      for (size_t i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
         const uint8_t *px = pix(&t, c[i].x, c[i].y);
         expect(px[0] == c[i].r && px[1] == c[i].g && px[2] == c[i].b,
                "multi viewport: %s at (%d,%d) = %u,%u,%u", c[i].what, c[i].x, c[i].y, px[0], px[1],
                px[2]);
      }
      D(DestroyPipeline)(dev, p, NULL);
   }
   D(DestroyPipelineLayout)(dev, pl, NULL);
   freetarget(&t);
}

/* ------------------------------------------------------------------------ */
/* Polygon modes and line lists                                              */

/* A triangle in a 64 x 64 target: corners at pixels (16,48), (48,48),
 * (32,16). As a line list: its three edges. */
static const float tri[] = {-0.5f, 0.5f, 0.5f, 0.5f, 0.0f, -0.5f};
static const float tri_lines[] = {-0.5f, 0.5f,  0.5f, 0.5f,  0.5f,  0.5f,
                                  0.0f,  -0.5f, 0.0f, -0.5f, -0.5f, 0.5f};
static const int tri_corner[3][2] = {{16, 48}, {48, 48}, {32, 16}};
static const int tri_edge[3][2] = {{32, 48}, {40, 32}, {24, 32}};
static const int tri_inside[2] = {32, 38};

enum index_kind { NOT_INDEXED, INDEX_16, INDEX_32 };

/* Draws count vertices of verts (or indexed by 0..count-1 pairs from idx)
 * with the given topology and polygon mode, white on black. */
static int
polygon_draw(struct target *t, VkPrimitiveTopology topo, VkPolygonMode mode, const float *verts,
             size_t verts_size, enum index_kind ik, const uint32_t *idx, uint32_t count,
             const char *ppm)
{
   VkPipelineLayout pl = mklayout(NULL, 16);
   struct gfx_desc gd = {
      GFX_STAGE(vs, pos2_vert),
      GFX_STAGE(fs, t2_solid_frag),
      .topology = topo,
      .polygon = mode,
      .layout = pl,
   };
   VkPipeline p = mkgfx(t, &gd);
   if (!p) {
      D(DestroyPipelineLayout)(dev, pl, NULL);
      return 0;
   }
   struct buf vb = mkbuf_data(verts, verts_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
   uint16_t idx16[16];
   for (uint32_t i = 0; ik == INDEX_16 && i < count; i++)
      idx16[i] = (uint16_t)idx[i];
   struct buf ib = {0};
   if (ik != NOT_INDEXED)
      ib = mkbuf_data(ik == INDEX_16 ? (const void *)idx16 : (const void *)idx,
                      count * (ik == INDEX_16 ? 2 : 4), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
   VkCommandBuffer cb = pass_begin(t, black);
   D(CmdBindPipeline)(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
   D(CmdPushConstants)(cb, pl, VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16, white);
   VkDeviceSize off = 0;
   D(CmdBindVertexBuffers)(cb, 0, 1, &vb.b, &off);
   if (ik != NOT_INDEXED) {
      D(CmdBindIndexBuffer)(cb, ib.b, 0,
                            ik == INDEX_16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
      D(CmdDrawIndexed)(cb, count, 1, 0, 0, 0);
   } else {
      D(CmdDraw)(cb, count, 1, 0, 0);
   }
   pass_end_readback(t, cb);
   if (ppm)
      write_ppm(t, ppm);
   freebuf(&vb);
   if (ik != NOT_INDEXED)
      freebuf(&ib);
   D(DestroyPipeline)(dev, p, NULL);
   D(DestroyPipelineLayout)(dev, pl, NULL);
   return 1;
}

static void
test_polygon_mode(void)
{
   if (!FEAT.fillModeNonSolid) {
      NOT_OFFERED("polygon mode");
      return;
   }
   struct target t = mktarget(64, 64);
   const uint32_t tri_idx[3] = {0, 1, 2};

   if (polygon_draw(&t, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_POINT, tri,
                    sizeof(tri), NOT_INDEXED, NULL, 3, "stages_point.ppm")) {
      int c[3], e = 0;
      for (int i = 0; i < 3; i++) {
         c[i] = lit_near(&t, tri_corner[i][0], tri_corner[i][1], 1);
         e |= lit_near(&t, tri_edge[i][0], tri_edge[i][1], 1);
      }
      const int in = lit(&t, tri_inside[0], tri_inside[1]);
      expect(c[0] && c[1] && c[2] && !e && !in,
             "polygon mode point: the three vertices lit, edge and inside not (%d%d%d %d %d)", c[0],
             c[1], c[2], e, in);
   }

   for (int indexed = 0; indexed < 2; indexed++) {
      if (!polygon_draw(&t, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_LINE, tri,
                        sizeof(tri), indexed ? INDEX_16 : NOT_INDEXED, tri_idx, 3,
                        indexed ? NULL : "stages_line.ppm"))
         continue;
      int e[3];
      for (int i = 0; i < 3; i++)
         e[i] = lit_near(&t, tri_edge[i][0], tri_edge[i][1], 1);
      const int in = lit(&t, tri_inside[0], tri_inside[1]);
      expect(e[0] && e[1] && e[2] && !in,
             "polygon mode line (%s): three edges lit, inside not (%d %d %d %d)",
             indexed ? "indexed list" : "list", e[0], e[1], e[2], in);
   }

   /* Plain line lists over the same edges: 16- and 32-bit indexed and not
    * indexed. */
   const uint32_t line_idx[6] = {0, 1, 1, 2, 2, 0};
   const struct {
      const char *what;
      enum index_kind ik;
   } plain[] = {
      {"plain indexed line list", INDEX_16},
      {"plain 32-bit indexed line list", INDEX_32},
      {"plain line list", NOT_INDEXED},
   };
   for (size_t k = 0; k < 3; k++) {
      const int ix = plain[k].ik != NOT_INDEXED;
      if (!polygon_draw(&t, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_POLYGON_MODE_FILL,
                        ix ? tri : tri_lines, ix ? sizeof(tri) : sizeof(tri_lines), plain[k].ik,
                        line_idx, 6, NULL))
         continue;
      int e[3];
      for (int i = 0; i < 3; i++)
         e[i] = lit_near(&t, tri_edge[i][0], tri_edge[i][1], 1);
      expect(e[0] && e[1] && e[2], "%s: three lines lit (%d %d %d)", plain[k].what, e[0], e[1],
             e[2]);
   }

   /* A strip of two triangles in line mode: the square's outline and its
    * diagonal from (48,16) to (16,48). */
   const float strip[] = {-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f};
   if (polygon_draw(&t, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_POLYGON_MODE_LINE, strip,
                    sizeof(strip), NOT_INDEXED, NULL, 4, "stages_strip.ppm")) {
      const int l = lit_near(&t, 16, 32, 1), r = lit_near(&t, 48, 32, 1),
                d = lit_near(&t, 32, 32, 1), h0 = lit(&t, 25, 25), h1 = lit(&t, 39, 39);
      expect(l && r && d && !h0 && !h1,
             "polygon mode line (strip): outline and diagonal lit, halves not (%d %d %d %d %d)", l,
             r, d, h0, h1);
   }
   freetarget(&t);
}

/* ------------------------------------------------------------------------ */
/* Geometry and tessellation shaders                                         */

/* Pipelines of the geometry and tessellation tests come from this cache
 * (VK_NULL_HANDLE: none); their creation time adds up in pipe_ms. */
static VkPipelineCache cur_cache;
static double pipe_ms;

static VkPipeline
mkgfx_timed(const struct target *t, struct gfx_desc *d)
{
   d->cache = cur_cache;
   const double t0 = vkt_now();
   VkPipeline p = mkgfx(t, d);
   pipe_ms += (vkt_now() - t0) * 1e3;
   return p;
}

/* Draws verts (vec2 each, n of them) with d's pipeline and the given
 * instance count, on black; returns 0 if the pipeline failed. */
static int
draw_with(struct target *t, struct gfx_desc *d, const float *verts, uint32_t n, uint32_t instances,
          const char *ppm)
{
   VkPipelineLayout pl = mklayout(NULL, 0);
   d->layout = pl;
   VkPipeline p = mkgfx_timed(t, d);
   if (!p) {
      D(DestroyPipelineLayout)(dev, pl, NULL);
      return 0;
   }
   struct buf vb = mkbuf_data(verts, n * 8, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
   VkCommandBuffer cb = pass_begin(t, black);
   D(CmdBindPipeline)(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
   VkDeviceSize off = 0;
   D(CmdBindVertexBuffers)(cb, 0, 1, &vb.b, &off);
   D(CmdDraw)(cb, n, instances, 0, 0);
   pass_end_readback(t, cb);
   if (ppm)
      write_ppm(t, ppm);
   freebuf(&vb);
   D(DestroyPipeline)(dev, p, NULL);
   D(DestroyPipelineLayout)(dev, pl, NULL);
   return 1;
}

static void
test_geometry_shader(void)
{
   if (!FEAT.geometryShader) {
      NOT_OFFERED("geometry shader");
      return;
   }

   /* A triangle in the left half; the shader adds a copy shifted right by
    * half the target. */
   {
      struct target t = mktarget(128, 64);
      const float v[] = {-0.9f, 0.8f, -0.1f, 0.8f, -0.5f, -0.8f};
      struct gfx_desc d = {
         GFX_STAGE(vs, gs_pass_vert),
         GFX_STAGE(gs, gs_pass_geom),
         GFX_STAGE(fs, gs_col_frag),
         .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      };
      if (draw_with(&t, &d, v, 3, 1, "stages_gs_pass.ppm")) {
         const uint8_t *a = pix(&t, 32, 40), *b = pix(&t, 96, 40);
         const unsigned o0 = pix(&t, 2, 2)[0], o1 = pix(&t, 66, 2)[0];
         int same = 1;
         for (int c = 0; c < 3; c++)
            same &= abs((int)a[c] - (int)b[c]) <= 1;
         expect(same && lit(&t, 32, 40) && !o0 && !o1,
                "geometry shader: triangle and its shifted copy, same colours "
                "(%u,%u,%u / %u,%u,%u; outside %u,%u)",
                a[0], a[1], a[2], b[0], b[1], b[2], o0, o1);
      }
      freetarget(&t);
   }

   /* Four points; the even ones become quads coloured (1, id/4, 0). */
   {
      struct target t = mktarget(128, 32);
      const float v[] = {-0.75f, 0, -0.25f, 0, 0.25f, 0, 0.75f, 0};
      struct gfx_desc d = {
         GFX_STAGE(vs, t3_pos_vert),
         GFX_STAGE(gs, t3_gs_quad_geom),
         GFX_STAGE(fs, gs_col_frag),
         .topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
      };
      if (draw_with(&t, &d, v, 4, 1, "stages_gs_quad.ppm")) {
         const uint8_t *p0 = pix(&t, 16, 16), *p1 = pix(&t, 48, 16), *p2 = pix(&t, 80, 16),
                       *p3 = pix(&t, 112, 16);
         const unsigned edge = pix(&t, 16, 2)[0];
         expect(p0[0] == 255 && p0[1] == 0 && p1[0] == 0 && p2[0] == 255 &&
                   abs((int)p2[1] - 128) <= 1 && p3[0] == 0 && edge == 0,
                "geometry shader: points to quads, odd ids none (%u,%u %u %u,%u %u, edge %u)",
                p0[0], p0[1], p1[0], p2[0], p2[1], p3[0], edge);
      }
      freetarget(&t);
   }

   /* A fixed output (no loop), drawn with 2 instances of the same
    * triangle: the same image as with 1. */
   {
      struct target t1 = mktarget(64, 64), t2 = mktarget(64, 64);
      const float v[] = {-0.8f, 0.8f, 0.4f, 0.8f, -0.4f, -0.8f};
      struct gfx_desc d = {
         GFX_STAGE(vs, gs_pass_vert),
         GFX_STAGE(gs, t3_gs_static_geom),
         GFX_STAGE(fs, gs_col_frag),
         .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      };
      if (draw_with(&t1, &d, v, 3, 1, NULL) &&
          draw_with(&t2, &d, v, 3, 2, "stages_gs_static.ppm")) {
         const uint8_t *c = pix(&t2, 24, 40);
         expect(same_image(&t1, &t2) && lit(&t2, 24, 40),
                "geometry shader: fixed output, 2 instances (%u,%u,%u)", c[0], c[1], c[2]);
      }
      freetarget(&t1);
      freetarget(&t2);
   }
}

/* A quad patch with corners at +-0.9 and a triangle patch. */
static const float ts_quad[] = {-0.9f, -0.9f, 0.9f, -0.9f, 0.9f, 0.9f, -0.9f, 0.9f};
static const float ts_tri[] = {-0.8f, 0.8f, 0.8f, 0.8f, 0.0f, -0.8f};

static int
ts_draw(struct target *t, const uint32_t *tcs, size_t tcs_size, const uint32_t *tes,
        size_t tes_size, int tri, uint32_t instances, const char *ppm)
{
   struct gfx_desc d = {
      GFX_STAGE(vs, ts_vert),      .tcs = tcs,
      .tcs_size = tcs_size,        .tes = tes,
      .tes_size = tes_size,        GFX_STAGE(fs, gs_col_frag),
      .patch_points = tri ? 3 : 4,
   };
   return draw_with(t, &d, tri ? ts_tri : ts_quad, tri ? 3 : 4, instances, ppm);
}
#define TS(name) name, sizeof(name)

static void
test_tessellation(void)
{
   if (!FEAT.tessellationShader) {
      NOT_OFFERED("tessellation");
      return;
   }
   struct target a = mktarget(256, 256), b = mktarget(256, 256);

   /* Quads, equal spacing, level 4: constant levels; computed levels and 4
    * instances must give the same image. */
   if (ts_draw(&a, TS(ts_tesc), TS(ts_quad_tese), 0, 1, "stages_ts_quad.ppm")) {
      if (ts_draw(&b, TS(ts_dyn_tesc), TS(ts_quad_tese), 0, 1, NULL)) {
         if (!same_image(&a, &b))
            write_ppm(&b, "stages_ts_dyn0.ppm");
         expect(same_image(&a, &b),
                "tessellation (quads, levels not constant): the same image as constant levels");
      }
      struct target c = mktarget(256, 256);
      if (ts_draw(&c, TS(ts_tesc), TS(ts_quad_tese), 0, 4, NULL))
         expect(same_image(&a, &c),
                "tessellation (quads, 4 instances, constant levels): the same image as one");
      freetarget(&c);
      /* The colour is the tessellation coordinate: about 0.5 at the centre,
       * 0 and 1 at opposite corners. */
      const uint8_t *m = pix(&a, 128, 128), *lo = pix(&a, 16, 16), *hi = pix(&a, 240, 240);
      const unsigned out = pix(&a, 4, 4)[0];
      expect(abs((int)m[0] - 128) <= 10 && abs((int)m[1] - 128) <= 10 && lo[0] <= 40 &&
                lo[1] <= 40 && hi[0] >= 215 && hi[1] >= 215 && out == 0,
             "tessellation (quads): the square lit with its tess coords "
             "(centre %u,%u; corners %u,%u %u,%u; outside %u)",
             m[0], m[1], lo[0], lo[1], hi[0], hi[1], out);
   }

   /* Triangles: the barycentrics, summing to 1. */
   if (ts_draw(&a, TS(t3_ts3_tesc), TS(t3_ts_tri_tese), 1, 1, "stages_ts_tri.ppm")) {
      const uint8_t *m = pix(&a, 128, 162), *o = pix(&a, 4, 4);
      const int sum = m[0] + m[1] + m[2];
      expect(sum >= 250 && sum <= 260 && !o[0] && !o[1] && !o[2],
             "tessellation (triangles): the triangle lit, barycentrics sum (%u,%u,%u), outside "
             "%u,%u,%u",
             m[0], m[1], m[2], o[0], o[1], o[2]);
   }

   /* Fractional spacing with uneven levels, quads and triangles: computed
    * levels give the same images as constant ones. */
   struct target qa = mktarget(256, 256), qb = mktarget(256, 256), ta = mktarget(256, 256),
                 tb = mktarget(256, 256);
   if (ts_draw(&qa, TS(t3_ts_frac_tesc), TS(t3_ts_frac_tese), 0, 1, "stages_ts_frac.ppm") &&
       ts_draw(&qb, TS(t3_ts_frac_dyn_tesc), TS(t3_ts_frac_tese), 0, 1, NULL) &&
       ts_draw(&ta, TS(t3_ts3_frac_tesc), TS(t3_ts_frac_tri_tese), 1, 1,
               "stages_ts_frac_tri.ppm") &&
       ts_draw(&tb, TS(t3_ts3_frac_dyn_tesc), TS(t3_ts_frac_tri_tese), 1, 1, NULL)) {
      const int same_q = same_image(&qa, &qb), same_t = same_image(&ta, &tb);
      if (!same_q)
         write_ppm(&qb, "stages_ts_dyn1.ppm");
      if (!same_t)
         write_ppm(&tb, "stages_ts_dyn2.ppm");
      expect(same_q && same_t,
             "tessellation (fractional, uneven levels, not constant): the same images");
      const uint8_t *q = pix(&qa, 128, 128), *t = pix(&ta, 128, 162);
      expect(lit(&qa, 128, 128) && lit(&ta, 128, 162),
             "tessellation (fractional, uneven levels): drawn (%u,%u / %u,%u)", q[0], q[1], t[0],
             t[1]);
   }
   freetarget(&qa);
   freetarget(&qb);
   freetarget(&ta);
   freetarget(&tb);
   freetarget(&a);
   freetarget(&b);
}

/* The geometry and tessellation tests with pipelines from cache; returns
 * the milliseconds spent creating their pipelines. */
static double
gs_ts_round(VkPipelineCache cache)
{
   cur_cache = cache;
   pipe_ms = 0;
   test_geometry_shader();
   test_tessellation();
   cur_cache = VK_NULL_HANDLE;
   return pipe_ms;
}

/* The geometry and tessellation tests twice more: once filling an empty
 * cache, once from a cache made from its serialized data. Both rounds must
 * pass like the uncached tests. */
static void
test_pipeline_cache(void)
{
   if (!FEAT.geometryShader && !FEAT.tessellationShader) {
      NOT_OFFERED("pipeline cache (geometry and tessellation)");
      return;
   }

   VkPipelineCacheCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
   VkPipelineCache a, b;
   CHECK(D(CreatePipelineCache)(dev, &ci, NULL, &a));
   const double compile_ms = gs_ts_round(a);
   size_t size = 0;
   CHECK(D(GetPipelineCacheData)(dev, a, &size, NULL));
   void *data = malloc(size ? size : 1);
   CHECK(D(GetPipelineCacheData)(dev, a, &size, data));
   ci.initialDataSize = size;
   ci.pInitialData = data;
   CHECK(D(CreatePipelineCache)(dev, &ci, NULL, &b));
   const int before = failures;
   const double cached_ms = gs_ts_round(b);
   expect(size > 0 && failures == before,
          "pipeline cache: geometry and tessellation pipelines from serialized cache data "
          "(%zu bytes; %.0f ms compiling, %.0f ms from the cache)",
          size, compile_ms, cached_ms);
   free(data);
   D(DestroyPipelineCache)(dev, a, NULL);
   D(DestroyPipelineCache)(dev, b, NULL);
}

/*
 * SPDX-FileCopyrightText: 2026 yourlin
 * SPDX-License-Identifier: MIT-0
 *
 * Group eden: features Eden needs, each checked by what it renders or
 * writes: variable pointers, draw parameters through an indirect-count
 * draw, logic ops, wide lines and host query reset.
 */

/* Variable pointers: odd invocations store into buffer 1, even ones into
 * buffer 0, id + 7 at index id; the other buffer's word stays 0. */
static void
test_variable_pointers(void)
{
   if (!vkt_features.vp.variablePointers || !vkt_features.vp.variablePointersStorageBuffer) {
      NOT_OFFERED("variable pointers");
      return;
   }
   const uint32_t n = 4096;
   struct buf b[2] = {
      mkbuf(n * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
      mkbuf(n * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
   };
   VkDescriptorSetLayout dsl =
      mkdsl(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2, VK_SHADER_STAGE_COMPUTE_BIT);
   VkDescriptorPool dp;
   VkDescriptorSet ds = mkset(dsl, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2, b, &dp);
   VkPipelineLayout pl = mklayout(&dsl, 0);
   VkShaderModule m = SHADER(t2_vptr);
   VkComputePipelineCreateInfo ci = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
                VK_SHADER_STAGE_COMPUTE_BIT, m, "main", NULL},
      .layout = pl,
   };
   VkPipeline p;
   CHECK(D(CreateComputePipelines)(dev, VK_NULL_HANDLE, 1, &ci, NULL, &p));
   VkCommandBuffer cb = begin();
   D(CmdBindPipeline)(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p);
   D(CmdBindDescriptorSets)(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, NULL);
   D(CmdDispatch)(cb, n / 64, 1, 1);
   VkMemoryBarrier mb = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
   };
   D(CmdPipelineBarrier)(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                         &mb, 0, NULL, 0, NULL);
   submit(cb);

   const uint32_t *b0 = b[0].p, *b1 = b[1].p;
   uint32_t good = 0;
   for (uint32_t i = 0; i < n; i++) {
      const uint32_t mine = i & 1 ? b1[i] : b0[i], other = i & 1 ? b0[i] : b1[i];
      good += mine == i + 7 && other == 0;
   }
   expect(good == n,
          "variable pointers: OpSelect between two SSBOs, %u/%u words right (b0[2]=%u b1[3]=%u)",
          good, n, b0[2], b1[3]);

   D(DestroyPipeline)(dev, p, NULL);
   D(DestroyShaderModule)(dev, m, NULL);
   D(DestroyPipelineLayout)(dev, pl, NULL);
   D(DestroyDescriptorPool)(dev, dp, NULL);
   D(DestroyDescriptorSetLayout)(dev, dsl, NULL);
   freebuf(&b[0]);
   freebuf(&b[1]);
}

/* Draw parameters: an indirect-count draw with room for 4 draws and a
 * count of 3. Draw i fills column i of an RGBA32UI target with its DrawID,
 * BaseVertex, BaseInstance and InstanceIndex; column 3 keeps the clear. */
static void
test_draw_parameters(void)
{
   PFN_vkCmdDrawIndirectCount dic =
      (PFN_vkCmdDrawIndirectCount)gdpa(dev, "vkCmdDrawIndirectCountKHR");
   if (!dic)
      dic = (PFN_vkCmdDrawIndirectCount)gdpa(dev, "vkCmdDrawIndirectCount");
   if (!vkt_features.dp.shaderDrawParameters || !dic) {
      NOT_OFFERED("draw parameters");
      return;
   }
   struct target t = mktarget_fmt(64, 16, VK_FORMAT_R32G32B32A32_UINT, 16);
   VkPipelineLayout pl = mklayout(NULL, 0);
   struct gfx_desc gd = {
      GFX_STAGE(vs, t2_ids_vert),
      GFX_STAGE(fs, t2_ids_frag),
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
      .no_vertex_input = 1,
      .layout = pl,
   };
   VkPipeline p = mkgfx(&t, &gd);
   if (p) {
      VkDrawIndirectCommand cmds[4];
      for (uint32_t i = 0; i < 4; i++)
         cmds[i] = (VkDrawIndirectCommand){4, 1, 10 * i, 5 + i};
      const uint32_t count = 3;
      struct buf ib = mkbuf_data(cmds, sizeof(cmds), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
      struct buf cntb = mkbuf_data(&count, sizeof(count), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
      VkClearValue cv = {.color.uint32 = {0xdead, 0xdead, 0xdead, 0xdead}};
      VkCommandBuffer cb = pass_begin_cv(&t, cv);
      D(CmdBindPipeline)(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
      dic(cb, ib.b, 0, cntb.b, 0, 4, sizeof(VkDrawIndirectCommand));
      pass_end_readback(&t, cb);
      for (uint32_t i = 0; i < 3; i++) {
         const uint32_t *v = (const uint32_t *)pix(&t, i * 16 + 8, 8);
         expect(v[0] == i && v[1] == 10 * i && v[2] == 5 + i && v[3] == 5 + i,
                "draw parameters: indirect-count draw %u reads DrawID %u BaseVertex %u "
                "BaseInstance %u InstanceIndex %u (want %u %u %u %u)",
                i, v[0], v[1], v[2], v[3], i, 10 * i, 5 + i, 5 + i);
      }
      const uint32_t *v = (const uint32_t *)pix(&t, 3 * 16 + 8, 8);
      expect(v[0] == 0xdead, "draw parameters: count 3 leaves column 3 clear (0x%x)", v[0]);
      freebuf(&ib);
      freebuf(&cntb);
      D(DestroyPipeline)(dev, p, NULL);
   }
   D(DestroyPipelineLayout)(dev, pl, NULL);
   freetarget(&t);
}

/* Logic op XOR of a pushed colour onto the clear colour. */
static void
test_logic_op(void)
{
   if (!FEAT.logicOp) {
      NOT_OFFERED("logic op");
      return;
   }
   struct target t = mktarget(16, 16);
   VkPipelineLayout pl = mklayout(NULL, 16);
   struct gfx_desc gd = {
      GFX_STAGE(vs, t2_fsq_vert),
      GFX_STAGE(fs, t2_solid_frag),
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .no_vertex_input = 1,
      .logic_op = 1,
      .op = VK_LOGIC_OP_XOR,
      .layout = pl,
   };
   VkPipeline p = mkgfx(&t, &gd);
   if (p) {
      const uint8_t src[4] = {0x55, 0x0f, 0xf0, 0xff}, dst[4] = {0xff, 0xff, 0x00, 0x00};
      float col[4], clear[4];
      for (int c = 0; c < 4; c++) {
         col[c] = src[c] / 255.0f;
         clear[c] = dst[c] / 255.0f;
      }
      VkCommandBuffer cb = pass_begin(&t, clear);
      D(CmdBindPipeline)(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
      D(CmdPushConstants)(cb, pl, VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16, col);
      D(CmdDraw)(cb, 3, 1, 0, 0);
      pass_end_readback(&t, cb);
      const uint8_t *px = pix(&t, 8, 8);
      int ok = 1;
      for (int c = 0; c < 4; c++)
         ok &= px[c] == (src[c] ^ dst[c]);
      expect(ok,
             "logic op: XOR of (55,0f,f0,ff) and (ff,ff,00,00) = %02x,%02x,%02x,%02x (want "
             "aa,f0,f0,ff)",
             px[0], px[1], px[2], px[3]);
      D(DestroyPipeline)(dev, p, NULL);
   }
   D(DestroyPipelineLayout)(dev, pl, NULL);
   freetarget(&t);
}

/* Wide lines: the rows a horizontal line covers at width 1 and 5. */
static int
line_rows(VkPipeline p, VkPipelineLayout pl, struct target *t, float width)
{
   const float black[4] = {0, 0, 0, 1}, white[4] = {1, 1, 1, 1};
   VkCommandBuffer cb = pass_begin(t, black);
   D(CmdBindPipeline)(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
   D(CmdSetLineWidth)(cb, width);
   D(CmdPushConstants)(cb, pl, VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16, white);
   D(CmdDraw)(cb, 2, 1, 0, 0);
   pass_end_readback(t, cb);
   int rows = 0;
   for (uint32_t y = 0; y < t->h; y++)
      rows += lit(t, t->w / 2, y);
   return rows;
}

static void
test_wide_lines(void)
{
   if (!FEAT.wideLines) {
      NOT_OFFERED("wide lines");
      return;
   }
   struct target t = mktarget(64, 64);
   VkPipelineLayout pl = mklayout(NULL, 16);
   struct gfx_desc gd = {
      GFX_STAGE(vs, t2_line_vert),
      GFX_STAGE(fs, t2_solid_frag),
      .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
      .no_vertex_input = 1,
      .dynamic_line_width = 1,
      .layout = pl,
   };
   VkPipeline p = mkgfx(&t, &gd);
   if (p) {
      const int r1 = line_rows(p, pl, &t, 1.0f), r5 = line_rows(p, pl, &t, 5.0f);
      expect(r1 >= 1 && r1 <= 2 && r5 >= 4 && r5 <= 6,
             "wide lines: width 1 covers %d row(s), width 5 covers %d (want 1-2 and 4-6)", r1, r5);
      D(DestroyPipeline)(dev, p, NULL);
   }
   D(DestroyPipelineLayout)(dev, pl, NULL);
   freetarget(&t);
}

/* Host query reset: an occlusion query reset on the host is unavailable,
 * then counts a full-target draw; twice, so the second reset follows a
 * written result. */
static void
test_host_query_reset(void)
{
   PFN_vkResetQueryPool reset = (PFN_vkResetQueryPool)gdpa(dev, "vkResetQueryPoolEXT");
   if (!reset)
      reset = (PFN_vkResetQueryPool)gdpa(dev, "vkResetQueryPool");
   if (!vkt_features.hqr.hostQueryReset || !reset) {
      NOT_OFFERED("host query reset");
      return;
   }
   struct target t = mktarget(64, 64);
   VkPipelineLayout pl = mklayout(NULL, 16);
   struct gfx_desc gd = {
      GFX_STAGE(vs, t2_fsq_vert),
      GFX_STAGE(fs, t2_solid_frag),
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .no_vertex_input = 1,
      .layout = pl,
   };
   VkPipeline p = mkgfx(&t, &gd);
   VkQueryPoolCreateInfo qpi = {
      .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType = VK_QUERY_TYPE_OCCLUSION,
      .queryCount = 1,
   };
   VkQueryPool qp;
   CHECK(D(CreateQueryPool)(dev, &qpi, NULL, &qp));
   const int precise = FEAT.occlusionQueryPrecise;
   for (int round = 0; p && round < 2; round++) {
      reset(dev, qp, 0, 1);
      uint64_t r[2] = {0, 0};
      VkResult res =
         D(GetQueryPoolResults)(dev, qp, 0, 1, sizeof(r), r, sizeof(r),
                                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
      expect(res == VK_NOT_READY && r[1] == 0,
             "host query reset: round %d, query unavailable after reset (%d, avail %u)", round, res,
             (unsigned)r[1]);

      const float black[4] = {0, 0, 0, 1}, white[4] = {1, 1, 1, 1};
      VkCommandBuffer cb = pass_begin(&t, black);
      D(CmdBindPipeline)(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
      D(CmdPushConstants)(cb, pl, VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16, white);
      D(CmdBeginQuery)(cb, qp, 0, precise ? VK_QUERY_CONTROL_PRECISE_BIT : 0);
      D(CmdDraw)(cb, 3, 1, 0, 0);
      D(CmdEndQuery)(cb, qp, 0);
      pass_end_readback(&t, cb);
      uint64_t samples = 0;
      CHECK(D(GetQueryPoolResults)(dev, qp, 0, 1, sizeof(samples), &samples, sizeof(samples),
                                   VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
      const uint64_t want = (uint64_t)t.w * t.h;
      expect(precise ? samples == want : samples != 0,
             "host query reset: round %d, occlusion count %llu (want %s%llu)", round,
             (unsigned long long)samples, precise ? "" : "nonzero, precise ",
             (unsigned long long)want);
   }
   D(DestroyQueryPool)(dev, qp, NULL);
   if (p)
      D(DestroyPipeline)(dev, p, NULL);
   D(DestroyPipelineLayout)(dev, pl, NULL);
   freetarget(&t);
}

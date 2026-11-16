/*
 * SPDX-FileCopyrightText: 2026 yourlin
 * SPDX-License-Identifier: MIT-0
 *
 * Group basic: the device's identity and the features Eden needs, a
 * compute dispatch and a draw (an indexed quad and a triangle with
 * per-vertex colours), checked word by word and pixel by pixel.
 */

/* The core features Eden requires (eden-emu/eden#4597). */
#define CORE(f) {#f, offsetof(VkPhysicalDeviceFeatures, f)}
static const struct {
   const char *name;
   size_t off;
} eden_core[] = {
   CORE(robustBufferAccess),
   CORE(imageCubeArray),
   CORE(independentBlend),
   CORE(geometryShader),
   CORE(tessellationShader),
   CORE(sampleRateShading),
   CORE(dualSrcBlend),
   CORE(logicOp),
   CORE(multiDrawIndirect),
   CORE(drawIndirectFirstInstance),
   CORE(depthClamp),
   CORE(depthBiasClamp),
   CORE(fillModeNonSolid),
   CORE(wideLines),
   CORE(largePoints),
   CORE(multiViewport),
   CORE(samplerAnisotropy),
   CORE(occlusionQueryPrecise),
   CORE(vertexPipelineStoresAndAtomics),
   CORE(fragmentStoresAndAtomics),
   CORE(shaderImageGatherExtended),
   CORE(shaderStorageImageWriteWithoutFormat),
   CORE(shaderClipDistance),
   CORE(shaderCullDistance),
};

static const char *const eden_exts[] = {
   VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
   VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME,
   VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME,
   VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME,
};

static void
print_device(void)
{
   VkPhysicalDeviceDriverProperties drv = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
   };
   VkPhysicalDeviceProperties2 p2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      .pNext = has_ext(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME) ? &drv : NULL,
   };
   I(GetPhysicalDeviceProperties2)(pd, &p2);
   const VkPhysicalDeviceProperties *p = &p2.properties;
   printf("device: %s  api %u.%u.%u  id 0x%08x  driver '%s' '%s'\n", p->deviceName,
          VK_API_VERSION_MAJOR(p->apiVersion), VK_API_VERSION_MINOR(p->apiVersion),
          VK_API_VERSION_PATCH(p->apiVersion), p->deviceID, drv.driverName, drv.driverInfo);
   printf("limits: lineWidthRange %.1f..%.1f  maxViewports %u  maxDrawIndirectCount %u\n",
          p->limits.lineWidthRange[0], p->limits.lineWidthRange[1], p->limits.maxViewports,
          p->limits.maxDrawIndirectCount);

   /* Every missing one, as -name. */
   char line[1024] = "eden mandatory:";
   int missing = 0;
   size_t total = 0;
#define MISSING(cond, name)                                                                        \
   do {                                                                                            \
      total++;                                                                                     \
      if (!(cond)) {                                                                               \
         missing++;                                                                                \
         size_t l_ = strlen(line);                                                                 \
         snprintf(line + l_, sizeof(line) - l_, " -%s", name);                                     \
      }                                                                                            \
   } while (0)
   for (size_t i = 0; i < sizeof(eden_core) / sizeof(eden_core[0]); i++)
      MISSING(*(const VkBool32 *)((const char *)&FEAT + eden_core[i].off), eden_core[i].name);
   MISSING(vkt_features.dp.shaderDrawParameters, "shaderDrawParameters");
   MISSING(vkt_features.vp.variablePointers, "variablePointers");
   MISSING(vkt_features.vp.variablePointersStorageBuffer, "variablePointersStorageBuffer");
   MISSING(vkt_features.hqr.hostQueryReset, "hostQueryReset");
   MISSING(vkt_features.demote.shaderDemoteToHelperInvocation, "shaderDemoteToHelperInvocation");
   for (size_t i = 0; i < sizeof(eden_exts) / sizeof(eden_exts[0]); i++)
      MISSING(has_ext(eden_exts[i]), eden_exts[i]);
#undef MISSING
   puts(missing ? line : "eden mandatory: all present");
   printf("eden mandatory missing: %d of %zu\n", missing, total);
}

static void
test_compute(void)
{
   const uint32_t n = 65536;
   struct buf ssbo = mkbuf(n * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
   VkDescriptorSetLayout dsl =
      mkdsl(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
   VkDescriptorPool dp;
   VkDescriptorSet ds = mkset(dsl, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, &ssbo, &dp);
   VkPipelineLayoutCreateInfo pli = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &dsl,
   };
   VkPipelineLayout pl;
   CHECK(D(CreatePipelineLayout)(dev, &pli, NULL, &pl));
   VkShaderModule cm = SHADER(t1_fill_comp);
   VkComputePipelineCreateInfo cpci = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
                VK_SHADER_STAGE_COMPUTE_BIT, cm, "main", NULL},
      .layout = pl,
   };
   VkPipeline cp;
   CHECK(D(CreateComputePipelines)(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &cp));

   VkCommandBuffer cb = begin();
   D(CmdBindPipeline)(cb, VK_PIPELINE_BIND_POINT_COMPUTE, cp);
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

   const uint32_t *d = ssbo.p;
   uint32_t good = 0;
   for (uint32_t i = 0; i < n; i++)
      good += d[i] == i * 3 + 1;
   expect(good == n, "compute: %u/%u words correct (d[1]=%u d[N-1]=%u)", good, n, d[1], d[n - 1]);

   D(DestroyPipeline)(dev, cp, NULL);
   D(DestroyShaderModule)(dev, cm, NULL);
   D(DestroyPipelineLayout)(dev, pl, NULL);
   D(DestroyDescriptorPool)(dev, dp, NULL);
   D(DestroyDescriptorSetLayout)(dev, dsl, NULL);
   freebuf(&ssbo);
}

static void
check_px(const struct target *t, const char *what, int x, int y, const uint8_t want[4])
{
   const uint8_t *p = pix(t, x, y);
   int ok = 1;
   for (int c = 0; c < 4; c++)
      ok &= abs((int)p[c] - (int)want[c]) <= 2;
   expect(ok, "draw: %s at (%d,%d) = %u,%u,%u,%u", what, x, y, p[0], p[1], p[2], p[3]);
}

static void
test_draw(void)
{
   struct target t = mktarget(256, 256);
   /* The left half (an indexed quad, red) and a triangle on the right
    * (green at every vertex). */
   const float pos[] = {
      -1,   -1,    0,     -1, 0,    1,    -1, 1, /* quad */
      0.2f, -0.8f, 0.95f, 0,  0.2f, 0.8f,        /* triangle */
   };
   const float col[] = {
      1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
   };
   const uint16_t idx[] = {0, 1, 2, 0, 2, 3};
   const float tint[4] = {1, 1, 1, 1};
   struct buf vbuf = mkbuf_data(pos, sizeof(pos), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
   struct buf cbuf = mkbuf_data(col, sizeof(col), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
   struct buf ibuf = mkbuf_data(idx, sizeof(idx), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
   struct buf ubuf = mkbuf_data(tint, sizeof(tint), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
   VkDescriptorSetLayout dsl =
      mkdsl(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
   VkDescriptorPool dp;
   VkDescriptorSet ds = mkset(dsl, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, &ubuf, &dp);
   VkPipelineLayout pl = mklayout(&dsl, 0);
   struct gfx_desc gd = {
      GFX_STAGE(vs, t1_col_vert),
      GFX_STAGE(fs, t1_tint_frag),
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .colour = 1,
      .layout = pl,
   };
   VkPipeline gp = mkgfx(&t, &gd);
   if (gp) {
      const float blue[4] = {0, 0, 1, 1};
      VkCommandBuffer cb = pass_begin(&t, blue);
      D(CmdBindPipeline)(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, gp);
      D(CmdBindDescriptorSets)(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &ds, 0, NULL);
      VkBuffer vbs[2] = {vbuf.b, cbuf.b};
      VkDeviceSize offs[2] = {0, 0};
      D(CmdBindVertexBuffers)(cb, 0, 2, vbs, offs);
      D(CmdBindIndexBuffer)(cb, ibuf.b, 0, VK_INDEX_TYPE_UINT16);
      D(CmdDrawIndexed)(cb, 6, 1, 0, 0, 0);
      D(CmdDraw)(cb, 3, 1, 4, 0);
      pass_end_readback(&t, cb);
      write_ppm(&t, "basic_draw.ppm");

      const uint8_t red[4] = {255, 0, 0, 255}, green[4] = {0, 255, 0, 255},
                    blu[4] = {0, 0, 255, 255};
      check_px(&t, "left half red (indexed quad)", 32, 128, red);
      check_px(&t, "left half red near top", 120, 20, red);
      check_px(&t, "right triangle green (varyings)", 230, 128, green);
      check_px(&t, "right half outside triangle blue (clear)", 150, 230, blu);
      check_px(&t, "corner blue (clear)", 250, 5, blu);
      D(DestroyPipeline)(dev, gp, NULL);
   }
   D(DestroyPipelineLayout)(dev, pl, NULL);
   D(DestroyDescriptorPool)(dev, dp, NULL);
   D(DestroyDescriptorSetLayout)(dev, dsl, NULL);
   freebuf(&vbuf);
   freebuf(&cbuf);
   freebuf(&ibuf);
   freebuf(&ubuf);
   freetarget(&t);
}

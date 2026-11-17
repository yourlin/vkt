/*
 * SPDX-FileCopyrightText: 2026 yourlin
 * SPDX-License-Identifier: MIT-0
 *
 * Group blend: the blending an emulator needs for blend modes Vulkan's
 * fixed-function blending cannot express (PCSX2 and ARMSX2's PS2 blending,
 * for example): dual-source blending, and the colour attachment read in the
 * fragment shader in rasterization order (framebuffer fetch).
 */

/* ------------------------------------------------------------------------ */
/* Dual-source blending                                                      */

static float
blend_factor(VkBlendFactor f, const float s0[4], const float s1[4], const float d[4], int c)
{
   switch (f) {
   case VK_BLEND_FACTOR_ZERO:
      return 0;
   case VK_BLEND_FACTOR_ONE:
      return 1;
   case VK_BLEND_FACTOR_SRC_ALPHA:
      return s0[3];
   case VK_BLEND_FACTOR_DST_ALPHA:
      return d[3];
   case VK_BLEND_FACTOR_SRC1_COLOR:
      return s1[c];
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:
      return 1 - s1[c];
   case VK_BLEND_FACTOR_SRC1_ALPHA:
      return s1[3];
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:
      return 1 - s1[3];
   default:
      return -1;
   }
}

static float
clamp01(float v)
{
   return v < 0 ? 0 : v > 1 ? 1 : v;
}

/* Each case: the blend state, the two sources, on a clear of dst. */
static void
test_dual_source(void)
{
   if (!FEAT.dualSrcBlend) {
      NOT_OFFERED("dual-source blending");
      return;
   }
   static const float dst[4] = {0.2f, 0.6f, 1.0f, 0.5f};
   static const struct {
      const char *what;
      VkBlendFactor sc, dc, sa, da;
      VkBlendOp op;
      float c0[4], c1[4];
   } cases[] = {
      {"src + dst * src1 colour",
       VK_BLEND_FACTOR_ONE,
       VK_BLEND_FACTOR_SRC1_COLOR,
       VK_BLEND_FACTOR_ONE,
       VK_BLEND_FACTOR_ZERO,
       VK_BLEND_OP_ADD,
       {0.1f, 0.2f, 0.0f, 1.0f},
       {0.5f, 0.25f, 0.75f, 0.0f}},
      {"src * src1 alpha + dst * (1 - src1 alpha)",
       VK_BLEND_FACTOR_SRC1_ALPHA,
       VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA,
       VK_BLEND_FACTOR_ONE,
       VK_BLEND_FACTOR_ZERO,
       VK_BLEND_OP_ADD,
       {1.0f, 0.0f, 0.5f, 1.0f},
       {0.0f, 0.0f, 0.0f, 0.25f}},
      {"dst - src * src1 colour",
       VK_BLEND_FACTOR_SRC1_COLOR,
       VK_BLEND_FACTOR_ONE,
       VK_BLEND_FACTOR_ZERO,
       VK_BLEND_FACTOR_ONE,
       VK_BLEND_OP_REVERSE_SUBTRACT,
       {0.4f, 0.4f, 0.4f, 1.0f},
       {0.5f, 1.0f, 0.25f, 0.0f}},
      {"src * dst alpha + dst * src1 alpha",
       VK_BLEND_FACTOR_DST_ALPHA,
       VK_BLEND_FACTOR_SRC1_ALPHA,
       VK_BLEND_FACTOR_ONE,
       VK_BLEND_FACTOR_ZERO,
       VK_BLEND_OP_ADD,
       {0.6f, 0.2f, 0.0f, 1.0f},
       {0.0f, 0.0f, 0.0f, 0.5f}},
   };
   struct target t = mktarget(16, 16);
   VkPipelineLayout pl = mklayout(NULL, 32);
   for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      VkPipelineColorBlendAttachmentState bs = {
         .blendEnable = VK_TRUE,
         .srcColorBlendFactor = cases[i].sc,
         .dstColorBlendFactor = cases[i].dc,
         .colorBlendOp = cases[i].op,
         .srcAlphaBlendFactor = cases[i].sa,
         .dstAlphaBlendFactor = cases[i].da,
         .alphaBlendOp = cases[i].op,
         .colorWriteMask = 0xf,
      };
      struct gfx_desc d = {
         GFX_STAGE(vs, t2_fsq_vert),
         GFX_STAGE(fs, t5_dual_frag),
         .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
         .no_vertex_input = 1,
         .blend = &bs,
         .layout = pl,
      };
      VkPipeline p = mkgfx(&t, &d);
      if (!p)
         continue;
      VkCommandBuffer cb = pass_begin(&t, dst);
      D(CmdBindPipeline)(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
      float pc[8];
      memcpy(pc, cases[i].c0, 16);
      memcpy(pc + 4, cases[i].c1, 16);
      D(CmdPushConstants)(cb, pl, VK_SHADER_STAGE_ALL_GRAPHICS, 0, 32, pc);
      D(CmdDraw)(cb, 3, 1, 0, 0);
      pass_end_readback(&t, cb);
      const uint8_t *px = pix(&t, 8, 8);
      int ok = 1;
      uint8_t want[4];
      for (int c = 0; c < 4; c++) {
         const int alpha = c == 3;
         const VkBlendFactor sf = alpha ? cases[i].sa : cases[i].sc,
                             df = alpha ? cases[i].da : cases[i].dc;
         const float s = cases[i].c0[c] * blend_factor(sf, cases[i].c0, cases[i].c1, dst, c);
         const float e = dst[c] * blend_factor(df, cases[i].c0, cases[i].c1, dst, c);
         const float v = cases[i].op == VK_BLEND_OP_REVERSE_SUBTRACT ? e - s : s + e;
         want[c] = (uint8_t)(clamp01(v) * 255.0f + 0.5f);
         ok &= abs((int)px[c] - (int)want[c]) <= 2;
      }
      expect(ok, "dual-source blending: %s = %u,%u,%u,%u (want %u,%u,%u,%u)", cases[i].what, px[0],
             px[1], px[2], px[3], want[0], want[1], want[2], want[3]);
      D(DestroyPipeline)(dev, p, NULL);
   }
   D(DestroyPipelineLayout)(dev, pl, NULL);
   freetarget(&t);
}

/* ------------------------------------------------------------------------ */
/* Framebuffer fetch in rasterization order                                  */

/* The colour attachment as the subpass's input attachment too, in the
 * GENERAL layout, with rasterization-order access: 32 full-target
 * triangles each add 4/255 to red, as 32 draws and as one draw of 32
 * instances. Every read seeing the previous write gives 128 at the end;
 * a read that missed one gives less. */
static void
test_framebuffer_fetch(void)
{
   if (!vkt_features.roaa.rasterizationOrderColorAttachmentAccess) {
      NOT_OFFERED("rasterization-order colour attachment access");
      return;
   }
   struct target t = mktarget(64, 64);
   /* A render pass and framebuffer of its own, over the target's image. */
   VkAttachmentDescription at = {
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
   };
   VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_GENERAL};
   VkSubpassDescription sp = {
      .flags = VK_SUBPASS_DESCRIPTION_RASTERIZATION_ORDER_ATTACHMENT_COLOR_ACCESS_BIT_EXT,
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .inputAttachmentCount = 1,
      .pInputAttachments = &ref,
      .colorAttachmentCount = 1,
      .pColorAttachments = &ref,
   };
   VkSubpassDependency deps[2] = {
      {
         .srcSubpass = 0,
         .dstSubpass = VK_SUBPASS_EXTERNAL,
         .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
         .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
      },
      {
         .srcSubpass = 0,
         .dstSubpass = 0,
         .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
         .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
         .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
      },
   };
   VkRenderPassCreateInfo rpi = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &at,
      .subpassCount = 1,
      .pSubpasses = &sp,
      .dependencyCount = 2,
      .pDependencies = deps,
   };
   VkRenderPass rp;
   CHECK(D(CreateRenderPass)(dev, &rpi, NULL, &rp));
   VkFramebufferCreateInfo fbi = {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass = rp,
      .attachmentCount = 1,
      .pAttachments = &t.view,
      .width = t.w,
      .height = t.h,
      .layers = 1,
   };
   VkFramebuffer fb;
   CHECK(D(CreateFramebuffer)(dev, &fbi, NULL, &fb));
   /* The target's own pass and framebuffer swapped for these. */
   VkRenderPass own_rp = t.rp;
   VkFramebuffer own_fb = t.fb;
   t.rp = rp;
   t.fb = fb;

   VkDescriptorSetLayout dsl =
      mkdsl(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
   VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1};
   VkDescriptorPoolCreateInfo dpi = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes = &ps,
   };
   VkDescriptorPool dp;
   CHECK(D(CreateDescriptorPool)(dev, &dpi, NULL, &dp));
   VkDescriptorSetAllocateInfo dai = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = dp,
      .descriptorSetCount = 1,
      .pSetLayouts = &dsl,
   };
   VkDescriptorSet ds;
   CHECK(D(AllocateDescriptorSets)(dev, &dai, &ds));
   VkDescriptorImageInfo ii = {VK_NULL_HANDLE, t.view, VK_IMAGE_LAYOUT_GENERAL};
   VkWriteDescriptorSet w = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = ds,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
      .pImageInfo = &ii,
   };
   D(UpdateDescriptorSets)(dev, 1, &w, 0, NULL);
   VkPipelineLayout pl = mklayout(&dsl, 0);
   struct gfx_desc d = {
      GFX_STAGE(vs, t2_fsq_vert),
      GFX_STAGE(fs, t5_fetch_frag),
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .no_vertex_input = 1,
      .blend_flags =
         VK_PIPELINE_COLOR_BLEND_STATE_CREATE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_BIT_EXT,
      .layout = pl,
   };
   VkPipeline p = mkgfx(&t, &d);
   for (int one_draw = 0; p && one_draw < 2; one_draw++) {
      const float clear[4] = {0, 0.5f, 0, 1};
      VkCommandBuffer cb = pass_begin(&t, clear);
      D(CmdBindPipeline)(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
      D(CmdBindDescriptorSets)(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &ds, 0, NULL);
      if (one_draw) {
         D(CmdDraw)(cb, 3, 32, 0, 0);
      } else {
         for (int i = 0; i < 32; i++)
            D(CmdDraw)(cb, 3, 1, 0, 0);
      }
      pass_end_readback(&t, cb);
      int lo = 255, hi = 0, bad = 0;
      for (uint32_t y = 0; y < t.h; y++) {
         for (uint32_t x = 0; x < t.w; x++) {
            const int r = pix(&t, x, y)[0];
            lo = r < lo ? r : lo;
            hi = r > hi ? r : hi;
            bad += abs(r - 128) > 1 || abs((int)pix(&t, x, y)[1] - 128) > 1;
         }
      }
      expect(!bad,
             "framebuffer fetch in rasterization order: 32 increments as %s, red %d..%d "
             "(want 128), %d of %u pixels wrong",
             one_draw ? "one draw of 32 instances" : "32 draws", lo, hi, bad, t.w * t.h);
   }
   if (p)
      D(DestroyPipeline)(dev, p, NULL);
   D(DestroyPipelineLayout)(dev, pl, NULL);
   D(DestroyDescriptorPool)(dev, dp, NULL);
   D(DestroyDescriptorSetLayout)(dev, dsl, NULL);
   t.rp = own_rp;
   t.fb = own_fb;
   D(DestroyFramebuffer)(dev, fb, NULL);
   D(DestroyRenderPass)(dev, rp, NULL);
   freetarget(&t);
}

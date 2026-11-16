/*
 * SPDX-FileCopyrightText: 2026 yourlin
 * SPDX-License-Identifier: MIT-0
 *
 * Group bench: CPU recording time and GPU time (submit to idle) of
 * dispatches and draws. Not run unless selected (vkt <driver> bench).
 *
 * The counts follow bench.n (default 5000): n plain draws, n/5 dispatches,
 * n/10 geometry and tessellation draws, n/50 of the 64-instance ones. Each
 * measurement runs bench.runs times (default 6); the first is a warm-up,
 * the best of the others is printed. bench.warm_up_ms keeps the GPU busy
 * before the first measurement; bench.delay_s sleeps before each one.
 *
 * Output lines (parsed by the result scripts, keep their format):
 *   <name>  <count> disp.  record <ms> ms (<us> us/disp)  gpu <ms> ms (<us> us/disp) ok|WRONG RESULT
 *   <name>  pipeline create <ms> ms
 *   <name>  <count> draws  record <ms> ms (<us> us/draw)  gpu <ms> ms (<us> us/draw)
 * A wrong dispatch result also counts as a failure.
 */

#include <unistd.h>

static struct {
   unsigned n, warm_up_ms, runs, delay_s;
} bench_cfg = {5000, 0, 6, 0};

static void
bench_pause(void)
{
   if (bench_cfg.delay_s)
      sleep(bench_cfg.delay_s);
}

/* Submits cb (already ended), waits for the queue to go idle, frees cb.
 * Returns the time from submit to idle, in seconds. */
static double
bench_submit_wait(VkCommandBuffer cb)
{
   VkSubmitInfo si = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &cb,
   };
   double t0 = vkt_now();
   CHECK(D(QueueSubmit)(q, 1, &si, VK_NULL_HANDLE));
   CHECK(D(QueueWaitIdle)(q));
   double t = vkt_now() - t0;
   D(FreeCommandBuffers)(dev, pool, 1, &cb);
   return t;
}

/* ------------------------------------------------------------------------ */
/* Dispatches                                                                */

/* Words in the storage buffer: the chain's table (4096) and the largest
 * grid's output (512 workgroups of 64). */
#define TABLE_WORDS 4096u
#define BUF_WORDS   (TABLE_WORDS + 512u * 64u)

struct compute {
   VkDescriptorSetLayout dl;
   VkPipelineLayout pl;
   VkDescriptorPool dp;
   VkDescriptorSet ds;
   VkPipeline shm, chain;
   struct buf buf;
};

static VkPipeline
compute_pipeline(VkPipelineLayout pl, VkShaderModule m)
{
   VkComputePipelineCreateInfo ci = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage =
         {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = m,
            .pName = "main",
         },
      .layout = pl,
   };
   VkPipeline p;
   CHECK(D(CreateComputePipelines)(dev, VK_NULL_HANDLE, 1, &ci, NULL, &p));
   return p;
}

static void
compute_init(struct compute *c)
{
   VkDescriptorSetLayoutBinding b = {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
   };
   VkDescriptorSetLayoutCreateInfo dli = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 1,
      .pBindings = &b,
   };
   CHECK(D(CreateDescriptorSetLayout)(dev, &dli, NULL, &c->dl));
   VkPipelineLayoutCreateInfo pli = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &c->dl,
   };
   CHECK(D(CreatePipelineLayout)(dev, &pli, NULL, &c->pl));
   VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
   VkDescriptorPoolCreateInfo dpi = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes = &ps,
   };
   CHECK(D(CreateDescriptorPool)(dev, &dpi, NULL, &c->dp));
   VkDescriptorSetAllocateInfo dai = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = c->dp,
      .descriptorSetCount = 1,
      .pSetLayouts = &c->dl,
   };
   CHECK(D(AllocateDescriptorSets)(dev, &dai, &c->ds));

   c->buf = mkbuf(BUF_WORDS * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
   VkDescriptorBufferInfo dbi = {c->buf.b, 0, VK_WHOLE_SIZE};
   VkWriteDescriptorSet w = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = c->ds,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &dbi,
   };
   D(UpdateDescriptorSets)(dev, 1, &w, 0, NULL);

   VkShaderModule shm = SHADER(shm_comp), chain = SHADER(chain_comp);
   c->shm = compute_pipeline(c->pl, shm);
   c->chain = compute_pipeline(c->pl, chain);
   D(DestroyShaderModule)(dev, shm, NULL);
   D(DestroyShaderModule)(dev, chain, NULL);
}

/* The chain's table: a fixed permutation-like mix of 0..4095. */
static uint32_t
table_word(uint32_t j)
{
   return (j * 2654435761u) >> 20;
}

static void
fill_table(uint32_t *d)
{
   for (uint32_t j = 0; j < TABLE_WORDS; j++)
      d[j] = table_word(j);
}

/* What the chain shader writes for local invocation i (every workgroup
 * reads the same table, so the result depends on i only). */
static uint32_t
chain_expect(uint32_t i)
{
   uint32_t v = i;
   for (uint32_t k = 0; k < 16; k++)
      v = table_word((v * 7u + k) & 4095u) & 4095u;
   return v;
}

static int
check(int chain, const uint32_t *d, uint32_t groups)
{
   const uint32_t n = groups * 64u;
   if (!chain) {
      for (uint32_t g = 0; g < n; g++) {
         if (d[g] != (g ^ 63u))
            return 0;
      }
      return 1;
   }
   uint32_t want[64];
   for (uint32_t i = 0; i < 64; i++)
      want[i] = chain_expect(i);
   for (uint32_t g = 0; g < n; g++) {
      if (d[TABLE_WORDS + g] != want[g % 64u])
         return 0;
   }
   for (uint32_t j = 0; j < TABLE_WORDS; j++) {
      if (d[j] != table_word(j))
         return 0;
   }
   return 1;
}

/* Records n dispatches of gx x gy workgroups; with chain, a compute to
 * compute barrier between each two. */
static VkCommandBuffer
record_dispatches(struct compute *c, int chain, uint32_t gx, uint32_t gy, uint32_t n)
{
   VkCommandBuffer cb = begin();
   D(CmdBindPipeline)(cb, VK_PIPELINE_BIND_POINT_COMPUTE, chain ? c->chain : c->shm);
   D(CmdBindDescriptorSets)(cb, VK_PIPELINE_BIND_POINT_COMPUTE, c->pl, 0, 1, &c->ds, 0, NULL);
   const VkMemoryBarrier mb = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
   };
   for (uint32_t i = 0; i < n; i++) {
      if (chain && i)
         D(CmdPipelineBarrier)(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
      D(CmdDispatch)(cb, gx, gy, 1);
   }
   CHECK(D(EndCommandBuffer)(cb));
   return cb;
}

static void
bench_dispatch_shape(struct compute *c, const char *name, int chain, uint32_t gx, uint32_t gy,
                     uint32_t n)
{
   bench_pause();
   uint32_t *d = c->buf.p;
   double rec = 1e9, gpu = 1e9;
   for (unsigned run = 0; run < bench_cfg.runs; run++) {
      memset(d, 0, BUF_WORDS * 4);
      if (chain)
         fill_table(d);
      double t0 = vkt_now();
      VkCommandBuffer cb = record_dispatches(c, chain, gx, gy, n);
      double r = vkt_now() - t0;
      double g = bench_submit_wait(cb);
      if (run == 0)
         continue;
      if (r < rec)
         rec = r;
      if (g < gpu)
         gpu = g;
   }
   const int ok = check(chain, d, gx * gy);
   printf("%-28s %6u disp.  record %8.3f ms (%6.2f us/disp)  gpu %8.3f ms (%6.2f us/disp) %s\n",
          name, n, rec * 1e3, rec * 1e6 / n, gpu * 1e3, gpu * 1e6 / n, ok ? "ok" : "WRONG RESULT");
   if (!ok)
      failures++;
}

/* Keeps the GPU busy for ms milliseconds, so the clocks are up before the
 * first measurement. */
static void
warm_up(struct compute *c, unsigned ms)
{
   const double end = vkt_now() + ms * 1e-3;
   unsigned subs = 0;
   while (vkt_now() < end) {
      bench_submit_wait(record_dispatches(c, 0, 512, 1, 2000));
      subs++;
   }
   printf("warm-up %u ms, %u submits\n", ms, subs);
}

static void
compute_fini(struct compute *c)
{
   D(DestroyPipeline)(dev, c->shm, NULL);
   D(DestroyPipeline)(dev, c->chain, NULL);
   D(DestroyDescriptorPool)(dev, c->dp, NULL);
   D(DestroyPipelineLayout)(dev, c->pl, NULL);
   D(DestroyDescriptorSetLayout)(dev, c->dl, NULL);
   freebuf(&c->buf);
}

/* ------------------------------------------------------------------------ */
/* Draws                                                                     */

#define RT_SIZE 256u

enum kind {
   K_PLAIN,      /* triangle, colour pushed once */
   K_PUSH,       /* triangle, colour pushed before every draw */
   K_GS,         /* triangle through a geometry shader (2 copies) */
   K_TESS,       /* quad patch, constant levels */
   K_TESS64,     /* the same, 64 instances */
   K_GS64,       /* geometry shader, 64 instances */
   K_TESS_DYN,   /* quad patch, levels computed by the control shader */
   K_TESS_DYN64, /* the same, 64 instances */
};

struct bench_target {
   VkImage img;
   VkDeviceMemory mem;
   VkImageView view;
   VkRenderPass rp;
   VkFramebuffer fb;
};

static void
bench_target_init(struct bench_target *t)
{
   VkImageCreateInfo ii = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .extent = {RT_SIZE, RT_SIZE, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
   };
   CHECK(D(CreateImage)(dev, &ii, NULL, &t->img));
   VkMemoryRequirements req;
   D(GetImageMemoryRequirements)(dev, t->img, &req);
   VkMemoryAllocateInfo ai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = req.size,
      .memoryTypeIndex = mem_type(req.memoryTypeBits, 0),
   };
   CHECK(D(AllocateMemory)(dev, &ai, NULL, &t->mem));
   CHECK(D(BindImageMemory)(dev, t->img, t->mem, 0));
   VkImageViewCreateInfo vi = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = t->img,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
   };
   CHECK(D(CreateImageView)(dev, &vi, NULL, &t->view));
   VkAttachmentDescription at = {
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
   };
   VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
   VkSubpassDescription sp = {
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = 1,
      .pColorAttachments = &ref,
   };
   VkRenderPassCreateInfo rpi = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &at,
      .subpassCount = 1,
      .pSubpasses = &sp,
   };
   CHECK(D(CreateRenderPass)(dev, &rpi, NULL, &t->rp));
   VkFramebufferCreateInfo fbi = {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass = t->rp,
      .attachmentCount = 1,
      .pAttachments = &t->view,
      .width = RT_SIZE,
      .height = RT_SIZE,
      .layers = 1,
   };
   CHECK(D(CreateFramebuffer)(dev, &fbi, NULL, &t->fb));
}

static void
bench_target_fini(struct bench_target *t)
{
   D(DestroyFramebuffer)(dev, t->fb, NULL);
   D(DestroyRenderPass)(dev, t->rp, NULL);
   D(DestroyImageView)(dev, t->view, NULL);
   D(DestroyImage)(dev, t->img, NULL);
   D(FreeMemory)(dev, t->mem, NULL);
}

/* The pipeline of a kind, VK_NULL_HANDLE if creation fails. Sets *ms to
 * the creation time in milliseconds. */
static VkPipeline
graphics_pipeline(enum kind k, VkRenderPass rp, VkPipelineLayout pl, double *ms)
{
   const int tess = k == K_TESS || k == K_TESS64 || k == K_TESS_DYN || k == K_TESS_DYN64;
   const int gs = k == K_GS || k == K_GS64;
   VkShaderModule mods[4];
   VkPipelineShaderStageCreateInfo st[4];
   unsigned n = 0;
#define STAGE(s, code)                                                                             \
   do {                                                                                            \
      mods[n] = SHADER(code);                                                                      \
      st[n] = (VkPipelineShaderStageCreateInfo){                                                   \
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,                             \
         .stage = s,                                                                               \
         .module = mods[n],                                                                        \
         .pName = "main",                                                                          \
      };                                                                                           \
      n++;                                                                                         \
   } while (0)
   if (tess) {
      STAGE(VK_SHADER_STAGE_VERTEX_BIT, ts_vert);
      if (k == K_TESS_DYN || k == K_TESS_DYN64)
         STAGE(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, ts_dyn_tesc);
      else
         STAGE(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, ts_tesc);
      STAGE(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, ts_quad_tese);
      STAGE(VK_SHADER_STAGE_FRAGMENT_BIT, gs_col_frag);
   } else if (gs) {
      STAGE(VK_SHADER_STAGE_VERTEX_BIT, gs_pass_vert);
      STAGE(VK_SHADER_STAGE_GEOMETRY_BIT, gs_pass_geom);
      STAGE(VK_SHADER_STAGE_FRAGMENT_BIT, gs_col_frag);
   } else {
      STAGE(VK_SHADER_STAGE_VERTEX_BIT, pos2_vert);
      STAGE(VK_SHADER_STAGE_FRAGMENT_BIT, solid_frag);
   }
#undef STAGE

   VkVertexInputBindingDescription vb = {0, 8, VK_VERTEX_INPUT_RATE_VERTEX};
   VkVertexInputAttributeDescription va = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
   VkPipelineVertexInputStateCreateInfo vis = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vb,
      .vertexAttributeDescriptionCount = 1,
      .pVertexAttributeDescriptions = &va,
   };
   VkPipelineInputAssemblyStateCreateInfo ia = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = tess ? VK_PRIMITIVE_TOPOLOGY_PATCH_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
   };
   VkPipelineTessellationStateCreateInfo ts = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
      .patchControlPoints = 4,
   };
   VkViewport vp = {0, 0, RT_SIZE, RT_SIZE, 0, 1};
   VkRect2D sc = {{0, 0}, {RT_SIZE, RT_SIZE}};
   VkPipelineViewportStateCreateInfo vps = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .pViewports = &vp,
      .scissorCount = 1,
      .pScissors = &sc,
   };
   VkPipelineRasterizationStateCreateInfo rs = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .lineWidth = 1.0f,
   };
   VkPipelineMultisampleStateCreateInfo msi = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
   };
   VkPipelineColorBlendAttachmentState cba = {.colorWriteMask = 0xf};
   VkPipelineColorBlendStateCreateInfo cb = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &cba,
   };
   VkGraphicsPipelineCreateInfo ci = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = n,
      .pStages = st,
      .pVertexInputState = &vis,
      .pInputAssemblyState = &ia,
      .pTessellationState = tess ? &ts : NULL,
      .pViewportState = &vps,
      .pRasterizationState = &rs,
      .pMultisampleState = &msi,
      .pColorBlendState = &cb,
      .layout = pl,
      .renderPass = rp,
   };
   VkPipeline p = VK_NULL_HANDLE;
   double t0 = vkt_now();
   VkResult r = D(CreateGraphicsPipelines)(dev, VK_NULL_HANDLE, 1, &ci, NULL, &p);
   *ms = (vkt_now() - t0) * 1e3;
   for (unsigned i = 0; i < n; i++)
      D(DestroyShaderModule)(dev, mods[i], NULL);
   if (r != VK_SUCCESS) {
      printf("FAIL graphics pipeline creation -> %d\n", r);
      return VK_NULL_HANDLE;
   }
   return p;
}

static void
bench(const char *name, enum kind k, uint32_t n)
{
   bench_pause();
   const int tess = k == K_TESS || k == K_TESS64 || k == K_TESS_DYN || k == K_TESS_DYN64;
   const int gs = k == K_GS || k == K_GS64;
   if ((tess && !FEAT.tessellationShader) || (gs && !FEAT.geometryShader)) {
      NOT_OFFERED(name);
      return;
   }
   const uint32_t verts = tess ? 4 : 3;
   const uint32_t instances = (k == K_TESS64 || k == K_GS64 || k == K_TESS_DYN64) ? 64 : 1;

   struct bench_target t;
   bench_target_init(&t);
   VkPushConstantRange pcr = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
   VkPipelineLayoutCreateInfo pli = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pcr,
   };
   VkPipelineLayout pl;
   CHECK(D(CreatePipelineLayout)(dev, &pli, NULL, &pl));
   double ms;
   VkPipeline p = graphics_pipeline(k, t.rp, pl, &ms);
   if (p == VK_NULL_HANDLE) {
      D(DestroyPipelineLayout)(dev, pl, NULL);
      bench_target_fini(&t);
      return;
   }
   printf("%-28s pipeline create %.1f ms\n", name, ms);

   /* A quad's corners, in the order the evaluation shader takes them; the
    * triangles use the first three. */
   struct buf vbuf = mkbuf(32, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
   const float quad[8] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f};
   memcpy(vbuf.p, quad, sizeof(quad));

   double rec = 1e9, gpu = 1e9;
   for (unsigned run = 0; run < bench_cfg.runs; run++) {
      double t0 = vkt_now();
      VkCommandBuffer cb = begin();
      VkClearValue clear = {.color = {{0, 0, 0, 1}}};
      VkRenderPassBeginInfo rbi = {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
         .renderPass = t.rp,
         .framebuffer = t.fb,
         .renderArea = {{0, 0}, {RT_SIZE, RT_SIZE}},
         .clearValueCount = 1,
         .pClearValues = &clear,
      };
      D(CmdBeginRenderPass)(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
      D(CmdBindPipeline)(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
      VkDeviceSize off = 0;
      D(CmdBindVertexBuffers)(cb, 0, 1, &vbuf.b, &off);
      float col[4] = {1, 0, 0, 1};
      if (k == K_PLAIN)
         D(CmdPushConstants)(cb, pl, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, col);
      for (uint32_t i = 0; i < n; i++) {
         if (k == K_PUSH) {
            col[1] = (float)(i & 255) / 255.0f;
            D(CmdPushConstants)(cb, pl, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, col);
         }
         D(CmdDraw)(cb, verts, instances, 0, 0);
      }
      D(CmdEndRenderPass)(cb);
      CHECK(D(EndCommandBuffer)(cb));
      double r = vkt_now() - t0;
      double g = bench_submit_wait(cb);
      if (run == 0)
         continue;
      if (r < rec)
         rec = r;
      if (g < gpu)
         gpu = g;
   }
   printf("%-28s %6u draws  record %8.3f ms (%6.2f us/draw)  gpu %8.3f ms (%6.2f us/draw)\n", name,
          n, rec * 1e3, rec * 1e6 / n, gpu * 1e3, gpu * 1e6 / n);

   freebuf(&vbuf);
   D(DestroyPipeline)(dev, p, NULL);
   D(DestroyPipelineLayout)(dev, pl, NULL);
   bench_target_fini(&t);
}

/* ------------------------------------------------------------------------ */
/* The group's tests                                                          */

/* The GPU warm-up, once, before the first measurement of the run. */
static void
bench_warm_once(void)
{
   static int done;
   if (done || !bench_cfg.warm_up_ms)
      return;
   done = 1;
   struct compute c;
   compute_init(&c);
   warm_up(&c, bench_cfg.warm_up_ms);
   compute_fini(&c);
}

static void
bench_shared_memory(void)
{
   bench_warm_once();
   struct compute c;
   compute_init(&c);
   const uint32_t nd = bench_cfg.n / 5;
   bench_dispatch_shape(&c, "shared-memory dispatch", 0, 64, 1, nd);
   bench_dispatch_shape(&c, "shared-memory 1x64 dispatch", 0, 1, 64, nd);
   bench_dispatch_shape(&c, "shared-memory 8x8 dispatch", 0, 8, 8, nd);
   compute_fini(&c);
}

static void
bench_chain_barrier(void)
{
   bench_warm_once();
   struct compute c;
   compute_init(&c);
   const uint32_t nd = bench_cfg.n / 5;
   bench_dispatch_shape(&c, "chain+barrier 64x1", 1, 64, 1, nd);
   bench_dispatch_shape(&c, "chain+barrier 1x64", 1, 1, 64, nd);
   bench_dispatch_shape(&c, "chain+barrier 8x8", 1, 8, 8, nd);
   bench_dispatch_shape(&c, "chain+barrier 2x32", 1, 2, 32, nd);
   bench_dispatch_shape(&c, "chain+barrier 1x512", 1, 1, 512, nd);
   bench_dispatch_shape(&c, "chain+barrier 16x32", 1, 16, 32, nd);
   compute_fini(&c);
}

static void
bench_draws(void)
{
   bench_warm_once();
   bench("plain draw", K_PLAIN, bench_cfg.n);
   bench("plain draw + push constants", K_PUSH, bench_cfg.n);
}

static void
bench_geometry(void)
{
   bench_warm_once();
   bench("geometry shader draw", K_GS, bench_cfg.n / 10);
   bench("geometry shader, 64 inst.", K_GS64, bench_cfg.n / 50);
}

static void
bench_tessellation(void)
{
   bench_warm_once();
   bench("tessellation draw", K_TESS, bench_cfg.n / 10);
   bench("tessellation, 64 patches", K_TESS64, bench_cfg.n / 50);
   bench("tess dynamic levels", K_TESS_DYN, bench_cfg.n / 10);
   bench("tess dynamic, 64 patches", K_TESS_DYN64, bench_cfg.n / 50);
}

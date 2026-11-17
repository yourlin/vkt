/*
 * SPDX-FileCopyrightText: 2026 yourlin
 * SPDX-License-Identifier: MIT-0
 *
 * The harness of vkt's tests (the files in tests/).
 *
 * vkt loads the system driver (-sys) or a driver library directly (its
 * vk_icdGetInstanceProcAddr) and creates a device with every feature in
 * vkt_features that the driver reports, but robustBufferAccess. Tests
 * render offscreen and check pixels or buffer words; each check prints
 * PASS or FAIL, a test of a feature the driver does not offer SKIP. The
 * run ends with "ALL PASSED: 0 failure(s)" or "FAILED: n failure(s)".
 */

#ifndef VKT_H
#define VKT_H

#include <dlfcn.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

static PFN_vkGetInstanceProcAddr gipa;
static PFN_vkGetDeviceProcAddr gdpa;
static VkInstance inst;
static VkPhysicalDevice pd;
static VkDevice dev;
static VkQueue q;
static VkCommandPool pool;
static uint32_t qfam;
static VkPhysicalDeviceMemoryProperties memprops;
static VkPhysicalDeviceProperties props;
static int failures;
static int skipped;
/* A feature the driver does not offer fails the run instead of skipping
 * its test (vkt --strict). */
static int strict;

/* What the driver reports (filled by vkt_init) and, the same, what the
 * device is created with. */
static struct {
   VkPhysicalDeviceFeatures2 f2;
   VkPhysicalDeviceVariablePointersFeatures vp;
   VkPhysicalDeviceShaderDrawParametersFeatures dp;
   VkPhysicalDeviceHostQueryResetFeatures hqr;
   VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures demote;
} vkt_features;
#define FEAT (vkt_features.f2.features)

#define I(name) ((PFN_vk##name)gipa(inst, "vk" #name))
#define D(name) ((PFN_vk##name)gdpa(dev, "vk" #name))

#define CHECK(expr)                                                                                \
   do {                                                                                            \
      VkResult r_ = (expr);                                                                        \
      if (r_ != VK_SUCCESS) {                                                                      \
         printf("FAIL %s:%d %s -> %d\n", __FILE__, __LINE__, #expr, r_);                           \
         exit(1);                                                                                  \
      }                                                                                            \
   } while (0)

static double
vkt_now(void)
{
   struct timespec t;
   clock_gettime(CLOCK_MONOTONIC, &t);
   return t.tv_sec + t.tv_nsec * 1e-9;
}

/* One check: PASS or FAIL and the message. */
static void __attribute__((format(printf, 2, 3)))
expect(int ok, const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   printf("%s ", ok ? "PASS" : "FAIL");
   vprintf(fmt, ap);
   printf("\n");
   va_end(ap);
   if (!ok)
      failures++;
}

/* A test that cannot run, as the driver does not offer what it tests:
 * SKIP, or with strict a FAIL (to catch a feature turned off by mistake on
 * a driver that should have it). */
static void
not_offered(const char *what)
{
   if (strict) {
      expect(0, "%s: not offered", what);
      return;
   }
   printf("SKIP %s: not offered\n", what);
   skipped++;
}
#define NOT_OFFERED(what) not_offered(what)

static int
vkt_finish(void)
{
   char skips[48] = "";
   if (skipped)
      snprintf(skips, sizeof(skips), ", %d skipped", skipped);
   if (failures)
      printf("FAILED: %d failure(s)%s\n", failures, skips);
   else
      printf("ALL PASSED: 0 failure(s)%s\n", skips);
   return failures ? 1 : 0;
}

/* ------------------------------------------------------------------------ */
/* Instance and device                                                       */

static VkExtensionProperties *dev_exts;
static uint32_t dev_ext_count;

static int
has_ext(const char *name)
{
   for (uint32_t i = 0; i < dev_ext_count; i++) {
      if (!strcmp(dev_exts[i].extensionName, name))
         return 1;
   }
   return 0;
}

static void
vkt_load(const char *arg)
{
   const int sys = !strcmp(arg, "-sys");
   /* A bare file name is in the current directory, not one dlopen would
    * search the system's libraries for. */
   char local[4096];
   snprintf(local, sizeof(local), "%s%s", strchr(arg, '/') ? "" : "./", arg);
   const char *lib = sys ? "libvulkan.so" : local;
   void *h = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
   if (!h) {
      printf("FAIL dlopen %s: %s\n", lib, dlerror());
      exit(1);
   }
   if (sys) {
      gipa = (PFN_vkGetInstanceProcAddr)dlsym(h, "vkGetInstanceProcAddr");
   } else {
      VkResult (*neg)(uint32_t *) =
         (VkResult (*)(uint32_t *))dlsym(h, "vk_icdNegotiateLoaderICDInterfaceVersion");
      if (neg) {
         uint32_t v = 5;
         neg(&v);
      }
      gipa = (PFN_vkGetInstanceProcAddr)dlsym(h, "vk_icdGetInstanceProcAddr");
   }
   if (!gipa) {
      printf("FAIL no GetInstanceProcAddr\n");
      exit(1);
   }
}

/* Extensions the tests use, enabled when the driver has them. */
static const char *const vkt_wanted_exts[] = {
   VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME,
   VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME,
   VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
   VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME,
   VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME,
   VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME,
   VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME,
};

/* The features2 chain of the structs the driver's extensions define. */
static void
vkt_chain_features(void)
{
   memset(&vkt_features, 0, sizeof(vkt_features));
   vkt_features.f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
   vkt_features.vp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES;
   vkt_features.dp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
   vkt_features.hqr.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES;
   vkt_features.demote.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES;
   void **next = &vkt_features.f2.pNext;
   *next = &vkt_features.vp;
   next = &vkt_features.vp.pNext;
   *next = &vkt_features.dp;
   next = &vkt_features.dp.pNext;
   if (has_ext(VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME)) {
      *next = &vkt_features.hqr;
      next = &vkt_features.hqr.pNext;
   }
   if (has_ext(VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME)) {
      *next = &vkt_features.demote;
      next = &vkt_features.demote.pNext;
   }
}

static void
vkt_init(const char *arg)
{
   vkt_load(arg);
   VkApplicationInfo app = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "vkt",
      .apiVersion = VK_API_VERSION_1_1,
   };
   VkInstanceCreateInfo ici = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app,
   };
   CHECK(((PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance"))(&ici, NULL, &inst));

   uint32_t n = 1;
   VkResult r = I(EnumeratePhysicalDevices)(inst, &n, &pd);
   if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || n == 0) {
      printf("FAIL no physical device (%d, %u)\n", r, n);
      exit(1);
   }
   I(GetPhysicalDeviceMemoryProperties)(pd, &memprops);
   I(GetPhysicalDeviceProperties)(pd, &props);

   CHECK(I(EnumerateDeviceExtensionProperties)(pd, NULL, &dev_ext_count, NULL));
   dev_exts = calloc(dev_ext_count + 1, sizeof(*dev_exts));
   CHECK(I(EnumerateDeviceExtensionProperties)(pd, NULL, &dev_ext_count, dev_exts));
   const char *exts[sizeof(vkt_wanted_exts) / sizeof(vkt_wanted_exts[0])];
   uint32_t n_exts = 0;
   for (size_t i = 0; i < sizeof(vkt_wanted_exts) / sizeof(vkt_wanted_exts[0]); i++) {
      if (has_ext(vkt_wanted_exts[i]))
         exts[n_exts++] = vkt_wanted_exts[i];
   }

   vkt_chain_features();
   I(GetPhysicalDeviceFeatures2)(pd, &vkt_features.f2);

   VkQueueFamilyProperties qf[16];
   uint32_t nq = 16;
   I(GetPhysicalDeviceQueueFamilyProperties)(pd, &nq, qf);
   const VkQueueFlags want = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
   for (qfam = 0; qfam < nq && (qf[qfam].queueFlags & want) != want; qfam++)
      ;
   if (qfam == nq) {
      printf("FAIL no graphics and compute queue\n");
      exit(1);
   }
   const float prio = 1.0f;
   VkDeviceQueueCreateInfo qci = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = qfam,
      .queueCount = 1,
      .pQueuePriorities = &prio,
   };
   /* Everything the driver reports, as reported, but robust buffer
    * access: no test needs it, and its bounds checks would weigh on the
    * benchmarks. vkt_features keeps what the driver reports. */
   const VkBool32 robust = FEAT.robustBufferAccess;
   FEAT.robustBufferAccess = VK_FALSE;
   VkDeviceCreateInfo dci = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &vkt_features.f2,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &qci,
      .enabledExtensionCount = n_exts,
      .ppEnabledExtensionNames = exts,
   };
   CHECK(I(CreateDevice)(pd, &dci, NULL, &dev));
   FEAT.robustBufferAccess = robust;
   gdpa = (PFN_vkGetDeviceProcAddr)gipa(inst, "vkGetDeviceProcAddr");
   D(GetDeviceQueue)(dev, qfam, 0, &q);
   VkCommandPoolCreateInfo cpi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = qfam,
   };
   CHECK(D(CreateCommandPool)(dev, &cpi, NULL, &pool));
}

/* ------------------------------------------------------------------------ */
/* Memory, shaders, command buffers                                          */

static uint32_t
mem_type(uint32_t bits, VkMemoryPropertyFlags want)
{
   for (uint32_t i = 0; i < memprops.memoryTypeCount; i++) {
      if ((bits & (1u << i)) && (memprops.memoryTypes[i].propertyFlags & want) == want)
         return i;
   }
   printf("FAIL no host-visible coherent memory\n");
   exit(1);
}

struct buf {
   VkBuffer b;
   VkDeviceMemory m;
   void *p;
   VkDeviceSize size;
};

/* A host-visible, coherent, mapped buffer, zeroed. */
static struct buf
mkbuf(VkDeviceSize size, VkBufferUsageFlags usage)
{
   struct buf r = {.size = size};
   VkBufferCreateInfo bi = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
      .usage = usage,
   };
   CHECK(D(CreateBuffer)(dev, &bi, NULL, &r.b));
   VkMemoryRequirements req;
   D(GetBufferMemoryRequirements)(dev, r.b, &req);
   VkMemoryAllocateInfo ai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = req.size,
      .memoryTypeIndex = mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
   };
   CHECK(D(AllocateMemory)(dev, &ai, NULL, &r.m));
   CHECK(D(BindBufferMemory)(dev, r.b, r.m, 0));
   CHECK(D(MapMemory)(dev, r.m, 0, VK_WHOLE_SIZE, 0, &r.p));
   memset(r.p, 0, size);
   return r;
}

static struct buf
mkbuf_data(const void *data, VkDeviceSize size, VkBufferUsageFlags usage)
{
   struct buf r = mkbuf(size, usage);
   memcpy(r.p, data, size);
   return r;
}

static void
freebuf(struct buf *r)
{
   D(DestroyBuffer)(dev, r->b, NULL);
   D(FreeMemory)(dev, r->m, NULL);
}

static VkShaderModule
mkshader(const uint32_t *code, size_t size)
{
   VkShaderModuleCreateInfo ci = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = size,
      .pCode = code,
   };
   VkShaderModule m;
   CHECK(D(CreateShaderModule)(dev, &ci, NULL, &m));
   return m;
}
#define SHADER(name) mkshader(name, sizeof(name))

static VkCommandBuffer
begin(void)
{
   VkCommandBufferAllocateInfo ai = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };
   VkCommandBuffer cb;
   CHECK(D(AllocateCommandBuffers)(dev, &ai, &cb));
   VkCommandBufferBeginInfo bi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
   };
   CHECK(D(BeginCommandBuffer)(cb, &bi));
   return cb;
}

/* Ends, submits and waits for cb, then frees it. */
static void
submit(VkCommandBuffer cb)
{
   CHECK(D(EndCommandBuffer)(cb));
   VkSubmitInfo si = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &cb,
   };
   CHECK(D(QueueSubmit)(q, 1, &si, VK_NULL_HANDLE));
   CHECK(D(QueueWaitIdle)(q));
   D(FreeCommandBuffers)(dev, pool, 1, &cb);
}

/* ------------------------------------------------------------------------ */
/* Render targets                                                            */

/* A colour target of w x h pixels (RGBA8 unless made by mktarget_fmt),
 * cleared at the start of its render pass, read back into rb by
 * pass_end_readback. */
struct target {
   uint32_t w, h, bpp;
   VkFormat format;
   VkImage img;
   VkDeviceMemory mem;
   VkImageView view;
   VkRenderPass rp;
   VkFramebuffer fb;
   struct buf rb;
};

static struct target
mktarget_fmt(uint32_t w, uint32_t h, VkFormat format, uint32_t bpp)
{
   struct target t = {.w = w, .h = h, .bpp = bpp, .format = format};
   VkImageCreateInfo ii = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = format,
      .extent = {w, h, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
   };
   CHECK(D(CreateImage)(dev, &ii, NULL, &t.img));
   VkMemoryRequirements req;
   D(GetImageMemoryRequirements)(dev, t.img, &req);
   VkMemoryAllocateInfo ai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = req.size,
      .memoryTypeIndex = mem_type(req.memoryTypeBits, 0),
   };
   CHECK(D(AllocateMemory)(dev, &ai, NULL, &t.mem));
   CHECK(D(BindImageMemory)(dev, t.img, t.mem, 0));
   VkImageViewCreateInfo vi = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = t.img,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
   };
   CHECK(D(CreateImageView)(dev, &vi, NULL, &t.view));
   VkAttachmentDescription at = {
      .format = format,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
   };
   VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
   VkSubpassDescription sp = {
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = 1,
      .pColorAttachments = &ref,
   };
   VkSubpassDependency dep = {
      .srcSubpass = 0,
      .dstSubpass = VK_SUBPASS_EXTERNAL,
      .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
   };
   VkRenderPassCreateInfo rpi = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &at,
      .subpassCount = 1,
      .pSubpasses = &sp,
      .dependencyCount = 1,
      .pDependencies = &dep,
   };
   CHECK(D(CreateRenderPass)(dev, &rpi, NULL, &t.rp));
   VkFramebufferCreateInfo fbi = {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass = t.rp,
      .attachmentCount = 1,
      .pAttachments = &t.view,
      .width = w,
      .height = h,
      .layers = 1,
   };
   CHECK(D(CreateFramebuffer)(dev, &fbi, NULL, &t.fb));
   t.rb = mkbuf((VkDeviceSize)w * h * bpp, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
   return t;
}

static struct target
mktarget(uint32_t w, uint32_t h)
{
   return mktarget_fmt(w, h, VK_FORMAT_R8G8B8A8_UNORM, 4);
}

static void
freetarget(struct target *t)
{
   freebuf(&t->rb);
   D(DestroyFramebuffer)(dev, t->fb, NULL);
   D(DestroyRenderPass)(dev, t->rp, NULL);
   D(DestroyImageView)(dev, t->view, NULL);
   D(DestroyImage)(dev, t->img, NULL);
   D(FreeMemory)(dev, t->mem, NULL);
}

/* Begins t's render pass on a new command buffer, cleared to cv. */
static VkCommandBuffer
pass_begin_cv(struct target *t, VkClearValue cv)
{
   VkCommandBuffer cb = begin();
   VkRenderPassBeginInfo rbi = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = t->rp,
      .framebuffer = t->fb,
      .renderArea = {{0, 0}, {t->w, t->h}},
      .clearValueCount = 1,
      .pClearValues = &cv,
   };
   D(CmdBeginRenderPass)(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
   return cb;
}

/* The same, cleared to a float colour. */
static VkCommandBuffer
pass_begin(struct target *t, const float clear[4])
{
   VkClearValue cv;
   memcpy(cv.color.float32, clear, 16);
   return pass_begin_cv(t, cv);
}

/* Ends the pass, copies the image to t->rb, submits and waits. */
static void
pass_end_readback(struct target *t, VkCommandBuffer cb)
{
   D(CmdEndRenderPass)(cb);
   VkBufferImageCopy c = {
      .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
      .imageExtent = {t->w, t->h, 1},
   };
   D(CmdCopyImageToBuffer)(cb, t->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->rb.b, 1, &c);
   VkBufferMemoryBarrier bb = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = t->rb.b,
      .size = VK_WHOLE_SIZE,
   };
   D(CmdPipelineBarrier)(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL,
                         1, &bb, 0, NULL);
   submit(cb);
}

/* The pixel at (x, y): bpp bytes (RGBA for RGBA8). */
static const uint8_t *
pix(const struct target *t, uint32_t x, uint32_t y)
{
   return (const uint8_t *)t->rb.p + ((size_t)y * t->w + x) * t->bpp;
}

/* Some channel above 64 (the tests draw full colours on black). */
static int
lit(const struct target *t, uint32_t x, uint32_t y)
{
   const uint8_t *p = pix(t, x, y);
   return p[0] > 64 || p[1] > 64 || p[2] > 64;
}

static int
same_image(const struct target *a, const struct target *b)
{
   return a->w == b->w && a->h == b->h && a->bpp == b->bpp &&
          !memcmp(a->rb.p, b->rb.p, (size_t)a->w * a->h * a->bpp);
}

static void
write_ppm(const struct target *t, const char *path)
{
   FILE *f = fopen(path, "wb");
   if (!f)
      return;
   fprintf(f, "P6\n%u %u\n255\n", t->w, t->h);
   for (uint32_t i = 0; i < t->w * t->h; i++)
      fwrite((const uint8_t *)t->rb.p + i * 4, 1, 3, f);
   fclose(f);
}

/* ------------------------------------------------------------------------ */
/* Graphics pipelines                                                        */

/* What mkgfx builds; zero is the default for every field. Vertex input:
 * vec2 positions at location 0, binding 0, unless no_vertex_input; with
 * colour, vec4 colours at location 1, binding 1. */
struct gfx_desc {
   const uint32_t *vs, *tcs, *tes, *gs, *fs;
   size_t vs_size, tcs_size, tes_size, gs_size, fs_size;
   VkPrimitiveTopology topology;
   uint32_t patch_points;
   VkPolygonMode polygon;
   float line_width;
   int dynamic_line_width;
   int colour;
   int no_vertex_input;
   uint32_t viewports; /* 0: 1, the target's size */
   const VkViewport *vp;
   const VkRect2D *sc;
   int logic_op;
   VkLogicOp op;
   /* Culling: cull_mode (default none) and the front face (default
    * counter-clockwise). */
   VkCullModeFlags cull_mode;
   int front_cw;
   /* Tessellation: the domain origin, chained when lower_left is set
    * (default upper left). */
   int lower_left;
   VkPipelineLayout layout;
   VkPipelineCache cache;
};
#define GFX_STAGE(field, name) .field = name, .field##_size = sizeof(name)

/* The pipeline, or VK_NULL_HANDLE (and a FAIL) if creation fails. */
static VkPipeline
mkgfx(const struct target *t, const struct gfx_desc *d)
{
   VkShaderModule mods[5];
   VkPipelineShaderStageCreateInfo st[5];
   unsigned n = 0;
   const struct {
      const uint32_t *code;
      size_t size;
      VkShaderStageFlagBits stage;
   } stages[] = {
      {d->vs, d->vs_size, VK_SHADER_STAGE_VERTEX_BIT},
      {d->tcs, d->tcs_size, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT},
      {d->tes, d->tes_size, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT},
      {d->gs, d->gs_size, VK_SHADER_STAGE_GEOMETRY_BIT},
      {d->fs, d->fs_size, VK_SHADER_STAGE_FRAGMENT_BIT},
   };
   for (unsigned i = 0; i < 5; i++) {
      if (!stages[i].code)
         continue;
      mods[n] = mkshader(stages[i].code, stages[i].size);
      st[n] = (VkPipelineShaderStageCreateInfo){
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = stages[i].stage,
         .module = mods[n],
         .pName = "main",
      };
      n++;
   }
   VkVertexInputBindingDescription vb[2] = {
      {0, 8, VK_VERTEX_INPUT_RATE_VERTEX},
      {1, 16, VK_VERTEX_INPUT_RATE_VERTEX},
   };
   VkVertexInputAttributeDescription va[2] = {
      {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
      {1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
   };
   VkPipelineVertexInputStateCreateInfo vis = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = d->no_vertex_input ? 0
                                       : d->colour        ? 2
                                                          : 1,
      .pVertexBindingDescriptions = vb,
      .vertexAttributeDescriptionCount = d->no_vertex_input ? 0
                                         : d->colour        ? 2
                                                            : 1,
      .pVertexAttributeDescriptions = va,
   };
   const int tess = d->tcs != NULL;
   VkPipelineInputAssemblyStateCreateInfo ia = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = tess ? VK_PRIMITIVE_TOPOLOGY_PATCH_LIST : d->topology,
   };
   VkPipelineTessellationDomainOriginStateCreateInfo origin = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_DOMAIN_ORIGIN_STATE_CREATE_INFO,
      .domainOrigin = VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT,
   };
   VkPipelineTessellationStateCreateInfo ts = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
      .pNext = d->lower_left ? &origin : NULL,
      .patchControlPoints = d->patch_points ? d->patch_points : 4,
   };
   VkViewport vp = {0, 0, (float)t->w, (float)t->h, 0, 1};
   VkRect2D sc = {{0, 0}, {t->w, t->h}};
   VkPipelineViewportStateCreateInfo vps = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = d->viewports ? d->viewports : 1,
      .pViewports = d->vp ? d->vp : &vp,
      .scissorCount = d->viewports ? d->viewports : 1,
      .pScissors = d->sc ? d->sc : &sc,
   };
   VkPipelineRasterizationStateCreateInfo rs = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = d->polygon,
      .cullMode = d->cull_mode,
      .frontFace = d->front_cw ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .lineWidth = d->line_width ? d->line_width : 1.0f,
   };
   VkPipelineMultisampleStateCreateInfo msi = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
   };
   VkPipelineColorBlendAttachmentState cba = {.colorWriteMask = 0xf};
   VkPipelineColorBlendStateCreateInfo cbs = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = d->logic_op,
      .logicOp = d->op,
      .attachmentCount = 1,
      .pAttachments = &cba,
   };
   VkDynamicState dyn = VK_DYNAMIC_STATE_LINE_WIDTH;
   VkPipelineDynamicStateCreateInfo ds = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = 1,
      .pDynamicStates = &dyn,
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
      .pColorBlendState = &cbs,
      .pDynamicState = d->dynamic_line_width ? &ds : NULL,
      .layout = d->layout,
      .renderPass = t->rp,
   };
   VkPipeline p = VK_NULL_HANDLE;
   VkResult r = D(CreateGraphicsPipelines)(dev, d->cache, 1, &ci, NULL, &p);
   for (unsigned i = 0; i < n; i++)
      D(DestroyShaderModule)(dev, mods[i], NULL);
   if (r != VK_SUCCESS) {
      expect(0, "graphics pipeline creation -> %d", r);
      return VK_NULL_HANDLE;
   }
   return p;
}

/* A pipeline layout: no sets, or one set with layout dsl; push constants
 * of push_size bytes for all graphics stages. */
static VkPipelineLayout
mklayout(const VkDescriptorSetLayout *dsl, uint32_t push_size)
{
   VkPushConstantRange pcr = {VK_SHADER_STAGE_ALL_GRAPHICS, 0, push_size};
   VkPipelineLayoutCreateInfo pli = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = dsl ? 1 : 0,
      .pSetLayouts = dsl,
      .pushConstantRangeCount = push_size ? 1 : 0,
      .pPushConstantRanges = &pcr,
   };
   VkPipelineLayout l;
   CHECK(D(CreatePipelineLayout)(dev, &pli, NULL, &l));
   return l;
}

/* One-binding descriptor set layouts and sets. */
static VkDescriptorSetLayout
mkdsl(VkDescriptorType type, uint32_t count, VkShaderStageFlags stages)
{
   VkDescriptorSetLayoutBinding b[4];
   for (uint32_t i = 0; i < count; i++)
      b[i] = (VkDescriptorSetLayoutBinding){i, type, 1, stages, NULL};
   VkDescriptorSetLayoutCreateInfo dli = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = count,
      .pBindings = b,
   };
   VkDescriptorSetLayout l;
   CHECK(D(CreateDescriptorSetLayout)(dev, &dli, NULL, &l));
   return l;
}

/* A set of layout l whose bindings 0..count-1 are the buffers bufs. */
static VkDescriptorSet
mkset(VkDescriptorSetLayout l, VkDescriptorType type, uint32_t count, const struct buf *bufs,
      VkDescriptorPool *pool_out)
{
   VkDescriptorPoolSize ps = {type, count};
   VkDescriptorPoolCreateInfo dpi = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes = &ps,
   };
   CHECK(D(CreateDescriptorPool)(dev, &dpi, NULL, pool_out));
   VkDescriptorSetAllocateInfo dai = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = *pool_out,
      .descriptorSetCount = 1,
      .pSetLayouts = &l,
   };
   VkDescriptorSet s;
   CHECK(D(AllocateDescriptorSets)(dev, &dai, &s));
   for (uint32_t i = 0; i < count; i++) {
      VkDescriptorBufferInfo bi = {bufs[i].b, 0, VK_WHOLE_SIZE};
      VkWriteDescriptorSet w = {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = s,
         .dstBinding = i,
         .descriptorCount = 1,
         .descriptorType = type,
         .pBufferInfo = &bi,
      };
      D(UpdateDescriptorSets)(dev, 1, &w, 0, NULL);
   }
   return s;
}

#endif

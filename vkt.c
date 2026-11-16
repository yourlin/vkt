/*
 * SPDX-FileCopyrightText: 2026 yourlin
 * SPDX-License-Identifier: MIT-0
 *
 * vkt: Vulkan smoke tests and benchmarks of a driver, on the system driver
 * (-sys) or a driver library loaded directly.
 *
 *   vkt [<driver>] [-c config.yaml] [--strict] [selector...]
 *   vkt -l [-c config.yaml] [selector...]
 *
 * <driver> is -sys or a path to a driver library (a word ending in .so);
 * without it, the configuration's driver. A selector is a group (basic,
 * eden, stages, bench), a test (stages/tessellation) or a pattern with *
 * and ? ('stages/geo*', '*-lines'). Without selectors, the configuration's
 * tests; without those, every test but the bench group's, which runs only
 * when selected. -l lists what would run.
 *
 * The configuration file (YAML) may set:
 *
 *   driver: ./libvulkan_malisx2.so   # or -sys
 *   strict: true                      # as --strict
 *   tests: [eden, stages/tess*]      # selectors
 *   bench:
 *     n: 5000                         # see tests/bench.c
 *     warm_up_ms: 3000
 *     runs: 6
 *     delay_s: 0
 *
 * The command line wins over the file: its driver, its selectors, and
 * --strict.
 *
 * A test of a feature the driver does not offer is skipped (SKIP, not a
 * failure); with strict it fails. The run ends with
 * "ALL PASSED: 0 failure(s)" (exit 0) or "FAILED: n failure(s)" (exit 1),
 * followed by ", n skipped" when tests were skipped. Exit 2: bad arguments
 * or configuration, or a selector that matches no test.
 */

#include <fnmatch.h>

#include "vkt.h"
#include "yaml.h"
#include "shaders.h"

#include "tests/basic.c"
#include "tests/eden.c"
#include "tests/stages.c"
#include "tests/bench.c"

static const struct test {
   const char *name;
   void (*run)(void);
   const char *what;
} tests[] = {
   {"basic/info", print_device, "device identity, limits, the features Eden needs"},
   {"basic/compute", test_compute, "a compute dispatch filling a buffer"},
   {"basic/draw", test_draw, "an indexed quad and a triangle with per-vertex colours"},
   {"eden/variable-pointers", test_variable_pointers, "OpSelect between two storage buffers"},
   {"eden/draw-parameters", test_draw_parameters,
    "DrawID, BaseVertex, BaseInstance in an indirect-count draw"},
   {"eden/logic-op", test_logic_op, "logic op XOR"},
   {"eden/wide-lines", test_wide_lines, "lines of width 1 and 5"},
   {"eden/host-query-reset", test_host_query_reset, "occlusion queries reset on the host"},
   {"stages/cull-distance", test_cull_distance, "cull and clip distances"},
   {"stages/multi-viewport", test_multi_viewport, "four viewports chosen by the vertex shader"},
   {"stages/polygon-mode", test_polygon_mode, "point and line polygon modes, line lists"},
   {"stages/geometry-shader", test_geometry_shader, "geometry shaders"},
   {"stages/tessellation", test_tessellation, "tessellation, equal and fractional spacing"},
   {"stages/pipeline-cache", test_pipeline_cache,
    "geometry and tessellation pipelines from a cache and its serialized data"},
   {"bench/shared-memory", bench_shared_memory,
    "dispatches using shared memory, three workgroup grids"},
   {"bench/chain-barrier", bench_chain_barrier,
    "dependent dispatches with barriers, six workgroup grids"},
   {"bench/draws", bench_draws, "plain draws, with and without push constants"},
   {"bench/geometry", bench_geometry, "geometry shader draws, 1 and 64 instances"},
   {"bench/tessellation", bench_tessellation, "tessellation draws, constant and computed levels"},
};
#define N_TESTS (sizeof(tests) / sizeof(tests[0]))

/* The groups that run only when selected. */
static int
opt_in(const char *name)
{
   return !strncmp(name, "bench/", 6);
}

#define MAX_SELECTORS 256
struct selectors {
   const char *s[MAX_SELECTORS];
   unsigned n;
};

static void
add_selector(struct selectors *sel, const char *s)
{
   if (sel->n == MAX_SELECTORS) {
      printf("too many selectors (at most %d)\n", MAX_SELECTORS);
      exit(2);
   }
   sel->s[sel->n++] = s;
}

/* Whether selector s picks the test called name: its group (the part
 * before /), its name, or a pattern matching it. */
static int
matches(const char *s, const char *name)
{
   const size_t gl = strchr(name, '/') - name;
   if (strlen(s) == gl && !strncmp(s, name, gl))
      return 1;
   return !fnmatch(s, name, 0);
}

static int
selected(const struct selectors *sel, const char *name)
{
   if (!sel->n)
      return !opt_in(name);
   for (unsigned i = 0; i < sel->n; i++) {
      if (matches(sel->s[i], name))
         return 1;
   }
   return 0;
}

/* ------------------------------------------------------------------------ */
/* Configuration                                                             */

struct config {
   const char *driver;
   int strict;
   struct selectors tests;
};

static void __attribute__((noreturn, format(printf, 3, 4)))
config_error(const char *path, int line, const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   printf("%s:%d: ", path, line);
   vprintf(fmt, ap);
   printf("\n");
   va_end(ap);
   exit(2);
}

static const char *
config_scalar(const char *path, const struct ynode *n, const char *key)
{
   if (n->type != Y_SCALAR)
      config_error(path, n->line, "%s: expected a single value", key);
   return n->str;
}

static int
config_bool(const char *path, const struct ynode *n, const char *key)
{
   const char *v = config_scalar(path, n, key);
   if (!strcmp(v, "true") || !strcmp(v, "yes") || !strcmp(v, "on"))
      return 1;
   if (!strcmp(v, "false") || !strcmp(v, "no") || !strcmp(v, "off"))
      return 0;
   config_error(path, n->line, "%s: expected true or false, not '%s'", key, v);
}

static unsigned
config_uint(const char *path, const struct ynode *n, const char *key, unsigned min, unsigned max)
{
   const char *v = config_scalar(path, n, key);
   char *end;
   const unsigned long u = strtoul(v, &end, 10);
   if (!*v || *end || v[0] == '-' || u < min || u > max)
      config_error(path, n->line, "%s: expected a whole number from %u to %u, not '%s'", key, min,
                   max, v);
   return (unsigned)u;
}

static void
load_config(const char *path, struct config *cfg)
{
   char err[512];
   const struct ynode *root = yload(path, err, sizeof(err));
   if (!root) {
      printf("%s\n", err);
      exit(2);
   }
   if (root->type == Y_NULL)
      return;
   if (root->type != Y_MAP)
      config_error(path, root->line, "expected keys (driver, strict, tests, bench)");
   for (int i = 0; i < root->n; i++) {
      const char *key = root->keys[i];
      const struct ynode *v = root->kids[i];
      if (!strcmp(key, "driver")) {
         cfg->driver = config_scalar(path, v, key);
      } else if (!strcmp(key, "strict")) {
         cfg->strict = config_bool(path, v, key);
      } else if (!strcmp(key, "tests")) {
         if (v->type == Y_SCALAR) {
            add_selector(&cfg->tests, v->str);
         } else if (v->type == Y_SEQ) {
            for (int j = 0; j < v->n; j++)
               add_selector(&cfg->tests, config_scalar(path, v->kids[j], "tests"));
         } else if (v->type != Y_NULL) {
            config_error(path, v->line, "tests: expected a list of selectors");
         }
      } else if (!strcmp(key, "bench")) {
         if (v->type == Y_NULL)
            continue;
         if (v->type != Y_MAP)
            config_error(path, v->line, "bench: expected keys (n, warm_up_ms, runs, delay_s)");
         for (int j = 0; j < v->n; j++) {
            const char *bk = v->keys[j];
            const struct ynode *bv = v->kids[j];
            if (!strcmp(bk, "n"))
               bench_cfg.n = config_uint(path, bv, "bench.n", 50, 10000000);
            else if (!strcmp(bk, "warm_up_ms"))
               bench_cfg.warm_up_ms = config_uint(path, bv, "bench.warm_up_ms", 0, 600000);
            else if (!strcmp(bk, "runs"))
               bench_cfg.runs = config_uint(path, bv, "bench.runs", 2, 1000);
            else if (!strcmp(bk, "delay_s"))
               bench_cfg.delay_s = config_uint(path, bv, "bench.delay_s", 0, 3600);
            else
               config_error(path, bv->line, "unknown key bench.%s (n, warm_up_ms, runs, delay_s)",
                            bk);
         }
      } else {
         config_error(path, v->line, "unknown key '%s' (driver, strict, tests, bench)", key);
      }
   }
}

/* ------------------------------------------------------------------------ */

static void __attribute__((noreturn))
usage(const char *argv0)
{
   printf("usage: %s [-sys|<driver.so>] [-c config.yaml] [--strict] [selector...]\n"
          "       %s -l [-c config.yaml] [selector...]\n"
          "selectors: a group (basic, eden, stages, bench), a test, or a pattern with * and ?;\n"
          "  none: the configuration's tests, else all but bench\n"
          "--strict: a feature the driver does not offer fails instead of skipping\n",
          argv0, argv0);
   exit(2);
}

static int
is_driver(const char *s)
{
   const size_t l = strlen(s);
   return !strcmp(s, "-sys") || (l > 3 && !strcmp(s + l - 3, ".so"));
}

int
main(int argc, char **argv)
{
   struct config cfg = {0};
   struct selectors cli = {0};
   const char *driver = NULL, *config = NULL;
   int list = 0, cli_strict = 0;
   for (int i = 1; i < argc; i++) {
      const char *a = argv[i];
      if (!strcmp(a, "-l")) {
         list = 1;
      } else if (!strcmp(a, "--strict")) {
         cli_strict = 1;
      } else if (!strcmp(a, "-c")) {
         if (++i == argc)
            usage(argv[0]);
         config = argv[i];
      } else if (is_driver(a)) {
         if (driver)
            usage(argv[0]);
         driver = a;
      } else if (a[0] == '-') {
         usage(argv[0]);
      } else {
         add_selector(&cli, a);
      }
   }
   if (config)
      load_config(config, &cfg);
   if (!driver)
      driver = cfg.driver;
   strict = cli_strict || cfg.strict;
   const struct selectors *sel = cli.n ? &cli : &cfg.tests;

   /* Every selector must pick something: a typo is an error, not an
    * empty run. */
   for (unsigned i = 0; i < sel->n; i++) {
      unsigned hits = 0;
      for (size_t t = 0; t < N_TESTS; t++)
         hits += matches(sel->s[i], tests[t].name);
      if (!hits) {
         printf("no test matches '%s' (vkt -l lists them)\n", sel->s[i]);
         return 2;
      }
   }

   if (list) {
      for (size_t t = 0; t < N_TESTS; t++) {
         if (selected(sel, tests[t].name))
            printf("%-26s %s\n", tests[t].name, tests[t].what);
      }
      return 0;
   }
   if (!driver)
      usage(argv[0]);

   vkt_init(driver);
   printf("device: %s\n", props.deviceName);
   printf("features: geometryShader %u tessellationShader %u multiViewport %u (maxViewports %u) "
          "fillModeNonSolid %u shaderCullDistance %u logicOp %u wideLines %u (lines up to %.2f) "
          "shaderDrawParameters %u variablePointersStorageBuffer %u hostQueryReset %u\n",
          FEAT.geometryShader, FEAT.tessellationShader, FEAT.multiViewport,
          props.limits.maxViewports, FEAT.fillModeNonSolid, FEAT.shaderCullDistance, FEAT.logicOp,
          FEAT.wideLines, props.limits.lineWidthRange[1], vkt_features.dp.shaderDrawParameters,
          vkt_features.vp.variablePointersStorageBuffer, vkt_features.hqr.hostQueryReset);
   for (size_t t = 0; t < N_TESTS; t++) {
      if (selected(sel, tests[t].name)) {
         printf("== %s\n", tests[t].name);
         tests[t].run();
      }
   }
   return vkt_finish();
}

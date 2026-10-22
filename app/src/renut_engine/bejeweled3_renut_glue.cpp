// peggle2-nx (2026-10-19): what Renut's renderer (src/renut_engine/gpu, reNut by masterspike52 as banjo-nx carries it)
// needs from the rest of banjo-nx's app, on Peggle 2's side:
//   - banjo-nx's enhancement switches (cpu/banjo_enhancements.h) on Peggle 2's two master switches;
//   - the cvars banjo-nx's own renderer defines and Renut's tuning reads (banjo-nx's defaults);
//   - the store cards: banjo-nx's generated code marks them (tools/store_cards.py), Peggle 2's does not: off;
//   - N&B's screen width hook (guest/widescreen.cpp, not built here): Peggle 2's screen is 1280 wide.
#include <cstdint>

#include <rex/cvar.h>

#include "cpu/banjo_enhancements.h"
#include "bejeweled3_enhancements.h"

REXCVAR_DECLARE(bool, bejeweled3_cpu_enhancements);
REXCVAR_DECLARE(bool, bejeweled3_noncpu_enhancements);

#if BEJEWELED3_NO_NATIVE_RENDERER
// peggle1-nx (2026-10-21): the NVK keys of the native renderer (src/me/masseffect/masseffect_native_draws.cpp, not built
// here), which Renut's tuning reads on the Switch (ControlNvkLocked). With deko3d and no Vulkan driver they do nothing;
// the native renderer's defaults.
REXCVAR_DEFINE_BOOL(masseffect_native_set4_differences, true, "Bejeweled3/Renut",
                    "NVK only: descriptor set 4 by differences (no driver in this build)");
REXCVAR_DEFINE_INT32(masseffect_native_nvk_measure, 64, "Bejeweled3/Renut",
                     "NVK only: measures inside NVK on 1 in N calls (no driver in this build)")
    .range(0, 4096);
REXCVAR_DEFINE_BOOL(masseffect_native_nvk_emission, true, "Bejeweled3/Renut",
                    "NVK only: each draw command written in one go (no driver in this build)");
REXCVAR_DEFINE_BOOL(masseffect_native_nvk_cbufs, true, "Bejeweled3/Renut",
                    "NVK only: only the cbuf slots that can get dirty looked at (no driver in this build)");
REXCVAR_DEFINE_BOOL(masseffect_native_nvk_dynamic, true, "Bejeweled3/Renut",
                    "NVK only: only the dirty dynamic state groups emitted (no driver in this build)");
REXCVAR_DEFINE_BOOL(masseffect_native_nvk_preload, true, "Bejeweled3/Renut",
                    "NVK only: cache hints for the pipeline and shaders (no driver in this build)");
#endif

REXCVAR_DEFINE_INT32(banjo_ring_core, 1, "Bejeweled3/Renut",
                     "Renut's renderer: its render thread is pinned to this core (-1 = the process's whole mask)")
    .range(-1, 3)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(banjo_background_compile_core, 2, "Bejeweled3/Renut",
                     "Renut's renderer: its pipeline prebuild workers and content verifiers run on this core")
    .range(-1, 3)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(banjo_prewarm_threads, 1, "Bejeweled3/Renut",
                     "Renut's renderer: pipeline prebuild worker threads (at the lowest priority)")
    .range(1, 3)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(banjo_cpu_verify, false, "Bejeweled3/Renut",
                    "Renut's renderer: its record worker and caches check themselves against the plain path (slower)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(banjo_gpu_nvk_draw_contract, true, "Bejeweled3/Renut",
                    "Renut's renderer on NVK: the driver's per-draw contract (nvk_switch_dibujo) when the driver has it")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(banjo_gpu_blend_skip, true, "Bejeweled3/Renut",
                    "Renut's renderer: transparent pixels whose blend leaves the target unchanged are discarded (also "
                    "needs banjo_renut_blend_skip)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(banjo_gpu_tess_grid, true, "Bejeweled3/Renut",
                    "Renut's renderer: tessellated patches drawn as an indexed grid (none in Peggle 2)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(banjo_gpu_robustness_off, true, "Bejeweled3/Renut",
                    "Renut's renderer: pipelines without buffer and image robustness (VK_EXT_pipeline_robustness)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(banjo_gpu_bool_spec, true, "Bejeweled3/Renut",
                    "Renut's renderer: the bool constants a pipeline's shaders test as specialization constants")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// The generated code does not mark store cards in Peggle 2 (no tools/store_cards.py): Renut hashes as it did before.
extern "C" const bool g_banjo_store_cards = false;

namespace banjo {

bool EnhancementActive(EnhancementGroup group, bool own_key) {
  if (!own_key) return false;
  return group == EnhancementGroup::kCpu ? REXCVAR_GET(bejeweled3_cpu_enhancements)
                                         : REXCVAR_GET(bejeweled3_noncpu_enhancements);
}

void CodegenChecksReport(int seconds) {
  bejeweled3::enhancements::CodegenChecksReport(seconds);
}

}  // namespace banjo

#if defined(PLUME_DEKO3D)
#include <plume_deko3d.h>

namespace bejeweled3::render {
bool Docked();  // src/me/bejeweled3_render_mode.cpp: the console's mode, as the window and the native renderer see it
}

namespace {
// deko3d's swapchain at the display's size for the mode (plume_deko3d.h g_deko3dWindowSize): with
// bejeweled3_renut_resolution_by_mode the docked frame is 1920x1080, and is shown at that size.
const bool g_deko3d_window_size_set = [] {
  plume::g_deko3dWindowSize = [](uint32_t &width, uint32_t &height) {
    const bool docked = bejeweled3::render::Docked();
    width = docked ? 1920 : 1280;
    height = docked ? 1080 : 720;
    return true;
  };
  return true;
}();
}  // namespace
#endif

namespace renut::gpu {

// N&B widens its screen for wide windows (guest/widescreen.cpp); Peggle 2 draws its own 1280x720.
uint32_t GuestScreenWidth() {
  return 1280;
}

}  // namespace renut::gpu

// banjo-nx's SDK counts the store-card memory for its verify-mode diagnostics (render/draw.cpp's 10 s line); Peggle 2's
// SDK keeps no store cards (g_banjo_store_cards is false), so there is nothing to count.
extern "C" uint64_t RexStoreCardsHostBytes(int /*which*/) {
  return 0;
}

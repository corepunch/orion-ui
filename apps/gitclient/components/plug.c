#include <orion/ui.h>
#include <orion/user/icons.h>
#include "diff_view.h"
#include "repo_board.h"

static const fe_component_desc_t k_gitclient_components[] = {
  {
    .class_name   = GC_DIFF_VIEW_CLASS_NAME,
    .name_prefix  = "GC_DIFF",
    .toolbar_icon = "database-check",
    .default_size = {260, 480},
    .default_layout_size = {0, 480},
    .capabilities = 0,
    .proc         = gc_diff_proc,
  },
  {
    .class_name   = GC_REPO_BOARD_CLASS_NAME,
    .name_prefix  = "GC_BOARD",
    .toolbar_icon = "git-repos",
    .default_size = {480, 360},
    .default_layout_size = {0, 360},
    .capabilities = 0,
    .proc         = gc_repo_board_proc,
  },
};

GEM_CLASSES(k_gitclient_components, "GitClient components", FE_PLUGIN_VERSION)

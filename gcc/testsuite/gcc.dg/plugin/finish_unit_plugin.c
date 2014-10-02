/* This plugin creates a fake function in the FINISH_UNIT callback, in
 * other words right after compilation of the translation unit. 
*/
#include "gcc-plugin.h"
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "toplev.h"
#include "predict.h"
#include "vec.h"
#include "hashtab.h"
#include "hash-set.h"
#include "machmode.h"
#include "hard-reg-set.h"
#include "input.h"
#include "function.h"
#include "basic-block.h"
#include "hash-table.h"
#include "ggc.h"
#include "predict.h"
#include "basic-block.h"
#include "tree-ssa-alias.h"
#include "internal-fn.h"
#include "gimple-fold.h"
#include "tree-eh.h"
#include "gimple-expr.h"
#include "is-a.h"
#include "gimple.h"
#include "tree.h"
#include "tree-pass.h"
#include "intl.h"
#include "hash-map.h"
#include "is-a.h"
#include "plugin-api.h"
#include "predict.h"
#include "basic-block.h"
#include "ipa-ref.h"
#include "dumpfile.h"
#include "cgraph.h"

int plugin_is_GPL_compatible;

static void finish_unit_callback (void *gcc_data, void *user_data)
{
  cgraph_build_static_cdtor ('I', NULL, DEFAULT_INIT_PRIORITY);
}

int plugin_init (struct plugin_name_args *plugin_info,
                 struct plugin_gcc_version *version)
{
  register_callback ("finish_unit", PLUGIN_FINISH_UNIT, &finish_unit_callback, NULL);
  return 0;
}

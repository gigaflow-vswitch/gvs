/* *************************************************************************
 *
 * Copyright 2023 Annus Zulfiqar (Purdue University),
 *                Venkat Kunaparaju (Purdue University)
 *                Ali Imran (Purdue University)
 *                Muhammad Shahbaz (Purdue University)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * *************************************************************************/

#include <config.h>
#include "gigaflow-config.h"

VLOG_DEFINE_THIS_MODULE(gigaflow_config);

struct gigaflow_config* gigaflow_config_init(void)
{
    struct gigaflow_config *gf_config;
    gf_config = xmalloc(sizeof(*gf_config));
    gf_config->gigaflow_enabled = false;
    gf_config->gigaflow_lookup_enabled = true;
    gf_config->gigaflow_debug_enabled = false;
    gf_config->gigaflow_tables_limit = GIGAFLOW_TABLES_LIMIT;
    gf_config->gigaflow_max_masks = GIGAFLOW_MAX_MASKS;
    gf_config->gigaflow_max_entries = GIGAFLOW_MAX_ENTRIES;
    gf_config->optimize_coupling = true;
    gf_config->optimize_paths = false;
    gf_config->coupling_base_score = COUPLING_BASE_SCORE;
    gf_config->coupling_scalar = COUPLING_SCALAR;
    gf_config->paths_scalar = PATHS_SCALAR;
    gf_config->batch_state_updates = false;
    gf_config->batch_update_interval = BATCH_UPDATE_INTERVAL;
    gf_config->warmup_batches = WARMUP_BATCHES;
    gf_config->estimate_flow_space = false;
    gf_config->hw_offload_p4sdnet = false;
    gf_config->offload_gigaflow = false;
    return gf_config;
}

bool
gigaflow_update_config(const struct smap *other_config,
                       struct gigaflow_config *gf_config)
{
    bool old_state = gf_config->gigaflow_enabled;
    gf_config->gigaflow_enabled = smap_get_bool(other_config, 
                                               "gigaflow-enable", false);

#ifndef GIGAFLOW_CACHE_ENABLED
    gf_config->gigaflow_enabled = false;
#endif

    /*
    if (!gf_config->gigaflow_enabled) {
        VLOG_INFO("Gigaflow is disabled");
        return old_state != gf_config->gigaflow_enabled;
    }
    */

    gf_config->gigaflow_lookup_enabled = smap_get_bool(other_config, 
                                                       "gigaflow-lookup-enable", 
                                                       true);
    gf_config->gigaflow_debug_enabled = smap_get_bool(other_config,
                                                      "gigaflow-debug-enable",
                                                      false);
    gf_config->gigaflow_tables_limit = smap_get_uint(other_config, 
                                                     "gigaflow-tables-limit", 
                                                     GIGAFLOW_TABLES_LIMIT);
    gf_config->gigaflow_max_masks = smap_get_uint(other_config,
                                                  "gigaflow-max-masks",
                                                  GIGAFLOW_MAX_MASKS);
    gf_config->gigaflow_max_entries = smap_get_uint(other_config,
                                                    "gigaflow-max-entries",
                                                    GIGAFLOW_MAX_ENTRIES);
    gf_config->optimize_coupling = smap_get_bool(other_config,
                                                 "gigaflow-optimize-coupling",
                                                 true);
    gf_config->optimize_paths = smap_get_bool(other_config,
                                              "gigaflow-optimize-paths",
                                              false);
    gf_config->coupling_base_score = smap_get_uint(other_config,
                                                   "gigaflow-coupling-base-score",
                                                   COUPLING_BASE_SCORE);
    gf_config->coupling_scalar = smap_get_uint(other_config, 
                                               "gigaflow-coupling-scaler",
                                               COUPLING_SCALAR);
    gf_config->paths_scalar = smap_get_uint(other_config, 
                                            "gigaflow-paths-scaler",
                                            PATHS_SCALAR);
    gf_config->batch_state_updates = smap_get_bool(other_config,
                                                   "gigaflow-batch-state-updates",
                                                   false);
    gf_config->batch_update_interval = smap_get_uint(other_config,
                                                     "gigaflow-batch-update-interval",
                                                     BATCH_UPDATE_INTERVAL);
    gf_config->warmup_batches = smap_get_uint(other_config, 
                                              "gigaflow-warmup-batches",
                                              WARMUP_BATCHES);
    gf_config->estimate_flow_space = smap_get_bool(other_config,
                                                   "gigaflow-estimate-flow-space",
                                                   false);
    gf_config->hw_offload_p4sdnet = smap_get_bool(other_config, 
                                                  "hw-offload-p4sdnet",
                                                  false);
    gf_config->offload_gigaflow = smap_get_bool(other_config,
                                                "gigaflow-offload",
                                                false);

    if (old_state != gf_config->gigaflow_enabled) {
        VLOG_INFO("Gigaflow cache: "
                  "enabled: %d, "
                  "software lookup: %d, "
                  "tables: %d, "
                  "max. masks per table: %d, "
                  "max. entries per table: %d, "
                  "optimize coupling: %d, "
                  "optimize paths: %d, "
                  "coupling base score: %d, "
                  "coupling scaler: %d, "
                  "paths scaler: %d, "
                  "batch state updates: %d, "
                  "warmup batches: %d, "
                  "p4sdnet offload: %d, "
                  "offload Gigaflow: %d",
                  gf_config->gigaflow_enabled,
                  gf_config->gigaflow_lookup_enabled,
                  gf_config->gigaflow_tables_limit,
                  gf_config->gigaflow_max_masks,
                  gf_config->gigaflow_max_entries,
                  gf_config->optimize_coupling,
                  gf_config->optimize_paths,
                  gf_config->coupling_base_score,
                  gf_config->coupling_scalar,
                  gf_config->paths_scalar,
                  gf_config->batch_state_updates,
                  gf_config->warmup_batches,
                  gf_config->hw_offload_p4sdnet,
                  gf_config->offload_gigaflow);
    }
    
    return old_state != gf_config->gigaflow_enabled;
}









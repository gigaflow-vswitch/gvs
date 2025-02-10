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

#ifndef GIGAFLOW_CONFIG_H
#define GIGAFLOW_CONFIG_H 1

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "openvswitch/vlog.h"
#include "ovs-atomic.h"
#include "smap.h"
#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif


#define GIGAFLOW_MAX_LIMIT        8

#define GIGAFLOW_CACHE_ENABLED    1

#define SLOW_PATH_TRAVERSAL_LIMIT 256
#define GIGAFLOW_TABLES_LIMIT     8
#define GIGAFLOW_PRIORITIES_LIMIT 64
#define _GIGAFLOW_NUM_TABLES      (GIGAFLOW_TABLES_LIMIT * GIGAFLOW_PRIORITIES_LIMIT)
#define _GET_GIGAFLOW_TABLE_ID(t, p) ((t * GIGAFLOW_PRIORITIES_LIMIT) + p)

#define GIGAFLOW_MAX_MASKS        64
#define GIGAFLOW_MAX_ENTRIES      100000

#define PATHS_SCALAR              1
#define COUPLING_SCALAR           100
#define COUPLING_BASE_SCORE       10
#define WARMUP_BATCHES            0
#define BATCH_UPDATE_INTERVAL     10

#define OUTPUT_TABLE_ID           255
#define DROP_TABLE_ID             254

#define GIGAFLOW_HIT              1

#define EMPTY_WC                  0b0000000000000000
#define IN_PORT                   0b0000000000000010
#define DL_TYPE                   0b0000000000000100
#define DL_SRC                    0b0000000000001000
#define DL_DST                    0b0000000000010000
#define NW_SRC                    0b0000000000100000
#define NW_DST                    0b0000000001000000
#define NW_PROTO                  0b0000000010000000
#define TP_SRC                    0b0000000100000000
#define TP_DST                    0b0000001000000000

#define SOURCE_LAYER              -1
#define SOURCE_VALUE              -1
#define SINK_LAYER                1000
#define SINK_VALUE                1000


/* configuration parameters for Gigaflow cache in dp_netdev */
struct gigaflow_config {
    bool gigaflow_enabled;
    bool gigaflow_lookup_enabled;
    bool gigaflow_debug_enabled;
    uint32_t gigaflow_tables_limit;
    uint32_t gigaflow_max_masks;
    uint32_t gigaflow_max_entries;
    bool optimize_coupling;
    bool optimize_paths;
    uint32_t coupling_base_score;
    uint32_t coupling_scalar;
    uint32_t paths_scalar;
    bool batch_state_updates;
    uint32_t batch_update_interval;
    uint32_t warmup_batches;
    bool estimate_flow_space;
    bool hw_offload_p4sdnet;
    bool offload_gigaflow;
};

/* initialize Gigaflow configuration */
struct gigaflow_config* gigaflow_config_init(void);

/* read and update Gigaflow configuration */
bool gigaflow_update_config(const struct smap *other_config, 
                            struct gigaflow_config *config);


#ifdef __cplusplus
}
#endif
#endif
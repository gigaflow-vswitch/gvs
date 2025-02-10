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

#ifndef GIGAFLOW_PERF_H
#define GIGAFLOW_PERF_H 1

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "openvswitch/dynamic-string.h"
#include "openvswitch/vlog.h"
#include "openvswitch/hmap.h"

#include "flow.h"
#include "gigaflow-config.h"

#ifdef __cplusplus
extern "C" {
#endif

enum gigaflow_stat_type {
    GF_STAT_TOTAL_PACKETS,                         /* (populated) total packets that passed through Gigaflow pipeline */
    GF_STAT_TOTAL_BATCHES,                         /* (populated) total packet batches processed until fast path processing */
    GF_STAT_TOTAL_BATCH_UPDATES,                   /* (populated) total batches that have seen state updates */
    GF_STAT_HIT_COUNT,                             /* (populated) total gigaflow hits out of all the seen packets
                                                      and gigaflow hit rate is calculated based on 
                                                      hit_count and total_packets in gf_perf_show_stats() */
    GF_STAT_HIT_RATE,                              /* (calculated) gigaflow hit rate calculated from total packet and hit count */
    
    GF_STAT_UPCALLS,                               /* (populated) total upcalls served successfully until now */
    GF_STAT_UPCALL_CYCLES,                         /* (populated) total cycles consumed by all upcalls until now */
    GF_STAT_UPCALL_CYCLES_MEAN,                    /* (calculated) mean cycles consumed by all upcalls until now */
    GF_STAT_UPCALL_CYCLES_VARIANCE,                /* (calculated) variance of cycles consumed by all upcalls until now */
    GF_STAT_UPCALL_CYCLES_STD_DEV,                 /* (calculated) standard deviation of cycles consumed by all upcalls until now */
    
    GF_STAT_LOOKUP_CYCLES,                         /* (populated) total cycles consumed by slow path lookup until now */
    GF_STAT_LOOKUP_CYCLES_MEAN,                    /* (calculated) mean cycles consumed by slow path lookup until now */
    GF_STAT_LOOKUP_CYCLES_VARIANCE,                /* (calculated) variance of cycles consumed by slow path lookup until now */
    GF_STAT_LOOKUP_CYCLES_STD_DEV,                 /* (calculated) standard deviation of cycles consumed by slow path lookup until now */
    GF_STAT_LOOKUP_CYCLES_PER_UPCALL,              /* (calculated) lookup's cycles per upcall consumption */
    GF_STAT_LOOKUP_PERC_OF_UPCALL,                 /* (calculated) lookup's avg. percentage time contribution to upcall */

    GF_STAT_MAP_CYCLES,                            /* (populated) total cycles consumed by mapper until now */
    GF_STAT_MAP_CYCLES_MEAN,                       /* (calculated) mean cycles consumed by mapper until now */
    GF_STAT_MAP_CYCLES_VARIANCE,                   /* (calculated) variance of cycles consumed by mapper until now */
    GF_STAT_MAP_CYCLES_STD_DEV,                    /* (calculated) standard deviation of cycles consumed by mapper until now */
    GF_STAT_MAP_CYCLES_PER_UPCALL,                 /* (calculated) mapper's cycles per upcall consumption */
    GF_STAT_MAP_PERC_OF_UPCALL,                    /* (calculated) mapper's avg. percentage time contribution to upcall */
    
    GF_STAT_OPTIMIZER_CYCLES,                      /* (populated) total cycles consumed by optimizer until now */
    GF_STAT_OPTIMIZER_CYCLES_MEAN,                 /* (calculated) mean cycles consumed by optimizer until now */     
    GF_STAT_OPTIMIZER_CYCLES_VARIANCE,             /* (calculated) variance of cycles consumed by optimizer until now */
    GF_STAT_OPTIMIZER_CYCLES_STD_DEV,              /* (calculated) standard deviation of cycles consumed by optimizer until now */        
    GF_STAT_OPTIMIZER_CYCLES_PER_UPCALL,           /* (calculated) optimizer's cycles per upcall consumption */
    GF_STAT_OPTIMIZER_PERC_OF_MAP,                 /* (calculated) optimizer's avg. percentage time contribution to mapping */
    
    GF_STAT_COMPOSITION_CYCLES,                    /* (populated) total cycles consumed by gigaflow composition until now */
    GF_STAT_COMPOSITION_CYCLES_MEAN,               /* (calculated) mean cycles consumed by gigaflow composition until now */     
    GF_STAT_COMPOSITION_CYCLES_VARIANCE,           /* (calculated) variance of cycles consumed by gigaflow composition until now */         
    GF_STAT_COMPOSITION_CYCLES_STD_DEV,            /* (calculated) standard deviation of cycles consumed by gigaflow composition until now */       
    GF_STAT_COMPOSITION_CYCLES_PER_UPCALL,         /* (calculated) composition's cycles per upcall consumption */
    GF_STAT_COMPOSITION_PERC_OF_MAP,               /* (calculated) composition's avg. percentage time contribution to mapping */
    
    GF_STAT_STATE_UPDATE_CYCLES,                   /* (populated) total cycles consumed by gigaflow state update until now */
    GF_STAT_STATE_UPDATE_CYCLES_MEAN,              /* (calculated) mean cycles consumed by gigaflow state update until now */      
    GF_STAT_STATE_UPDATE_CYCLES_VARIANCE,          /* (calculated) variance of cycles consumed by gigaflow state update until now */          
    GF_STAT_STATE_UPDATE_CYCLES_STD_DEV,           /* (calculated) standard deviation of cycles consumed by gigaflow state update until now */         
    GF_STAT_STATE_UPDATE_CYCLES_PER_UPCALL,        /* (calculated) state update's cycles per upcall consumption */
    GF_STAT_STATE_UPDATE_PERC_OF_MAP,              /* (calculated) state update's avg. percentage time contribution to mapping */

    GF_STAT_FLOW_SETUP_CYCLES,                     /* (populated) total cycles consumed by gigaflow flow setup until now */
    GF_STAT_FLOW_SETUP_CYCLES_MEAN,                /* (calculated) mean cycles consumed by gigaflow flow setup until now */    
    GF_STAT_FLOW_SETUP_CYCLES_VARIANCE,            /* (calculated) variance of cycles consumed by gigaflow flow setup until now */        
    GF_STAT_FLOW_SETUP_CYCLES_STD_DEV,             /* (calculated) standard deviation of cycles consumed by gigaflow flow setup until now */       
    GF_STAT_FLOW_SETUP_CYCLES_PER_UPCALL,          /* (calculated) flow setup's cycles per upcall consumption */
    GF_STAT_FLOW_SETUP_PERC_OF_UPCALL,             /* (calculated) flow setup's avg. percentage time contribution to upcall */

    GF_STAT_STATE_BATCH_UPDATE_CYCLES,             /* (populated) total cycles consumed by gigaflow state batch update until now */
    GF_STAT_STATE_BATCH_UPDATE_CYCLES_MEAN,        /* (calculated) mean cycles consumed by gigaflow state batch update until now */            
    GF_STAT_STATE_BATCH_UPDATE_CYCLES_VARIANCE,    /* (calculated) variance of cycles consumed by gigaflow state batch update until now */                
    GF_STAT_STATE_BATCH_UPDATE_CYCLES_STD_DEV,     /* (calculated) standard deviation of cycles consumed by gigaflow state batch update until now */               
    GF_STAT_STATE_BATCH_UPDATE_CYCLES_PER_BATCH,   /* (calculated) batch state update's cycles per batch consumption */
    
    GF_N_STATS
};

/* a bit-wildcard representation that Gigaflow uses to maintain 
   unique masks in each Gigaflow table */
struct gigaflow_bit_wildcard {
    uint16_t bit_wildcard;
    ovs_be32 nw_src;
    ovs_be32 nw_dst;
};

/* a single node to represent a unique mask in a Gigaflow table */
struct gigaflow_mask_node {
    struct hmap_node node;
    struct gigaflow_bit_wildcard wildcard;
    struct flow_wildcards flow_wc;
};

/* statistics of a single unique mapping in Gigaflow */
struct unique_mapping_stats {
    struct hmap_node node;                         /* hmap node for unique_mappings hmap */
    uint32_t cache_occupancy[GIGAFLOW_MAX_LIMIT];  /* cache occupancy of this mapping */
    bool has_overlaps;                             /* does this mapping have overlaps */
    uint32_t rule_space;                           /* total number of Megaflows captured in this mapping */
    uint32_t prev_rule_space;                      /* previous total number of Megaflows captured in this mapping */
};

/* performance statistics for Gigaflow cache */
struct gigaflow_perf_stats {
    double stats[GF_N_STATS];
    double last_samples[GF_N_STATS];
    uint32_t std_deviation_updates;
    uint32_t cache_occupancy[GIGAFLOW_MAX_LIMIT]; 
    uint32_t cache_occupancy_per_priority_table[_GIGAFLOW_NUM_TABLES];
    uint32_t mask_occupancy[GIGAFLOW_MAX_LIMIT]; 
    uint32_t cache_recycled[GIGAFLOW_MAX_LIMIT];
    double cache_recyle_rate[GIGAFLOW_MAX_LIMIT];
    uint32_t gigaflow_occupancy, gigaflow_recycled;
    double gigaflow_recycle_rate;
    struct hmap masks_per_table;
    struct hmap unique_mappings_cache_occupancy; // hash table of unique mappings
    uint32_t num_unique_mappings; // total number of unique mappings
    uint64_t megaflow_rule_space; // total number of Megaflows captured in Gigaflow
    bool rule_space_hard_to_estimate; // is the rule space hard to estimate
};

/* initialize Gigaflow performance statistics */
void gf_perf_stats_init(struct gigaflow_perf_stats *gf_s);

/* initialize Gigaflow performance statistics */
void gf_perf_stats_destroy(struct gigaflow_perf_stats *gf_s);

/* overwrite some statistic in this gigaflow performance struct */
void gf_perf_write_counter(struct gigaflow_perf_stats *gf_s,
                           enum gigaflow_stat_type counter, 
                           double value);

/* update some statistic in this gigaflow performance struct by delta */
void gf_perf_update_counter(struct gigaflow_perf_stats *gf_s,
                            enum gigaflow_stat_type counter, 
                            double delta);

/* increment mask occupancy counter of a Gigaflow table */
void gf_perf_inc_mask_occupancy(struct gigaflow_perf_stats *gf_s,
                                uint32_t gf_table_id);

/* increment cache occupancy counter of a Gigaflow table */
void gf_perf_inc_cache_occupancy(struct gigaflow_perf_stats *gf_s,
                                 uint32_t gf_table_id);

/* increment cache occupancy counter of a Gigaflow table with priority */
void
gf_perf_inc_cache_occupancy_with_priority(struct gigaflow_perf_stats *gf_s,
                                          uint32_t gf_table_id, uint32_t priority);

/* increment cache recyled counter of a Gigaflow table */
void gf_perf_inc_cache_recycled(struct gigaflow_perf_stats *gf_s,
                                uint32_t gf_table_id);

/* check if a mask exists in a given Gigaflow table */
bool gf_perf_is_mask_in_table(struct gigaflow_perf_stats *gf_s,
                              struct gigaflow_bit_wildcard *mask,
                              struct flow_wildcards *flow_wc,
                              uint32_t gf_table_id);

/* add new mask to a given Gigaflow table */
void gf_perf_add_mask_in_table(struct gigaflow_perf_stats *gf_s,
                               struct gigaflow_bit_wildcard *mask,
                               struct flow_wildcards *flow_wc,
                               uint32_t gf_table_id);

/* increment cache occupancy in one table of a unique mapping */
void 
gf_perf_inc_cache_occupancy_of_unique_mask_with_hash(struct gigaflow_perf_stats *gf_s,
                                                     uint32_t unique_map_hash, 
                                                     uint32_t gf_table_id);

/* updates all statistics; some of them need to be calculated */
void gf_perf_update_stats(struct gigaflow_perf_stats *gf_s,
                          struct gigaflow_config *gf_config);

/* formats gigaflow per-table performance from gf_s* into ds* */
void gf_perf_show_table_stats(struct ds *reply, 
                              struct gigaflow_perf_stats *gf_s,
                              struct gigaflow_config *gf_config);

/* formats gigaflow performance from gf_s* into ds* with the given seperator */
void gf_perf_show_pmd_perf(struct ds *reply, struct gigaflow_perf_stats *gf_s,
                           struct gigaflow_config *gf_config);

/* formats gigaflow statistics from gf_s* into ds* with the given seperator */
void gf_perf_show_pmd_stats(struct ds *reply, 
                            struct gigaflow_perf_stats *gf_s,
                            struct gigaflow_config *gf_config);

/* clear all statistics in this gigaflow performance struct */
void
gf_perf_stats_clear(struct gigaflow_perf_stats *gf_s);

#ifdef __cplusplus
}
#endif
#endif
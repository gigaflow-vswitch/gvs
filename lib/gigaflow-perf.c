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
#include "gigaflow-perf.h"


void
gf_perf_stats_init(struct gigaflow_perf_stats *gf_s)
{
    memset(gf_s->stats, 0, sizeof gf_s->stats);
    memset(gf_s->last_samples, 0, sizeof gf_s->last_samples);
    memset(gf_s->cache_occupancy, 0, sizeof gf_s->cache_occupancy);
    memset(gf_s->cache_occupancy_per_priority_table, 
        0, sizeof gf_s->cache_occupancy_per_priority_table);
    memset(gf_s->mask_occupancy, 0, sizeof gf_s->mask_occupancy);
    memset(gf_s->cache_recycled, 0, sizeof gf_s->cache_recycled);
    gf_s->gigaflow_occupancy = 0;
    gf_s->gigaflow_recycled = 0;
    gf_s->gigaflow_recycle_rate = 0;
    hmap_init(&gf_s->masks_per_table);
    hmap_init(&gf_s->unique_mappings_cache_occupancy);
    gf_s->std_deviation_updates = 0;
    gf_s->num_unique_mappings = 0;
    gf_s->megaflow_rule_space = 0;
    gf_s->rule_space_hard_to_estimate = true;
}

void gf_perf_stats_destroy(struct gigaflow_perf_stats *gf_s)
{
    struct gigaflow_mask_node *gf_mask_entry;
    HMAP_FOR_EACH_POP (gf_mask_entry, node, &gf_s->masks_per_table) {
        free(gf_mask_entry);
    }
    hmap_destroy(&gf_s->masks_per_table);
    struct unique_mapping_stats *unique_map_stats;
    HMAP_FOR_EACH_POP (unique_map_stats, node, 
        &gf_s->unique_mappings_cache_occupancy) {
        free(unique_map_stats);
    }
    hmap_destroy(&gf_s->unique_mappings_cache_occupancy);
}

void gf_perf_write_counter(struct gigaflow_perf_stats *gf_s,
                           enum gigaflow_stat_type counter, 
                           double value)
{
    gf_s->stats[counter] = value;
}

void
gf_perf_update_counter(struct gigaflow_perf_stats *gf_s,
                       enum gigaflow_stat_type counter, 
                       double delta)
{
    gf_s->stats[counter] += delta;
    gf_s->last_samples[counter] = delta;
}

void
gf_perf_inc_mask_occupancy(struct gigaflow_perf_stats *gf_s,
                           uint32_t gf_table_id)
{
    gf_s->mask_occupancy[gf_table_id]++;
}

void
gf_perf_inc_cache_occupancy(struct gigaflow_perf_stats *gf_s,
                            uint32_t gf_table_id)
{
    gf_s->cache_occupancy[gf_table_id]++;
    gf_s->gigaflow_occupancy++;
}

void
gf_perf_inc_cache_occupancy_with_priority(struct gigaflow_perf_stats *gf_s,
                                          uint32_t gf_table_id, uint32_t priority)
{
    gf_s->cache_occupancy_per_priority_table[_GET_GIGAFLOW_TABLE_ID(gf_table_id, priority)]++;
}

void 
gf_perf_inc_cache_recycled(struct gigaflow_perf_stats *gf_s,
                           uint32_t gf_table_id)
{
    gf_s->cache_recycled[gf_table_id]++;
    gf_s->gigaflow_recycled++;
}

bool 
gf_perf_is_mask_in_table(struct gigaflow_perf_stats *gf_s,
                         struct gigaflow_bit_wildcard *mask,
                         struct flow_wildcards *flow_wc,
                         uint32_t gf_table_id)
{
    struct gigaflow_mask_node *gf_mask_entry;
    uint32_t gf_mask_hash = hash_4words_plus1(gf_table_id, mask->bit_wildcard,
                                              mask->nw_src, mask->nw_dst);
    HMAP_FOR_EACH_WITH_HASH (gf_mask_entry, node, gf_mask_hash, 
                             &gf_s->masks_per_table) {
        // if (gf_mask_entry->wildcard.bit_wildcard == mask->bit_wildcard
        //     && gf_mask_entry->wildcard.nw_src == mask->nw_src
        //     && gf_mask_entry->wildcard.nw_dst == mask->nw_dst) {
        //     /* mask already exists, no need to increment */
        //     return;
        // }
        // compare the incoming flow wildcard with the existing mask
        if (flow_wildcards_equal(flow_wc, &gf_mask_entry->flow_wc)) {
            return true;
        }
    }
    return false;
}

void 
gf_perf_add_mask_in_table(struct gigaflow_perf_stats *gf_s,
                          struct gigaflow_bit_wildcard *mask,
                          struct flow_wildcards *flow_wc,
                          uint32_t gf_table_id)
{
    if (gf_perf_is_mask_in_table(gf_s, mask, flow_wc, gf_table_id)) {
        return;
    }
    struct gigaflow_mask_node *gf_mask_entry;
    uint32_t gf_mask_hash = hash_4words_plus1(gf_table_id, mask->bit_wildcard,
                                              mask->nw_src, mask->nw_dst);
    /* new mask, add to masks-per-table */
    gf_mask_entry = xzalloc(sizeof *gf_mask_entry);
    gf_mask_entry->wildcard.bit_wildcard = mask->bit_wildcard;
    gf_mask_entry->wildcard.nw_src = mask->nw_src;
    gf_mask_entry->wildcard.nw_dst = mask->nw_dst;
    gf_mask_entry->flow_wc = *flow_wc; // store the actual flow wildcard too
    hmap_insert(&gf_s->masks_per_table, &gf_mask_entry->node, gf_mask_hash);
    gf_perf_inc_mask_occupancy(gf_s, gf_table_id);
}

void 
gf_perf_inc_cache_occupancy_of_unique_mask_with_hash(struct gigaflow_perf_stats *gf_s,
                                                     uint32_t unique_map_hash, 
                                                     uint32_t gf_table_id)
{
    /* increment this one table's occupancy in this unique mapping */
    struct unique_mapping_stats *unique_map_stats;
    HMAP_FOR_EACH_WITH_HASH (unique_map_stats, node, unique_map_hash, 
        &gf_s->unique_mappings_cache_occupancy) {
        unique_map_stats->cache_occupancy[gf_table_id] += 1;
        break;
    }
}

void
gf_perf_update_stats(struct gigaflow_perf_stats *gf_s, 
                     struct gigaflow_config *gf_config)
{
    const uint32_t gf_tables_limit = gf_config->gigaflow_tables_limit;

    double gf_total_pkts = gf_s->stats[GF_STAT_TOTAL_PACKETS] ? gf_s->stats[GF_STAT_TOTAL_PACKETS] : 1;
    double gf_total_batch_updates = gf_s->stats[GF_STAT_TOTAL_BATCH_UPDATES] ? gf_s->stats[GF_STAT_TOTAL_BATCH_UPDATES] : 1;
    
    gf_s->stats[GF_STAT_HIT_RATE] = 100 * gf_s->stats[GF_STAT_HIT_COUNT] / gf_total_pkts;

    double gf_total_upcalls = gf_s->stats[GF_STAT_UPCALLS] ? gf_s->stats[GF_STAT_UPCALLS] : 1;
    double gf_upcall_cycles = gf_s->stats[GF_STAT_UPCALL_CYCLES] ? gf_s->stats[GF_STAT_UPCALL_CYCLES] : 1;
    
    gf_s->stats[GF_STAT_LOOKUP_CYCLES_PER_UPCALL] = gf_s->stats[GF_STAT_LOOKUP_CYCLES] / gf_total_upcalls;
    gf_s->stats[GF_STAT_LOOKUP_PERC_OF_UPCALL] = 100 * gf_s->stats[GF_STAT_LOOKUP_CYCLES] / gf_upcall_cycles;

    gf_s->stats[GF_STAT_MAP_CYCLES_PER_UPCALL] = gf_s->stats[GF_STAT_MAP_CYCLES] / gf_total_upcalls;
    gf_s->stats[GF_STAT_MAP_PERC_OF_UPCALL] = 100 * gf_s->stats[GF_STAT_MAP_CYCLES] / gf_upcall_cycles;

    gf_s->stats[GF_STAT_OPTIMIZER_CYCLES_PER_UPCALL] = gf_s->stats[GF_STAT_OPTIMIZER_CYCLES] / gf_total_upcalls;
    gf_s->stats[GF_STAT_OPTIMIZER_PERC_OF_MAP] = 100 * gf_s->stats[GF_STAT_OPTIMIZER_CYCLES] / gf_s->stats[GF_STAT_MAP_CYCLES];

    gf_s->stats[GF_STAT_COMPOSITION_CYCLES_PER_UPCALL] = gf_s->stats[GF_STAT_COMPOSITION_CYCLES] / gf_total_upcalls;
    gf_s->stats[GF_STAT_COMPOSITION_PERC_OF_MAP] = 100 * gf_s->stats[GF_STAT_COMPOSITION_CYCLES] / gf_s->stats[GF_STAT_MAP_CYCLES];

    gf_s->stats[GF_STAT_STATE_UPDATE_CYCLES_PER_UPCALL] = gf_s->stats[GF_STAT_STATE_UPDATE_CYCLES] / gf_total_upcalls;
    gf_s->stats[GF_STAT_STATE_UPDATE_PERC_OF_MAP] = 100 * gf_s->stats[GF_STAT_STATE_UPDATE_CYCLES] / gf_s->stats[GF_STAT_MAP_CYCLES];

    gf_s->stats[GF_STAT_FLOW_SETUP_CYCLES_PER_UPCALL] = gf_s->stats[GF_STAT_FLOW_SETUP_CYCLES] / gf_total_upcalls;
    gf_s->stats[GF_STAT_FLOW_SETUP_PERC_OF_UPCALL] = 100 * gf_s->stats[GF_STAT_FLOW_SETUP_CYCLES] / gf_upcall_cycles;

    gf_s->stats[GF_STAT_STATE_BATCH_UPDATE_CYCLES_PER_BATCH] = gf_s->stats[GF_STAT_STATE_BATCH_UPDATE_CYCLES] / gf_total_batch_updates;

    /* standard deviation on a streaming input of samples */
    double delta_1 = 0, delta_2 = 0;
    gf_s->std_deviation_updates++;

    /* upcall */
    if (gf_s->last_samples[GF_STAT_UPCALL_CYCLES]) {
        delta_1 = gf_s->last_samples[GF_STAT_UPCALL_CYCLES] - gf_s->stats[GF_STAT_UPCALL_CYCLES_MEAN];
        gf_s->stats[GF_STAT_UPCALL_CYCLES_MEAN] += delta_1 / gf_s->std_deviation_updates;
        delta_2 = gf_s->last_samples[GF_STAT_UPCALL_CYCLES] - gf_s->stats[GF_STAT_UPCALL_CYCLES_MEAN];
        gf_s->stats[GF_STAT_UPCALL_CYCLES_VARIANCE] += delta_1 * delta_2;
        gf_s->stats[GF_STAT_UPCALL_CYCLES_STD_DEV] = 
            sqrt(gf_s->stats[GF_STAT_UPCALL_CYCLES_VARIANCE] / gf_s->std_deviation_updates);
    }
    
    /* upcall -> lookup */
    if (gf_s->last_samples[GF_STAT_LOOKUP_CYCLES]) {
        delta_1 = gf_s->last_samples[GF_STAT_LOOKUP_CYCLES] - gf_s->stats[GF_STAT_LOOKUP_CYCLES_MEAN];
        gf_s->stats[GF_STAT_LOOKUP_CYCLES_MEAN] += delta_1 / gf_s->std_deviation_updates;
        delta_2 = gf_s->last_samples[GF_STAT_LOOKUP_CYCLES] - gf_s->stats[GF_STAT_LOOKUP_CYCLES_MEAN];
        gf_s->stats[GF_STAT_LOOKUP_CYCLES_VARIANCE] += delta_1 * delta_2;
        gf_s->stats[GF_STAT_LOOKUP_CYCLES_STD_DEV] = 
            sqrt(gf_s->stats[GF_STAT_LOOKUP_CYCLES_VARIANCE] / gf_s->std_deviation_updates);
    }
    
    /* upcall -> mapping */
    if (gf_s->last_samples[GF_STAT_MAP_CYCLES]) {
        delta_1 = gf_s->last_samples[GF_STAT_MAP_CYCLES] - gf_s->stats[GF_STAT_MAP_CYCLES_MEAN];
        gf_s->stats[GF_STAT_MAP_CYCLES_MEAN] += delta_1 / gf_s->std_deviation_updates;
        delta_2 = gf_s->last_samples[GF_STAT_MAP_CYCLES] - gf_s->stats[GF_STAT_MAP_CYCLES_MEAN];
        gf_s->stats[GF_STAT_MAP_CYCLES_VARIANCE] += delta_1 * delta_2;
        gf_s->stats[GF_STAT_MAP_CYCLES_STD_DEV] = 
            sqrt(gf_s->stats[GF_STAT_MAP_CYCLES_VARIANCE] / gf_s->std_deviation_updates);
    }
    
    /* mapping -> optimizer */
    if (gf_s->last_samples[GF_STAT_OPTIMIZER_CYCLES]) {
        delta_1 = gf_s->last_samples[GF_STAT_OPTIMIZER_CYCLES] - gf_s->stats[GF_STAT_OPTIMIZER_CYCLES_MEAN];
        gf_s->stats[GF_STAT_OPTIMIZER_CYCLES_MEAN] += delta_1 / gf_s->std_deviation_updates;
        delta_2 = gf_s->last_samples[GF_STAT_OPTIMIZER_CYCLES] - gf_s->stats[GF_STAT_OPTIMIZER_CYCLES_MEAN];
        gf_s->stats[GF_STAT_OPTIMIZER_CYCLES_VARIANCE] += delta_1 * delta_2;
        gf_s->stats[GF_STAT_OPTIMIZER_CYCLES_STD_DEV] = 
            sqrt(gf_s->stats[GF_STAT_OPTIMIZER_CYCLES_VARIANCE] / gf_s->std_deviation_updates);
    }
    
    /* mapping -> composition */
    if (gf_s->last_samples[GF_STAT_COMPOSITION_CYCLES]) {
        delta_1 = gf_s->last_samples[GF_STAT_COMPOSITION_CYCLES] - gf_s->stats[GF_STAT_COMPOSITION_CYCLES_MEAN];
        gf_s->stats[GF_STAT_COMPOSITION_CYCLES_MEAN] += delta_1 / gf_s->std_deviation_updates;
        delta_2 = gf_s->last_samples[GF_STAT_COMPOSITION_CYCLES] - gf_s->stats[GF_STAT_COMPOSITION_CYCLES_MEAN];
        gf_s->stats[GF_STAT_COMPOSITION_CYCLES_VARIANCE] += delta_1 * delta_2;
        gf_s->stats[GF_STAT_COMPOSITION_CYCLES_STD_DEV] = 
            sqrt(gf_s->stats[GF_STAT_COMPOSITION_CYCLES_VARIANCE] / gf_s->std_deviation_updates);
    }
    
    /* mapping -> state update */
    if (gf_s->last_samples[GF_STAT_STATE_UPDATE_CYCLES]) {
        delta_1 = gf_s->last_samples[GF_STAT_STATE_UPDATE_CYCLES] - gf_s->stats[GF_STAT_STATE_UPDATE_CYCLES_MEAN];
        gf_s->stats[GF_STAT_STATE_UPDATE_CYCLES_MEAN] += delta_1 / gf_s->std_deviation_updates;
        delta_2 = gf_s->last_samples[GF_STAT_STATE_UPDATE_CYCLES] - gf_s->stats[GF_STAT_STATE_UPDATE_CYCLES_MEAN];
        gf_s->stats[GF_STAT_STATE_UPDATE_CYCLES_VARIANCE] += delta_1 * delta_2;
        gf_s->stats[GF_STAT_STATE_UPDATE_CYCLES_STD_DEV] = 
            sqrt(gf_s->stats[GF_STAT_STATE_UPDATE_CYCLES_VARIANCE] / gf_s->std_deviation_updates);
    }

    /* mapping -> flow setup */
    if (gf_s->last_samples[GF_STAT_FLOW_SETUP_CYCLES]) {
        delta_1 = gf_s->last_samples[GF_STAT_FLOW_SETUP_CYCLES] - gf_s->stats[GF_STAT_FLOW_SETUP_CYCLES_MEAN];
        gf_s->stats[GF_STAT_FLOW_SETUP_CYCLES_MEAN] += delta_1 / gf_s->std_deviation_updates;
        delta_2 = gf_s->last_samples[GF_STAT_FLOW_SETUP_CYCLES] - gf_s->stats[GF_STAT_FLOW_SETUP_CYCLES_MEAN];
        gf_s->stats[GF_STAT_FLOW_SETUP_CYCLES_VARIANCE] += delta_1 * delta_2;
        gf_s->stats[GF_STAT_FLOW_SETUP_CYCLES_STD_DEV] = 
            sqrt(gf_s->stats[GF_STAT_FLOW_SETUP_CYCLES_VARIANCE] / gf_s->std_deviation_updates);
    }

    /* batch state update */
    if (gf_s->last_samples[GF_STAT_STATE_BATCH_UPDATE_CYCLES]) {
        delta_1 = gf_s->last_samples[GF_STAT_STATE_BATCH_UPDATE_CYCLES] - gf_s->stats[GF_STAT_STATE_BATCH_UPDATE_CYCLES_MEAN];
        gf_s->stats[GF_STAT_STATE_BATCH_UPDATE_CYCLES_MEAN] += delta_1 / gf_s->std_deviation_updates;
        delta_2 = gf_s->last_samples[GF_STAT_STATE_BATCH_UPDATE_CYCLES] - gf_s->stats[GF_STAT_STATE_BATCH_UPDATE_CYCLES_MEAN];
        gf_s->stats[GF_STAT_STATE_BATCH_UPDATE_CYCLES_VARIANCE] += delta_1 * delta_2;
        gf_s->stats[GF_STAT_STATE_BATCH_UPDATE_CYCLES_STD_DEV] = 
            sqrt(gf_s->stats[GF_STAT_STATE_BATCH_UPDATE_CYCLES_VARIANCE] / gf_s->std_deviation_updates);
    }
    
    /* clear last samples before reuse */
    memset(gf_s->last_samples, 0, sizeof gf_s->last_samples);

    /* update cache occupancy */
    for (int k=0; k<gf_tables_limit; k++) {
        uint32_t cache_occ_k = gf_s->cache_occupancy[k] ? gf_s->cache_occupancy[k] : 1;
        gf_s->cache_recyle_rate[k] = 100 * gf_s->cache_recycled[k] / (cache_occ_k + gf_s->cache_recycled[k]);
    }
    uint32_t gigaflow_occ = gf_s->gigaflow_occupancy ? gf_s->gigaflow_occupancy : 1;
    gf_s->gigaflow_recycle_rate = 100 * gf_s->gigaflow_recycled / (gigaflow_occ + gf_s->gigaflow_recycled);

    /* estimate Megaflow flow space captured in Gigaflow */
    const bool estimate_flow_space = gf_config->estimate_flow_space;
    if (!estimate_flow_space) {
        return;
    }

    /* update rule space in terms of number of Megaflow captured */
    gf_s->megaflow_rule_space = 0;

    /* if rule space is not hard to estimate, then calculate a size */
    struct unique_mapping_stats *unique_map_stats;
    HMAP_FOR_EACH (unique_map_stats, node, &gf_s->unique_mappings_cache_occupancy) {
        uint32_t this_unique_map_rule_space = 1;
        uint32_t maximum_occupancy = 1;
        int max_index = -1;
        /* get maximum cache occupancy of a table in this mapping */
        for (int k=0; k < gf_tables_limit; k++) {
            if (unique_map_stats->cache_occupancy[k] > maximum_occupancy) {
                maximum_occupancy = unique_map_stats->cache_occupancy[k];
                max_index = k;
            }
        }
        /* overlap means no contribution from permutations 
           so we just return maximum occupancy to be conservative */
        if (unique_map_stats->has_overlaps) {
            unique_map_stats->prev_rule_space = unique_map_stats->rule_space;
            unique_map_stats->rule_space = maximum_occupancy;
            gf_s->megaflow_rule_space += unique_map_stats->rule_space;
            continue;
        }
        if (max_index == -1) {
            continue;
        }
        /* get all non-zero occupancies */
        uint32_t non_zero_occupancies[GIGAFLOW_TABLES_LIMIT];
        int non_zeros = 0;
        for (int k=0; k<gf_tables_limit; k++) {
            if (unique_map_stats->cache_occupancy[k]) {
                non_zero_occupancies[non_zeros++] = 
                    unique_map_stats->cache_occupancy[k];
            }
        }
        /* get the maximum product of consecutive non-zero occupancies */
        uint32_t maximum_product = 1, product = 1;
        for (int i=0; i<non_zeros-1; i++) {
            product = non_zero_occupancies[i] * non_zero_occupancies[i+1];
            if (product > maximum_product) {
                maximum_product = product;
            }
        }
        
        /* add this mapping's contribution to total Megaflow rule space */
        this_unique_map_rule_space = maximum_product;
        unique_map_stats->prev_rule_space = unique_map_stats->rule_space;
        unique_map_stats->rule_space = this_unique_map_rule_space;
        gf_s->megaflow_rule_space += unique_map_stats->rule_space;
    }

    /* if rule space is hard to estimate, just show number of upcalls
       basically Gigaflow rule space is equal to Megaflow rule space
       Note: this check will have to be replaced with Megaflow occupancy count
       when upcalls don't match the number of Megaflows anymore, such as when
       rules expire faster in Megaflow */
    if (gf_s->megaflow_rule_space < gf_total_upcalls) {
        gf_s->megaflow_rule_space = gf_total_upcalls;
        gf_s->rule_space_hard_to_estimate = true;
        return;
    }

}

void 
gf_perf_show_table_stats(struct ds *reply, struct gigaflow_perf_stats *gf_s,
                         struct gigaflow_config *gf_config)
{
    const uint32_t gf_tables_limit = gf_config->gigaflow_tables_limit;
    uint32_t priority_usage;
    for (int k=0; k<gf_tables_limit; k++) {
        ds_put_format(reply,
                      "    - Table-%d:        %12"PRIu32" / %3"PRIu32" "
                      "(%5.1f %% recyle rate, %2"PRIu32" masks, priority usage: ",
                      k, 
                      gf_s->cache_occupancy[k], 
                      gf_s->cache_recycled[k], 
                      gf_s->cache_recyle_rate[k],
                      gf_s->mask_occupancy[k]);
        for (int p=0; p<GIGAFLOW_PRIORITIES_LIMIT; p++) {
            /* show non-zero priority utilization only */
            priority_usage = 
                gf_s->cache_occupancy_per_priority_table[_GET_GIGAFLOW_TABLE_ID(k, p)];
            if (priority_usage > 0) {
                ds_put_format(reply, "%1"PRIu32": %1"PRIu32", ", p, priority_usage);
            }
        }
        ds_put_format(reply, ")\n");
    }
}

void 
gf_perf_show_pmd_perf(struct ds *reply, 
                      struct gigaflow_perf_stats *gf_s,
                      struct gigaflow_config *gf_config)
{
    ds_put_format(reply,
            "  Upcall cycles:      %12"PRIu64"\n"
            "  Gigaflow stats:\n"
            "  - Hits:             %12"PRIu64"  (%5.1f %%)\n"
            "  - Lookup cycles:    %12"PRIu64"  (%5.1f cycles/upcall [std-dev: %5.1f], %5.1f %% of upcall)\n"
            "  - Mapping cycles:   %12"PRIu64"  (%5.1f cycles/upcall [std-dev: %5.1f], %5.1f %% of upcall)\n"
            "    - Optimizer:      %12"PRIu64"  (%5.1f cycles/upcall [std-dev: %5.1f], %5.1f %% of mapping)\n"
            "    - Composition:    %12"PRIu64"  (%5.1f cycles/upcall [std-dev: %5.1f], %5.1f %% of mapping)\n"
            "    - State update:   %12"PRIu64"  (%5.1f cycles/upcall [std-dev: %5.1f], %5.1f %% of mapping)\n"
            "  - Setup cycles:     %12"PRIu64"  (%5.1f cycles/upcall [std-dev: %5.1f], %5.1f %% of upcall)\n"
            "  - Batch update:     %12"PRIu64"  (%5.1f cycles/batch [std-dev: %5.1f])\n"
            "  - Unique mappings:  %12"PRIu32"\n"
            "  - Rule space:       %12"PRIu64" megaflows",
            (uint64_t)gf_s->stats[GF_STAT_UPCALL_CYCLES],
            (uint64_t)gf_s->stats[GF_STAT_HIT_COUNT],
            gf_s->stats[GF_STAT_HIT_RATE],
            (uint64_t)gf_s->stats[GF_STAT_LOOKUP_CYCLES],
            gf_s->stats[GF_STAT_LOOKUP_CYCLES_PER_UPCALL],
            gf_s->stats[GF_STAT_LOOKUP_CYCLES_STD_DEV],
            gf_s->stats[GF_STAT_LOOKUP_PERC_OF_UPCALL],
            (uint64_t)gf_s->stats[GF_STAT_MAP_CYCLES],
            gf_s->stats[GF_STAT_MAP_CYCLES_PER_UPCALL],
            gf_s->stats[GF_STAT_MAP_CYCLES_STD_DEV],
            gf_s->stats[GF_STAT_MAP_PERC_OF_UPCALL],
            (uint64_t)gf_s->stats[GF_STAT_OPTIMIZER_CYCLES],
            gf_s->stats[GF_STAT_OPTIMIZER_CYCLES_PER_UPCALL],
            gf_s->stats[GF_STAT_OPTIMIZER_CYCLES_STD_DEV],
            gf_s->stats[GF_STAT_OPTIMIZER_PERC_OF_MAP],
            (uint64_t)gf_s->stats[GF_STAT_COMPOSITION_CYCLES],
            gf_s->stats[GF_STAT_COMPOSITION_CYCLES_PER_UPCALL],
            gf_s->stats[GF_STAT_COMPOSITION_CYCLES_STD_DEV],
            gf_s->stats[GF_STAT_COMPOSITION_PERC_OF_MAP],
            (uint64_t)gf_s->stats[GF_STAT_STATE_UPDATE_CYCLES],
            gf_s->stats[GF_STAT_STATE_UPDATE_CYCLES_PER_UPCALL],
            gf_s->stats[GF_STAT_STATE_UPDATE_CYCLES_STD_DEV],
            gf_s->stats[GF_STAT_STATE_UPDATE_PERC_OF_MAP],
            (uint64_t)gf_s->stats[GF_STAT_FLOW_SETUP_CYCLES],
            gf_s->stats[GF_STAT_FLOW_SETUP_CYCLES_PER_UPCALL],
            gf_s->stats[GF_STAT_FLOW_SETUP_CYCLES_STD_DEV],
            gf_s->stats[GF_STAT_FLOW_SETUP_PERC_OF_UPCALL],
            (uint64_t)gf_s->stats[GF_STAT_STATE_BATCH_UPDATE_CYCLES],
            gf_s->stats[GF_STAT_STATE_BATCH_UPDATE_CYCLES_PER_BATCH],
            gf_s->stats[GF_STAT_STATE_BATCH_UPDATE_CYCLES_STD_DEV],
            gf_s->num_unique_mappings,
            gf_s->megaflow_rule_space);
    
    /* was the rule space hard to estimate? */
    if (gf_s->rule_space_hard_to_estimate) {
        ds_put_format(reply, " (hard to estimate)\n");
        gf_s->rule_space_hard_to_estimate = false;
    } else {
        ds_put_format(reply, "\n");
    }
    
    /* add Gigaflow cache stats to log output */
    ds_put_format(reply,
            "  - Cache occupancy:  %12"PRIu32" / %3"PRIu32" (%5.1f %% recyle rate)\n",
            gf_s->gigaflow_occupancy,
            gf_s->gigaflow_recycled,
            gf_s->gigaflow_recycle_rate);
    gf_perf_show_table_stats(reply, gf_s, gf_config);
}

void 
gf_perf_show_pmd_stats(struct ds *reply, 
                       struct gigaflow_perf_stats *gf_s,
                       struct gigaflow_config *gf_config)
{
    ds_put_format(reply,
                  "  gigaflow hits: %"PRIu64"\n"
                  "  gigaflow occupancy: %"PRIu32" / %"PRIu32" (%5.1f %% recyle rate)\n",
                  (uint64_t)gf_s->stats[GF_STAT_HIT_COUNT],
                  gf_s->gigaflow_occupancy,
                  gf_s->gigaflow_recycled,
                  gf_s->gigaflow_recycle_rate);
    /* add per-Gigaflow table stats to log output */
    gf_perf_show_table_stats(reply, gf_s, gf_config);
}

void
gf_perf_stats_clear(struct gigaflow_perf_stats *gf_s)
{
    // memset(gf_s->stats, 0, sizeof gf_s->stats);
    // memset(gf_s->last_samples, 0, sizeof gf_s->last_samples);
    // memset(gf_s->cache_occupancy, 0, sizeof gf_s->cache_occupancy);
    // memset(gf_s->cache_occupancy_per_priority_table, 
    //     0, sizeof gf_s->cache_occupancy_per_priority_table);
    // memset(gf_s->mask_occupancy, 0, sizeof gf_s->mask_occupancy);
    // memset(gf_s->cache_recycled, 0, sizeof gf_s->cache_recycled);
    // gf_s->gigaflow_occupancy = 0;
    // gf_s->gigaflow_recycled = 0;
    // gf_s->gigaflow_recycle_rate = 0;
    // gf_s->std_deviation_updates = 0;
    // hmap_destroy(&gf_s->masks_per_table);
    // hmap_init(&gf_s->masks_per_table);

    /* reset the rx/hit statistics only */
    gf_s->stats[GF_STAT_TOTAL_PACKETS] = 0; // reset total rx packets
    gf_s->stats[GF_STAT_TOTAL_BATCHES] = 0; // reset total rx batches
    gf_s->stats[GF_STAT_HIT_COUNT] = 0;     // reset Gigaflow hit count
    gf_s->stats[GF_STAT_HIT_RATE] = 0;      // reset Gigaflow hit rate
}
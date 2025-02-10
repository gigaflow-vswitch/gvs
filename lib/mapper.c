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
#include "mapper.h"

VLOG_DEFINE_THIS_MODULE(mapper);

void gigaflow_xlate_ctx_init(struct gigaflow_xlate_context* gf_xlate_ctx) {
    gf_xlate_ctx->xlate_error = XLATE_OK;
    // init tables looked up to zero
    gf_xlate_ctx->tables_looked_up_in_upcall = 0;
    gf_xlate_ctx->ptables_looked_up_in_upcall = 
        &gf_xlate_ctx->tables_looked_up_in_upcall;
    // init output port to 0
    gf_xlate_ctx->final_output_odp_port = 0;
    // initialize individual flows to zero
    for (int i=0; i<SLOW_PATH_TRAVERSAL_LIMIT; i++) {
        // individual_flows[i].nw_tos = 0;
        memset(&gf_xlate_ctx->individual_flows[i], 0, 
            sizeof gf_xlate_ctx->individual_flows[i]);
    }
    // initialize individual wildcards to zero
    for (int i=0; i<SLOW_PATH_TRAVERSAL_LIMIT; i++) {
        memset(&gf_xlate_ctx->individual_wcs[i], 0, 
            sizeof gf_xlate_ctx->individual_wcs[i]);
    }
    // initialize table traversal order to zero
    for (int k=0; k<SLOW_PATH_TRAVERSAL_LIMIT; k++) {
        gf_xlate_ctx->table_traversal_order[k] = 0;
    }
}

void 
gigaflow_mapping_ctx_init(struct gigaflow_mapping_context* gf_map_ctx,
                          struct mapper_state* state, 
                          struct gigaflow_config* gf_config)
{
    // current mapper state and Gigaflow tables to consider for mapping
    gf_map_ctx->gf_config = gf_config;
    const uint32_t gigaflow_tables_limit = gf_config->gigaflow_tables_limit;
    gf_map_ctx->state = state;
    gf_map_ctx->left_gf_table = 0;
    gf_map_ctx->right_gf_table = gigaflow_tables_limit - 1;
    // initialize context variables
    for (int k=0; k<gigaflow_tables_limit; k++) {
        // clear all accel tables' flows
        memset(&gf_map_ctx->accel_flows[k], 0, 
               sizeof gf_map_ctx->accel_flows[k]);
        // clear all accel tables' wildcards
        memset(&gf_map_ctx->accel_wcs[k], 0, 
               sizeof gf_map_ctx->accel_wcs[k]);
        // clear bit-wildcard
        gf_map_ctx->accel_bit_wc[k].bit_wildcard = EMPTY_WC;
        gf_map_ctx->accel_bit_wc[k].nw_src = 0;
        gf_map_ctx->accel_bit_wc[k].nw_dst = 0;
        // clear all accel tables' actions
        ofpbuf_use_stub(&gf_map_ctx->accel_odp_actions[k], 
                        gf_map_ctx->accel_odp_actions_stubs[k], 
                        sizeof gf_map_ctx->accel_odp_actions_stubs[k]);
        ofpbuf_clear(&gf_map_ctx->accel_odp_actions[k]);
        // init the mapping objects for debugging
        gf_table_mapping_init(&gf_map_ctx->debug_mappings[k], 0, 0, 0, 0);
    }
    // clear this traversal's mapping
    memset(gf_map_ctx->has_mapping, 0, sizeof gf_map_ctx->has_mapping);
    // clear this traversal mapping's priorities
    memset(gf_map_ctx->priorities, 0, sizeof gf_map_ctx->priorities);
    // initialize decoupler's variables
    memset(gf_map_ctx->decoupled_traversal_indices, 0, 
           sizeof gf_map_ctx->decoupled_traversal_indices);
    // initialize decoupling count
    gf_map_ctx->decoupling_cnt = 0;
    // initialize available tables
    memset(gf_map_ctx->available_tables, 0, 
        sizeof gf_map_ctx->available_tables);
    // initialize available tables count
    gf_map_ctx->available_cnt = 0;
    // initialize number of tables mapped to
    gf_map_ctx->mapped_cnt = 0;
    // current matching unique mapping written to Gigaflow
    gf_map_ctx->this_unique_mapping = NULL;
    gf_map_ctx->unique_mapping_hash = 0;
}

void 
gigaflow_mapping_ctx_assign_mapping(struct gigaflow_xlate_context *gf_xlate_ctx,
                                    struct gigaflow_mapping_context* gf_map_ctx,
                                    struct mapper_out *map_out)
{
    if (map_out == NULL) {
        // no tables were mapped
        gf_map_ctx->map_out = get_empty_mapper_out();
        return;
    }
    /* assign the mapping to mapping context */
    gf_map_ctx->map_out = map_out;
    const uint32_t gigaflow_tables_limit = 
        gf_map_ctx->gf_config->gigaflow_tables_limit;
    const bool debug_enabled = 
        gf_map_ctx->gf_config->gigaflow_debug_enabled;
    struct gf_table_mapping *gf_table_map;
    for (int k=0; k<gigaflow_tables_limit; k++) {
        /* check if this table has a mapping */
        gf_table_map = mapper_out_get_table_mapping(gf_map_ctx->map_out, k);
        if (!gf_table_map) {
            continue;
        }
        gf_map_ctx->has_mapping[k] = true;
        if (debug_enabled) {
            gf_map_ctx->debug_mappings[k] = *gf_table_map;
        }
        /* use number of composed tables as priority of this mapping */
        gf_map_ctx->priorities[k] = gf_table_map->tables;
        gf_map_ctx->mapped_cnt++;
    }
    if (gf_map_ctx->mapped_cnt == 0) {
        // no tables were mapped
        gf_map_ctx->map_out = get_empty_mapper_out();
        return;
    }
    if (debug_enabled) {
        struct ds map_out_str;
        ds_init(&map_out_str);
        mapper_out_to_string(gf_map_ctx->map_out, 
                            gf_xlate_ctx->table_traversal_order,
                            &map_out_str);
        char* map_out_str_cstr = ds_steal_cstr(&map_out_str);
        VLOG_INFO("mapping: %s", map_out_str_cstr);
        free(map_out_str_cstr);
    }
}

void 
gigaflow_mapping_ctx_uninit(struct gigaflow_mapping_context* gf_map_ctx) 
{
    const uint32_t gigaflow_tables_limit = 
        gf_map_ctx->gf_config->gigaflow_tables_limit;
    // clear the Gigaflow actions buffers
    for (int k=0; k<gigaflow_tables_limit; k++) {
        ofpbuf_uninit(&gf_map_ctx->accel_odp_actions[k]);
    }
    mapper_out_destroy(gf_map_ctx->map_out);
}

void 
gigaflow_xlate_ctx_insert_current_flow(struct gigaflow_xlate_context* gf_xlate_ctx, 
                                       struct flow this_flow)
{
    // copy the flow representation going into this table before the lookup
    uint32_t tables_looked_up = *(gf_xlate_ctx->ptables_looked_up_in_upcall);
    gf_xlate_ctx->individual_flows[tables_looked_up] = this_flow;
}

void 
gigaflow_xlate_ctx_insert_current_table_id(struct gigaflow_xlate_context* gf_xlate_ctx, 
                                           uint8_t this_table_id)
{
    // save table ID that's being looked up
    uint32_t tables_looked_up = *(gf_xlate_ctx->ptables_looked_up_in_upcall);
    gf_xlate_ctx->table_traversal_order[tables_looked_up] = this_table_id;
}

struct flow_wildcards* 
gigaflow_xlate_ctx_get_current_wildcard(struct gigaflow_xlate_context* gf_xlate_ctx)
{
    uint32_t tables_looked_up = *(gf_xlate_ctx->ptables_looked_up_in_upcall);
    return &gf_xlate_ctx->individual_wcs[tables_looked_up];
}

struct flow_wildcards* 
gigaflow_xlate_ctx_get_prev_wildcard(struct gigaflow_xlate_context* gf_xlate_ctx) 
{
    uint32_t tables_looked_up = *(gf_xlate_ctx->ptables_looked_up_in_upcall);
    return &gf_xlate_ctx->individual_wcs[tables_looked_up-1];
}

void 
gigaflow_xlate_ctx_update_megaflow_wildcard(struct gigaflow_xlate_context* gf_xlate_ctx, 
                                            struct flow_wildcards* megaflow_wildcards)
{
    // get 64-bit pointers to each of the wildcards
    const uint64_t *individual_wc_u64;
    uint32_t tables_looked_up = *(gf_xlate_ctx->ptables_looked_up_in_upcall);
    individual_wc_u64 = (const uint64_t *) 
        &gf_xlate_ctx->individual_wcs[tables_looked_up].masks;
    uint64_t *upcall_wc_u64 = (uint64_t *) &megaflow_wildcards->masks;
    // bitwise OR the wildcards to get Megaflow up-to-date
    for (size_t i = 0; i < FLOW_U64S; i++) {
        upcall_wc_u64[i] |= individual_wc_u64[i];
    }
}

void 
gigaflow_xlate_ctx_increment_lookedup_tables_cnt(struct gigaflow_xlate_context* gf_xlate_ctx)
{
    gf_xlate_ctx->tables_looked_up_in_upcall++;
}

uint32_t 
gigaflow_xlate_ctx_get_lookedup_tables_cnt(struct gigaflow_xlate_context* gf_xlate_ctx) 
{
    return gf_xlate_ctx->tables_looked_up_in_upcall;
}

void
gigaflow_xlate_ctx_set_output_odp_port(struct gigaflow_xlate_context* gf_xlate_ctx, 
                                       odp_port_t output_port)
{
    gf_xlate_ctx->final_output_odp_port = output_port;
}

void 
mapper_init(struct mapper_state* state)
{
    multigraph_init(&state->gf_lm, GIGAFLOW_TABLES_LIMIT);
    hmap_init(&state->unique_mappings);
}

void 
mapper_destroy(struct mapper_state* state)
{
    multigraph_destroy(&state->gf_lm);
    /* free each entry in unique mappings */
    struct unique_mapping_entry *unique_mapping;
    HMAP_FOR_EACH_POP (unique_mapping, node, &state->unique_mappings) {
        /* free the mapping contained in this unique mapping entry */
        mapper_out_destroy(unique_mapping->map_out);
        free(unique_mapping);
    }
    hmap_destroy(&state->unique_mappings);
}

void 
gigaflow_xlate_ctx_clean_flows(struct gigaflow_xlate_context* gf_xlate_ctx) 
{
    for (int x=0; x<gf_xlate_ctx->tables_looked_up_in_upcall; x++) {
        gf_xlate_ctx->individual_flows[x].nw_tos = 0;
        /* cleanup the wildcard fields that are not important 
           for Gigaflow */
        gf_xlate_ctx->individual_wcs[x].masks.recirc_id = 0;
        gf_xlate_ctx->individual_wcs[x].masks.packet_type = 0;   
        gf_xlate_ctx->individual_wcs[x].masks.nw_frag = 0;
    }
}

void 
gigaflow_mapping_ctx_clean_flows(struct gigaflow_mapping_context *gf_map_ctx) 
{
    const uint32_t gigaflow_tables_limit = 
        gf_map_ctx->gf_config->gigaflow_tables_limit;
    for (int y=0; y<gigaflow_tables_limit; y++) {
        gf_map_ctx->accel_flows[y].nw_tos = 0;
    }
}

uint16_t 
flow_wildcard_to_bits(struct flow_wildcards *this_wc)
{
    uint16_t bit_wildcard = EMPTY_WC;
    /* Ethernet masks have to be handled like so */
    int eth_src_wildcard = this_wc->masks.dl_src.be16[0] 
        + this_wc->masks.dl_src.be16[1] + this_wc->masks.dl_src.be16[2];
    int eth_dst_wildcard = this_wc->masks.dl_dst.be16[0] 
        + this_wc->masks.dl_dst.be16[1] + this_wc->masks.dl_dst.be16[2];
    /* whichever field is unwildcarded, bit-bang it into bit_wildcard */
    bit_wildcard |= (eth_src_wildcard > 0)        ? DL_SRC   : 0;
    bit_wildcard |= (eth_dst_wildcard > 0)        ? DL_DST   : 0;
    bit_wildcard |= (this_wc->masks.nw_src > 0)   ? NW_SRC   : 0;
    bit_wildcard |= (this_wc->masks.nw_dst > 0)   ? NW_DST   : 0;
    bit_wildcard |= (this_wc->masks.nw_proto > 0) ? NW_PROTO : 0;
    bit_wildcard |= (this_wc->masks.tp_src > 0)   ? TP_SRC   : 0;
    bit_wildcard |= (this_wc->masks.tp_dst > 0)   ? TP_DST   : 0;
    return bit_wildcard;
}

void 
bit_wildcards_format(struct ds *s, uint16_t bit_wildcards)
{
    ds_put_cstr(s, "[");
    if (bit_wildcards & DL_SRC) {
        ds_put_cstr(s, "DL_SRC,");
    }
    if (bit_wildcards & DL_DST) {
        ds_put_cstr(s, "DL_DST,");
    }
    if (bit_wildcards & NW_SRC) {
        ds_put_cstr(s, "NW_SRC,");
    }
    if (bit_wildcards & NW_DST) {
        ds_put_cstr(s, "NW_DST,");
    }
    if (bit_wildcards & NW_PROTO) {
        ds_put_cstr(s, "NW_PROTO,");
    }
    if (bit_wildcards & TP_SRC) {
        ds_put_cstr(s, "TP_SRC,");
    }
    if (bit_wildcards & TP_DST) {
        ds_put_cstr(s, "TP_DST,");
    }
    ds_put_cstr(s, "]");
}

void 
generate_bit_wildcards(struct gigaflow_xlate_context *gf_xlate_ctx,
                       struct gigaflow_mapping_context *gf_map_ctx)
{
    for (int i=0; i<gf_xlate_ctx->tables_looked_up_in_upcall; i++) {
        gf_map_ctx->bit_wildcards[i] = 
            flow_wildcard_to_bits(&gf_xlate_ctx->individual_wcs[i]);
    }
}

/* this function will iteratively take the individual wildcards from 
   gf->xlate_ctx->individual_wildcards, convert to bits representation,
   maintain this bits representation for ongoing wildcard composition;
   anytime the next table has a disjoint wildcard compared to currently
   maintain wildcard, we say a new decoupling point has been identified,
   assign current wildcard the new wildcard, and store its index in 
   decoupled_traversal_indices */
void 
opt_pass_decouple_traversal(struct gigaflow_xlate_context *gf_xlate_ctx,
                            struct gigaflow_mapping_context *gf_map_ctx)
{
    uint16_t next_wc_bits = EMPTY_WC;
    uint16_t curr_wc_bits = flow_wildcard_to_bits(&gf_xlate_ctx->individual_wcs[0]);
    gf_map_ctx->bit_wildcards[0] = curr_wc_bits;
    gf_map_ctx->decoupled_traversal_indices[gf_map_ctx->decoupling_cnt++] = 0;
    for (int i=1; i<gf_xlate_ctx->tables_looked_up_in_upcall; i++) {
        next_wc_bits = flow_wildcard_to_bits(&gf_xlate_ctx->individual_wcs[i]);
        gf_map_ctx->bit_wildcards[i] = next_wc_bits;
        /* if either of current or next are empty wildcards OR 
           if not, have common bits, subsume into current and move on */
        if ((curr_wc_bits == EMPTY_WC) || (next_wc_bits == EMPTY_WC)
            || (curr_wc_bits & next_wc_bits)) {
            curr_wc_bits |= next_wc_bits;
        } else {
            /* found decoupling; update current and move on */
            gf_map_ctx->decoupled_traversal_indices[gf_map_ctx->decoupling_cnt++] = i;
            curr_wc_bits = next_wc_bits;
        }
    }
    /* insert output table ID into last decoupled index */
    gf_map_ctx->decoupled_traversal_indices[gf_map_ctx->decoupling_cnt] = 
        gf_xlate_ctx->table_traversal_order[gf_xlate_ctx->tables_looked_up_in_upcall];
}

void 
find_available_tables_for_mapping(struct gigaflow_mapping_context *gf_map_ctx,
                                  struct gigaflow_perf_stats *gf_stats)
{
    const uint32_t gigaflow_tables_limit = 
        gf_map_ctx->gf_config->gigaflow_tables_limit;
    const uint32_t gigaflow_max_entries = 
        gf_map_ctx->gf_config->gigaflow_max_entries;
    /* find available tables for mapping (without mask limit check) */
    for (int k=0; k<gigaflow_tables_limit; k++) {
        if (gf_stats->cache_occupancy[k] < gigaflow_max_entries) {
            gf_map_ctx->available_tables[gf_map_ctx->available_cnt++] = k;
        }
    }
}

void
accept_mapping_if_masks_within_limits(struct gigaflow_mapping_context *gf_map_ctx,
                                      struct gigaflow_perf_stats *gf_stats)
{
    const uint32_t gigaflow_tables_limit = 
        gf_map_ctx->gf_config->gigaflow_tables_limit;
    const uint32_t gigaflow_max_masks = 
        gf_map_ctx->gf_config->gigaflow_max_masks;
    for (int k=0; k<gigaflow_tables_limit; k++) {
        /* only look at the tables that were mapped to */
        if (!gf_map_ctx->has_mapping[k]) {
            continue;
        }
        /* check if the mask is new to this table */
        if (!gf_perf_is_mask_in_table(gf_stats, &gf_map_ctx->accel_bit_wc[k], 
                                      &gf_map_ctx->accel_wcs[k], k)) {
            /* if not, and we exceed the mask limit in any Gigaflow table, 
               reject the whole mapping */
            if ((gf_stats->mask_occupancy[k] + 1) >= gigaflow_max_masks) {
                gf_map_ctx->mapped_cnt = 0;
                return;
            }
        }
    }
}

void 
compose_wildcards(struct flow_wildcards *wc1, struct flow_wildcards *wc2) 
{
    // composes wc2 into wc1
    uint64_t *wc1_u64 = (uint64_t *) &wc1->masks;
    const uint64_t *wc2_u64 = (const uint64_t *) &wc2->masks;
    for (size_t i = 0; i < FLOW_U64S; i++) {
        wc1_u64[i] |= wc2_u64[i];
    }
}

void 
compose_one_gigaflow(struct gigaflow_xlate_context *gf_xlate_ctx,
                     struct gigaflow_mapping_context *gf_map_ctx,
                     int first_wildcard_i, int curr_wildcard_i,
                     int starting_table_id, int next_table_id,
                     int curr_gigaflow_table)
{
    // compose out into curr_gigaflow table
    for (int j=first_wildcard_i; j<curr_wildcard_i; j++) {
        compose_wildcards(&gf_map_ctx->accel_wcs[curr_gigaflow_table], 
                          &gf_xlate_ctx->individual_wcs[j]);
        /* compose bit wildcard representation for Gigaflow */
        gf_map_ctx->accel_bit_wc[curr_gigaflow_table].bit_wildcard |= 
            gf_map_ctx->bit_wildcards[j];
    }
    /* bit wildcard on nw_src and nw_dst*/
    gf_map_ctx->accel_bit_wc[curr_gigaflow_table].nw_src =
        gf_map_ctx->accel_wcs[curr_gigaflow_table].masks.nw_src;
    gf_map_ctx->accel_bit_wc[curr_gigaflow_table].nw_dst = 
        gf_map_ctx->accel_wcs[curr_gigaflow_table].masks.nw_dst;
    /* unwildcard the slow path wildcard [N] to exact match on 
       slow path table ID our chosen field is nw_tos in IPv4 */
    gf_map_ctx->accel_wcs[curr_gigaflow_table].masks.nw_tos = 0xFF;
    /* our flow is the first input flow */
    gf_map_ctx->accel_flows[curr_gigaflow_table] = 
        gf_xlate_ctx->individual_flows[first_wildcard_i];
    /* create a dummy flow with next-table id */
    struct flow flow_with_next_table_id = 
        gf_xlate_ctx->individual_flows[curr_wildcard_i];
    flow_with_next_table_id.nw_tos = next_table_id;
    /* append to the table's action to write next-table ID */
    commit_odp_actions(&flow_with_next_table_id, 
                       &gf_map_ctx->accel_flows[curr_gigaflow_table], 
                       &gf_map_ctx->accel_odp_actions[curr_gigaflow_table], 
                       &gf_map_ctx->accel_wcs[curr_gigaflow_table], 
                       true, false, false, NULL);
    /* now insert the starting table of this Megaflow
       THIS SHOULD BE THE LAST STEP!! */
    gf_map_ctx->accel_flows[curr_gigaflow_table].nw_tos = starting_table_id;
}

void 
compose_gigaflows(struct gigaflow_xlate_context *gf_xlate_ctx,
                  struct gigaflow_mapping_context *gf_map_ctx)
{
    if (gf_map_ctx->mapped_cnt == 0) {
        // no tables were mapped
        return;
    }
    const uint32_t gigaflow_tables_limit = 
        gf_map_ctx->gf_config->gigaflow_tables_limit;
    int first_wildcard_i, curr_wildcard_i;
    int curr_gigaflow_table = -1; // , prev_gigaflow_table;
    int starting_table_id, next_table_id;
    struct gf_table_mapping *gf_table_map;
    for (int k=0; k<gigaflow_tables_limit; k++) {
        // check if this table has a mapping
        gf_table_map = mapper_out_get_table_mapping(gf_map_ctx->map_out, k);
        if (!gf_table_map) {
            continue;
        }
        first_wildcard_i = gf_table_map->t_start;
        curr_wildcard_i = gf_table_map->t_next;
        // prev_gigaflow_table = curr_gigaflow_table; // store previous table
        curr_gigaflow_table = gf_table_map->g_table;
        starting_table_id = gf_xlate_ctx->table_traversal_order[first_wildcard_i];
        next_table_id = gf_xlate_ctx->table_traversal_order[curr_wildcard_i];
        compose_one_gigaflow(gf_xlate_ctx, gf_map_ctx, 
                             first_wildcard_i, curr_wildcard_i, 
                             starting_table_id, next_table_id,
                             curr_gigaflow_table);
        // this check is hurting upcall reduction
        // /* check overlap with previous wildcard; if overlap exists, 
        //    just compose a Megaflow */
        // if (prev_gigaflow_table != -1) {
        //     continue;
        // }
        // if (flow_wildcards_overlap(&gf_map_ctx->accel_wcs[curr_gigaflow_table], 
        //                             &gf_map_ctx->accel_wcs[prev_gigaflow_table])) {
        //     /* clear previous mapping metadata */
        //     for (int i=0; i<gigaflow_tables_limit; i++) {
        //         gf_map_ctx->has_mapping[i] = false;
        //         gf_map_ctx->priorities[i] = 0;
        //     }
        //     /* free the previous mapping and compose a new one 
        //         with all slow path tables mapped to one Gigaflow table */
        //     mapper_out_destroy(gf_map_ctx->map_out);
        //     gf_map_ctx->map_out = get_empty_mapper_out();
        //     curr_gigaflow_table = gf_map_ctx->available_tables[0];
        //     uint32_t tables_looked_up = gf_xlate_ctx->tables_looked_up_in_upcall;
        //     mapper_out_update_mapping(gf_map_ctx->map_out, 0, tables_looked_up-1, 
        //                               tables_looked_up, curr_gigaflow_table);
        //     gf_map_ctx->mapped_cnt = 1;
        //     gf_map_ctx->has_mapping[curr_gigaflow_table] = true;
        //     gf_map_ctx->priorities[curr_gigaflow_table] = tables_looked_up;
        //     /* finally, compose the Gigaflow entry itself */
        //     compose_one_gigaflow(gf_xlate_ctx, gf_map_ctx, 
        //                          0, tables_looked_up, 
        //                          gf_xlate_ctx->table_traversal_order[0], 
        //                          gf_xlate_ctx->table_traversal_order[tables_looked_up],
        //                          curr_gigaflow_table);
        //     /* break out of this loop and add the output action */
        //     break;
        // }
    }
    // insert the output action with output odp_port_t returned from slow path lookup
    nl_msg_put_odp_port(&gf_map_ctx->accel_odp_actions[curr_gigaflow_table], 
                        OVS_ACTION_ATTR_OUTPUT, 
                        gf_xlate_ctx->final_output_odp_port);
}

void 
compose_gigaflows_with_state_update(struct gigaflow_xlate_context *gf_xlate_ctx,
                                    struct gigaflow_mapping_context *gf_map_ctx)
{
    if (gf_map_ctx->mapped_cnt == 0) {
        // no tables were mapped
        return;
    }
    const uint32_t gigaflow_tables_limit = 
        gf_map_ctx->gf_config->gigaflow_tables_limit;
    // source and termination node for gigaflow state multigraph..
    int prev_layer = SOURCE_LAYER, prev_value = SOURCE_VALUE;
    int first_wildcard_i, curr_wildcard_i, curr_gigaflow_table;
    int starting_table_id, next_table_id;
    struct gf_table_mapping *gf_table_map;
    for (int k=0; k<gigaflow_tables_limit; k++) {
        // check if this table has a mapping
        gf_table_map = mapper_out_get_table_mapping(gf_map_ctx->map_out, k);
        if (!gf_table_map) {
            continue;
        }
        first_wildcard_i = gf_table_map->t_start;
        curr_wildcard_i = gf_table_map->t_next;
        curr_gigaflow_table = gf_table_map->g_table;
        starting_table_id = 
            gf_xlate_ctx->table_traversal_order[first_wildcard_i];
        next_table_id = gf_xlate_ctx->table_traversal_order[curr_wildcard_i];
        compose_one_gigaflow(gf_xlate_ctx, gf_map_ctx, 
                             first_wildcard_i, curr_wildcard_i, 
                             starting_table_id, next_table_id,
                             curr_gigaflow_table);
        // update the Gigaflow multigraph state with this new node
        multigraph_add_node(&gf_map_ctx->state->gf_lm, 
                            curr_gigaflow_table, starting_table_id); 
        // add parent edge for this new node
        multigraph_add_edge(&gf_map_ctx->state->gf_lm, 
                            prev_layer, prev_value, 
                            curr_gigaflow_table, starting_table_id);
        // update prev..
        prev_layer = curr_gigaflow_table;
        prev_value = starting_table_id;
    }
    /* insert the output action with output odp_port_t returned 
       from slow path lookup */
    nl_msg_put_odp_port(&gf_map_ctx->accel_odp_actions[curr_gigaflow_table], 
                        OVS_ACTION_ATTR_OUTPUT, 
                        gf_xlate_ctx->final_output_odp_port);
    // add termination edge for this last node
    multigraph_add_edge(&gf_map_ctx->state->gf_lm, prev_layer, prev_value,
                        SINK_LAYER, SINK_VALUE);
}

int 
search_paths_memo(struct mapper_memo *memo, 
                  int terminal, int table_id, int g_id)
{
    struct paths_memo_entry *paths_entry;
    uint32_t paths_hash = hash_3words_plus1(terminal, table_id, g_id);
    HMAP_FOR_EACH_WITH_HASH (paths_entry, node, paths_hash, &memo->paths) {
        if (paths_entry->terminal == terminal 
            && paths_entry->table_id == table_id 
            && paths_entry->g_id == g_id) {
            return paths_entry->paths;
        }
    }
    return -1;
}

void
insert_into_paths_memo(struct mapper_memo *memo,
                       int terminal, int table_id, int g_id, int paths)
{
    // insert new entry into dp memo
    struct paths_memo_entry *paths_entry;
    paths_entry = xzalloc(sizeof *paths_entry);
    paths_entry->terminal = terminal;
    paths_entry->table_id = table_id;
    paths_entry->g_id = g_id;
    paths_entry->paths = paths;
    uint32_t paths_hash = hash_3words_plus1(terminal, table_id, g_id);
    hmap_insert(&memo->paths, &paths_entry->node, paths_hash);
}

int 
paths_to_sink(struct mapper_memo *memo, 
              struct gigaflow_mapping_context *gf_map_ctx, 
              int next_table, int g_id)
{
    // check memo before making any calls
    int cached_paths = search_paths_memo(memo, SINK_VALUE, next_table, g_id);
    if (cached_paths != -1) {
        return cached_paths;
    }
    int sink_paths = multigraph_paths_to_sink(&gf_map_ctx->state->gf_lm, 
                                              next_table, g_id);
    // insert paths result into memo before returning
    insert_into_paths_memo(memo, SINK_VALUE, next_table, g_id, sink_paths);
    return sink_paths;
}

int 
paths_to_source(struct mapper_memo *memo, 
                struct gigaflow_mapping_context *gf_map_ctx, 
                int start_table, int g_id)
{
    // check memo before making any calls
    int cached_paths = search_paths_memo(memo, SOURCE_VALUE, start_table, g_id);
    if (cached_paths != -1) {
        return cached_paths;
    }
    int source_paths = multigraph_paths_to_source(&gf_map_ctx->state->gf_lm, 
                                                  start_table, g_id);
    // insert paths result into memo before returning
    insert_into_paths_memo(memo, SOURCE_VALUE, start_table, g_id, source_paths);
    return source_paths;
}

int 
paths(struct mapper_memo *memo, 
      struct gigaflow_xlate_context *gf_xlate_ctx,
      struct gigaflow_mapping_context *gf_map_ctx, 
      int t_start, int t_end, int t_next, int g_id)
{
    // base case
    if (t_start > t_end) {
        return 0;
    }
    /* recursive calls: get added paths count from a given mapping
       return new paths with traversal[t_start:t_end]->t_next 
       mapped to Gigaflow[g_id] */
    int start_table = gf_xlate_ctx->table_traversal_order[t_start];
    int next_table = gf_xlate_ctx->table_traversal_order[t_next];
    /* if next_table is OUTPUT_TABLE_ID or DROP_TABLE_ID, that should 
       be replaced with SINK_VALUE before quering the paths */
    if (next_table == OUTPUT_TABLE_ID || next_table == DROP_TABLE_ID) {
        next_table = SINK_VALUE;
    }
    /* calculate paths of next to SINK, then propagate back to SOURCE */
    int sink_paths = paths_to_sink(memo, gf_map_ctx, next_table, g_id);
    int source_paths = paths_to_source(memo, gf_map_ctx, start_table, g_id);
    return source_paths ? source_paths*sink_paths : sink_paths;
    /* this one below is non-cached version */
    // return multigraph_paths_query(&gf_map_ctx->state->gf_lm, 
    //                               start_table, next_table, g_id);
}

int 
get_coupling(struct gigaflow_mapping_context *gf_map_ctx,
             int t_start, int t_end)
{
    const uint32_t coupling_base_score = 
        gf_map_ctx->gf_config->coupling_base_score;
    int coupling = 0; // just one wildcard is not a coupling
    uint16_t next_wc_bits = EMPTY_WC;
    uint16_t curr_wc_bits = gf_map_ctx->bit_wildcards[t_start];
    for (int i=t_start+1; i<=t_end; i++) {
        next_wc_bits = gf_map_ctx->bit_wildcards[i];
        /* if either of current or next are empty wildcards OR 
           if not, have common bits, subsume into current and move on */
        if ((curr_wc_bits == EMPTY_WC) || (next_wc_bits == EMPTY_WC)
            || (curr_wc_bits & next_wc_bits)) {
            curr_wc_bits |= next_wc_bits;
            coupling += coupling_base_score; // better coupling
        } else {
            /* found decoupling within this range; bad combination */
            return 0;
        }
    }
    return coupling;
}

bool
table_will_overflow(struct gigaflow_mapping_context *gf_map_ctx, 
                    struct gigaflow_perf_stats *gf_stats, int g_id)
{
    const uint32_t gigaflow_max_entries = 
        gf_map_ctx->gf_config->gigaflow_max_entries;
    const uint32_t gigaflow_max_masks = 
        gf_map_ctx->gf_config->gigaflow_max_masks;
    if (((gf_stats->cache_occupancy[g_id] + 1) > gigaflow_max_entries)
        || (gf_stats->mask_occupancy[g_id] > gigaflow_max_masks)) {
        return true;
    }
    return false;
}

int 
get_mapping_optimality(struct mapper_memo *memo OVS_UNUSED, 
                       struct gigaflow_xlate_context *gf_xlate_ctx OVS_UNUSED,
                       struct gigaflow_mapping_context *gf_map_ctx, 
                       int t_start, int t_end, int t_next OVS_UNUSED, int g_id OVS_UNUSED)
{
    bool optimize_coupling = gf_map_ctx->gf_config->optimize_coupling;
    // bool optimize_paths = gf_map_ctx->gf_config->optimize_paths;
    // uint32_t coupling_scalar = gf_map_ctx->gf_config->coupling_scalar;
    // uint32_t paths_scalar = gf_map_ctx->gf_config->paths_scalar;
    int coupling = 0;
    if (optimize_coupling) {
        coupling = get_coupling(gf_map_ctx, t_start, t_end);
    }
    // int new_paths = 1;
    // if (optimize_paths) {
    //     new_paths = paths(memo, gf_xlate_ctx, gf_map_ctx, 
    //                       t_start, t_end, t_next, g_id);
    // }
    // this score is always greater than 0
    // return (paths_scalar * new_paths) + (coupling_scalar * coupling);
    // return (coupling * new_paths) + 1;
    return coupling;
}

struct dp_memo_entry* 
clone_dp_memo_entry(struct dp_memo_entry* dp_memo_ent)
{
    struct dp_memo_entry *clone_dp_entry;
    clone_dp_entry = xzalloc(sizeof *clone_dp_entry);
    clone_dp_entry->map_out = clone_mapper_out(dp_memo_ent->map_out);
    clone_dp_entry->t_start = dp_memo_ent->t_start;
    clone_dp_entry->t_end = dp_memo_ent->t_end;
    clone_dp_entry->g_start = dp_memo_ent->g_start;
    clone_dp_entry->g_end = dp_memo_ent->g_end;
    clone_dp_entry->opt = dp_memo_ent->opt;
    return clone_dp_entry;
}

struct dp_memo_entry * 
search_dp_memo(struct mapper_memo *memo,
               int t_start, int t_end, 
               int g_start, int g_end)
{
    // check memo before making any calls
    struct dp_memo_entry *dp_entry;
    uint32_t dp_hash = hash_4words_plus1(t_start, t_end, g_start, g_end);
    HMAP_FOR_EACH_WITH_HASH (dp_entry, node, dp_hash, &memo->dp) {
        if (dp_entry->t_start == t_start && dp_entry->t_end == t_end
            && dp_entry->g_start == g_start && dp_entry->g_end == g_end) {
            // return a deep copy of this entry; not the actual node
            return clone_dp_memo_entry(dp_entry);
        }
    }
    return NULL;
}

void
insert_into_dp_memo(struct mapper_memo *memo,
                    struct mapper_out *map_out,
                    int t_start, int t_end, 
                    int g_start, int g_end, int opt)
{
    // insert new entry into dp memo
    struct dp_memo_entry *dp_entry;
    dp_entry = xzalloc(sizeof *dp_entry);
    // cache optimal mapping for this config
    dp_entry->map_out = clone_mapper_out(map_out);
    dp_entry->t_start = t_start;
    dp_entry->t_end = t_end;
    dp_entry->g_start = g_start;
    dp_entry->g_end = g_end;
    dp_entry->opt = opt;
    uint32_t dp_hash = hash_4words_plus1(t_start, t_end, g_start, g_end);
    hmap_insert(&memo->dp, &dp_entry->node, dp_hash);
}

struct mapper_out* 
maximize_optimality_dp(struct mapper_memo *memo,
                       struct gigaflow_xlate_context *gf_xlate_ctx,
                       struct gigaflow_mapping_context *gf_map_ctx,
                       int t_start, int t_end, int g_start, int g_end)
{
    /* check memo before making any calls */
    struct dp_memo_entry *dp_cache;
    dp_cache = search_dp_memo(memo, t_start, t_end, g_start, g_end);
    if (dp_cache) {
        return dp_cache->map_out;
    }
    struct mapper_out *map_out = get_empty_mapper_out();
    /* base case - 1: 
       no more traversal tables to map? */
    if (t_end == -1) {
        map_out->score = 0;
        // nothing to map..
        return map_out;
    }
    /* base case - 2:
       only one Gigaflow table left?
       map remaining traversal tables to this Gigaflow table */
    if (g_end == 0) {
        /* get actual Gigaflow table ID before measuring optimality
           or updating a mapping on that table */
        int gf_table_id = gf_map_ctx->available_tables[g_end];
        int score = get_mapping_optimality(memo, gf_xlate_ctx, gf_map_ctx, 
                                           t_start+1, t_end, t_end+1, 
                                           gf_table_id);
        map_out->score = score;
        // update this path mapping in mapper_out
        mapper_out_update_mapping(map_out, t_start+1, t_end, t_end+1, 
                                  gf_table_id);
        // insert dp result into memo before returning
        insert_into_dp_memo(memo, map_out, t_start, t_end, 
                            g_start, g_end, score);
        return map_out;
    }
    // recursive calls
    struct mapper_out *dp_map_out;
    int mapped_score = 0, new_score = 0, max_score = 0;
    for (int t_i=t_start; t_i<=t_end; t_i++) {
        dp_map_out = maximize_optimality_dp(memo, gf_xlate_ctx, gf_map_ctx, 
                                            t_start, t_i, g_start, 
                                            g_end-1);
        /* get actual Gigaflow table ID before measuring optimality
           or updating a mapping on that table */
        int gf_table_id = gf_map_ctx->available_tables[g_end];
        mapped_score = get_mapping_optimality(memo, gf_xlate_ctx, gf_map_ctx, 
                                              t_i+1, t_end, t_end+1, 
                                              gf_table_id);
        new_score = dp_map_out->score + mapped_score;
        /* found a better or equally optimal solution? 
           replace the prior mapping for this table */
        if (new_score > max_score) {
            // free previous map_out and update to new best
            mapper_out_destroy(map_out);
            // update this path mapping in mapper_out iff t_i+1 <= t_end
            if (t_i + 1 <= t_end) {
                mapper_out_update_mapping(dp_map_out, t_i+1, t_end, 
                                          t_end+1, gf_table_id);
            }
            dp_map_out->score = new_score;
            map_out = dp_map_out;
            max_score = new_score;
        } else {
            // this solution we tried is not any better
            mapper_out_destroy(dp_map_out);
        }
    }
    // insert dp result into memo before returning
    insert_into_dp_memo(memo, map_out, t_start, t_end, 
                        g_start, g_end, max_score);
    return map_out;
}

struct mapper_out* 
maximize_optimality_dp_iterative(struct mapper_memo *memo,
                                 struct gigaflow_xlate_context *gf_xlate_ctx,
                                 struct gigaflow_mapping_context *gf_map_ctx,
                                 int t_start, int t_end, 
                                 int g_start, int g_end)
{
    /* Step 1: Initialize DP table
       Assuming struct mapper_out has an integer field 'score'
       and the max number of traversal and Gigaflow tables 
       are predefined as SLOW_PATH_TRAVERSAL_LIMIT 
       and GIGAFLOW_TABLES_LIMIT */
    const uint32_t slow_path_tables_looked_up = 
        gf_xlate_ctx->tables_looked_up_in_upcall;
    const uint32_t gigaflow_tables_limit = 
        gf_map_ctx->gf_config->gigaflow_tables_limit;
    struct mapper_out dp[slow_path_tables_looked_up][gigaflow_tables_limit];
    // memset(dp, 0, sizeof(dp));
    for (int t=0; t<slow_path_tables_looked_up; t++) {
        for (int g=0; g<gigaflow_tables_limit; g++) {
            mapper_out_init(&dp[t][g]);
        }
    }

    // Step 2: Fill the table iteratively
    for (int g=g_start; g<=g_end; g++) {
        for (int t=t_start; t<=t_end; t++) {
            if (t == -1) {
                dp[t][g].score = 0;
            } else if (g == 0) {
                int gf_table_id = gf_map_ctx->available_tables[g];
                int score = get_mapping_optimality(memo, 
                                                   gf_xlate_ctx, 
                                                   gf_map_ctx, 
                                                   t+1, t_end, 
                                                   t_end+1, 
                                                   gf_table_id);
                mapper_out_update_mapping(&dp[t][g], t+1, t_end, 
                                          t_end+1, gf_table_id);
                dp[t][g].score = score;
            } else {
                for (int t_i=t_start; t_i<=t_end; t_i++) {
                    int gf_table_id = gf_map_ctx->available_tables[g];
                    int mapped_score = get_mapping_optimality(memo, 
                                                              gf_xlate_ctx, 
                                                              gf_map_ctx, 
                                                              t_i+1, t_end, 
                                                              t_end+1, 
                                                              gf_table_id);
                    int new_score = dp[t_start][t_i].score + mapped_score;
                    if (new_score > dp[t][g].score) {
                        dp[t][g].score = new_score;
                        mapper_out_update_mapping(&dp[t][g], t_i+1, t_end, 
                                                  t_end+1, gf_table_id);
                    }
                }
            }
        }
    }

    // Step 3: Retrieve the final result
    struct mapper_out *result = get_empty_mapper_out();
    result = clone_mapper_out(&dp[t_start][t_end]);
    return result;
}

void mapper_memo_init(struct mapper_memo* memo)
{
    hmap_init(&memo->dp);
    hmap_init(&memo->paths);
}

void mapper_memo_destroy(struct mapper_memo* memo)
{
    struct dp_memo_entry *dp_entry;
    struct paths_memo_entry *paths_entry;
    HMAP_FOR_EACH_POP (dp_entry, node, &memo->dp) {
        // destroy the mapping cached in this entry
        mapper_out_destroy(dp_entry->map_out);
        free(dp_entry);
    }
    HMAP_FOR_EACH_POP (paths_entry, node, &memo->paths) {
        free(paths_entry);
    }
    hmap_destroy(&memo->dp);
    hmap_destroy(&memo->paths);
}

struct gf_table_mapping*
create_new_gf_table_mapping(int t_start, int t_end, int t_next, 
                            int gf_table_id)
{
    struct gf_table_mapping *new_mapping;
    new_mapping = xzalloc(sizeof *new_mapping);
    new_mapping->t_start = t_start;
    new_mapping->t_end = t_end;
    new_mapping->t_next = t_next;
    new_mapping->g_table = gf_table_id;
    new_mapping->tables = t_end - t_start + 1;
    return new_mapping;
}

void
gf_table_mapping_init(struct gf_table_mapping* gf_table_map, 
                      int t_start, int t_end, int t_next, 
                      int gf_table_id)
{
    gf_table_map->t_start = t_start;
    gf_table_map->t_end = t_end;
    gf_table_map->t_next = t_next;
    gf_table_map->g_table = gf_table_id;
}

void 
write_gf_table_mapping(struct gf_table_mapping* gf_table_map,
                       int t_start, int t_end, int t_next, 
                       int gf_table_id)
{
    gf_table_map->t_start = t_start;
    gf_table_map->t_end = t_end;
    gf_table_map->t_next = t_next;
    gf_table_map->g_table = gf_table_id;
}

struct gf_table_mapping*
clone_gf_table_mapping(struct gf_table_mapping* gf_table_map)
{
    struct gf_table_mapping *clone_mapping;
    clone_mapping = create_new_gf_table_mapping(gf_table_map->t_start, 
                                                gf_table_map->t_end, 
                                                gf_table_map->t_next, 
                                                gf_table_map->g_table);
    return clone_mapping;
}

void 
gf_table_mapping_destroy(struct gf_table_mapping* gft_mapping)
{
    free(gft_mapping);
}

struct mapper_out* 
get_empty_mapper_out(void)
{
    struct mapper_out *map_out;
    map_out = xzalloc(sizeof *map_out);
    hmap_init(&map_out->mapping);
    map_out->score = 0;
    return map_out;
}

void 
mapper_out_init(struct mapper_out* map_out)
{
    hmap_init(&map_out->mapping);
    map_out->score = 0;
}

struct mapper_out* 
clone_mapper_out(struct mapper_out* map_out)
{
    struct mapper_out *clone_map_out;
    struct gf_table_mapping *clone_gf_table_map;
    clone_map_out = get_empty_mapper_out();
    clone_map_out->score = map_out->score;
    if (!hmap_is_empty(&map_out->mapping)) {
        struct gf_table_mapping *gf_table_map_iter;
        HMAP_FOR_EACH (gf_table_map_iter, node, &map_out->mapping) {
            clone_gf_table_map = clone_gf_table_mapping(gf_table_map_iter);
            hmap_insert(&clone_map_out->mapping, &clone_gf_table_map->node, 
                        gf_table_map_iter->node.hash);
        }
    }
    return clone_map_out;
}

void
mapper_out_update_mapping(struct mapper_out* map_out,
                          int t_start, int t_end, int t_next,
                          int gf_table_id)
{
    struct gf_table_mapping* gft_mapping;
    gft_mapping = create_new_gf_table_mapping(t_start, t_end, t_next, gf_table_id);
    uint32_t gftm_hash = hash_uint64_plus1(gf_table_id);
    hmap_insert(&map_out->mapping, &gft_mapping->node, gftm_hash);
}

struct gf_table_mapping* 
mapper_out_get_table_mapping(struct mapper_out* map_out, int gf_table_id)
{
    struct gf_table_mapping *gft_mapping;
    uint32_t gft_hash = hash_uint64_plus1(gf_table_id);
    HMAP_FOR_EACH_WITH_HASH (gft_mapping, node, gft_hash, &map_out->mapping) {
        if (gft_mapping->g_table == gf_table_id) {
            return gft_mapping;
        }
    }
    return NULL;
}

void
mapper_out_destroy(struct mapper_out* map_out)
{
    struct gf_table_mapping *gft_mapping;
    if (!hmap_is_empty(&map_out->mapping)) {
        HMAP_FOR_EACH_POP (gft_mapping, node, &map_out->mapping) {
            free(gft_mapping);
        }
        hmap_destroy(&map_out->mapping);
    }
    free(map_out);
}

void
mapper_out_destroy_with_keep(struct mapper_out* map_out, 
                             struct mapper_out *keep)
{
    struct gf_table_mapping *gft_mapping;
    HMAP_FOR_EACH_POP (gft_mapping, node, &map_out->mapping) {
        if (hmap_contains(&keep->mapping, &gft_mapping->node)) {
            continue;
        }
        free(gft_mapping);
    }
    hmap_destroy(&map_out->mapping);
    free(map_out);
}

void 
mapper_out_map_to_traversal(struct mapper_out *map_out, 
                            uint32_t *table_traversal_order)
{
    /* replace the table indices of traversal with actual table IDs */
    struct gf_table_mapping *gf_table_map_iter;
    HMAP_FOR_EACH (gf_table_map_iter, node, &map_out->mapping) {
        gf_table_map_iter->t_start = 
            table_traversal_order[gf_table_map_iter->t_start];
        gf_table_map_iter->t_end = 
            table_traversal_order[gf_table_map_iter->t_end];
        gf_table_map_iter->t_next = 
            table_traversal_order[gf_table_map_iter->t_next];
    }
}

bool mapper_out_equal(struct mapper_out *map_out1, 
                      struct mapper_out *map_out2)
{
    struct gf_table_mapping *gft_mapping1, *gft_mapping2;
    /* if mapping size is not the same, then mappings are not the same */
    if (hmap_count(&map_out1->mapping) != hmap_count(&map_out2->mapping)) {
        return false;
    }
    HMAP_FOR_EACH (gft_mapping1, node, &map_out1->mapping) {
        gft_mapping2 = mapper_out_get_table_mapping(map_out2, 
                                                    gft_mapping1->g_table);
        /* first mapping wrote to a table, but second did not */
        if (!gft_mapping2) {
            return false;
        }
        /* start table, end table and next table must match */
        if (gft_mapping1->t_start != gft_mapping2->t_start
            || gft_mapping1->t_end != gft_mapping2->t_end
            || gft_mapping1->t_next != gft_mapping2->t_next
            || gft_mapping1->g_table != gft_mapping2->g_table) {
            return false;
        }
    }
    /* the two mappings each have written same parts of the traversal 
       to exactly the same Gigaflow tables, so they are equal */
    return true;
}

bool 
mapping_equal_with_wildcards(struct gigaflow_mapping_context *gf_map_ctx,
                             struct unique_mapping_entry *unique_mapping)
{
    /* compare the wildcards if wildcards are not matching, 
       reject the whole comparison it's a new mapping altogether */
    const uint32_t gigaflow_tables_limit = 
        gf_map_ctx->gf_config->gigaflow_tables_limit;
    for (int k=0; k<gigaflow_tables_limit; k++) {
        if (gf_map_ctx->has_mapping[k] != unique_mapping->has_mapping[k]) {
            return false;
        }
        /* if mapping is there, then compare the wildcards */
        if (gf_map_ctx->has_mapping[k]) {
            if (!flow_wildcards_equal(&gf_map_ctx->accel_wcs[k], 
                                      &unique_mapping->map_wildcards[k])) {
                return false;
            }
        }
    }
    return true;
}

bool
mapping_equal_with_bit_wildcards(struct gigaflow_mapping_context *gf_map_ctx,
                                 struct unique_mapping_entry *unique_mapping)
{
    /* compare the wildcards if wildcards are not matching, 
       reject the whole comparison it's a new mapping altogether */
    const uint32_t gigaflow_tables_limit = 
        gf_map_ctx->gf_config->gigaflow_tables_limit;
    for (int k=0; k<gigaflow_tables_limit; k++) {
        if (gf_map_ctx->has_mapping[k] != unique_mapping->has_mapping[k]) {
            return false;
        }
        /* if mapping is there, then compare the bit wildcards */
        if (gf_map_ctx->has_mapping[k]) {
            if (gf_map_ctx->accel_bit_wc[k].bit_wildcard 
                != unique_mapping->map_bit_wildcards[k]) {
                return false;
            }
        }
    }
    return true;
}

void 
gigaflow_state_add_new_mapping(struct gigaflow_xlate_context *gf_xlate_ctx,
                               struct gigaflow_mapping_context *gf_map_ctx,
                               struct gigaflow_perf_stats *gf_stats)
{
    if (gf_map_ctx->mapped_cnt == 0) {
        // no tables were mapped
        return;
    }
    /* translate from table indices to table IDs */
    mapper_out_map_to_traversal(gf_map_ctx->map_out,
                                gf_xlate_ctx->table_traversal_order);
    const uint32_t gigaflow_tables_limit = 
        gf_map_ctx->gf_config->gigaflow_tables_limit;
    /* compare map_out to all mappings in map_state->unique_mappings
       and if it doesn't exist, then add this as a new mapping */
    struct unique_mapping_entry *existing_map_out;
    HMAP_FOR_EACH (existing_map_out, node, &gf_map_ctx->state->unique_mappings) {
        /* mappings based on just table IDs are not matching */
        if (!mapper_out_equal(existing_map_out->map_out, gf_map_ctx->map_out)) {
            continue;
        }
        /* if table IDs are matching, compare the wildcards 
           if wildcards are not matching, reject the whole comparison
           it's a new mapping altogether */
        if (!mapping_equal_with_wildcards(gf_map_ctx, existing_map_out)) {
            continue;
        }
        // /* if table IDs are matching, compare the bit wildcards 
        //    if bit wildcards are not matching, reject the whole comparison
        //    it's a new mapping altogether */
        // if (!mapping_equal_with_bit_wildcards(gf_map_ctx, existing_map_out)) {
        //     continue;
        // }
        /* mapping already exists, so don't increment anything
           save pointer to the unique mapping that current 
           mapper output is exactly equal to */
        gf_map_ctx->this_unique_mapping = existing_map_out;
        gf_map_ctx->unique_mapping_hash = existing_map_out->node.hash;
        existing_map_out->repeat_count++;
        return;
    }
    /* add this new mapping to the unique mappings */
    struct unique_mapping_entry *new_map_out;
    new_map_out = xzalloc(sizeof *new_map_out);
    new_map_out->repeat_count = 1;
    new_map_out->has_overlaps = false; // for now
    /* save the mappings using table IDs */
    new_map_out->map_out = clone_mapper_out(gf_map_ctx->map_out);
    /* save the mappings using wildcards and also determine 
       if this mapping contributes to Gigaflow permutations */
    struct flow_wildcards prev_wildcard;
    memset(&prev_wildcard, 0, sizeof(prev_wildcard));
    for (int k=0; k<gigaflow_tables_limit; k++) {
        if (gf_map_ctx->has_mapping[k]) {
            new_map_out->map_wildcards[k] = gf_map_ctx->accel_wcs[k];
            new_map_out->map_bit_wildcards[k] = 
                flow_wildcard_to_bits(&gf_map_ctx->accel_wcs[k]);
            new_map_out->has_mapping[k] = true;
            if (flow_wildcards_overlap(&new_map_out->map_wildcards[k],
                                       &prev_wildcard)) {
                new_map_out->has_overlaps = true;
            }
            /* update previous wildcard to current */
            prev_wildcard = gf_map_ctx->accel_wcs[k];
        } else {
            new_map_out->has_mapping[k] = false;
        }
    }
    /* show wildcards in this mapping in vswitchd logs if not overlapping */
    struct ds flow_output;
    ds_init(&flow_output);
    if (!new_map_out->has_overlaps) {
        ds_put_cstr(&flow_output, "NEW UNIQUE MAPPING: [");
        for (int k=0; k<gigaflow_tables_limit; k++) {
            if (new_map_out->has_mapping[k]) {
                bit_wildcards_format(&flow_output, 
                                     new_map_out->map_bit_wildcards[k]);
                ds_put_cstr(&flow_output, ", ");
            }
        }
        ds_put_cstr(&flow_output, "]");
        VLOG_INFO("%s", ds_cstr(&flow_output));
    }
    ds_destroy(&flow_output);
    uint32_t unique_mapping_hash = hash_2words(gf_stats->num_unique_mappings++, 0);
    hmap_insert(&gf_map_ctx->state->unique_mappings, &new_map_out->node, 
                unique_mapping_hash);
    /* add the new mapping to Gigaflow statistics */
    struct unique_mapping_stats *new_map_stats;
    new_map_stats = xzalloc(sizeof *new_map_stats);
    new_map_stats->has_overlaps = new_map_out->has_overlaps;
    memset(new_map_stats->cache_occupancy, 0, 
        sizeof(new_map_stats->cache_occupancy));
    hmap_insert(&gf_stats->unique_mappings_cache_occupancy, 
                &new_map_stats->node, unique_mapping_hash);
    /* save pointer to the unique mapping that current 
       mapper output is exactly equal to */
    gf_map_ctx->this_unique_mapping = new_map_out;
    gf_map_ctx->unique_mapping_hash = unique_mapping_hash;
}

struct mapper_out* 
maximize_optimality(struct gigaflow_xlate_context *gf_xlate_ctx,
                    struct gigaflow_mapping_context *gf_map_ctx)
{
    if (gf_map_ctx->available_cnt == 0) {
        // no available tables for mapping
        return NULL;
    }
    /* consider full traversal, from nothing (index:-1) to end
       and full Gigaflow pipeline for mapping */
    struct mapper_memo memo;
    mapper_memo_init(&memo);
    struct mapper_out *map_out;
    uint32_t tables_looked_up = gf_xlate_ctx->tables_looked_up_in_upcall;
    map_out = maximize_optimality_dp(&memo, 
                                     gf_xlate_ctx, gf_map_ctx,
                                     -1, tables_looked_up-1, 
                                     0, gf_map_ctx->available_cnt-1);
    /* if the returned mapping has a score of 0, but tables were available 
       it means that no coupling was found; then we should write a Megaflow 
       in the first available table */
    if (map_out->score == 0) {
        mapper_out_destroy(map_out);
        map_out = get_empty_mapper_out();
        int gf_table_id = gf_map_ctx->available_tables[0];
        mapper_out_update_mapping(map_out, 0, tables_looked_up-1, 
                                  tables_looked_up, gf_table_id);
    }
    mapper_memo_destroy(&memo);
    return map_out;
}

void 
mapper_out_to_string(struct mapper_out *map_out, 
                     uint32_t *table_traversal_order, struct ds *str)
{
    struct gf_table_mapping *gf_table_map_iter;
    int starting_table, last_table, next_table, gigaflow_table;
    HMAP_FOR_EACH (gf_table_map_iter, node, &map_out->mapping) {
        starting_table = table_traversal_order[gf_table_map_iter->t_start];
        last_table = table_traversal_order[gf_table_map_iter->t_end];
        next_table = table_traversal_order[gf_table_map_iter->t_next];
        gigaflow_table = gf_table_map_iter->g_table;
        ds_put_format(str, "G%d:[%d,%d]->%d, ", 
                      gigaflow_table, starting_table, last_table, 
                      next_table);
    }
}

void 
cleanup_before_mapping(struct gigaflow_xlate_context *gf_xlate_ctx,
                       struct gigaflow_mapping_context *gf_map_ctx)
{
    // cleanup the flows before calling the mapper
    gigaflow_xlate_ctx_clean_flows(gf_xlate_ctx);
    gigaflow_mapping_ctx_clean_flows(gf_map_ctx);
    // insert drop or output table IDs based on the last table traversed
    int output_table = gf_xlate_ctx->tables_looked_up_in_upcall;
    if (gf_xlate_ctx->table_traversal_order[output_table] != OUTPUT_TABLE_ID)
    {
        // insert a drop table ID for the last accel table
        gf_xlate_ctx->table_traversal_order[output_table] = DROP_TABLE_ID;
    }
}

void
map_traversal_to_gigaflow(struct gigaflow_xlate_context *gf_xlate_ctx,
                          struct gigaflow_mapping_context *gf_map_ctx,
                          struct gigaflow_perf_stats *gf_stats,
                          struct pmd_perf_stats *stats)
{

    /* no need to map if there was an error in translation 
       this happens for recursion too deep during resubmissions */
    if (gf_xlate_ctx->xlate_error != XLATE_OK) {
        gf_map_ctx->map_out = get_empty_mapper_out();
        gf_map_ctx->mapped_cnt = 0;
        return;
    }

    /* cleanup contexts before we start mapping 
       THIS MUST BE THE FIRST STEP WHEN WE START MAPPING */
    cleanup_before_mapping(gf_xlate_ctx, gf_map_ctx);

    /* 1. optimization passes to reduce the number of tables 
          to consider before calling path maximization DP
        a. decoupling points in traversal?
        b. header space locality opportunity?
        c. explore vs exploit? */

    /* generate bit wildcards and determine available tables for mapping */
    generate_bit_wildcards(gf_xlate_ctx, gf_map_ctx);
    find_available_tables_for_mapping(gf_map_ctx, gf_stats);

    /* 2. find and assign a traversal mapping to Gigaflow tables 
       by maximizing added paths, coupling and other objectives */
    uint64_t optimizer_cycles = cycles_counter_update(stats);
    struct mapper_out *opt_map_out = maximize_optimality(gf_xlate_ctx, 
                                                         gf_map_ctx);
    optimizer_cycles = cycles_counter_update(stats) - optimizer_cycles;
    gf_perf_update_counter(gf_stats, GF_STAT_OPTIMIZER_CYCLES, optimizer_cycles);
    gigaflow_mapping_ctx_assign_mapping(gf_xlate_ctx, gf_map_ctx, opt_map_out);

    /* 3. compose and map the wildcards as Gigaflow entries to the accel */
    uint64_t compose_cycles = cycles_counter_update(stats);
    compose_gigaflows(gf_xlate_ctx, gf_map_ctx);
    compose_cycles = cycles_counter_update(stats) - compose_cycles;
    gf_perf_update_counter(gf_stats, GF_STAT_COMPOSITION_CYCLES, compose_cycles);

    /* 4. accept this mapping iff masks are not exceeded in any Gigaflow table */
    accept_mapping_if_masks_within_limits(gf_map_ctx, gf_stats);

    const bool estimate_flow_space = 
        gf_map_ctx->gf_config->estimate_flow_space;
    if (estimate_flow_space) {
        /* 5. add this new mapping to our unique mappings */
        gigaflow_state_add_new_mapping(gf_xlate_ctx, gf_map_ctx, gf_stats);
    }
}

void 
update_gigaflow_state_with_mapping(struct gigaflow_xlate_context *gf_xlate_ctx,
                                   struct gigaflow_mapping_context *gf_map_ctx)
{
    if (gf_map_ctx->mapped_cnt == 0) {
        // no tables were mapped
        return;
    }
    const uint32_t gigaflow_tables_limit = 
        gf_map_ctx->gf_config->gigaflow_tables_limit;
    /* source and termination node for gigaflow state multigraph.. */
    int prev_layer = SOURCE_LAYER, prev_value = SOURCE_VALUE;
    int first_wildcard_i, curr_gigaflow_table;
    int starting_table_id;
    struct gf_table_mapping *gf_table_map;
    for (int k=0; k<gigaflow_tables_limit; k++) {
        /* check if this table has a mapping */
        gf_table_map = mapper_out_get_table_mapping(gf_map_ctx->map_out, k);
        if (!gf_table_map) {
            continue;
        }
        first_wildcard_i = gf_table_map->t_start;
        curr_gigaflow_table = gf_table_map->g_table;
        starting_table_id = gf_xlate_ctx->table_traversal_order[first_wildcard_i];
        /* update the Gigaflow multigraph state with this new node */
        multigraph_add_node(&gf_map_ctx->state->gf_lm, 
                            curr_gigaflow_table, starting_table_id); 
        /* add parent edge for this new node */
        multigraph_add_edge(&gf_map_ctx->state->gf_lm, 
                            prev_layer, prev_value, 
                            curr_gigaflow_table, starting_table_id);
        /* update previous */
        prev_layer = curr_gigaflow_table;
        prev_value = starting_table_id;
    }
    /* add termination edge for this last node */
    multigraph_add_edge(&gf_map_ctx->state->gf_lm, prev_layer, prev_value,
                        SINK_LAYER, SINK_VALUE);
}

void
update_gigaflow_state_paths(struct layered_multigraph *gf_lm)
{
    /* update paths_out for each node in the multigraph */
    multigraph_update_paths_out(gf_lm, SOURCE_LAYER, SOURCE_VALUE);
    /* update paths_in for each node in the multigraph */
    multigraph_update_paths_in(gf_lm, SINK_LAYER, SINK_VALUE);
}
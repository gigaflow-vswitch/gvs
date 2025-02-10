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

#ifndef MAPPER_H
#define MAPPER_H 1

#include "openvswitch/vlog.h"
#include "openvswitch/ofpbuf.h"
#include "openvswitch/match.h"
#include "dpif-netdev-perf.h"
#include "lib/multigraph.h"
#include "lib/odp-util.h"
#include "util.h"

#include "gigaflow-config.h"
#include "gigaflow-perf.h"


#ifdef __cplusplus
extern "C" {
#endif

#define minimum(a, b) (((a) < (b)) ? (a) : (b))

/* mapper state maintains an internal representation of the 
   current Gigaflow occupancy as required by the mapping function. 
   This representation keeps track of how slow path wildcards have 
   been mapped in previous upcalls using the next-table and starting-table
   tags of each individual Megaflow in the accelerator tables */
struct mapper_state {
    struct layered_multigraph gf_lm;
    /* maintain unique mappings generated to calculate flow space */
    struct hmap unique_mappings; // key: uint32_t, value: struct unique_mapping_entry
};

/* Gigaflow mapping for a single Gigaflow table */
struct gf_table_mapping {
    struct hmap_node node;      /* hmap node for mapper_out hmap */
    int t_start, t_end, t_next; /* starting, end, and next table of this mapping */
    int g_table;                /* Gigaflow table ID of this mapping */
    int tables;                 /* number of slow path tables mapped to this Gigaflow table */
};

/* The DP path maximizer's output data structure
 * It should contain the score and the mapping used to get this much score
*/
struct mapper_out {
    struct hmap mapping;        /* hash table: (Gigaflow table ID) -> (gf_table_mapping) */
    int score;                  /* optimality score of this mapping */
};

/* A node for hmap that contains unique mappings */
struct unique_mapping_entry {
    struct hmap_node node;                                       /* hmap node for unique_mappings hmap */
    struct mapper_out *map_out;                                  /* the unique mapping for this config */
    bool has_mapping[GIGAFLOW_TABLES_LIMIT];                     /* was mapping written for this gigaflow table or not? */
    struct flow_wildcards map_wildcards[GIGAFLOW_TABLES_LIMIT];  /* flow wildcards of this mapping */
    uint16_t map_bit_wildcards[GIGAFLOW_TABLES_LIMIT];           /* bit wildcards of this mapping */
    uint32_t repeat_count;                                       /* number of times this mapping has been repeated */
    bool has_overlaps;                                           /* does this mapping have overlaps? */
};

/* DP memoization node for saving OPT values */
struct dp_memo_entry {
    struct hmap_node node;      /* hmap node for dp_memo hmap */
    struct mapper_out *map_out; /* the optimal mapping for this config */
    int t_start, t_end;         /* start and end slow path table of this memo entry */
    int g_start, g_end;         /* start and end Gigaflow table of this memo entry */
    int opt;                    /* optimal solution for this DP cache entry */
};

/* DP memoization node for saving paths count. 
   Paths are cached either from node to SINK or node to SOURCE
   terminal: SOURCE or SINK, table_id: the node value in question, 
   g_id: the Gigaflow table in question */
struct paths_memo_entry {
    struct hmap_node node;
    int terminal, table_id, g_id;
    int paths;
};

/* Mapper's memoization data structure that should be cleared before each upcall
 * It is populated as our DP progresses calculating maximum possible paths
 * that can be added given a traversal
*/
struct mapper_memo {
    struct hmap dp;             /* DP memoization hash map */
    struct hmap paths;          /* path() memoization hash map */
};

/* Upcall-level context: Gigaflow needs some translation context
 * to track flows and wildcards in the slow path on a per-table basis
 * for each traversal
*/
struct gigaflow_xlate_context {
    enum xlate_error xlate_error;                                     /* error code for the slow path lookup */
    struct flow individual_flows[SLOW_PATH_TRAVERSAL_LIMIT];          /* flows going into each slow path table */
    struct flow_wildcards individual_wcs[SLOW_PATH_TRAVERSAL_LIMIT];  /* flow wildcards from each slow path table*/
    uint32_t tables_looked_up_in_upcall;                              /* number of tables looked up in the slow path for an upcall */
    uint32_t *ptables_looked_up_in_upcall;                            /* pointer to tables_looked_up_in_upcall */
    uint32_t table_traversal_order[SLOW_PATH_TRAVERSAL_LIMIT];        /* table traversal order for an upcall */
    odp_port_t final_output_odp_port;                                 /* output odp_port_t if the upcall results in output port */
};

/* Mapping-level context: Gigaflow needs these variables to determine
 * an optimal mapping for the given traversal onto the Gigaflow pipeline
*/
struct gigaflow_mapping_context {
    struct gigaflow_config *gf_config;                                /* Gigaflow configuration */
    struct mapper_state *state;                                       /* a pointer to the Gigaflow mapper state */
    struct mapper_out *map_out;                                       /* optimal traversal mapping for this config */
    int left_gf_table, right_gf_table;                                /* starting and ending Gigaflow tables to consider for mapping */
    struct flow accel_flows[GIGAFLOW_TABLES_LIMIT];                   /* flows for each Gigaflow entry */
    struct flow_wildcards accel_wcs[GIGAFLOW_TABLES_LIMIT];           /* flow wildcards for each Gigaflow entry */
    struct gigaflow_bit_wildcard accel_bit_wc[GIGAFLOW_TABLES_LIMIT]; /* flow wildcards for each Gigaflow entry */
    struct ofpbuf accel_odp_actions[GIGAFLOW_TABLES_LIMIT];           /* odp actions for each Gigaflow entry */
    uint64_t accel_odp_actions_stubs[GIGAFLOW_TABLES_LIMIT][512 / 8]; /* action stubs required to commit odp actions */
    bool has_mapping[GIGAFLOW_TABLES_LIMIT];                          /* was mapping written for this gigaflow table or not? */
    uint32_t priorities[GIGAFLOW_TABLES_LIMIT];                       /* priorities for each Gigaflow entry */
    struct gf_table_mapping debug_mappings[GIGAFLOW_TABLES_LIMIT];    /* for debugging: readable mapped tables in map_out* */
    uint32_t decoupled_traversal_indices[SLOW_PATH_TRAVERSAL_LIMIT];  /* indices of traversal where decoupling is possible */
    uint32_t bit_wildcards[SLOW_PATH_TRAVERSAL_LIMIT];                /* bitwise representations of wildcards */
    uint32_t decoupling_cnt;                                          /* number of decoupling points in traversal */
    int available_tables[GIGAFLOW_TABLES_LIMIT];                      /* Gigaflow tables available for mapping */
    uint32_t available_cnt;                                           /* number of Gigaflow tables available for mapping */
    uint32_t mapped_cnt;                                              /* number of Gigaflow tables mapped to in this mapping */
    struct unique_mapping_entry *this_unique_mapping;                 /* pointer to the current unique mapping matching this mapper output */    
    uint32_t unique_mapping_hash;                                     /* hash of the current unique mapping */
};

/* Initialize flows, wildcards and other variables
 * for slow path lookup context */
void gigaflow_xlate_ctx_init(struct gigaflow_xlate_context* gf_xlate_ctx);

/* insert flow to be looked up in the current slow path table
 * into its current slow in the context */
void 
gigaflow_xlate_ctx_insert_current_flow(struct gigaflow_xlate_context* gf_xlate_ctx, 
                                       struct flow this_flow);

/* insert flow wildcard after a lookup in the current slow path table
 * into its current slow in the context */
void 
gigaflow_xlate_ctx_insert_current_table_id(struct gigaflow_xlate_context* gf_xlate_ctx, 
                                           uint8_t this_table_id);

/* return the current flow wildcard (empty) to be used for slow path
 * lookup in order to track fields/wildcards used in this table only */
struct flow_wildcards* 
gigaflow_xlate_ctx_get_current_wildcard(struct gigaflow_xlate_context* gf_xlate_ctx);

/* return the previous flow wildcard (populated) to be extended during 
 * flow actions execuation in its slow path table's do_xlate_actions */
struct flow_wildcards* 
gigaflow_xlate_ctx_get_prev_wildcard(struct gigaflow_xlate_context* gf_xlate_ctx);

/* use the current slow path table's wildcard and update the Megaflow
 * wildcard being tracked by Vanilla OVS */
void 
gigaflow_xlate_ctx_update_megaflow_wildcard(struct gigaflow_xlate_context* gf_xlate_ctx, 
                                            struct flow_wildcards* megaflow_wildcards);

/* move the iterator to the next slot, for next table lookups */
void 
gigaflow_xlate_ctx_increment_lookedup_tables_cnt(struct gigaflow_xlate_context* gf_xlate_ctx);

/* return the iterator of the current slot */
uint32_t 
gigaflow_xlate_ctx_get_lookedup_tables_cnt(struct gigaflow_xlate_context* gf_xlate_ctx);

/* sets the final output odp_port_t in the gigaflow context */
void 
gigaflow_xlate_ctx_set_output_odp_port(struct gigaflow_xlate_context* gf_xlate_ctx, 
                                       odp_port_t output_port);

/* Initialize flows, wildcards and other variables for Gigaflow mapping */
void
gigaflow_mapping_ctx_init(struct gigaflow_mapping_context* gf_map_ctx,
                          struct mapper_state* map_state, 
                          struct gigaflow_config *gf_config);

/* uninitialize flows, wildcards and other variables for Gigaflow mapping */
void gigaflow_mapping_ctx_uninit(struct gigaflow_mapping_context* gf_map_ctx);

/* accepts a struct mapper_out* and assigns to its own map_out* */
void 
gigaflow_mapping_ctx_assign_mapping(struct gigaflow_xlate_context *gf_xlate_ctx,
                                    struct gigaflow_mapping_context* gf_map_ctx,
                                    struct mapper_out *map_out);

/* initializes the Gigaflow mapper state */
void mapper_init(struct mapper_state* map_state);

/* frees the Gigaflow mapper state */
void mapper_destroy(struct mapper_state* state);

/* Hack to save the Slow Path + Gigaflow from acting like a Moron! */
/* cleanup traversal flows before calling the mapper */
void 
gigaflow_xlate_ctx_clean_flows(struct gigaflow_xlate_context *gf_xlate_ctx);

/* Hack to save the Slow Path + Gigaflow from acting like a Moron! */
/* cleanup accel flows before calling the mapper */
void 
gigaflow_mapping_ctx_clean_flows(struct gigaflow_mapping_context *gf_map_ctx);

/* converts flow wildcards to a bit representation helpful for decoupling */
uint16_t flow_wildcard_to_bits(struct flow_wildcards *this_wc);

/* format a given bit_wildcards into ds *s */
void bit_wildcards_format(struct ds *s, uint16_t bit_wildcards);

/* generates bit wildcards for each slow path wildcard in traversal */
void
generate_bit_wildcards(struct gigaflow_xlate_context *gf_xlate_ctx,
                       struct gigaflow_mapping_context *gf_map_ctx);

/* decouples traversal at points where consecutive wildcards are non-overlapping */
void opt_pass_decouple_traversal(struct gigaflow_xlate_context *gf_xlate_ctx,
                                 struct gigaflow_mapping_context *gf_map_ctx);

/* looks at the Gigaflow entries and mask utilization to determine 
   which of the Gigaflow tables are available for mapping */
void find_available_tables_for_mapping(
    struct gigaflow_mapping_context *gf_map_ctx,
    struct gigaflow_perf_stats *gf_stats);

void accept_mapping_if_masks_within_limits(
    struct gigaflow_mapping_context *gf_map_ctx,
    struct gigaflow_perf_stats *gf_stats);

/* composes two wildcards needed to construct Gigaflow entries */
void compose_wildcards(struct flow_wildcards *wc1, struct flow_wildcards *wc2);

/* composes a single Gigaflow using its wildcards and flows into a Gigaflow entry 
 * for the accel pipeline */
void 
compose_one_gigaflow(struct gigaflow_xlate_context *gf_xlate_ctx,
                     struct gigaflow_mapping_context *gf_map_ctx,
                     int first_wildcard_i, int curr_wildcard_i,
                     int starting_table_id, int next_table_id,
                     int curr_gigaflow_table);

/* composes Gigaflow wildcards and flows into Gigaflow entries 
 * using map_out*; will NOT update the Gigaflow state in multigraph */
void 
compose_gigaflows(struct gigaflow_xlate_context *gf_xlate_ctx,
                  struct gigaflow_mapping_context *gf_map_ctx);

/* composes Gigaflow wildcards and flows into Gigaflow entries 
 * using map_out*; will also update the Gigaflow state in multigraph */
void 
compose_gigaflows_with_state_update(struct gigaflow_xlate_context *gf_xlate_ctx,
                                    struct gigaflow_mapping_context *gf_map_ctx);

/* search a memo object for a particular new node insertion; 
   returns number of new paths to be created */
int search_paths_memo(struct mapper_memo *memo, 
                      int terminal, int table_id, int g_id);

/* caches paths count for a new node insertion in a memo object */
void insert_into_paths_memo(struct mapper_memo *memo,
                            int terminal, int table_id, int g_id, int paths);

/* returns paths from given t_next to SINK node */
int paths_to_sink(struct mapper_memo *memo, 
                  struct gigaflow_mapping_context *gf_map_ctx, 
                  int next_table, int g_id);

/* returns paths from given t_start placed in g_id to SOURCE node */
int paths_to_source(struct mapper_memo *memo, 
                    struct gigaflow_mapping_context *gf_map_ctx, 
                    int start_table, int g_id);

/* calculates and caches new paths to be created 
   if a new node is inserted into multigraph */
int 
paths(struct mapper_memo *memo, 
      struct gigaflow_xlate_context *gf_xlate_ctx,
      struct gigaflow_mapping_context *gf_map_ctx, 
      int t_start, int t_end, int t_next, int g_id);

/* returns a coupling score between t_start and t_end 
   tables of the slow path traversal */
int 
get_coupling(struct gigaflow_mapping_context *gf_map_ctx,
             int t_start, int t_end);

/* returns true if Gigaflow table g_id is not yet filled in terms of 
   number of entries and number of masks limits, false otherwise */
bool
table_will_overflow(struct gigaflow_mapping_context *gf_map_ctx, 
                    struct gigaflow_perf_stats *gf_stats, int g_id);

/* our mapping objective function: returns a combined score of paths, 
   and coupling of a given composition of slow path wildcards in a 
   slow path traversal */
int 
get_mapping_optimality(struct mapper_memo *memo, 
                       struct gigaflow_xlate_context *gf_xlate_ctx,
                       struct gigaflow_mapping_context *gf_map_ctx, 
                       int t_start, int t_end, int t_next, int g_id);

/* create and return a clone of a dp memo entry */
struct dp_memo_entry* 
clone_dp_memo_entry(struct dp_memo_entry* dp_memo_ent);

/* searches a memo object for a cached value of DP calculation 
   for path maximization in Gigaflow */
struct dp_memo_entry* 
search_dp_memo(struct mapper_memo *memo, int t_start, int t_end, 
               int g_start, int g_end);

/* caches DP value and its corresponding mapping (shallow copy) 
   in a dp memo object */
void insert_into_dp_memo(struct mapper_memo *memo,
                         struct mapper_out *map_out,
                         int t_start, int t_end, 
                         int g_start, int g_end, int opt);

/* initializes mapper's memo objects */
void mapper_memo_init(struct mapper_memo* memo);

/* frees mapper's memo objects after one invocation of maximize_paths() */
void mapper_memo_destroy(struct mapper_memo* memo);

/* initializes and returns a new DP optimizer's output data structure */
struct gf_table_mapping* 
create_new_gf_table_mapping(int t_start, int t_end, int t_next, 
                            int gf_table_id);

/* initializes a given gf_table_mapping data structure */
void
gf_table_mapping_init(struct gf_table_mapping* gf_table_map, 
                      int t_start, int t_end, int t_next, 
                      int gf_table_id);

/* write a given table mapping into a gf_table_map* */
void write_gf_table_mapping(struct gf_table_mapping* gf_table_map,
                            int t_start, int t_end, int t_next, 
                            int gf_table_id);

/* creates and returns a clone of gf_table_mapping* */
struct gf_table_mapping* 
clone_gf_table_mapping(struct gf_table_mapping* gf_table_map);

/* destroy the DP optimizer's output data structure */
void gf_table_mapping_destroy(struct gf_table_mapping* gf_table_map);

/* initializes and returns a new DP optimizer's output data structure */
struct mapper_out* get_empty_mapper_out(void);

/* initializes the given DP optimizer's output data structure */
void mapper_out_init(struct mapper_out* map_out);

/* returns a clone of DP optimizer's output data structure */
struct mapper_out* clone_mapper_out(struct mapper_out* map_out);

/* adds a new Gigaflow table mapping in a given mapper_out data structure */
void mapper_out_update_mapping(struct mapper_out* map_out,
                               int t_start, int t_end, int t_next,
                               int gf_table_id);

/* retrieves a table mapping in a given mapper_out, NULL if non-existent */
struct gf_table_mapping* 
mapper_out_get_table_mapping(struct mapper_out* map_out, int gf_table_id);

/* destroy the DP optimizer's output data structure */
void mapper_out_destroy(struct mapper_out *map_out);

/* destroy the DP optimizer's output data structure, 
   while keeping nodes from *keep */
void mapper_out_destroy_with_keep(struct mapper_out *map_out, 
                                  struct mapper_out *keep);

/* maps a given mapper out to traversal table IDs */
void mapper_out_map_to_traversal(struct mapper_out *map_out, 
                                 uint32_t *table_traversal_order);

/* compare two mapper_out and return true if they are equal */
bool mapper_out_equal(struct mapper_out *map_out1, 
                      struct mapper_out *map_out2);

/* compare two mapper_out and return true if they are equal */
bool 
mapping_equal_with_wildcards(struct gigaflow_mapping_context *gf_map_ctx,
                             struct unique_mapping_entry *unique_mapping);

/* compare the bit_wildcards in gf_map_ctx with unique_mapping's 
   bit_wildcards and return true if equal; false otherwise */
bool 
mapping_equal_with_bit_wildcards(struct gigaflow_mapping_context *gf_map_ctx,
                                 struct unique_mapping_entry *unique_mapping);

/* adds new mapping to unique_mappings in mapper state */
void gigaflow_state_add_new_mapping(struct gigaflow_xlate_context *gf_xlate_ctx,
                                    struct gigaflow_mapping_context *gf_map_ctx,
                                    struct gigaflow_perf_stats *gf_stats);

/* DP implementation of a path maximization algorithm 
   for best Gigaflow utilization */
struct mapper_out* 
maximize_optimality_dp(struct mapper_memo *memo,
                       struct gigaflow_xlate_context *gf_xlate_ctx,
                       struct gigaflow_mapping_context *gf_map_ctx,
                       int t_start, int t_end, int g_start, int g_end);

/* an iterative implementation of maximize_optimality_dp() */
struct mapper_out* 
maximize_optimality_dp_iterative(struct mapper_memo *memo,
                                 struct gigaflow_xlate_context *gf_xlate_ctx,
                                 struct gigaflow_mapping_context *gf_map_ctx,
                                 int t_start, int t_end, 
                                 int g_start, int g_end);

/* converts a given map_out* to string representation for logging */
void mapper_out_to_string(struct mapper_out *map_out, 
                          uint32_t *table_traversal_order, struct ds *str);

/* cleanup xlate and mapping context objects before path maximization calls */
void cleanup_before_mapping(struct gigaflow_xlate_context *gf_xlate_ctx,
                            struct gigaflow_mapping_context *gf_map_ctx);

/* path maximizer based on our DP algorithm */
struct mapper_out* 
maximize_optimality(struct gigaflow_xlate_context *gf_xlate_ctx,
                    struct gigaflow_mapping_context *gf_map_ctx);

/* takes a traversal and maps it to Gigaflow pipeline 
   based on current state of mapping */
void 
map_traversal_to_gigaflow(struct gigaflow_xlate_context *gf_xlate_ctx,
                          struct gigaflow_mapping_context *gf_map_ctx,
                          struct gigaflow_perf_stats *gf_stats,
                          struct pmd_perf_stats *s);

/* updates the layered-multigraph Gigaflow state based on a a given mapping; 
   MUST be called after a mapping has been written to gf_xlate_ctx->map_out
   This will not update path counts needed for performing paths() queries.
   For that, call update_gigaflow_state_paths() */
void 
update_gigaflow_state_with_mapping(struct gigaflow_xlate_context *gf_xlate_ctx,
                                   struct gigaflow_mapping_context *gf_map_ctx);

/* updates the layered-multigraph Gigaflow state by recalculating paths
   to each node from SOURCE and from each node to SINK */
void
update_gigaflow_state_paths(struct layered_multigraph *gf_lm);

#ifdef __cplusplus
}
#endif
#endif
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

#ifndef MULTIGRAPH_H
#define MULTIGRAPH_H 1

#include "lib/util.h"
#include "lib/hash.h"
#include "openvswitch/hmap.h"
#include "openvswitch/vlog.h"
#include "gigaflow-config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The following is a directed, layered, multigraph implementation that 
 * permits duplicate nodes and multiple edges between any pair of nodes.
 * This multigraph implementation is specifically needed for implementing
 * the Gigaflow state representation to track how upcalls are mapped to 
 * the hardware Gigaflow pipeline. Mapper uses this multigraph implementation
 * to determine how many new paths that will be created if a certain mapping 
 * is performed, tries out all possible combinations and (if exploiting) 
 * returns the most optimal mapping */

/* An edge in a layered multigraph */
struct lm_edge {
    struct hmap_node node;     /* hmap_node node to insert in an hmap */
    struct lm_node *l_lmnode;  /* left lm_node* of this edge */
    struct lm_node *r_lmnode;  /* right lm_node* of this edge */
    uint32_t repeat_count;     /* count of how many times this relation has repeated */
    int l_layer;               /* layer in which the left node is situated */
    int l_value;               /* value of the left node */
    int r_layer;               /* layer in which the right node is situated */
    int r_value;               /* value of the right node */
};

/* A single node in a layered multigraph */
struct lm_node {
    struct hmap_node node;     /* hmap_node node to insert into an hmap */
    struct hmap parents;       /* a list of parents (lm_edge*) of this node */
    struct hmap children;      /* a list of children (lm_edge*) of this node */
    uint32_t repeat_count;     /* count of how many times this node has been inserted */
    uint32_t paths_in;         /* total paths to this node from source */
    uint32_t paths_out;        /* total paths from this node to sink */
    int layer;                 /* the layer at which this node is present */
    int value;                 /* the value contained in this node (the node ID) */
};

/* A layered multigraph organized as a hashmap of (layers, node_id)->lm_node* */
struct layered_multigraph {
    struct hmap nodes;         /* a hashmap from (layer, value) to (lm_node*) node */
    struct hmap edges;         /* a hashmap from (l_layer, l_val, r_layer, r_val) to (lm_edge*) node */
    struct lm_node *source;    /* source node of this layered multigraph; no parent -> source parent */
    struct lm_node *sink;      /* sink node of this layered multigraph; no children -> sink child */
    uint32_t layer_count;      /* total number of layers in this graph */
    uint32_t node_count;       /* total number of nodes in this graph */
    uint32_t edge_count;       /* total number of edges in this graph */
};

/* init a node in directed layered multigraph */
void lm_node_init(struct lm_node* lmn, int layer, int value);

/* destroy a node in directed layered multigraph */
void lm_node_destroy(struct lm_node* lmn);

/* init an edge in directed layered multigraph */
void 
lm_edge_init(struct lm_edge* lme, int l_layer, int l_value,
             int r_layer, int r_value);

/* destroy an edge in directed layered multigraph */
void lm_edge_destroy(struct lm_edge* lme);

/* init the directed layered multigraph */
void multigraph_init(struct layered_multigraph *lm, uint32_t layers);

/* destroy the directed layered multigraph */
void 
multigraph_destroy(struct layered_multigraph *lm);

/* returns lm_node* if already exists in the graph, else NULL */
struct lm_node* 
multigraph_get_node(struct layered_multigraph *lm, 
                    int layer, int value);

/* inserts a new node in layer with value */
void multigraph_add_node(struct layered_multigraph *lm, 
                         int layer, int value);

/* returns lm_edge* if already exists in the graph, else NULL */
struct lm_edge* 
multigraph_get_edge(struct layered_multigraph *lm, 
                    int l_layer, int l_value,
                    int r_layer, int r_value);

/* creates and returns an edge with given parameters */
struct lm_edge* 
multigraph_create_edge_with_params(struct layered_multigraph *lm, 
                                   int l_layer, int l_value,
                                   int r_layer, int r_value,
                                   uint32_t repeat_count);

/* creates a new edge and inserts in both parent and children nodes */
struct lm_edge* 
multigraph_create_new_edge(struct layered_multigraph *lm, 
                           int l_layer, int l_value,
                           int r_layer, int r_value);

/* this function takes an existing edge and 
   replicates it (by giving same parents) to the new child */
struct lm_edge* 
multigraph_replicate_edge(struct layered_multigraph *lm, 
                          struct lm_edge* existing,
                          int r_layer, int r_value);

/* This function must be called on existing nodes to add an edge between them 
   i.e., call multigraph_add_node() on both new nodes before this function
   - A directed edge is added from (l_layer, l_value) to (r_layer, r_value)
   - Left node is the parent, right node is the child
      - for the new parent, finds new children in layers in front of it
      - for the new child, finds other parents in layers behind it */
void multigraph_add_edge(struct layered_multigraph *lm, 
                         int l_layer, int l_value,
                         int r_layer, int r_value);

/* given a node, returns total paths to the sink node from this node */
uint32_t 
multigraph_paths_to_sink_helper(struct layered_multigraph *lm, 
                                struct lm_node *lmn);

/* given a node, returns total paths to the sink node from this node */
uint32_t
multigraph_paths_to_sink(struct layered_multigraph *lm, int next, int layer);

/* given a node and starting path count, returns total paths from source 
   to this node in multigraph via parents behind 'limit' layer */
uint32_t 
multigraph_paths_to_source_helper(struct layered_multigraph *lm, 
                                  struct lm_node *lmn, int limit);

/* given a node, returns total paths from the source node to this node */
uint32_t
multigraph_paths_to_source(struct layered_multigraph *lm, 
                           int value, int layer);

/* returns new paths count that will be created if this value
   is added in this layer and it's child is next */
uint32_t 
multigraph_paths_query(struct layered_multigraph *lm, 
                       int value, int next, int layer);

/* updates paths_out from a given node to SINK node */
void
multigraph_update_paths_out(struct layered_multigraph *lm, 
                            int layer, int value);

/* updates paths_in from a given node to SOURCE node */
void
multigraph_update_paths_in(struct layered_multigraph *lm, 
                           int layer, int value);

#ifdef __cplusplus
}
#endif
#endif
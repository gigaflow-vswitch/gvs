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
#include "multigraph.h"

void
lm_node_init(struct lm_node* lmn, int layer, int value)
{
    lmn = xzalloc(sizeof *lmn);
    hmap_init(&lmn->parents);
    hmap_init(&lmn->children);
    // reserve space; without it, edge addition breaks..
    hmap_reserve(&lmn->parents, 256);
    hmap_reserve(&lmn->children, 256);
    lmn->layer = layer;
    lmn->value = value;
    lmn->repeat_count = 0;
    lmn->paths_in = 0;
    lmn->paths_out = 0;
}

void
lm_node_destroy(struct lm_node* lmn)
{
    // TODO: complete this function..
    free(lmn);
}

void 
lm_edge_init(struct lm_edge* lme, int l_layer, int l_value,
             int r_layer, int r_value)
{
    lme = xzalloc(sizeof *lme);
    lme->l_layer = l_layer;
    lme->l_value = l_value;
    lme->r_layer = r_layer;
    lme->r_value = r_value;
    lme->l_lmnode = NULL;
    lme->r_lmnode = NULL;
    // first appearance of this edge
    lme->repeat_count = 1;
}

void 
lm_edge_destroy(struct lm_edge* lme)
{
    free(lme);
}

void
multigraph_init(struct layered_multigraph *lm, uint32_t layers)
{
    hmap_init(&lm->nodes);
    hmap_init(&lm->edges);
    // reserve space; without it, edge addition breaks..
    hmap_reserve(&lm->nodes, 1024);
    hmap_reserve(&lm->edges, 1024);
    multigraph_add_node(lm, SOURCE_LAYER, SOURCE_VALUE);
    multigraph_add_node(lm, SINK_LAYER, SINK_VALUE);
    lm->source = multigraph_get_node(lm, SOURCE_LAYER, SOURCE_VALUE);
    lm->sink = multigraph_get_node(lm, SINK_LAYER, SINK_VALUE);
    lm->layer_count = layers;
    lm->node_count = 2; // source + sink nodes
    lm->edge_count = 0;
}

void
multigraph_destroy(struct layered_multigraph *lm)
{
    struct lm_node *lmn;
    HMAP_FOR_EACH_POP (lmn, node, &lm->nodes) {
        free(lmn);
    }
    hmap_destroy(&lm->nodes);
    lm_node_destroy(lm->source);
    lm_node_destroy(lm->sink);
    lm->node_count = 0;
    lm->edge_count = 0;
}

struct lm_node*
multigraph_get_node(struct layered_multigraph *lm, int layer, int value) 
{
    struct lm_node *lmn_entry;
    uint32_t lmn_hash = hash_2words_plus1(layer, value);
    HMAP_FOR_EACH_WITH_HASH (lmn_entry, node, lmn_hash, &lm->nodes) {
        if (lmn_entry->layer == layer && lmn_entry->value == value) {
            return lmn_entry;
        }
    }
    return NULL;
}

void
multigraph_add_node(struct layered_multigraph *lm, int layer, int value) 
{
    struct lm_node *new_lmn = multigraph_get_node(lm, layer, value);
    if (new_lmn) {
        new_lmn->repeat_count++;
        return;
    }
    // insert new node into this layered multigraph
    new_lmn = xzalloc(sizeof *new_lmn);
    new_lmn->repeat_count = 1; // first insertion
    new_lmn->layer = layer;
    new_lmn->value = value;
    hmap_init(&new_lmn->parents);
    hmap_init(&new_lmn->children);
    hmap_reserve(&new_lmn->parents, 256);
    hmap_reserve(&new_lmn->children, 256);
    uint32_t lmn_hash = hash_2words_plus1(layer, value);
    hmap_insert(&lm->nodes, &new_lmn->node, lmn_hash);
    lm->node_count++;
}

struct lm_edge* 
multigraph_get_edge(struct layered_multigraph *lm, int l_layer, int l_value, 
                    int r_layer, int r_value)
{
    struct lm_edge *lme_entry;
    uint32_t lme_hash = hash_4words_plus1(l_layer, l_value, r_layer, r_value);
    HMAP_FOR_EACH_WITH_HASH (lme_entry, node, lme_hash, &lm->edges) {
        if (lme_entry->l_layer == l_layer && lme_entry->l_value == l_value && 
                lme_entry->r_layer == r_layer && lme_entry->r_value == r_value) {
            return lme_entry;
        }
    }
    return NULL;
}

struct lm_edge* 
multigraph_create_edge_with_params(struct layered_multigraph *lm, 
                                   int l_layer, int l_value,
                                   int r_layer, int r_value,
                                   uint32_t repeat_count)
{
    // creates a new parameterized edge
    struct lm_node *l_lmn = multigraph_get_node(lm, l_layer, l_value);
    struct lm_node *r_lmn = multigraph_get_node(lm, r_layer, r_value);
    // make sure the nodes exist
    ovs_assert(l_lmn != NULL && r_lmn != NULL);
    // check if edge already exists, else create it
    struct lm_edge* edge = multigraph_get_edge(lm, l_layer, l_value, 
                                               r_layer, r_value);
    // edge already exists; just increment repeat count
    if (edge) {
        /* if edge was created between a SOURCE and another node,
           then we don't repeat the edge; SOURCE to node edge should
           never repeat more than once */
        if ((l_layer == SOURCE_LAYER && l_value == SOURCE_VALUE)) {
            return edge;
        }
        /* this edge already exists in the edges collection of this multigraph
           it already exists in the left node's children
           same edge also already exists in the right node's parents
           this increment operation repeats that edge for each of them */
        edge->repeat_count += repeat_count;
        return edge;
    }
    // create new edge (previously NULL)
    edge = xzalloc(sizeof *edge);
    edge->l_lmnode = l_lmn;
    edge->r_lmnode = r_lmn;
    edge->l_layer = l_layer;
    edge->l_value = l_value;
    edge->r_layer = r_layer;
    edge->r_value = r_value;
    // first appearance of this edge
    edge->repeat_count = repeat_count;
    lm->edge_count++; // increment total edges in multigraph
    uint32_t lme_hash = hash_4words_plus1(l_layer, l_value, r_layer, r_value);
    // add edge to multigraph's set of edges
    hmap_insert(&lm->edges, &edge->node, lme_hash);
    // add edge to left node's children
    hmap_insert(&l_lmn->children, &edge->node, lme_hash);
    // add edge to right node's parents
    hmap_insert(&r_lmn->parents, &edge->node, lme_hash);
    return edge;
}

struct lm_edge* 
multigraph_create_new_edge(struct layered_multigraph *lm, 
                           int l_layer, int l_value, int r_layer, int r_value)
{
    return multigraph_create_edge_with_params(lm, l_layer, l_value, 
                                              r_layer, r_value, 1);
}

struct lm_edge* 
multigraph_replicate_edge(struct layered_multigraph *lm, 
                          struct lm_edge* existing, int r_layer, int r_value)
{
    return multigraph_create_edge_with_params(lm, existing->l_layer, 
                                              existing->l_value, 
                                              r_layer, r_value,
                                              existing->repeat_count);
}

void 
multigraph_add_edge(struct layered_multigraph *lm, int l_layer, int l_value,
                    int r_layer, int r_value) 
{
    struct lm_edge* edge;
    /* 1. create the new edge between this parent and child */
    edge = multigraph_create_new_edge(lm, l_layer, l_value, r_layer, r_value);
    /* if edge was created between a SOURCE and another node or
       between a node and SINK, no further processing is needed */
    if ((l_layer == SOURCE_LAYER && l_value == SOURCE_VALUE)
            || (r_layer == SINK_LAYER && r_value == SINK_VALUE)) {
        return;
    }
    /* if edge repeated more than once, this means edge already existed, 
       so both nodes already have parents and children correctly 
       designated to them, no further processing is needed */
    if (edge->repeat_count > 1) {
        return;
    }
    /* this is a new edge, so the parent might have other children
       and the child might have other parents */
    struct lm_node *lmn_iter;
    /* 2. add other children for the new parent (left node)
       a child can only be in a layer ahead of its parent */
    for (int i=l_layer+1; i<lm->layer_count; i++) {
        /* if layer is the right layer (which we just updated)
           or a valid child node doesn't exist in this layer.. */
        lmn_iter = multigraph_get_node(lm, i, r_value);
        if (i == r_layer || !lmn_iter) {
            continue;
        }
        /* 2. found valid child in some some layer ahead of the parent
            so update the parent */
        multigraph_create_new_edge(lm, l_layer, l_value, lmn_iter->layer, 
                                   lmn_iter->value);
    }
    /* 3. add other parents for the new child (right node) 
       [optimization]: only one node can be picked to get all parents!
       we can pick the right most node to get those parents */
    for (int j=lm->layer_count; j>=0; j--) {
        /* if layer is the right layer (which we just updated)
           or a valid child node doesn't exist in this layer.. */
        lmn_iter = multigraph_get_node(lm, j, r_value);
        if (j == r_layer || !lmn_iter) {
            continue;
        }
        /* replicate this node's parents for the new child */
        struct lm_edge *p_lme_iter;
        HMAP_FOR_EACH (p_lme_iter, node, &lmn_iter->parents) {
            /* 3. only if the parents are in layers behind it, replicate 
               that edge (with its repeat count) for this new child */
            if (p_lme_iter->l_layer < r_layer) {
                multigraph_replicate_edge(lm, p_lme_iter, r_layer, r_value);
            }
        }
        break;
    }
}

uint32_t 
multigraph_paths_to_sink_helper(struct layered_multigraph *lm, 
                                struct lm_node *lmn)
{
    // base case
    if (lmn->layer == SINK_LAYER && lmn->value == SINK_VALUE) {
        /* update paths_out from this node to SINK node */
        lmn->paths_out = 1;
        return lmn->paths_out;
    }
    // recursive case
    struct lm_edge *child_lme;
    uint32_t total_paths = 0;
    HMAP_FOR_EACH (child_lme, node, &lmn->children) {
        total_paths += child_lme->repeat_count * 
            multigraph_paths_to_sink_helper(lm, child_lme->r_lmnode);
    }
    /* update paths_out from this node to SINK node */
    lmn->paths_out = total_paths;
    return lmn->paths_out;
}

uint32_t
multigraph_paths_to_sink(struct layered_multigraph *lm, int next, int layer)
{
    /* if lm_node(layer, value)->next was placed in 'layer', 
       how many additional paths would appear?
        1. for 'next', how many paths out (sum of children) would be possible?
        2. for lm_node(layer, value), each parent will get those "paths_out"
           as new paths and will be propagated back to the source node */
    /* collect paths out for each child of 'next' node */
    struct lm_node *next_lmn;
    uint32_t next_paths_out = 1; // one path for the new insertion
    for (int i=layer+1; i<lm->layer_count; i++) {
        next_lmn = multigraph_get_node(lm, i, next);
        if (next_lmn == NULL) {
            continue;
        }
        // next_paths_out += multigraph_paths_to_sink_helper(lm, next_lmn);
        next_paths_out += next_lmn->paths_out;
    }
    return next_paths_out;
}

uint32_t
multigraph_paths_to_source_helper(struct layered_multigraph *lm, 
                                  struct lm_node *lmn, int limit)
{
    // base case
    if (lmn->layer == SOURCE_LAYER && lmn->value == SOURCE_VALUE) {
        /* update paths_out from this node to SINK node */
        lmn->paths_in = 1;
        return lmn->paths_in;
    }
    // recursive case
    struct lm_edge *p_lme;
    uint32_t total_paths = 0;
    HMAP_FOR_EACH (p_lme, node, &lmn->parents) {
        // parents behind this layer only!
        if (p_lme->l_layer < limit) {
            total_paths += p_lme->repeat_count *
                multigraph_paths_to_source_helper(lm, p_lme->l_lmnode, limit);
        }
    }
    /* update paths_in to this node from SOURCE node */
    lmn->paths_in = total_paths;
    return lmn->paths_in;
}

/* given a node, returns total paths from the source node to this node */
uint32_t
multigraph_paths_to_source(struct layered_multigraph *lm, int value, int layer)
{
    /* if value already exists in layer, directly work on it */
    struct lm_node *value_lmn;
    value_lmn = multigraph_get_node(lm, layer, value);
    if (value_lmn) {
        return value_lmn->paths_in;
    }
    /* propagate the paths out to each parent; to find an existing node 
       of same value, pick one from right most layer and use only those 
       parents which are behind the current layer being inserted
       [optimization]: only one node can be picked to get all parents! */
    uint32_t new_paths = 0;
    for (int j=lm->layer_count; j>=0; j--) {
        value_lmn = multigraph_get_node(lm, j, value);
        if (value_lmn == NULL) {
            continue;
        }
        // found a valid node (only one is needed)
        // new_paths = multigraph_paths_to_source_helper(lm, value_lmn, layer);
        struct lm_edge *p_lme;
        HMAP_FOR_EACH (p_lme, node, &value_lmn->parents) {
            // parents behind this layer only!
            if (p_lme->l_layer < layer) {
                new_paths += p_lme->repeat_count * 
                    p_lme->l_lmnode->paths_in;
            }
        }
        break;
    }
    return new_paths;
}

uint32_t
multigraph_paths_query(struct layered_multigraph *lm, 
                       int value, int next, int layer)
{
    /* collect paths out for each child of 'next' node */
    uint32_t paths_to_sink = multigraph_paths_to_sink(lm, next, layer);
    /* propagate the paths out to each parent; to find an existing node 
       of same value, pick one from right most layer and use only those 
       parents which are behind the current layer being inserted
       [optimization]: only one node can be picked to get all parents! */
    uint32_t paths_to_src = paths_to_sink * 
        multigraph_paths_to_source(lm, value, layer);
    return paths_to_src > 0 ? paths_to_src : paths_to_sink;
}

void
multigraph_update_paths_out(struct layered_multigraph *lm, 
                            int layer, int value)
{
    /* fetch the starting node and call helper */
    struct lm_node *lmn = multigraph_get_node(lm, layer, value);
    if (lmn) {
        multigraph_paths_to_sink_helper(lm, lmn);
    }
}

void
multigraph_update_paths_in(struct layered_multigraph *lm, 
                           int layer, int value)
{
    /* fetch the termination node and call helper */
    struct lm_node *lmn = multigraph_get_node(lm, layer, value);
    if (lmn) {
        multigraph_paths_to_source_helper(lm, lmn, layer);
    }
}
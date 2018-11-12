/*
 * Copyright (c) 2018 Nicira, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef IPF_H
#define IPF_H 1

#include "dp-packet.h"
#include "openvswitch/types.h"

void ipf_preprocess_conntrack(struct dp_packet_batch *pb, long long now,
                              ovs_be16 dl_type, uint16_t zone,
                              uint32_t hash_basis);

void ipf_postprocess_conntrack(struct dp_packet_batch *pb, long long now,
                               ovs_be16 dl_type);

void ipf_init(void);
void ipf_destroy(void);
int ipf_set_enabled(bool v6, bool enable);
int ipf_set_min_frag(bool v6, uint32_t value);

#endif /* ipf.h */

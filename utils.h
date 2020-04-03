//
// Created by Hanyi Wang on 3/30/20.
//

#ifndef PROJECT3_UTILS_H
#define PROJECT3_UTILS_H

#include "global.h"

#define NO_NEIGHBOR_FLAG 0xffff
#define PINGPONG_PACKET_SIZE 12
#define PAYLOAD_POS 8

struct PortEntry {
    uint16_t direct_neighbor_id;
    unsigned int cost;
    unsigned int last_update_time;
    bool isConnected;
};

struct DirectNeighborEntry {
    uint16_t port_num;
    uint16_t router_id;
    unsigned int cost;
};

struct DVEntry {
    uint16_t next_hop;
    unsigned int cost;
};

struct ForwardTableEntry {
    uint16_t next_router_id;

    ForwardTableEntry(uint16_t router_id) {
        this->next_router_id = router_id;
    }
};

#endif //PROJECT3_UTILS_H

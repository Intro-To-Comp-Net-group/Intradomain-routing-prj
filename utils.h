//
// Created by Hanyi Wang on 3/30/20.
//

#ifndef PROJECT3_UTILS_H
#define PROJECT3_UTILS_H

#include "global.h"
#include <set>

#define NO_NEIGHBOR_FLAG 0xffff
#define PINGPONG_PACKET_SIZE 12
#define PAYLOAD_POS 8
#define LS_PAYLOAD_POS 12
#define SECOND 1000

#define FLOOD_ALL_FLAG -1

enum eAlarmType {
    PINGPONG_ALARM,
    DV_UPDATE_ALARM,
    LS_UPDATE_ALARM,
    EXPIRE_ALARM
};

struct PortEntry {
    uint16_t direct_neighbor_id;
    unsigned int cost;
    unsigned int last_update_time;
    bool isConnected;
};

struct DirectNeighborEntry {
    uint16_t port_num;
//    uint16_t router_id;
    unsigned int cost;
};

struct DVEntry {
    uint16_t next_hop;
    unsigned int cost;
    unsigned int last_update_time;
};

struct ForwardTableEntry {
    uint16_t next_router_id;
};

struct LSEntry {
    unsigned int cost;
    unsigned int last_update_time;
};


#endif //PROJECT3_UTILS_H

//
// Created by Hanyi Wang on 3/30/20.
//

#ifndef PROJECT3_UTILS_H
#define PROJECT3_UTILS_H
#include "global.h"

#define PINGPONG_PACKET_SIZE 12

struct PortEntry {
    uint16_t dest_port;
    unsigned int cost;
    unsigned int last_update_time;
};


#endif //PROJECT3_UTILS_H

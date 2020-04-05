#include "RoutingProtocolImpl.h"

RoutingProtocolImpl::RoutingProtocolImpl(Node *n) : RoutingProtocol(n) {
    sys = n;
    // add your own code
    alarmHandler = new AlarmHandler();

//    pingpong_alarm_data = (eAlarmType *) malloc(sizeof(char));
//    expire_alarm_data = (eAlarmType *) malloc(sizeof(char));
//    dv_update_alarm_data = (eAlarmType *) malloc(sizeof(char));
//    ls_update_alarm_data = (eAlarmType *) malloc(sizeof(char));
}

RoutingProtocolImpl::~RoutingProtocolImpl() {
    // add your own code (if needed)
    delete alarmHandler;
}

void RoutingProtocolImpl::init(unsigned short num_ports, unsigned short router_id, eProtocolType protocol_type) {
    // add your own code
    this->num_ports = num_ports;
    this->router_id = router_id;
    this->packet_type = protocol_type;

    // Iterate through num_ports to set ports
    for (int i = 0; i < num_ports; i++) {
        PortEntry port;
        port.cost = INFINITY_COST;
        port.direct_neighbor_id = NO_NEIGHBOR_FLAG;
        port.last_update_time = 0;
//        port.isConnected = false;
        port_graph.push_back(port);
    }
    init_pingpong();

    alarmHandler->init_alarm(sys,this);
//    sys->set_alarm(this, 10000, (void*) pingpong_alarm_data);
//    sys->set_alarm(this, 30000, (void*) dv_update_alarm_data);
//    sys->set_alarm(this, 1000, (void*) expire_alarm_data);
}

void RoutingProtocolImpl::handle_alarm(void *data) {
    // add your own code
    eAlarmType alarm_type = *(eAlarmType *) data;

    if (alarm_type == PINGPONG_ALARM) {
        init_pingpong();
        sys->set_alarm(this, 10 * SECOND, data);
    } else if (alarm_type == EXPIRE_ALARM) {
        if (packet_type == P_DV) {
            //TODO: handle DV expire

        } else if (packet_type == P_LS) {
            // TODO: handle LS expire
        }
        sys->set_alarm(this, 1 * SECOND, data);
    } else if (alarm_type == DV_UPDATE_ALARM) {
        if (packet_type == P_DV) {
            //TODO: handle DV_update_alarm

        }
        sys->set_alarm(this, 30 * SECOND, data);
    } else if (alarm_type == LS_UPDATE_ALARM) {
        if (packet_type == P_LS) {
            //TODO: handle ls_update_alarm

        }
        sys->set_alarm(this, 30 * SECOND, data);
    } else {
        cout << "Alarm type not acceptable. " << endl;
        exit(1);
    }
}

void RoutingProtocolImpl::recv(unsigned short port, void *packet, unsigned short size) {
    // add your own code
    char *recv_packet = (char *) packet;
    char recv_pkt_type = *(char *) recv_packet;

    if (recv_pkt_type == DATA) {
        recv_data(port, packet, size);
    } else if (recv_pkt_type == PING) {
        recv_ping_packet(port, packet, size);
    } else if (recv_pkt_type == PONG) {
        recv_pong_packet(port, packet, size);
    } else if (recv_pkt_type == DV) {
//        recv_dv_packet(port, packet, size);
    } else if (recv_pkt_type == LS) {
//        recv_ls_packet(port, packet, size);
    }
}

void RoutingProtocolImpl::recv_ping_packet(unsigned short port, void *packet, unsigned short size) {
    char *pong_to_send = (char *) packet;
    uint16_t from_port_id = ntohs(*(uint16_t *) (pong_to_send + 4));
    unsigned int recv_time = ntohl(*(unsigned int *) (pong_to_send + 8));
    // Send back pong packet
    *(char *) pong_to_send = PONG;
    *(uint16_t *) (pong_to_send + 2) = htons(12);
    *(uint16_t *) (pong_to_send + 4) = htons(this->router_id);
    *(uint16_t *) (pong_to_send + 6) = htons(from_port_id);
    *(unsigned int *) (pong_to_send + 8) = htonl(recv_time);
    sys->send(port, pong_to_send, PINGPONG_PACKET_SIZE);
}

void RoutingProtocolImpl::recv_pong_packet(unsigned short port, void *packet, unsigned short size) {
    char *recv_packet = (char *) packet;

    // Get rtt: recv_timestamp is the timestamp where PING sent, curr - get_time measure the RTT.
    unsigned int current_time = sys->time();
    unsigned int get_time = ntohl(*(unsigned int *) (recv_packet + 8));
    unsigned int rtt = current_time - get_time;

    // DEBUG FLAG
    cout << endl;
    cout << "RTT IS: "<< rtt << " Current time is " << current_time << " recv_time "<< get_time << endl;
    for (auto entryP: DV_table) {
        auto dest_id = entryP.first;
        DVEntry entry = entryP.second;
        cout << "For router "<<router_id << " DEST NODE ID: "<< dest_id << " COST: " << entry.cost << " NEXTHOP: " << entry.next_hop << " ";
    }
    cout << endl;
    // DEBUG FLAG END


    uint16_t sourceRouterID = ntohs(*(uint16_t *) (recv_packet + 4));
    bool isConnected = port_graph[port].direct_neighbor_id != NO_NEIGHBOR_FLAG;
    port_graph[port].direct_neighbor_id = sourceRouterID;
    port_graph[port].last_update_time = current_time;
//    unsigned int prev_cost = port_graph[port].cost;
    port_graph[port].cost = rtt;    // update cost

    // Update direct_neighbor_map
    bool sourceRouterInMap = direct_neighbor_map.count(sourceRouterID) != 0;
    if (!sourceRouterInMap) {
        DirectNeighborEntry dn;
        dn.cost = rtt;
//        dn.router_id = sourceRouterID;
        dn.port_num = port;
        direct_neighbor_map[sourceRouterID] = dn;
        // Create forwarding entry and DV entry if not exists
        bool hasCreateEntry = createEntryIfNotExists(sourceRouterID, rtt);
        if (hasCreateEntry) {
            send_dv_packet();
            return;
        }
    } else {
        DirectNeighborEntry *dn = &direct_neighbor_map[sourceRouterID];
        dn->port_num = port;
//        dn->router_id = sourceRouterID;
        unsigned int prev_cost = dn->cost;
        dn->cost = rtt;

        if (isConnected) {
//            unsigned int cost_update = (rtt - prev_cost);
            int cost_update = rtt - prev_cost;
            if (cost_update == 0) { // No change
                // Do nothing
//                cout << "No change in cost, just update the time directly related to sourceID in DV_table" << endl;
                for (auto &it_pair : DV_table) {
                    uint16_t dest_id = it_pair.first;
                    DVEntry &dv_entry = it_pair.second;
                    if (sourceRouterID == dv_entry.next_hop || sourceRouterID == dest_id)
                        dv_entry.last_update_time = sys->time();
                }
            } else if (cost_update < 0) {
                for (auto &it_pair : DV_table) {
                    uint16_t dest_id = it_pair.first;
                    DVEntry &dv_entry = it_pair.second;
                    if (dv_entry.next_hop == sourceRouterID) {
                        // 本来就是最短的，变短了说明routing不变，更新DV table的cost即可
                        dv_entry.cost = rtt;
                    } else if (dest_id == sourceRouterID && rtt < dv_entry.cost) {
                        // 这里dest_id即为一个direct neighbor（sourceRouterID），但是通过DVtable是绕道走的（而非直接到达），如果rtt<dvtable的最优路径，则选用直接到达的路径
                        dv_entry.cost = rtt;
                        dv_entry.next_hop = sourceRouterID;

//                        ForwardTableEntry newFWDEntry(sourceRouterID);
                        ForwardTableEntry newFWDEntry;
                        newFWDEntry.next_router_id = sourceRouterID;
                        forward_table[dest_id] = newFWDEntry;
                    }
                    dv_entry.last_update_time = sys->time();
                }
                send_dv_packet();
            } else {
                for (auto &it_pair : DV_table) {
                    uint16_t dest_id = it_pair.first;
                    DVEntry &dv_entry = it_pair.second;
                    if (dv_entry.next_hop ==
                        sourceRouterID) {  // The source is a next_hop of some destinations in DV_table
                        unsigned int rtt2 = cost_update + dv_entry.cost;
                        // If a direct neighbor is better (direct_neighbor[dest_id].cost < dv_entry.cost + cost_update)
                        unsigned int direct_neighbor_cost = direct_neighbor_map[dest_id].cost;
                        if (direct_neighbor_cost < rtt2) {
                            dv_entry.next_hop = dest_id;
                            dv_entry.cost = direct_neighbor_cost;

//                            ForwardTableEntry newFWDEntry(dest_id);
                            ForwardTableEntry newFWDEntry;
                            newFWDEntry.next_router_id = dest_id;
                            forward_table[dest_id] = newFWDEntry;
                        } else {    // Else: update cost but still use the current route
                            dv_entry.cost = rtt2;
                        }
                    } else if (dest_id == sourceRouterID && rtt < dv_entry.cost) {
                        dv_entry.cost = rtt;
                        dv_entry.next_hop = sourceRouterID;

//                        ForwardTableEntry newFWDEntry(sourceRouterID);
                        ForwardTableEntry newFWDEntry;
                        newFWDEntry.next_router_id = sourceRouterID;
                        forward_table[dest_id] = newFWDEntry;
                    }
                    dv_entry.last_update_time = sys->time();
                }
                send_dv_packet();
            }
        } else {
            if (rtt < DV_table[sourceRouterID].cost) {  // If direct_neighbor is better
                DVEntry *dv_entry = &DV_table[sourceRouterID];
                dv_entry->cost = rtt;
                dv_entry->last_update_time = sys->time();
                send_dv_packet();
            }
        }

    }
}

void RoutingProtocolImpl::recv_data(unsigned short port, void *packet, unsigned short size) {
    char *data_to_flood = (char *) packet;
    uint16_t dest_router_id = ntohs(*(uint16_t *) (data_to_flood + 6));

    // If originating from this router
    if (port == SPECIAL_PORT) {
        *(uint16_t *) (data_to_flood + 4) = router_id;
    }

    // If we reach the destination
    if (dest_router_id == router_id) {
        free(packet);
        return;
    } else {
        if (forward_table.find(dest_router_id) != forward_table.end()) {
            uint16_t next_router = forward_table[dest_router_id].next_router_id;
            DirectNeighborEntry next_router_entry = direct_neighbor_map[next_router];
            uint16_t out_port = next_router_entry.port_num;
            unsigned int link_cost = next_router_entry.cost;
            if (link_cost == INFINITY_COST) return;
            else sys->send(out_port, data_to_flood, size);
        } else return;
    }
}


void RoutingProtocolImpl::recv_dv_packet(unsigned short port, void *packet, unsigned short size) {
    char *dv_packet = (char *) packet;
    uint16_t packet_size = ntohs(*(uint16_t *) (dv_packet + 2));
    uint16_t fromRouterID = ntohs(*(uint16_t *) (dv_packet + 4));
    uint16_t toRouterID = ntohs(*(uint16_t *) (dv_packet + 6));

    // Parse DV_table to get a vector of pairs
    uint16_t dv_map_size = (packet_size - PAYLOAD_POS) / 4;
    vector<pair<uint16_t, uint16_t>> dv_entry_vec;
    for (int i = 0; i < dv_map_size; i++) {
        uint16_t node_id = *(uint16_t *) (dv_packet + PAYLOAD_POS + 4 * i);
        uint16_t cost = *(uint16_t *) (dv_packet + PAYLOAD_POS + 4 * i + 2);
        pair<uint16_t, uint16_t> dv_entry;
        dv_entry.first = node_id;
        dv_entry.second = cost;
        dv_entry_vec.push_back(dv_entry);
    }

    // Create neighbor entry if not exists
    bool findNeighbor = direct_neighbor_map.count(fromRouterID) != 0;
//    if (!findNeighbor) {
//        DirectNeighborEntry entry;
//        unsigned int cost_to_fill;
//        for (auto dv_pair: dv_entry_vec) {
//            uint16_t dest_id = dv_pair.first;
//            uint16_t cost = dv_pair.second;
//            if (dest_id == router_id) {
//                cost_to_fill = cost;
//            }
//        }
//        entry.port_num = port;
//        entry.router_id = router_id;
//        entry.cost = cost_to_fill;
//
//        direct_neighbor_map[fromRouterID] = entry;
//    }

    if (!findNeighbor) return;
    else {
        for (pair<uint16_t, uint16_t> recv_pair: dv_entry_vec) {
            uint16_t node_id = recv_pair.first;
            uint16_t cost = recv_pair.second;
            if (node_id == this->router_id) continue;   // Itself!
            else if (DV_table.count(node_id) == 0) {    // node_id not exists in DV_Table
                // Just add it
                if (cost != INFINITY_COST) {
                    unsigned int cost_to_source = DV_table[fromRouterID].cost;
                    unsigned int new_route_cost = cost + cost_to_source;

                    DVEntry dv_entry;
                    dv_entry.cost = new_route_cost;
                    dv_entry.next_hop = fromRouterID;
                    dv_entry.last_update_time = sys->time();
                    DV_table[node_id] = dv_entry;

//                    ForwardTableEntry fwd_entry(fromRouterID);
                    ForwardTableEntry fwd_entry;
                    fwd_entry.next_router_id = fromRouterID;
                    forward_table[node_id] = fwd_entry;
                }
            } else {    // node_id is in the DV_table
                if (cost != INFINITY_COST) {
                    // Update DV_table if the new route is better
                    unsigned int cost_to_source = DV_table[fromRouterID].cost;
                    unsigned int new_route_cost = cost_to_source + cost;
                    unsigned int old_route_cost = DV_table[node_id].cost;
                    if (new_route_cost < old_route_cost) {
                        DV_table[node_id].cost = new_route_cost;
                        DV_table[node_id].next_hop = fromRouterID;

//                        ForwardTableEntry fwd_entry(fromRouterID);
                        ForwardTableEntry fwd_entry;
                        fwd_entry.next_router_id = fromRouterID;
                        forward_table[node_id] = fwd_entry;
                    }
                    DV_table[node_id].last_update_time = sys->time();
                } else {    // INFINITY COST, poison reverse
                    DV_table[node_id].last_update_time = sys->time();
                }

            }
        }
        send_dv_packet();
    }
}


void RoutingProtocolImpl::recv_ls_packet(unsigned short port, void *packet, unsigned short size) {

}


//************************************************************************************************//
//************************************************************************************************//
// HELPER FUNCTION AREA
//************************************************************************************************//
//************************************************************************************************//

void RoutingProtocolImpl::init_pingpong() {
    for (int i = 0; i < this->num_ports; i++) {
        char *ping_packet = (char *) malloc(PINGPONG_PACKET_SIZE * sizeof(char));
        *(char *) ping_packet = PING;
        *(uint16_t *) (ping_packet + 2) = htons(12);
        *(uint16_t *) (ping_packet + 4) = htons(this->router_id);
        *(unsigned int *) (ping_packet + 8) = htonl(sys->time());
        cout << "PING: send_Time: " << sys->time() <<endl;
        sys->send(i, ping_packet, PINGPONG_PACKET_SIZE);
    }
}

bool RoutingProtocolImpl::createEntryIfNotExists(uint16_t sourceID, unsigned int cost) {
    // Search through forwarding_table, if not exists, update DV_table and fwd_table
    bool entryExists = false;
    if (forward_table.count(sourceID) != 0) {
        entryExists = true;
    }
    if (entryExists) return false;
    else {
        // Forwarding table
//        ForwardTableEntry fwdEntry(sourceID);
        ForwardTableEntry fwdEntry;
        fwdEntry.next_router_id = sourceID;
        forward_table[sourceID] = fwdEntry;

        // DV table
        DVEntry dvEntry;
        dvEntry.cost = cost;
        dvEntry.next_hop = sourceID;
        DV_table[sourceID] = dvEntry;

        return true;
    }
}

void RoutingProtocolImpl::send_dv_packet() {
    vector<uint16_t> dest_to_send;
    uint16_t vec_size = 0;
    for (auto dv_pair: DV_table) {
        uint16_t dest_id = dv_pair.first;
        auto entry = dv_pair.second;
//        if (entry.cost != INFINITY_COST) {
        vec_size += 1;
        dest_to_send.push_back(dest_id);
//        }
    }
    uint16_t send_size = vec_size * 4 + PAYLOAD_POS;

    for (uint16_t i = 0; i < num_ports; i++) {
        PortEntry port = port_graph[i];
        if (port.cost != INFINITY_COST && port.direct_neighbor_id != NO_NEIGHBOR_FLAG) {
            uint16_t dest_router_id = port.direct_neighbor_id;
            char *dv_packet = (char *) malloc(send_size * sizeof(char));
            *(ePacketType *) dv_packet = DV;
            *(uint16_t *) (dv_packet + 2) = htons(send_size);
            *(uint16_t *) (dv_packet + 4) = htons(this->router_id);
            *(uint16_t *) (dv_packet + 6) = htons(dest_router_id);

            int pos = PAYLOAD_POS;
            for (uint16_t j = 0; j < vec_size; j++) {
                uint16_t dest_id = dest_to_send[j];
                unsigned int cost = DV_table[dest_id].cost;

                *(uint16_t *) (dv_packet + pos) = htons(dest_id);
                // Poison reverse
//                auto direct_neighbor_entry = direct_neighbor_map[dest_id];
//                cost = (direct_neighbor_entry.port_num == i) ? INFINITY_COST : cost;
                cost = (dest_router_id == DV_table[dest_id].next_hop) ? INFINITY_COST: cost;    // 当dest_router_id = entry.nextHop, CHANGE TO INFINITY_COST
                *(uint16_t *) (dv_packet + pos + 2) = htons((uint16_t) cost);

                pos += 4;
            }

            sys->send(i, dv_packet, send_size);
        }
    }
}


// add more of your own code

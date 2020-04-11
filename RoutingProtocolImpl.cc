#include "RoutingProtocolImpl.h"

RoutingProtocolImpl::RoutingProtocolImpl(Node *n) : RoutingProtocol(n) {
    sys = n;
    // add your own code
    alarmHandler = new AlarmHandler();
    EMPTY_PACKET = malloc(sizeof(char));
}

RoutingProtocolImpl::~RoutingProtocolImpl() {
    // add your own code (if needed)
    delete alarmHandler;
    free(EMPTY_PACKET);
}

void RoutingProtocolImpl::init(unsigned short num_ports, unsigned short router_id, eProtocolType protocol_type) {
    // add your own code
    this->num_ports = num_ports;
    this->router_id = router_id;
    this->packet_type = protocol_type;

    this->seq_num = 0;

    // Iterate through num_ports to set ports
    for (int i = 0; i < num_ports; i++) {
        PortEntry port;
        port.cost = INFINITY_COST;
        port.direct_neighbor_id = NO_NEIGHBOR_FLAG;
        port.last_update_time = 0;
        port.isConnected = false;
        port_graph.push_back(port);
    }
    init_pingpong();
    alarmHandler->init_alarm(sys, this, protocol_type);
}

void RoutingProtocolImpl::handle_alarm(void *data) {
    // add your own code
    eAlarmType alarm_type = *(eAlarmType *) data;
    cout << "alarm type: " << alarm_type << endl;
    if (alarm_type == PINGPONG_ALARM) {
        init_pingpong();
        sys->set_alarm(this, 10 * SECOND, data);
    } else if (alarm_type == EXPIRE_ALARM) {
        if (packet_type == P_DV) {
            handle_dv_expire();
        } else if (packet_type == P_LS) {
//            handle_ls_expire();
        }
        sys->set_alarm(this, 1 * SECOND, data);
    } else if (alarm_type == DV_UPDATE_ALARM) {
        send_dv_packet();
        sys->set_alarm(this, 30 * SECOND, data);
    } else if (alarm_type == LS_UPDATE_ALARM) {
        flood_ls_packet(true, FLOOD_ALL_FLAG, EMPTY_PACKET, -1);
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
        recv_dv_packet(port, packet, size);
    } else if (recv_pkt_type == LS) {
        recv_ls_packet(port, packet, size);
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
    *(unsigned int *) (pong_to_send + 8) = htonl((unsigned int) recv_time);
    sys->send(port, pong_to_send, PINGPONG_PACKET_SIZE);
}


void RoutingProtocolImpl::recv_pong_packet(unsigned short port, void *packet, unsigned short size) {
    char *recv_packet = (char *) packet;
    // Get rtt: recv_timestamp is the timestamp where PING sent, curr - get_time measure the RTT.
    unsigned int current_time = sys->time();
    unsigned int get_time = ntohl(*(unsigned int *) (recv_packet + 8));
    uint16_t rtt = current_time - get_time;
    uint16_t sourceRouterID = ntohs(*(uint16_t *) (recv_packet + 4));
//    bool isConnected = port_graph[port].direct_neighbor_id != NO_NEIGHBOR_FLAG;
//    bool isConnected = port_graph[port].isConnected;
    port_graph[port].isConnected = true;
    port_graph[port].direct_neighbor_id = sourceRouterID;
    port_graph[port].last_update_time = current_time;
    port_graph[port].cost = rtt;    // update cost
    bool sourceRouterInMap = direct_neighbor_map.count(sourceRouterID) != 0;
//    bool table_changed = false;

    if (packet_type == P_DV) {
        if (!sourceRouterInMap) {
            // not in direct neighbor
            // insert intp neighbor directly
            insert_neighbor(sourceRouterID, rtt, port);
            if (DV_table.count(sourceRouterID) == 0) {
                // also not exists in  DV ..insert into DV
                insert_DV(sourceRouterID, direct_neighbor_map[sourceRouterID].cost, sourceRouterID);
                insert_forward(sourceRouterID, sourceRouterID);
            } else {
                if (direct_neighbor_map[sourceRouterID].cost < DV_table[sourceRouterID].cost) {
                    // exist in DV but  current is smaller
                    update_DV(sourceRouterID, direct_neighbor_map[sourceRouterID].cost, sourceRouterID);
                    update_forward(sourceRouterID, sourceRouterID);
                }
            }
        } else {
            // we have DV and neighbor
            DirectNeighborEntry *dn = &direct_neighbor_map[sourceRouterID];
            int prev_cost = dn->cost;
            update_neighbor(sourceRouterID, rtt, port);
            int cur_cost = dn->cost;
            int diff = cur_cost - prev_cost;
            if (diff != 0) {
                for (auto it = DV_table.begin(); it != DV_table.end(); ++it) {
                    unsigned int new_cost = diff + it->second.cost;
                    if (it->second.next_hop == sourceRouterID) {
                        // the nodes pass the source
                        if (direct_neighbor_map.count(it->first) != 0) {
                            // in direct neighbor
                            if (direct_neighbor_map[it->first].cost < diff) {
                                update_DV(it->first, direct_neighbor_map[it->first].cost, it->first);
                                update_forward(it->first, it->first);
                            } else {
                                // upodate the cost
                                update_DV(it->first, new_cost, it->second.next_hop);
                            }
                        }
                    } else if (it->first == sourceRouterID && new_cost < DV_table[sourceRouterID].cost) {
                        update_DV(sourceRouterID, new_cost, sourceRouterID);
                        update_forward(sourceRouterID, sourceRouterID);
                    }
                }
            }
        }
        send_dv_packet();
    } else if (packet_type == P_LS) {

        cout << endl;
        cout << "BEFORE: RECV_PONG: FROM source: " << sourceRouterID << endl;
        printLSTable();

        if (!sourceRouterInMap) {   // Not in DirectNeighborMap
            cout <<"SOURCE " << sourceRouterID << " NOT IN MAP" <<endl;
            insert_neighbor(sourceRouterID, rtt, port);
            if (check_link_in_LSTable(router_id, sourceRouterID) == false) {
                cout << "haha" <<endl;
                insert_LS(router_id, sourceRouterID, rtt);
                insert_forward(sourceRouterID, sourceRouterID);
                // Dijkstra()
                flood_ls_packet(true, FLOOD_ALL_FLAG, EMPTY_PACKET, size);
            }
        } else {    // In DirectNeighbor
            DirectNeighborEntry *dn = &direct_neighbor_map[sourceRouterID];
            int prev_cost = dn->cost;
            update_neighbor(sourceRouterID, rtt, port);
            int cur_cost = dn->cost;
            int diff = cur_cost - prev_cost;
            if (diff != 0) {
                update_LS(router_id, sourceRouterID, cur_cost);
                // Dijkstra()
                flood_ls_packet(true, FLOOD_ALL_FLAG, EMPTY_PACKET, size);
            }
        }
        cout << endl;
        cout << "AFTER: RECV_PONG: FROM source: " << sourceRouterID << endl;
        printLSTable();
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
    bool table_changed = false;
    char *dv_packet = (char *) packet;
    uint16_t packet_size = ntohs(*(uint16_t *) (dv_packet + 2));
    uint16_t fromRouterID = ntohs(*(uint16_t *) (dv_packet + 4));
//    uint16_t toRouterID = ntohs(*(uint16_t *) (dv_packet + 6));

    // Parse DV_table to get a vector of pairs
    uint16_t dv_map_size = (packet_size - PAYLOAD_POS) / 4;
    vector<pair<uint16_t, uint16_t>> dv_entry_vec;
    for (int i = 0; i < dv_map_size; i++) {
        uint16_t node_id = ntohs(*(uint16_t *) (dv_packet + PAYLOAD_POS + 4 * i));
        uint16_t cost = ntohs(*(uint16_t *) (dv_packet + PAYLOAD_POS + 4 * i + 2));
        pair<uint16_t, uint16_t> dv_entry;
        dv_entry.first = node_id;
        dv_entry.second = cost;
        dv_entry_vec.push_back(dv_entry);
    }

    // recalculate the dis between curNode and sourceNode, if it does not exists in neighbor
    bool isInDirectNeighbor = direct_neighbor_map.count(fromRouterID) > 0;
    if (!isInDirectNeighbor) {
        for (pair<uint16_t, uint16_t> recv_pair: dv_entry_vec) {
            uint16_t dest_id = recv_pair.first;
            uint16_t cost = recv_pair.second;
            if (dest_id == this->router_id) {
                insert_neighbor(fromRouterID, cost, port);
            }
        }
    }

    bool isInDVTable = DV_table.count(fromRouterID) > 0;
    if (!isInDVTable) {
        int neigh_cost = direct_neighbor_map[fromRouterID].cost;
        insert_DV(fromRouterID, neigh_cost, fromRouterID);
        insert_forward(fromRouterID, fromRouterID);
        table_changed = true;
    } else {
        unsigned int old_cost = DV_table[fromRouterID].cost;
        unsigned int direct_cost = direct_neighbor_map[fromRouterID].cost;
        if (direct_cost < old_cost) {
            update_DV(fromRouterID, direct_cost, fromRouterID);
            update_forward(fromRouterID, fromRouterID);
            table_changed = true;
        }
    }

    unsigned int cost_to_fromRouter = DV_table[fromRouterID].cost;
    for (pair<uint16_t, uint16_t> recv_pair: dv_entry_vec) {
        uint16_t dest_id = recv_pair.first;
        unsigned int recv_cost = recv_pair.second;
        if (recv_cost == INFINITY_COST) continue;   // Ignore if receive INFINITY

        if (dest_id == router_id) continue;     // Ignore if goes to itself
        // dest_id does not exist Just add new entry
        if (DV_table.count(dest_id) == 0) {
            table_changed = true;
            unsigned int new_cost = recv_cost + cost_to_fromRouter;
            insert_DV(dest_id, new_cost, fromRouterID);
            insert_forward(dest_id, fromRouterID);
        } else {
            // dest_id exist
            unsigned int old_cost = DV_table[dest_id].cost;
            unsigned int new_cost = cost_to_fromRouter + recv_cost;
            if (new_cost < old_cost) {
                table_changed = true;
                insert_DV(dest_id, new_cost, fromRouterID);
                insert_forward(dest_id, fromRouterID);
            }
        }
    }
    if (table_changed) {
//        printNeighborTable();
//        printDVTable();
        send_dv_packet();
    }
}


void RoutingProtocolImpl::recv_ls_packet(unsigned short port, void *packet, unsigned short size) {

    bool hasChange = false;
    char * recv_packet = (char *) packet;
    uint16_t sourceRouterID = ntohs(*(uint16_t *) (recv_packet + 4));

    uint32_t seq_num_send = ntohl(*(uint32_t *) (recv_packet + 8));
    if (router_id == sourceRouterID && seq_num_send <= this->seq_num) {
        free(packet);
        return;
    }
    pair<uint16_t, uint32_t> curr_pair = make_pair(sourceRouterID, seq_num_send);
    if (haveSeenSet.count(curr_pair) > 0) {    // have recv packet before
        free(packet);
        return;
    }
    // 1. put seen pair into the SET
    haveSeenSet.insert(curr_pair);
    vector<pair<uint16_t, uint16_t>> recv_ls_list;
    unsigned short num_entry = (size - LS_PAYLOAD_POS) / 4;
    for (int i = 0; i < num_entry; i++) {
        uint16_t node_id = ntohs(*(uint16_t *) (recv_packet + LS_PAYLOAD_POS + 4 * i));
        uint16_t cost = ntohs(*(uint16_t *) (recv_packet + LS_PAYLOAD_POS + 4 * i + 2));
        auto curr_node_cost_pair = make_pair(node_id, cost);
        recv_ls_list.push_back(curr_node_cost_pair);
    }
    // 2. Update LS table
    // 对于收到的每一个entry，首先判断在不在LS TABLE里面：如果不在，插入；如果在，更新，之后Dijkstra
    for (auto &pair: recv_ls_list) {
        uint16_t dest_id = pair.first;
        uint16_t cost = pair.second;
        if (router_id == dest_id) continue;
        if (check_link_in_LSTable(sourceRouterID, dest_id) == false) { // received entry not in my LS table, add it!
            hasChange = true;
            insert_LS(sourceRouterID,dest_id,cost);

        } else {    // update it!
            uint16_t old_cost = LS_table[dest_id][router_id].cost;
            if (cost != old_cost) {
                hasChange = true;
                update_LS(sourceRouterID, dest_id, cost);
            }
//            else update_LS(sourceRouterID, dest_id, cost);    // TODO: Do we need to update time when there's no change? If no, delete this line
        }
    }

    // 3. flood my LS packet, and re-transmit(flood) others' packets
    flood_ls_packet(false, port, recv_packet, size);  // flooding re-transmit
//    if (hasChange) {
//        flood_ls_packet(true, FLOOD_ALL_FLAG, EMPTY_PACKET, size);
//    }
    // Finally, free this packet since it's been re-transmitted
    free(packet);

    cout << "RECV_LS: FROM source: " << sourceRouterID << endl;
    printLSTable();
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
        sys->send(i, ping_packet, PINGPONG_PACKET_SIZE);
    }
}


void RoutingProtocolImpl::send_dv_packet() {
    vector<uint16_t> dest_to_send;
    uint16_t vec_size = 0;
    for (auto dv_pair: DV_table) {
        uint16_t dest_id = dv_pair.first;
        vec_size += 1;
        dest_to_send.push_back(dest_id);
    }
    uint16_t send_size = vec_size * 4 + PAYLOAD_POS;

    for (uint16_t i = 0; i < num_ports; i++) {
        PortEntry port = port_graph[i];
//        if (port.cost != INFINITY_COST && port.direct_neighbor_id != NO_NEIGHBOR_FLAG) {
        if (port.isConnected) {
            uint16_t dest_router_id = port.direct_neighbor_id;
            char *dv_packet = (char *) malloc(send_size * sizeof(char));
            *(ePacketType *) dv_packet = DV;
            *(uint16_t *) (dv_packet + 2) = htons(send_size);
            *(uint16_t *) (dv_packet + 4) = htons(this->router_id);
            *(uint16_t *) (dv_packet + 6) = htons(dest_router_id);

            int pos = PAYLOAD_POS;
            for (uint16_t j = 0; j < vec_size; j++) {
                uint16_t dest_id = dest_to_send[j];
                uint16_t cost = DV_table[dest_id].cost;
                *(uint16_t *) (dv_packet + pos) = htons(dest_id);
                // Poison reverse
                if (DV_table[dest_id].next_hop == dest_router_id || dest_id == dest_router_id) {
                    cost = INFINITY_COST;
                }
//                cost = ( ) ? : cost;    // 当dest_router_id = entry.nextHop, CHANGE TO INFINITY_COST
                *(uint16_t *) (dv_packet + pos + 2) = htons((uint16_t) cost);
                pos += 4;
            }
            sys->send(i, dv_packet, send_size);
        }
    }
}

void RoutingProtocolImpl::handle_port_expire() {
    // Iterate through ports, disconnect some ports, remove entries in DVtable and DirectNeighbor Table
    // remove all entries in DVtable whose next_hop is ports.to
    // remove all entries in directNeighborTable connected to that expire port

    vector<uint16_t> remove_list;

    for (int i = 0; i < num_ports; i++) {
        PortEntry &port = port_graph[i];
        unsigned int time_lag = sys->time() - port.last_update_time;
        if (time_lag > 15 * SECOND && port.direct_neighbor_id != NO_NEIGHBOR_FLAG) {
            cout << "route_id: " << router_id << "port: " << i << " expires ";
            port.isConnected = false;
            port.cost = INFINITY_COST;
            uint16_t connected_router = port.direct_neighbor_id;
            port.direct_neighbor_id = NO_NEIGHBOR_FLAG;

            // remove direct neighbor entries connected with this port
            if (direct_neighbor_map.count(connected_router) > 0) {
                direct_neighbor_map.erase(connected_router);
            }

            // remove DV_table entries whose nextHop is connected_router and destination not in neighbor
            for (auto it = DV_table.begin(); it != DV_table.end(); it++) {
                uint16_t dest_id = it->first;
                auto &dv_entry = it->second;
                if (connected_router == dv_entry.next_hop) {    //find routers need to be reached by going to connected_router as next hop
                    bool notInDirectNeighbor = direct_neighbor_map.count(dest_id) == 0;
                    if (notInDirectNeighbor) { // delete if destination is not in direct_neighbor_map
                        remove_list.push_back(dest_id);
                    } else {
                        update_DV(dest_id, direct_neighbor_map[dest_id].cost, dest_id);
                    }
                }
            }
            port.direct_neighbor_id = NO_NEIGHBOR_FLAG;
        }
    }

    for (uint16_t dest_to_remove: remove_list) {
        DV_table.erase(dest_to_remove);
        forward_table.erase(dest_to_remove);
    }
}

void RoutingProtocolImpl::handle_dv_expire() {
    handle_port_expire();
    vector<uint16_t> remove_list;
    for (auto it = DV_table.begin(); it != DV_table.end(); ++it) {
        if (sys->time() - it->second.last_update_time > 45 * SECOND) {
            if (direct_neighbor_map.count(it->first) != 0) {
                update_DV(it->first, direct_neighbor_map[it->first].cost, it->first);
                update_forward(it->first, it->first);
            } else {
                remove_list.push_back(it->first);
            }
        }
    }
    for (uint16_t dest_to_remove: remove_list) {
        DV_table.erase(dest_to_remove);
        forward_table.erase(dest_to_remove);
    }
    send_dv_packet();
}


void RoutingProtocolImpl::printDVTable() {
    cout << endl;
    cout << "*********************************" << endl;
    cout << "This is DV table" << endl;
    cout << "Router ID: " << router_id << endl;
    cout << "*********************************" << endl;
    cout << "DestID\tcost\tnextHop\tupdateTime" << endl;
    cout << "*********************************" << endl;
    for (auto &entry: DV_table) {
        cout << entry.first << "\t" << entry.second.cost << "\t" << entry.second.next_hop << "\t"
             << entry.second.last_update_time << endl;
    }
    cout << "*********************************" << endl;
}

void RoutingProtocolImpl::printNeighborTable() {
    cout << endl;
    cout << "*********************************" << endl;
    cout << "This is neighbor table" << endl;
    cout << "Router ID: " << router_id << endl;
    cout << "*********************************" << endl;
    cout << "DestID\tcost" << endl;
    cout << "*********************************" << endl;
    for (auto &entry: direct_neighbor_map) {
        cout << entry.first << "\t" << entry.second.cost << endl;
    }
    cout << "*********************************" << endl;
}


void RoutingProtocolImpl::insert_neighbor(uint16_t neighbor_id, unsigned int cost, uint16_t port_num) {
    DirectNeighborEntry dn;
    dn.cost = cost;
    dn.port_num = port_num;
    direct_neighbor_map[neighbor_id] = dn;

}

void RoutingProtocolImpl::insert_DV(int16_t dest_id, unsigned int cost, uint16_t next_hop) {
    DVEntry dv_entry;
    dv_entry.cost = cost;
    dv_entry.next_hop = next_hop;
    dv_entry.last_update_time = sys->time();
    DV_table[dest_id] = dv_entry;
}

void RoutingProtocolImpl::insert_forward(uint16_t dest_id, uint16_t next_hop) {
    ForwardTableEntry fwd_entry;
    fwd_entry.next_router_id = next_hop;
    forward_table[dest_id] = fwd_entry;
}

void RoutingProtocolImpl::update_neighbor(uint16_t neighbor_id, unsigned int cost, uint16_t port_num) {
    DirectNeighborEntry *dn = &direct_neighbor_map[neighbor_id];
    dn->port_num = port_num;
//    unsigned int prev_cost = dn->cost;
    dn->cost = cost;
}

void RoutingProtocolImpl::update_DV(int16_t dest_id, unsigned int cost, uint16_t next_hop) {
    DVEntry *dv_entry = &DV_table[dest_id];;
    dv_entry->cost = cost;
    dv_entry->next_hop = next_hop;
    dv_entry->last_update_time = sys->time();
}

void RoutingProtocolImpl::update_forward(uint16_t dest_id, uint16_t next_hop) {
    ForwardTableEntry *fwd_entry = &forward_table[dest_id];
    fwd_entry->next_router_id = next_hop;
}

//
void RoutingProtocolImpl::flood_ls_packet(bool isSendMyLSP, uint16_t in_port_num, void * input_packet, int in_packet_size) {

    if (isSendMyLSP) {  // Sending my LSP
        // Update
        uint16_t packet_size;
        update_seq_num();
        for (int i = 0; i < num_ports; i++) {
            packet_size = direct_neighbor_map.size() * 4 + LS_PAYLOAD_POS;
            if (port_graph[i].isConnected && port_graph[i].direct_neighbor_id != NO_NEIGHBOR_FLAG) {
                char *packet = (char *) malloc(packet_size);
                *(ePacketType *) packet = LS;
                *(uint16_t *) (packet + 2) = htons((uint16_t)packet_size);
                *(uint16_t *) (packet + 4) = htons((uint16_t)router_id);
                *(uint32_t *) (packet + 8) = htonl((uint32_t)seq_num);
                int curr_pos = LS_PAYLOAD_POS;
                for (auto &pair: direct_neighbor_map) {
                    uint16_t dest_id = pair.first;
                    uint16_t cost = pair.second.cost;
                    *(uint16_t *) (packet + curr_pos) = htons(dest_id);
                    *(uint16_t *) (packet + curr_pos + 2) = htons((uint16_t)cost);
                    curr_pos += 4;
                }
                sys->send(i, packet, packet_size);
            }
        }
        //DEBUG
//        cout <<"WITH FLOODING" <<endl;
//        printNeighborTable();
//        cout << endl;
    } else {    // Re-transmit LSP
//        uint16_t packet_size;
        for (int i = 0; i < num_ports; i++) {
            if (i == in_port_num) continue; // Not flood to the port received packet
            if (port_graph[i].isConnected && port_graph[i].direct_neighbor_id != NO_NEIGHBOR_FLAG) {
                char * new_packet = (char *) malloc(in_packet_size + 1);
                memcpy(new_packet, input_packet, in_packet_size);
                sys->send(i, new_packet, in_packet_size);
            }
        }
    }
}


void RoutingProtocolImpl::insert_LS(int16_t dest_id, unsigned int cost) {
    // We know that dest_id not in this table
    if (LS_table.count(router_id)) {    // router id in LS_table
        auto &target_map = LS_table[router_id];
        struct LSEntry curr_entry1 = {cost, sys->time()};
        target_map[dest_id] = curr_entry1;

        unordered_map<uint16_t, LSEntry> sub_map;
        struct LSEntry curr_entry2 = {cost, sys->time()};
        sub_map[router_id] = curr_entry2;
        LS_table[dest_id] = sub_map;

    } else {    // router id not in LS_table
        unordered_map<uint16_t, LSEntry> sub_map1;
        struct LSEntry curr_entry1 = {cost, sys->time()};
        sub_map1[dest_id] = curr_entry1;
        LS_table[router_id] = sub_map1;

        unordered_map<uint16_t, LSEntry> sub_map2;
        struct LSEntry curr_entry2 = {cost, sys->time()};
        sub_map2[router_id] = curr_entry2;
        LS_table[dest_id] = sub_map2;
    }
}

void RoutingProtocolImpl::insert_LS(uint16_t source_id, uint16_t dest_id, unsigned int cost) {
    // We know that dest_id not in this table
//    if (LS_table.count(source_id)) {    // router id in LS_table
        auto &target_map1 = LS_table[source_id];
        struct LSEntry curr_entry1 = {cost, sys->time()};
        target_map1[dest_id] = curr_entry1;

//        if (LS_table.count(dest_id)) {
//            auto &target_map2 = LS_table[dest_id];
//            struct LSEntry curr_entry2 = {cost, sys->time()};
//            target_map2[source_id] = curr_entry2;
//        }

        auto &target_map2 = LS_table[dest_id];
        struct LSEntry curr_entry2 = {cost, sys->time()};
        target_map2[source_id] = curr_entry2;

//    } else {    // router id not in LS_table
//        unordered_map<uint16_t, LSEntry> sub_map1;
//        struct LSEntry curr_entry1 = {cost, sys->time()};
//        sub_map1[dest_id] = curr_entry1;
//        LS_table[source_id] = sub_map1;
//
//        unordered_map<uint16_t, LSEntry> sub_map2;
//        struct LSEntry curr_entry2 = {cost, sys->time()};
//        sub_map2[source_id] = curr_entry2;
//        LS_table[dest_id] = sub_map2;
//    }
}


void RoutingProtocolImpl::update_LS(uint16_t dest_id, unsigned int cost) {
    struct LSEntry curr_entry = {cost, sys->time()};
    LS_table[router_id][dest_id] = curr_entry;
    LS_table[dest_id][router_id] = curr_entry;
}

void RoutingProtocolImpl::update_LS(uint16_t source_id, uint16_t dest_id, unsigned int cost) {
    struct LSEntry curr_entry = {cost, sys->time()};
    LS_table[source_id][dest_id] = curr_entry;
    LS_table[dest_id][source_id] = curr_entry;
}


void RoutingProtocolImpl::update_seq_num() {
    this->seq_num += 1;
}

void RoutingProtocolImpl::handle_ls_expire() {
    for (int i = 0; i < num_ports; i++) {
        PortEntry &port = port_graph[i];
        unsigned int time_lag = sys->time() - port.last_update_time;
        if (time_lag > 15 * SECOND && port.direct_neighbor_id != NO_NEIGHBOR_FLAG) {
            cout << "route_id: " << router_id << "port: " << i << " expires ";
            port.isConnected = false;
            port.cost = INFINITY_COST;
            uint16_t connected_router = port.direct_neighbor_id;
            port.direct_neighbor_id = NO_NEIGHBOR_FLAG;

            // remove direct neighbor entries connected with this port
            if (direct_neighbor_map.count(connected_router) > 0) {
                direct_neighbor_map.erase(connected_router);
            }
            // remove entries in Fwd table
            if (forward_table.count(connected_router) > 0) {
                forward_table.erase(connected_router);
            }
            // remove link in LS table
            remove_LS(router_id, connected_router);
        }
    }

    vector<pair<uint16_t, uint16_t>> delete_list;
    for (auto &super_entry: LS_table) {
        uint16_t node1_id = super_entry.first;
        auto &sub_map = super_entry.second;
        for (auto &sub_entry: sub_map) {
            uint16_t node2_id = sub_entry.first;
            LSEntry &ls_entry = sub_entry.second;
            if (sys->time() - ls_entry.last_update_time > 45 *SECOND) {
                delete_list.emplace_back(node1_id, node2_id);   // push_back(make_pair(node1_id, node2_id));
            }
        }
    }

    // check and delete link, beware of duplicates: eg. <a,b> and <b,a>
    for (pair<uint16_t, uint16_t> d_pair: delete_list) {
        if (check_link_in_LSTable(d_pair.first, d_pair.second)) {   // TODO DO WE NEED THIS CHECK???
            remove_LS(d_pair.first, d_pair.second);
        }
    }

//    Dijkstra()
    flood_ls_packet(true, FLOOD_ALL_FLAG, EMPTY_PACKET, -1);
}

void RoutingProtocolImpl::remove_LS(uint16_t node1_id, uint16_t node2_id) {
    if (LS_table.count(node2_id) > 0) {
        auto &target_sub_map = LS_table[node2_id];
        if (target_sub_map.count(node1_id) > 0) {
            target_sub_map.erase(node1_id);
        }
    }
    if (LS_table.count(node1_id) > 0) {
        auto &target_sub_map = LS_table[node1_id];
        if (target_sub_map.count(node2_id) > 0) {
            target_sub_map.erase(node2_id);
        }
    }
}

bool RoutingProtocolImpl::check_link_in_LSTable(uint16_t node1_id, uint16_t node2_id) { // TODO DO WE NEED THIS??
    bool check1 = false;
    bool check2 = false;
    if (LS_table.count(node2_id)) {
        auto &sub_map = LS_table[node2_id];
        if (sub_map.count(node1_id)) {
            check1 = true;
        }
    }

    if (LS_table.count(node1_id)) {
        auto &sub_map = LS_table[node1_id];
        if (sub_map.count(node2_id)) {
            check2 = true;
        }
    }
    return check1 && check2;    // OR &&?
}

void RoutingProtocolImpl::printLSTable() {
    cout << endl;
    cout << "*********************************" << endl;
    cout << "This is LS table" << endl;
    cout << "Router ID: " << router_id << endl;
    cout << "*********************************" << endl;
    for (auto &first_pair: LS_table) {
        uint16_t node1 = first_pair.first;
        auto &sub_map = first_pair.second;
        for (auto &second_pair: sub_map) {
            uint16_t node2 = second_pair.first;
            auto &entry = second_pair.second;
            cout << node1 << "->" << node2 << ": cost: "<< entry.cost << " time: " << entry.last_update_time << endl;
        }
    }
    cout << "*********************************" << endl;
    cout << endl;
}


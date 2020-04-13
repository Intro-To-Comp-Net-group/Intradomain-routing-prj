#include "RoutingProtocolImpl.h"

RoutingProtocolImpl::RoutingProtocolImpl(Node *n) : RoutingProtocol(n) {
    sys = n;
    this->num_ports = 0;
    this->router_id = 0;
    // add your own code
    alarmHandler = new AlarmHandler();
    EMPTY_PACKET = malloc(sizeof(char));
}

RoutingProtocolImpl::~RoutingProtocolImpl() {
    // add your own code (if needed)
    delete alarmHandler;
    free(EMPTY_PACKET);
}

void RoutingProtocolImpl::init(unsigned short in_num_ports, unsigned short in_router_id, eProtocolType protocol_type) {
    // add your own code
    this->num_ports = in_num_ports;
    this->router_id = in_router_id;
    this->packet_type = protocol_type;

    this->seq_num = 0;
    init_ports();
    init_pingpong();
    alarmHandler->init_alarm(sys, this, protocol_type);
}

void RoutingProtocolImpl::handle_alarm(void *data) {
    // add your own code
    eAlarmType alarm_type = *(eAlarmType *) data;
//    cout << "alarm type: " << alarm_type << endl;
    if (alarm_type == PINGPONG_ALARM) {
        init_pingpong();
        sys->set_alarm(this, 10 * SECOND, data);
    } else if (alarm_type == EXPIRE_ALARM) {
        if (packet_type == P_DV) {
            handle_dv_expire();
        } else if (packet_type == P_LS) {
            handle_ls_expire();
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
    }
}

void RoutingProtocolImpl::recv(unsigned short port, void *packet, unsigned short size) {
    // add your own code
    char *recv_packet = (char *) packet;
    char recv_pkt_type = *(ePacketType *) recv_packet;

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
    } else {
        cout << "Packet type not acceptable. " << endl;
    }
}

void RoutingProtocolImpl::recv_ping_packet(unsigned short port, void *packet, unsigned short size) {
    char *recv_ping = (char *) packet;
    uint16_t from_port_id = ntohs(*(uint16_t *) (recv_ping + 4));
    unsigned int recv_time = ntohl(*(unsigned int *) (recv_ping + 8));
    free(packet);

    // Send back pong packet
    char *pong_to_send = (char *) malloc(sizeof(char) * size);
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
    port_graph[port].direct_neighbor_id = sourceRouterID;
    port_graph[port].last_update_time = current_time;
    port_graph[port].cost = rtt;    // update cost
    bool sourceRouterInMap = direct_neighbor_map.count(sourceRouterID) != 0;

    free(packet);

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
                            if (direct_neighbor_map[it->first].cost < new_cost) {
                                update_DV(it->first, direct_neighbor_map[it->first].cost, it->first);
                                update_forward(it->first, it->first);
                            } else {
                                // upodate the cost
                                update_DV(it->first, new_cost, it->second.next_hop);
                                update_forward(it->first, it->second.next_hop);
                            }
                        }else{
                            update_DV(it->first, new_cost, it->second.next_hop);
                            update_forward(it->first, it->second.next_hop);
                        }
                    } else if (it->first == sourceRouterID && new_cost < DV_table[sourceRouterID].cost) {
                        update_DV(sourceRouterID, new_cost, sourceRouterID);
                        update_forward(sourceRouterID, sourceRouterID);
                    }
                }
            }
        }
//        printDVTable();
        send_dv_packet();
    } else if (packet_type == P_LS) {
        if (!sourceRouterInMap) {   // Not in DirectNeighborMap
            insert_neighbor(sourceRouterID, rtt, port);
            if (!check_link_in_LSTable(router_id, sourceRouterID)) {    // link not exists in LST
                insert_LS(router_id, sourceRouterID, rtt);
                insert_forward(sourceRouterID, sourceRouterID);

                Dijkstra_update();
                flood_ls_packet(true, FLOOD_ALL_FLAG, EMPTY_PACKET, size);
            }
        } else {    // In DirectNeighbor
            DirectNeighborEntry &dn = direct_neighbor_map[sourceRouterID];
            int prev_cost = dn.cost;
            update_neighbor(sourceRouterID, rtt, port);
            int cur_cost = dn.cost;
            int diff = cur_cost - prev_cost;
            if (diff != 0) {    // cost change
                update_LS(router_id, sourceRouterID, cur_cost);

                Dijkstra_update();
                flood_ls_packet(true, FLOOD_ALL_FLAG, EMPTY_PACKET, size);
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
        } else {
            free(packet);
            return;
        }
    }
}


void RoutingProtocolImpl::recv_dv_packet(unsigned short port, void *packet, unsigned short size) {
    bool hasChange = false;
    char *dv_packet = (char *) packet;
    uint16_t packet_size = ntohs(*(uint16_t *) (dv_packet + 2));
    uint16_t fromRouterID = ntohs(*(uint16_t *) (dv_packet + 4));

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
    free(packet);

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
        hasChange = true;
    } else {
        unsigned int old_cost = DV_table[fromRouterID].cost;
        unsigned int direct_cost = direct_neighbor_map[fromRouterID].cost;
        if (direct_cost < old_cost) {
            update_DV(fromRouterID, direct_cost, fromRouterID);
            update_forward(fromRouterID, fromRouterID);
            hasChange = true;
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
            hasChange = true;
            unsigned int new_cost = recv_cost + cost_to_fromRouter;
            insert_DV(dest_id, new_cost, fromRouterID);
            insert_forward(dest_id, fromRouterID);
        } else {
            // dest_id exist
            unsigned int old_cost = DV_table[dest_id].cost;
            unsigned int new_cost = cost_to_fromRouter + recv_cost;
            if (new_cost < old_cost) {
                hasChange = true;
                insert_DV(dest_id, new_cost, fromRouterID);
                insert_forward(dest_id, fromRouterID);
            }
        }
    }
    if (hasChange) send_dv_packet();
}


void RoutingProtocolImpl::recv_ls_packet(unsigned short port, void *packet, unsigned short size) {
    bool hasChange = false;
    char *recv_packet = (char *) packet;
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
    // Put seen pair into the SET and re-transmit
    haveSeenSet.insert(curr_pair);
    vector<pair<uint16_t, uint16_t>> recv_ls_list;
    unsigned short num_entry = (size - LS_PAYLOAD_POS) / 4;
    for (int i = 0; i < num_entry; i++) {
        uint16_t node_id = ntohs(*(uint16_t *) (recv_packet + LS_PAYLOAD_POS + 4 * i));
        uint16_t cost = ntohs(*(uint16_t *) (recv_packet + LS_PAYLOAD_POS + 4 * i + 2));
        auto curr_node_cost_pair = make_pair(node_id, cost);
        recv_ls_list.push_back(curr_node_cost_pair);
    }
    flood_ls_packet(false, port, recv_packet, size);  // flooding re-transmit
    free(packet);

    // special case handling
    if (LS_table.count(sourceRouterID) > 0) {
        auto tmp_map = LS_table[sourceRouterID];
        for (auto &pair: tmp_map) {
            uint16_t target_node = pair.first;
            bool isIn = false;
            for (auto &pair1: recv_ls_list) {
                uint16_t dest_id = pair1.first;
                if (target_node == dest_id) isIn = true;
            }
            if (!isIn) {
                remove_LS(sourceRouterID, target_node);
                hasChange = true;
            }
        }
    }

    // 2. Update LS table
    for (auto &pair: recv_ls_list) {
        uint16_t dest_id = pair.first;
        uint16_t cost = pair.second;
        // See if it's in NeighborMap
        bool source_router_not_in_neighbor = direct_neighbor_map.count(sourceRouterID) == 0;
        // Deal with situation where: the sourceRouterID is not in Neighbor!
        if (source_router_not_in_neighbor && dest_id == router_id) {
            insert_neighbor(sourceRouterID, cost, port);
        }

        if (!check_link_in_LSTable(sourceRouterID, dest_id)) { // received entry not in my LS table, add it!
            hasChange = true;
            insert_LS(sourceRouterID, dest_id, cost);
        } else {    // update it!
            uint16_t old_cost = LS_table[dest_id][sourceRouterID].cost;
            if (cost != old_cost) {
                hasChange = true;
                update_LS(sourceRouterID, dest_id, cost);
            }
        }
    }

    // 3. flood my LS packet, and re-transmit(flood) others' packets
    if (hasChange) {
        Dijkstra_update();
        flood_ls_packet(true, FLOOD_ALL_FLAG, EMPTY_PACKET, size);
    }
//    printNeighborTable();
//    printLSTable();
//    printFwdTable();
}

//************************************************************************************************//
//************************************************************************************************//
//  ALARM HANDLING
//************************************************************************************************//
//************************************************************************************************//

bool RoutingProtocolImpl::handle_port_expire() {
    // Iterate through ports, disconnect some ports, remove entries in DVtable and DirectNeighbor Table
    // remove all entries in DVtable whose next_hop is ports.to
    // remove all entries in directNeighborTable connected to that expire port

    bool hasChange = false;
    if (packet_type == P_DV) {
        vector<uint16_t> remove_list;
        for (int i = 0; i < num_ports; i++) {
            PortEntry &port = port_graph[i];
            unsigned int time_lag = sys->time() - port.last_update_time;
            if (time_lag > 15 * SECOND && port.direct_neighbor_id != NO_NEIGHBOR_FLAG) {
                hasChange = true;

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
                    if (connected_router == dv_entry.next_hop) { //find routers with connected_router as next hop
                        bool notInDirectNeighbor = direct_neighbor_map.count(dest_id) == 0;
                        if (notInDirectNeighbor) { // delete if destination is not in direct_neighbor_map
                            remove_list.push_back(dest_id);
                        } else {
                            update_DV(dest_id, direct_neighbor_map[dest_id].cost, dest_id);
                            update_forward(dest_id, dest_id);
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
        if (hasChange) send_dv_packet();
    } else if (packet_type == P_LS) {
        for (int i = 0; i < num_ports; i++) {
            PortEntry &port = port_graph[i];
            unsigned int time_lag = sys->time() - port.last_update_time;
            if (time_lag > 15 * SECOND && port.direct_neighbor_id != NO_NEIGHBOR_FLAG) {
                hasChange = true;
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
                Dijkstra_update();
            }
        }
        if (hasChange) flood_ls_packet(true, FLOOD_ALL_FLAG, EMPTY_PACKET, -1);
    }
    return hasChange;
}

void RoutingProtocolImpl::handle_dv_expire() {
    bool hasChange = handle_port_expire();
    vector<uint16_t> remove_list;
    for (auto & it : DV_table) {
        if (sys->time() - it.second.last_update_time > 45 * SECOND) {
            hasChange = true;
            if (direct_neighbor_map.count(it.first) != 0) {
                update_DV(it.first, direct_neighbor_map[it.first].cost, it.first);
                update_forward(it.first, it.first);
            } else {
                remove_list.push_back(it.first);
            }
        }
    }
    for (uint16_t dest_to_remove: remove_list) {
        DV_table.erase(dest_to_remove);
        forward_table.erase(dest_to_remove);
        hasChange = true;
    }
    if (hasChange) {
        send_dv_packet();
    }
}


void RoutingProtocolImpl::handle_ls_expire() {
    bool hasChange = handle_port_expire();
    vector<pair<uint16_t, uint16_t>> delete_list;
    for (auto &super_entry: LS_table) {
        uint16_t node1_id = super_entry.first;
        auto &sub_map = super_entry.second;
        for (auto &sub_entry: sub_map) {
            uint16_t node2_id = sub_entry.first;
            LSEntry &ls_entry = sub_entry.second;
            if (sys->time() - ls_entry.last_update_time > 45 * SECOND) {
                delete_list.emplace_back(node1_id, node2_id);
                hasChange = true;
            }
        }
    }

    // check and delete link, beware of duplicates: eg. <a,b> and <b,a>
    for (pair<uint16_t, uint16_t> d_pair: delete_list) {
        if (check_link_in_LSTable(d_pair.first, d_pair.second)) {
            remove_LS(d_pair.first, d_pair.second);
        }
    }
    if (hasChange) {
        Dijkstra_update();
        flood_ls_packet(true, FLOOD_ALL_FLAG, EMPTY_PACKET, -1);
    }
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


void RoutingProtocolImpl::init_ports() {
    for (int i = 0; i < num_ports; i++) {
        PortEntry port;
        port.cost = INFINITY_COST;
        port.direct_neighbor_id = NO_NEIGHBOR_FLAG;
        port.last_update_time = 0;
        port_graph.push_back(port);
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
        if (port.direct_neighbor_id != NO_NEIGHBOR_FLAG) {
            uint16_t dest_router_id = port.direct_neighbor_id;
            char *dv_packet = (char *) malloc(send_size * sizeof(char));
            *(char *) dv_packet = DV;
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
                *(uint16_t *) (dv_packet + pos + 2) = htons((uint16_t) cost);
                pos += 4;
            }
            sys->send(i, dv_packet, send_size);
        }
    }
}


void RoutingProtocolImpl::Dijkstra_update() {
    unordered_map<uint16_t, uint16_t> dis;
    unordered_map<uint16_t, bool> visit;
    priority_queue<pair<uint16_t, DijkstraEntry>, vector<pair<uint16_t, DijkstraEntry>>, CustomCompare> pq;

    struct DijkstraEntry new_entry = {.fromRouterID = router_id, .cost = 0};
    pq.push(make_pair(router_id, new_entry));
    dis[router_id] = 0;
    while (!pq.empty()) {
        pair<uint16_t, DijkstraEntry> point = pq.top();
        pq.pop();

        uint16_t cur_node = point.first;
        uint16_t cur_distance = point.second.cost;
        if (visit.count(cur_node) != 0 && visit[cur_node]) {
            continue;
        }
        uint16_t fromRouterID = point.second.fromRouterID;
        if (fromRouterID == router_id) {
            ForwardTableEntry fwd_entry = {.next_router_id = cur_node};
            forward_table[cur_node] = fwd_entry;
        } else {
            uint16_t next_hop = forward_table[fromRouterID].next_router_id;
            ForwardTableEntry fwd_entry = {.next_router_id = next_hop};
            forward_table[cur_node] = fwd_entry;
        }

        visit[cur_node] = true;

        vector<pair<uint16_t, uint16_t>> candidates;
        for (auto &entry: LS_table[cur_node]) {
            uint16_t dest_id = entry.first;
            uint16_t cost = entry.second.cost;
            candidates.emplace_back(dest_id, cost);
        }

        for (pair<uint16_t, uint16_t> neighbor : candidates) {
            uint16_t dest = neighbor.first;
            uint16_t cost = neighbor.second;

            if (visit[dest]) {
                continue;
            }
            uint16_t new_distance = cur_distance + cost;
            DijkstraEntry new_dentry = {.fromRouterID= cur_node, .cost = new_distance};
            pair<uint16_t, DijkstraEntry> new_child = make_pair(dest, new_dentry);
            if (dis.count(dest) == 0) {
                dis[dest] = new_distance;
                pq.push(new_child);
            } else {
                if (new_distance < dis[dest]) {
                    dis[dest] = new_distance;
                    pq.push(new_child);
                }
            }
        }
    }

    // PRINT DIJKSTRA RESULTS
//    cout << endl;
//    cout << "RESULT OF DIJKSTRA" << endl;
//    cout << "routerId:" << router_id << endl;
//    for (auto &pair: dis) {
//        cout << "DEST_ID: " << pair.first << " COST: " << pair.second << endl;
//    }
//    cout << "========================" << endl;
//    cout << endl;
//
//    cout << "RESULT OF FORWARD TABLE" << endl;
//    for (auto &pair: forward_table) {
//        cout << "DEST_ID: " << pair.first << " NEXT_HOP: " << pair.second.next_router_id << endl;
//    }
}



void RoutingProtocolImpl::flood_ls_packet(bool isSendMyLSP, uint16_t in_port_num, void *input_packet, int in_packet_size) {

    if (isSendMyLSP) {  // Sending my LSP
        // Update
        uint16_t packet_size;
        update_seq_num();
        for (int i = 0; i < num_ports; i++) {
            packet_size = direct_neighbor_map.size() * 4 + LS_PAYLOAD_POS;
            if (port_graph[i].direct_neighbor_id != NO_NEIGHBOR_FLAG) {
                char *packet = (char *) malloc(packet_size);
                *(char *) packet = LS;
                *(uint16_t *) (packet + 2) = htons((uint16_t) packet_size);
                *(uint16_t *) (packet + 4) = htons((uint16_t) router_id);
                *(uint32_t *) (packet + 8) = htonl((uint32_t) seq_num);
                int curr_pos = LS_PAYLOAD_POS;
                for (auto &pair: direct_neighbor_map) {
                    uint16_t dest_id = pair.first;
                    uint16_t cost = pair.second.cost;
                    *(uint16_t *) (packet + curr_pos) = htons(dest_id);
                    *(uint16_t *) (packet + curr_pos + 2) = htons((uint16_t) cost);
                    curr_pos += 4;
                }
                sys->send(i, packet, packet_size);
            }
        }
    } else {    // Re-transmit LSP
        for (int i = 0; i < num_ports; i++) {
            if (i == in_port_num) continue; // Not flood to the port received packet
            if (port_graph[i].direct_neighbor_id != NO_NEIGHBOR_FLAG) {
                char *new_packet = (char *) malloc(in_packet_size + 1);
                memcpy(new_packet, input_packet, in_packet_size);
                sys->send(i, new_packet, in_packet_size);
            }
        }
    }
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
    DirectNeighborEntry &dn = direct_neighbor_map[neighbor_id];
    dn.port_num = port_num;
    dn.cost = cost;
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


void RoutingProtocolImpl::insert_LS(uint16_t source_id, uint16_t dest_id, unsigned int cost) {
    auto &target_map1 = LS_table[source_id];
    struct LSEntry curr_entry1 = {cost, sys->time()};
    target_map1[dest_id] = curr_entry1;
    auto &target_map2 = LS_table[dest_id];
    struct LSEntry curr_entry2 = {cost, sys->time()};
    target_map2[source_id] = curr_entry2;
}


void RoutingProtocolImpl::update_LS(uint16_t source_id, uint16_t dest_id, unsigned int cost) {
    if ((LS_table.count(source_id) > 0 && LS_table[source_id].count(dest_id) > 0)
        || (LS_table.count(dest_id) > 0 && LS_table[dest_id].count(source_id) > 0)) {
        struct LSEntry curr_entry = {cost, sys->time()};
        LS_table[source_id][dest_id] = curr_entry;
        LS_table[dest_id][source_id] = curr_entry;
    }
}


void RoutingProtocolImpl::update_seq_num() {
    this->seq_num += 1;
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

bool RoutingProtocolImpl::check_link_in_LSTable(uint16_t node1_id, uint16_t node2_id) {
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
    return check1 && check2;
}

void RoutingProtocolImpl::printLSTable() {
    cout << endl;
    cout << "====================================" << endl;
    cout << "LS table" << endl;
    cout << "Router ID: " << router_id << endl;
    cout << "====================================" << endl;
    for (auto first_pair: LS_table) {
        uint16_t node1 = first_pair.first;
        auto sub_map = first_pair.second;
        for (auto &second_pair: sub_map) {
            uint16_t node2 = second_pair.first;
            auto entry = second_pair.second;
            cout << node1 << "->" << node2 << ": cost: " << entry.cost << " time: " << entry.last_update_time << endl;
        }
    }
    cout << "====================================" << endl;
    cout << endl;
}


void RoutingProtocolImpl::printDVTable() {
    cout << endl;
    cout << "==================================" << endl;
    cout << "DV table" << endl;
    cout << "Router ID: " << router_id << endl;
    cout << "==================================" << endl;
    cout << "DestID\tcost\tnextHop\tupdateTime" << endl;
    cout << "==================================" << endl;
    for (auto entry: DV_table) {
        cout << entry.first << "\t" << entry.second.cost << "\t" << entry.second.next_hop << "\t"
             << entry.second.last_update_time << endl;
    }
    cout << "==================================" << endl;
}

void RoutingProtocolImpl::printNeighborTable() {
    cout << endl;
    cout << "==================================" << endl;
    cout << "Neighbor table" << endl;
    cout << "Router ID: " << router_id << endl;
    cout << "==================================" << endl;
    cout << "DestID\tcost" << endl;
    cout << "==================================" << endl;
    for (auto entry: direct_neighbor_map) {
        cout << entry.first << "\t" << entry.second.cost << endl;
    }
    cout << "==================================" << endl;
}

void RoutingProtocolImpl::printFwdTable() {
    cout << endl;
    cout << "==================================" << endl;
    cout << "FWD table" << endl;
    cout << "Router ID: " << router_id << endl;
    cout << "==================================" << endl;
    cout << "DestID\tNext_hop" << endl;
    cout << "==================================" << endl;
    for (auto entry: forward_table) {
        cout << entry.first << "\t" << entry.second.next_router_id << endl;
    }
    cout << "==================================" << endl;
}
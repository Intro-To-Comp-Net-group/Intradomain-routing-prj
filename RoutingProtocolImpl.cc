#include "RoutingProtocolImpl.h"

RoutingProtocolImpl::RoutingProtocolImpl(Node *n) : RoutingProtocol(n) {
    sys = n;
    // add your own code
    alarmHandler = new AlarmHandler();
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
        port.isConnected = false;
        port_graph.push_back(port);
    }
    init_pingpong();
    alarmHandler->init_alarm(sys, this);
}

void RoutingProtocolImpl::handle_alarm(void *data) {
    // add your own code
    eAlarmType alarm_type = *(eAlarmType *) data;
    cout << "alarm type: " << alarm_type << endl;
    if (alarm_type == PINGPONG_ALARM) {
        init_pingpong();
        sys->set_alarm(this, 10 * SECOND, data);
    } else if (alarm_type == EXPIRE_ALARM) {
        handle_dv_expire();
        sys->set_alarm(this, 1 * SECOND, data);
    } else if (alarm_type == DV_UPDATE_ALARM) {
        send_dv_packet();
        sys->set_alarm(this, 30 * SECOND, data);
    } else if (alarm_type == LS_UPDATE_ALARM) {

//        sys->set_alarm(this, 30 * SECOND, data);
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
    bool isConnected = port_graph[port].isConnected;
    port_graph[port].isConnected = true;
    port_graph[port].direct_neighbor_id = sourceRouterID;
    port_graph[port].last_update_time = current_time;
    port_graph[port].cost = rtt;    // update cost
//    bool table_changed = false;

    if (packet_type == P_DV)
    {
        bool sourceRouterInMap = direct_neighbor_map.count(sourceRouterID) != 0;
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
//        printDVTable();
    }
    else if (packet_type == P_LS)
    {
        
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
    uint16_t toRouterID = ntohs(*(uint16_t *) (dv_packet + 6));

    // Parse DV_table to get a vector of pairs
    uint16_t dv_map_size = (packet_size - PAYLOAD_POS) / 4;
    vector<pair<uint16_t, uint16_t>> dv_entry_vec;
//    cout<<"_*******************************"<<endl;
//    cout<< "dv package reveived: " << endl;
//    cout<<"route: " <<router_id <<" from router_id: " <<fromRouterID<<endl;
    for (int i = 0; i < dv_map_size; i++) {
        uint16_t node_id = ntohs(*(uint16_t *) (dv_packet + PAYLOAD_POS + 4 * i));
        uint16_t cost = ntohs(*(uint16_t *) (dv_packet + PAYLOAD_POS + 4 * i + 2));
        pair<uint16_t, uint16_t> dv_entry;
//        cout<< " dest_id: " << node_id <<" cost: "<< cost <<endl;
        dv_entry.first = node_id;
        dv_entry.second = cost;
        dv_entry_vec.push_back(dv_entry);
    }
    cout << "_*******************************" << endl;
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
//            DVEntry dv_entry;
//            dv_entry.cost = recv_cost + cost_to_fromRouter;
//            dv_entry.next_hop = fromRouterID;
//            dv_entry.last_update_time = sys->time();
//            DV_table[dest_id] = dv_entry;
//            ForwardTableEntry fwd_entry;
//            fwd_entry.next_router_id = fromRouterID;
//            forward_table[dest_id] = fwd_entry;
        } else {
            // dest_id exist
            unsigned int old_cost = DV_table[dest_id].cost;
            unsigned int new_cost = cost_to_fromRouter + recv_cost;
            if (new_cost < old_cost) {
                table_changed = true;
                insert_DV(dest_id, new_cost, fromRouterID);
                insert_forward(dest_id, fromRouterID);
//                DV_table[dest_id].cost = new_cost;
//                DV_table[dest_id].next_hop = fromRouterID;
//                DV_table[dest_id].last_update_time = sys->time();
//                ForwardTableEntry fwd_entry;
//                fwd_entry.next_router_id = fromRouterID;
//                forward_table[dest_id] = fwd_entry;
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
//        cout << "PING: send_Time: " << sys->time() <<endl;
        sys->send(i, ping_packet, PINGPONG_PACKET_SIZE);
    }
}


void RoutingProtocolImpl::send_dv_packet() {
    cout << "I am sending dv packet id: " << router_id << " " << endl;
    vector<uint16_t> dest_to_send;
    uint16_t vec_size = 0;
    for (auto dv_pair: DV_table) {
        uint16_t dest_id = dv_pair.first;
        auto entry = dv_pair.second;
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
    cout << "check whether the port is expired?" << endl;

    vector<uint16_t> remove_list;

    for (int i = 0; i < num_ports; i++) {
        PortEntry &port = port_graph[i];
        unsigned int time_lag = sys->time() - port.last_update_time;
        if (time_lag > 15 * SECOND) {
            cout << "route_id: " << router_id << "port: " << i << " expires ";
            port.isConnected = false;
            port.cost = INFINITY_COST;
            uint16_t connected_router = port.direct_neighbor_id;

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
    // 如果一个DV entry要被移除：
    //      1。 看看DV entry的目的地在不在direct neighbor里，在的话就根据direct neighbor来更新dv entry
    //      2。 如果不再DirectNeighbor，删除。这个时候，其他接收到updated DVtable的router，看到发来的router里面少了一个entry，会不会有什么问题？

    handle_port_expire();

    vector<uint16_t> remove_list;

    for (auto it = DV_table.begin(); it != DV_table.end(); ++it) {
        if (sys->time() - it->second.last_update_time > 45 * SECOND) {
            if (direct_neighbor_map.count(it->first) != 0) {
                update_DV(it->first, direct_neighbor_map[it->first].cost, it->first);
                update_forward(it->first, it->first);
            } else {
//                forward_table.erase(forward_table.find(it->first));
//                DV_table.erase(it);
                remove_list.push_back(it->first);
            }
        }
    }
//    cout << endl;
//    cout << "Remove_list size: "<< remove_list.size()<<endl;
//    cout << "Before, DVMAP SIZE" << DV_table.size() <<endl;
    for (uint16_t dest_to_remove: remove_list) {
        DV_table.erase(dest_to_remove);
        forward_table.erase(dest_to_remove);
    }
//    cout << "AFter, DVMAP SIZE" << DV_table.size() <<endl;
//    cout << endl;
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
        cout << entry.first << "\t" << entry.second.cost << "\t" << entry.second.next_hop<<"\t"<<entry.second.last_update_time << endl;
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
    unsigned int prev_cost = dn->cost;
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

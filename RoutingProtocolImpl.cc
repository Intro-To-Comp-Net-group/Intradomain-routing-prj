#include "RoutingProtocolImpl.h"

RoutingProtocolImpl::RoutingProtocolImpl(Node *n) : RoutingProtocol(n) {
    sys = n;
    // add your own code
}

RoutingProtocolImpl::~RoutingProtocolImpl() {
    // add your own code (if needed)
}

void RoutingProtocolImpl::init(unsigned short num_ports, unsigned short router_id, eProtocolType protocol_type) {
    // add your own code
    this->num_ports = num_ports;
    this->router_id = router_id;
    this->packet_type = protocol_type;

    // Iterate through num_ports to set ports
    for (int i = 0; i < num_ports; i++) {
        PortEntry port;
        port.cost = 0;
        port.direct_neighbor_id = NO_NEIGHBOR_FLAG;
        port.last_update_time = 0;
        port_graph.push_back(port);
    }
    init_pingpong();

    // TODO deal with alarm
}

void RoutingProtocolImpl::handle_alarm(void *data) {
    // add your own code
    char alarm_type = ((char *) data)[0];
}

void RoutingProtocolImpl::recv(unsigned short port, void *packet, unsigned short size) {
    // add your own code
    char *recv_packet = (char *) packet;
    char recv_pkt_type = recv_packet[0];

    if (recv_pkt_type == DATA) {
        recv_data(port, packet, size);
    } else if (recv_pkt_type == PING) {
        recv_ping_packet(port, packet, size);
    } else if (recv_pkt_type == PONG) {
        recv_pong_packet(port, packet, size);
    } else if (recv_pkt_type == DV) {
        // re-calculate all the distance from current node


//         recv_distance_vector()
    } else if (recv_pkt_type == LS) {
        // recv_link_state()
    }
}

void RoutingProtocolImpl::recv_ping_packet(unsigned short port, void *packet, unsigned short size) {
    char *pong_to_send = (char *) packet;
    uint16_t from_port_id = ntohs(*(uint16_t *) (pong_to_send + 4));
    unsigned int recv_time = ntohs(*(unsigned int *) (pong_to_send + 8));
    // Send back pong packet
    *(char *) pong_to_send = PONG;
    *(uint16_t *) (pong_to_send + 2) = htons(12);
    *(uint16_t *) (pong_to_send + 4) = htons(this->router_id);
    *(uint16_t *) (pong_to_send + 6) = htons(from_port_id);
    *(unsigned int *) (pong_to_send + 8) = htons(recv_time);
    sys->send(port, pong_to_send, PINGPONG_PACKET_SIZE);
}

void RoutingProtocolImpl::recv_pong_packet(unsigned short port, void *packet, unsigned short size) {
    /*
     * 1. calculate RTT
     * 2. Update Direct Neighbor
     * 3. diff = RTT - old RTT
     * 4. If diff != 0:
     *      update DV_table
     *      update Forward_table
     *      send DV packet
     *    else:
     *      do nothing
     */

    char *recv_packet = (char *) packet;

    // Get rtt: recv_timestamp is the timestamp where PING sent, curr - get_time measure the RTT.
    unsigned int current_time = sys->time();
    unsigned int get_time = ntohs(*(uint16_t *) (recv_packet + 8));
    unsigned int rtt = current_time - get_time;

    uint16_t sourceRouterID = ntohs(*(uint16_t *) (recv_packet + 4));
    port_graph[port].direct_neighbor_id = sourceRouterID;
    port_graph[port].last_update_time = current_time;

    unsigned int prev_cost = port_graph[port].cost;
    port_graph[port].cost = rtt;    // update cost
    bool isUpdate = rtt != prev_cost;

    // Update direct_neighbor_map
    bool sourceRouterInMap = direct_neighbor_map.count(sourceRouterID) != 0;
    if (!sourceRouterInMap) {
        DirectNeighborEntry dn;
        dn.cost = rtt;
        dn.router_id = sourceRouterID;
        dn.port_num = port;
        direct_neighbor_map[sourceRouterID] = dn;
    } else {
        DirectNeighborEntry *dn = &direct_neighbor_map[sourceRouterID];
        dn->port_num = port;
        dn->router_id = sourceRouterID;
        dn->cost = rtt;
    }

    // Create forwarding entry and DV entry if not exists
    if (this->packet_type == P_DV) {
        bool hasCreateEntry = createEntryIfNotExists(sourceRouterID, rtt);
        if (hasCreateEntry) {
            send_dv_packet();
            return;
        }

        if (!isUpdate) {
            // Do nothing
            cout << "No change in cost, do nothing" << endl;
        } else {
            cout << "Cost change, update tables and send DV" << endl;
            // Update DV table
            // Update Forward Table
            /*
             *
             *  2. The destination is in direct neighbor
             *  3.
             *
             */



            // Send DV packet
            send_dv_packet();
        }
    } else if (this->packet_type == P_LS) {

    } else cout << "Unknown Packet Protocol Type" << endl;

}

void RoutingProtocolImpl::init_pingpong() {
    for (int i = 0; i < this->num_ports; i++) {
        char *ping_packet = (char *) malloc(PINGPONG_PACKET_SIZE * sizeof(char));
        *(char *) ping_packet = PING;
        *(uint16_t *) (ping_packet + 2) = htons(12);
        *(uint16_t *) (ping_packet + 4) = htons(this->router_id);
        *(unsigned int *) (ping_packet + 8) = htons(sys->time());
        sys->send(i, ping_packet, PINGPONG_PACKET_SIZE);
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


//************************************************************************************************//
//************************************************************************************************//
// HELPER FUNCTION AREA
//************************************************************************************************//
//************************************************************************************************//

bool RoutingProtocolImpl::createEntryIfNotExists(uint16_t sourceID, unsigned int cost) {
    // Search through forwarding_table, if not exists, update DV_table and fwd_table
    bool entryExists = false;
    if (forward_table.count(sourceID) == 0) {
        entryExists = true;
    }
    if (entryExists) return false;
    else {
        // Forwarding table
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
    vector<pair<uint16_t, unsigned int>> dest_to_send;
    uint16_t vec_size = 0;
    for (auto dv_pair: DV_table) {
        uint16_t dest_id = dv_pair.first;
        auto entry = dv_pair.second;
        if (entry.cost != INFINITY_COST) {
            vec_size += 1;
            pair<uint16_t, unsigned int> dest_pair;
            dest_pair.first = dest_id;
            dest_pair.second = entry.cost;
            dest_to_send.push_back(dest_pair);
        }
    }
    uint16_t send_size = vec_size * 4 + PAYLOAD_POS;

    for (uint16_t i = 0; i < num_ports; i++) {
        PortEntry port = port_graph[i];
        if (port.cost != INFINITY_COST && port.direct_neighbor_id != NO_NEIGHBOR_FLAG) {
            char *dv_packet = (char *) malloc(send_size * sizeof(char));
            *(ePacketType *) dv_packet = DV;
            *(uint16_t *) (dv_packet + 2) = htons(send_size);
            *(uint16_t *) (dv_packet + 4) = htons(this->router_id);
            *(uint16_t *) (dv_packet + 6) = htons(port.direct_neighbor_id);

            int pos = PAYLOAD_POS;
            for (uint16_t j = 0; j < vec_size; j++) {
                auto dest_pair = dest_to_send[j];
                uint16_t dest_id = dest_pair.first;
                unsigned int cost = dest_pair.second;

                *(uint16_t *) (dv_packet + pos) = htons(dest_id);
//                *(uint16_t *) (dv_packet + pos + 2) = htons((uint16_t) cost);

                auto direct_neighbor_entry = direct_neighbor_map[dest_id];
                // Poison reverse
                if (direct_neighbor_entry.port_num == i) {
                    *(uint16_t *) (dv_packet + pos + 2) = htons((uint16_t) INFINITY_COST);
                } else {
                    *(uint16_t *) (dv_packet + pos + 2) = htons((uint16_t) cost);
                }

                pos += 4;
            }

            sys->send(i, dv_packet, send_size);
        }
    }

//    char * dv_packet = (char *) malloc(send_size * sizeof(char));
//    *(char *) dv_packet = DV;
//    *(uint16_t *) (dv_packet + 2) = htons(send_size);
//    *(uint16_t *) (dv_packet + 4) = htons(this->router_id);
//    *(unsigned int *) (dv_packet + 8) = htons(sys->time());
}


// add more of your own code

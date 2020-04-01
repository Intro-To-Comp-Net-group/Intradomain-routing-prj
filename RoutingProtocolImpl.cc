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
        // recv_data()
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

    uint16_t sourceRouterID = *(uint16_t *) (recv_packet + 4);
    port_graph[port].to_router_id = sourceRouterID;
    port_graph[port].last_update_time = current_time;

    unsigned int prev_cost = port_graph[port].cost;
    port_graph[port].cost = rtt;    // update cost
    unsigned diff = rtt - prev_cost;

    // Update direct_neighbor_map
    bool sourceRouterInMap = direct_neighbor_map.count();

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

// add more of your own code

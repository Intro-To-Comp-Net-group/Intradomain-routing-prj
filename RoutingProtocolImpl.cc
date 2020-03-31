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
//    for (int i = 0; i < num_ports; i++) {
//        PortEntry pe;
//    }

    init_pingpong();

}

void RoutingProtocolImpl::handle_alarm(void *data) {
    // add your own code
    char alarm_type = ((char *) data)[0];
}

void RoutingProtocolImpl::recv(unsigned short port, void *packet, unsigned short size) {
    // add your own code
    char * recv_packet = (char *) packet;
    char recv_pkt_type = recv_packet[0];

    if (recv_pkt_type == DATA) {
        // recv_data()
    } else if (recv_pkt_type == PING) {
        recv_ping_packet(port, packet, size);
    } else if (recv_pkt_type == PONG) {
        recv_pong_packet(port, packet, size);
    } else if (recv_pkt_type == DV) {
        // recv_distance_vector()
    } else if (recv_pkt_type == LS) {
        // recv_link_state()
    }
}

void RoutingProtocolImpl::recv_ping_packet(unsigned short port, void *packet, unsigned short size) {
    char * pong_to_send = (char *) packet;
    uint16_t from_port_id = ntohs(*(uint16_t *) (pong_to_send + 4));

    // Send back pong packet
    *(char *) pong_to_send = PONG;
    *(uint16_t *) (pong_to_send + 2) = htons(12);
    *(uint16_t *) (pong_to_send + 4) = htons(this->router_id);
    *(uint16_t *) (pong_to_send + 6) = htons(from_port_id);
    sys->send(port, pong_to_send, PINGPONG_PACKET_SIZE);
}

void RoutingProtocolImpl::recv_pong_packet(unsigned short port, void *packet, unsigned short size) {
    // get rtt

    
}

void RoutingProtocolImpl::init_pingpong() {
    for (int i = 0; i < this->num_ports; i++) {
        char * ping_packet = (char *) malloc(PINGPONG_PACKET_SIZE * sizeof(char));
        *(char *) ping_packet = PING;
        *(uint16_t *) (ping_packet + 2) = htons(12);
        *(uint16_t *) (ping_packet + 4) = htons(this->router_id);
        *(uint16_t *) (ping_packet + 8) = htons(sys->time());
        sys->send(i, ping_packet, PINGPONG_PACKET_SIZE);
    }
}

// add more of your own code

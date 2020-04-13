#ifndef ROUTINGPROTOCOLIMPL_H
#define ROUTINGPROTOCOLIMPL_H

#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/time.h>
#include <fcntl.h>
#include <vector>
#include "RoutingProtocol.h"
#include "utils.h"
#include "Node.h"
#include "AlarmHandler.h"
#include <queue>
#include <cstring>



class RoutingProtocolImpl : public RoutingProtocol {
public:
    RoutingProtocolImpl(Node *n);

    ~RoutingProtocolImpl();

    void init(unsigned short num_ports, unsigned short router_id, eProtocolType protocol_type);
    // As discussed in the assignment document, your RoutingProtocolImpl is
    // first initialized with the total number of ports on the router,
    // the router's ID, and the protocol type (P_DV or P_LS) that
    // should be used. See global.h for definitions of constants P_DV
    // and P_LS.

    void handle_alarm(void *data);
    // As discussed in the assignment document, when an alarm scheduled by your
    // RoutingProtoclImpl fires, your RoutingProtocolImpl's
    // handle_alarm() function will be called, with the original piece
    // of "data" memory supplied to set_alarm() provided. After you
    // handle an alarm, the memory pointed to by "data" is under your
    // ownership and you should free it if appropriate.

    void recv(unsigned short port, void *packet, unsigned short size);
    // When a packet is received, your recv() function will be called
    // with the port number on which the packet arrives from, the
    // pointer to the packet memory, and the size of the packet in
    // bytes. When you receive a packet, the packet memory is under
    // your ownership and you should free it if appropriate. When a
    // DATA packet is created at a router by the simulator, your
    // recv() function will be called for such DATA packet, but with a
    // special port number of SPECIAL_PORT (see global.h) to indicate
    // that the packet is generated locally and not received from
    // a neighbor router.




private:
    Node *sys; // To store Node object; used to access GSR9999 interfaces
    unsigned short num_ports;
    unsigned short router_id;
    eProtocolType packet_type;

    // Alarm Part
    AlarmHandler * alarmHandler;    // deal with alarms
    void * EMPTY_PACKET;    // used for flooding, an empty flag

    // Data Part
    vector<PortEntry> port_graph;   // store ports information
    unordered_map<uint16_t, DirectNeighborEntry> direct_neighbor_map;   // <neighbor_id, <port, cost, last_update_time>>   PHYSICAL connection
    unordered_map<uint16_t, ForwardTableEntry> forward_table;   // <dest_id, next_hop> used to forward data

    // DV Part
    unordered_map<uint16_t, DVEntry> DV_table;  // <dest_id, <cost, next_hop, last_update_time>>

    // LS Part
    uint32_t seq_num;   // along with sourceRouterID, uniquely identify an LSP
    unordered_map<uint16_t, unordered_map<uint16_t, LSEntry>> LS_table; // use for Dijkstra to update forward_table
    set<pair<uint16_t, uint32_t>> haveSeenSet;  // store seen LSP to terminate flooding

private:
    // Additional Helper Functions Implemented by Our Group
    // ====================================================================================
    // 1. Initialization and alarm handling
    void init_ports();  // initialize ports

    void init_pingpong();   // send ping packets

    bool handle_port_expire();   //

    void handle_dv_expire();

    void handle_ls_expire();
    // ====================================================================================

    // ====================================================================================
    // 2. PINGPONG helper functions
    void recv_ping_packet(unsigned short port, void *packet, unsigned short size);

    void recv_pong_packet(unsigned short port, void *packet, unsigned short size);

    // ====================================================================================

    // ====================================================================================
    // 3. DV helper functions
    void recv_dv_packet(unsigned short port, void *packet, unsigned short size);

    void send_dv_packet();

    void update_DV(int16_t dest_id,  unsigned int cost, uint16_t next_hop);

    void insert_DV(int16_t dest_id, unsigned int cost, uint16_t next_hop);

    // ====================================================================================

    // ====================================================================================
    // 4. LS helper functions
    void recv_ls_packet(unsigned short port, void *packet, unsigned short size);

    void flood_ls_packet(bool isSendMyLSP, uint16_t in_port_num, void * input_packet, int in_packet_size);

    void Dijkstra_update();

    bool check_link_in_LSTable(uint16_t node1_id, uint16_t node2_id);

    void insert_LS(uint16_t source_id, uint16_t dest_id, unsigned int cost);

    void update_LS(uint16_t source_id, uint16_t dest_id, unsigned int cost);

    void update_seq_num();

    void remove_LS(uint16_t node1_id, uint16_t node2_id);

    void printLSTable();
    // ====================================================================================

    // ====================================================================================
    // 5. General helper functions -- update, delete Neighbor table, etc.

    void recv_data(unsigned short port, void *packet, unsigned short size);

    void update_forward(uint16_t dest_id,uint16_t next_hop);

    void update_neighbor(uint16_t neighbor_id, unsigned int cost, uint16_t port_num);

    void insert_neighbor(uint16_t neighbor_id, unsigned int cost, uint16_t port_num);

    void insert_forward(uint16_t dest_id, uint16_t next_hop);

    void printDVTable();

    void printNeighborTable();

    void printFwdTable();
    // ====================================================================================
};

#endif


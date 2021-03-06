/*----------------------------------------------------------------
//                                                              //
//  boot-loader-ethmac.c                                        //
//                                                              //
//  This file is part of the Amber project                      //
//  http://www.opencores.org/project,amber                      //
//                                                              //
//  Description                                                 //
//  The main functions for the boot loader application. This    //
//  application is embedded in the FPGA's SRAM and is used      // 
//  to load larger applications into the DDR3 memory on         //
//  the development board.                                      //
//                                                              //
//  Author(s):                                                  //
//      - Conor Santifort, csantifort.amber@gmail.com           //
//                                                              //
//////////////////////////////////////////////////////////////////
//                                                              //
// Copyright (C) 2011 Authors and OPENCORES.ORG                 //
//                                                              //
// This source file may be used and distributed without         //
// restriction provided that this copyright statement is not    //
// removed from the file and that any derivative work contains  //
// the original copyright notice and the associated disclaimer. //
//                                                              //
// This source file is free software; you can redistribute it   //
// and/or modify it under the terms of the GNU Lesser General   //
// Public License as published by the Free Software Foundation; //
// either version 2.1 of the License, or (at your option) any   //
// later version.                                               //
//                                                              //
// This source is distributed in the hope that it will be       //
// useful, but WITHOUT ANY WARRANTY; without even the implied   //
// warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR      //
// PURPOSE.  See the GNU Lesser General Public License for more //
// details.                                                     //
//                                                              //
// You should have received a copy of the GNU Lesser General    //
// Public License along with this source; if not, download it   //
// from http://www.opencores.org/lgpl.shtml                     //
//                                                              //
----------------------------------------------------------------*/


#include "amber_registers.h"
#include "address_map.h"
#include "line-buffer.h"
#include "timer.h"
#include "utilities.h"
#include "packet.h"
#include "tcp.h"


/* Global variables */
mac_ip_t self_g = { {0x00, 0x0e, 0x70, 0x70, 0x70, 0x70},  /* MAC Address  */
                    {192, 168, 0, 17}                      /* IPv4 address */
                  };


packet_t*   rx_packet_g;
socket_t*   socket0_g;
socket_t*   socket1_g;



socket_t* init_socket(int socket_id)
{
    socket_t* socket;
    
    socket = (socket_t*) malloc(sizeof(socket_t));
    socket->rx_packet =(packet_t*) malloc(sizeof(packet_t));
    init_packet_buffers(socket);
    
    socket->telnet_txbuf = init_line_buffer(0x80000);
    socket->telnet_rxbuf = init_line_buffer(0x1000);
    
    socket->id = socket_id;  
      
    socket->packets_sent = 0;
    socket->packets_received = 0;
    socket->packets_resent = 0;
    
    socket->telnet_sent_opening_message = 0;
    socket->telnet_echo_mode = 0;
    socket->telnet_connection_state = TELNET_CLOSED;
    socket->telnet_options_sent = 0;
       
    socket->tcp_current_buf = 0;
    socket->tcp_reset = 0;
    socket->tcp_connection_state = TCP_CLOSED;
    socket->tcp_disconnect = 0;
    socket->tcp_seq = 0x100;  /* should be random initial seq number for tcp */
    socket->tcp_last_seq = socket->tcp_seq; 
    socket->tcp_last_ack = 0;

    return socket;
}


void init_packet_buffers (socket_t* socket)
{
    int i;
    
    /* Create space for an array of pointers */
    socket->tcp_buf = malloc (TCP_TX_BUFFERS * sizeof (void *));
    
    /* Create space for a set of buffers, each pointed to by an element of the array */
    for (i=0;i<TCP_TX_BUFFERS;i=i+1) {
        socket->tcp_buf[i] = (packet_buffer_t*) malloc (sizeof (packet_buffer_t));
        socket->tcp_buf[i]->payload_valid = 0;
        socket->tcp_buf[i]->starting_seq = 0;
        socket->tcp_buf[i]->ending_seq   = 0;
        socket->tcp_buf[i]->len_bytes    = 0;
        socket->tcp_buf[i]->ack_received = 0;
        }
}


void ethernet_header(char *buf, mac_ip_t* target, unsigned short type)
{
    /* ethernet header */
    /* DA */
    buf[ 0] = target->mac[0];
    buf[ 1] = target->mac[1];
    buf[ 2] = target->mac[2];
    buf[ 3] = target->mac[3];
    buf[ 4] = target->mac[4];
    buf[ 5] = target->mac[5];
    
    /* SA */
    buf[ 6] = self_g.mac[0];
    buf[ 7] = self_g.mac[1];
    buf[ 8] = self_g.mac[2];
    buf[ 9] = self_g.mac[3];
    buf[10] = self_g.mac[4];
    buf[11] = self_g.mac[5];
    
    /* type */
    buf[12] = type>>8;
    buf[13] = type&0xff;
}


void ip_header(char *buf, mac_ip_t* target, unsigned short ip_len, char ip_proto)
{
    unsigned short header_checksum;
    static unsigned short ip_id = 0;

    /* Version, Header length */
    buf[0] = 0x45; 
    
    /* dscp */
    buf[1] = 0;
        
    /* ip len */
    buf[2] = ip_len>>8; 
    buf[3] = ip_len&0xff;
    
    /* ID */
    buf[4] = (ip_id>>8)&0xff;
    buf[5] = ip_id&0xff; 
    //ip_id++;
    
    /* Fragment */
    buf[6] = 0;
    buf[7] = 0;
    
    /* ttl */
    buf[8] = 64;
    
    /* Protocol */
    buf[9] = ip_proto;
    
    /* header checksum */
    buf[10] = 0;
    buf[11] = 0;
    
    /* Source IP */
    buf[12] = self_g.ip[0];
    buf[13] = self_g.ip[1];
    buf[14] = self_g.ip[2];
    buf[15] = self_g.ip[3];
    
    /* Destination IP */
    buf[16] = target->ip[0];
    buf[17] = target->ip[1];
    buf[18] = target->ip[2];
    buf[19] = target->ip[3];
    
    /* header checksum */
    header_checksum = header_checksum16(buf, 20, 0);
    buf[10] = (header_checksum>>8)&0xff;
    buf[11] = header_checksum&0xff;
}


void arp_reply(char *buf, mac_ip_t* arp_sender)
{

    ethernet_header(buf, arp_sender, 0x0806);
    
    /* Hardware Type */
    buf[14] = 0x00;
    buf[15] = 0x01;
    /* Protocol Type */
    buf[16] = 0x08;
    buf[17] = 0x00;
    /* HLEN */
    buf[18] = 0x06;
    /* PLEN */
    buf[19] = 0x04;
    
    /* Operation = Reply */
    buf[20] = 0x00;
    buf[21] = 0x02;
    
    /* Sender MAC */
    buf[22] = self_g.mac[0];
    buf[23] = self_g.mac[1];
    buf[24] = self_g.mac[2];
    buf[25] = self_g.mac[3];
    buf[26] = self_g.mac[4];
    buf[27] = self_g.mac[5];
    
    /* Sender IP */
    buf[28] = self_g.ip[0];
    buf[29] = self_g.ip[1];
    buf[30] = self_g.ip[2];
    buf[31] = self_g.ip[3];
    
    /* Target MAC */
    buf[32] = arp_sender->mac[0];
    buf[33] = arp_sender->mac[1];
    buf[34] = arp_sender->mac[2];
    buf[35] = arp_sender->mac[3];
    buf[36] = arp_sender->mac[4];
    buf[37] = arp_sender->mac[5];
    
    /* Target IP */
    buf[38] = arp_sender->ip[0];
    buf[39] = arp_sender->ip[1];
    buf[40] = arp_sender->ip[2];
    buf[41] = arp_sender->ip[3];
    tx_packet(42);
}


void ping_reply(packet_t* rx_packet, int ping_id, int ping_seq, char * rxbuf)
{

    int i;
    unsigned short header_checksum;
    mac_ip_t target;
    char * buf = (char*)ETHMAC_TX_BUFFER;
         
    target.mac[0] = rx_packet->src_mac[0];
    target.mac[1] = rx_packet->src_mac[1];
    target.mac[2] = rx_packet->src_mac[2];
    target.mac[3] = rx_packet->src_mac[3];
    target.mac[4] = rx_packet->src_mac[4];
    target.mac[5] = rx_packet->src_mac[5];
    
    target.ip[0]  = rx_packet->src_ip[0];
    target.ip[1]  = rx_packet->src_ip[1];
    target.ip[2]  = rx_packet->src_ip[2];
    target.ip[3]  = rx_packet->src_ip[3];

    ethernet_header(buf, &target, 0x0800);  /*bytes 0 to 13*/
    ip_header(&buf[14], &target, rx_packet->ip_len, 1); /* bytes 14 to 33, ip_proto = 1, ICMP*/
    
    /* ICMP */
    /* Type = reply */
    buf[34] = 0;
    
    /* Code = 0 */
    buf[35] = 0;
    
    /* checksum */
    buf[36] = 0;
    buf[37] = 0;
    
    /* ID */
    buf[38] = ping_id>>8;
    buf[39] = ping_id&0xff;
    
    /* SEQ */
    buf[40] = ping_seq>>8;
    buf[41] = ping_seq&0xff;

    for (i=8; i< rx_packet->ip_len - rx_packet->ip_header_len*4; i++) {
        buf[34+i] = rxbuf[i];
        }

    header_checksum = header_checksum16(&buf[34], (rx_packet->ip_len)-20, 0);
    buf[36] = (header_checksum>>8)&0xff;
    buf[37] = header_checksum&0xff;
    tx_packet(rx_packet->ip_len+14);
}


void parse_rx_packet(char * buf, packet_t* rx_packet)
{
    int i;
            
    rx_packet->dst_mac[0] = buf[0];
    rx_packet->dst_mac[1] = buf[1];
    rx_packet->dst_mac[1] = buf[2];
    rx_packet->dst_mac[3] = buf[3];
    rx_packet->dst_mac[4] = buf[4];
    rx_packet->dst_mac[5] = buf[5];

    rx_packet->src_mac[0] = buf[6];
    rx_packet->src_mac[1] = buf[7];
    rx_packet->src_mac[2] = buf[8];
    rx_packet->src_mac[3] = buf[9];
    rx_packet->src_mac[4] = buf[10];
    rx_packet->src_mac[5] = buf[11];    
    rx_packet->eth_type   = (buf[12]<<8) + buf[13];
    
    
    /* ARP */
    if (rx_packet->eth_type == 0x0806) {
        parse_arp_packet(&buf[14]);
        }


    /* Internet Protocol  */
    else if (rx_packet->eth_type == 0x0800){
        parse_ip_packet(&buf[14], rx_packet);
        }
}


void parse_arp_packet(char * buf)
{
    /* ARP is a broadcast message (mac broadcast address)
      asking 'does this IP address belong to you?"
    */
    int arp_op;
    mac_ip_t arp_sender, arp_target;
    
    arp_op = buf[6]<<8 | buf[7];

    arp_sender.mac[0] = buf[8];
    arp_sender.mac[1] = buf[9];
    arp_sender.mac[2] = buf[10];
    arp_sender.mac[3] = buf[11];
    arp_sender.mac[4] = buf[12];
    arp_sender.mac[5] = buf[13];
    
    arp_sender.ip [0] = buf[14];
    arp_sender.ip [1] = buf[15];
    arp_sender.ip [2] = buf[16];
    arp_sender.ip [3] = buf[17];
    
    arp_target.mac[0] = buf[18];
    arp_target.mac[1] = buf[19];
    arp_target.mac[2] = buf[20];
    arp_target.mac[3] = buf[21];
    arp_target.mac[4] = buf[22];
    arp_target.mac[5] = buf[23];
    
    arp_target.ip [0] = buf[24];
    arp_target.ip [1] = buf[25];
    arp_target.ip [2] = buf[26];
    arp_target.ip [3] = buf[27];
                
    /* Send a reply ? */
    if (arp_op==1 && 
        arp_target.ip[0]==self_g.ip[0] && 
        arp_target.ip[1]==self_g.ip[1] && 
        arp_target.ip[2]==self_g.ip[2] && 
        arp_target.ip[3]==self_g.ip[3]) {
        
        // ARP reply
        arp_reply((char*)ETHMAC_TX_BUFFER, &arp_sender);
        }
}



void parse_ip_packet(char * buf, packet_t* rx_packet)
{
    unsigned int ip_version;
    
    ip_version = buf[0]>>4;
    if (ip_version != 4) {
        //printf("%s: IP version %d not supported\n", __func__, ip_version);
        return;
        }
    
    /* Get destination IP address */
    rx_packet->dst_ip[0] = buf[16];
    rx_packet->dst_ip[1] = buf[17];
    rx_packet->dst_ip[2] = buf[18];
    rx_packet->dst_ip[3] = buf[19];

    /* If its not my address then ignore the packet */
    if (rx_packet->dst_ip[0] != self_g.ip[0] || 
        rx_packet->dst_ip[1] != self_g.ip[1] || 
        rx_packet->dst_ip[2] != self_g.ip[2] || 
        rx_packet->dst_ip[3] != self_g.ip[3] ) {
        return;
        }
        
    rx_packet->ip_len         = buf[ 2]<<8|buf[ 3];
    rx_packet->ip_header_len  = buf[0] & 0xf;
    rx_packet->ip_proto       = buf[9];
    rx_packet->src_ip[0]      = buf[12];
    rx_packet->src_ip[1]      = buf[13];
    rx_packet->src_ip[2]      = buf[14];
    rx_packet->src_ip[3]      = buf[15];

    
    /* Ping packets */    
    if (rx_packet->ip_proto == 1){
        parse_ping_packet(&buf[(rx_packet->ip_header_len)*4], rx_packet);
        }
        
    /* TCP packets */
    else if (rx_packet->ip_proto == 6){
        parse_tcp_packet(&buf[(rx_packet->ip_header_len)*4], rx_packet);
        }
        
    /* UDP packets */
    else if (rx_packet->ip_proto == 17){
        parse_udp_packet(&buf[(rx_packet->ip_header_len)*4], rx_packet);
        }
}


void parse_ping_packet(char * buf, packet_t* rx_packet)
{
    int ping_id;
    int ping_seq;

    ping_id     = buf[4]<<8|buf[5];
    ping_seq    = buf[6]<<8|buf[7];
    
    ping_reply(rx_packet, ping_id, ping_seq, buf);
}


unsigned short header_checksum16(unsigned char *buf, unsigned short len, unsigned int sum)
 {
     // build the sum of 16bit words
     while(len>1) {
         sum += 0xFFFF & (*buf<<8|*(buf+1));
         buf+=2;
         len-=2;
         }
     // if there is a byte left then add it (padded with zero)
     if (len) {
         sum += (0xFF & *buf)<<8;
         }
     // now calculate the sum over the bytes in the sum
     // until the result is only 16bit long
     while (sum>>16) {
         sum = (sum & 0xFFFF)+(sum >> 16);
         }
     
     // build 1's complement:
     return( (unsigned short ) sum ^ 0xFFFF);
}


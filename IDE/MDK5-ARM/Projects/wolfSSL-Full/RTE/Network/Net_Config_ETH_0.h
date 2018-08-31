/*------------------------------------------------------------------------------
 * MDK Middleware - Component ::Network:Interface
 * Copyright (c) 2004-2014 ARM Germany GmbH. All rights reserved.
 *------------------------------------------------------------------------------
 * Name:    Net_Config_ETH_0.h
 * Purpose: Network Configuration for ETH Interface
 * Rev.:    V5.01
 *----------------------------------------------------------------------------*/

//-------- <<< Use Configuration Wizard in Context Menu >>> --------------------

// <h>Ethernet Network Interface 0
#define ETH0_ENABLE             1

//   <o>Connect to hardware via Driver_ETH# <0-255>
//   <i>Select driver control block for MAC and PHY interface
#define ETH0_DRIVER             0

//   <s.17>MAC Address
//   <i>Local Ethernet MAC Address
//   <i>Value FF:FF:FF:FF:FF:FF is not allowed.
//   <i>It is an ethernet Broadcast MAC address.
//   <i>Default: "1E-30-6C-A2-45-5E"
#define ETH0_MAC_ADDR          	"1E-30-6C-A2-45-5E"


//		<s.15>IP Address
//		<i>Default: 192.168.0.100
#define ETH0_IP4_ADDR 					"192.168.0.100"

//   <s.15>Subnet mask
//   <i>Local Subnet mask
//   <i>Default: 255.255.255.0
#define ETH0_IP4_MASK           "255.255.255.0"

//   <s.15>Gateway
//   <i>Local Gateway
//   <i>Default: 192.168.0.254
#define ETH0_IP4_GATEWAY				"192.168.0.254"

//   <S.15>Primary DNS Server
//   <i>Primary DNS Server IP Address
//   <i>Default: 194.25.4.129
#define ETH0_IP4_PRIMARY_DNS    "194.25.4.129"

//   <S.15>Secondary DNS Server
//   <i>Secondary DNS Server IP Address
//   <i>Default: 194.25.2.130
#define ETH0_IP4_SECONDARY_DNS  "194.25.2.130"

//   <h>ARP Definitions
//   <i>Address Resolution Protocol Definitions
//     <o>Cache Table size <5-100>
//     <i>Number of cached hardware/IP addresses
//     <i>Default: 10
#define ETH0_ARP_TAB_SIZE       10

//     <o>Cache Timeout in seconds <5-255>
//     <i>A timeout for a cached hardware/IP addresses
//     <i>Default: 150
#define ETH0_ARP_CACHE_TOUT     150

//     <o>Number of Retries <0-20>
//     <i>Number of Retries to resolve an IP address
//     <i>before ARP module gives up
//     <i>Default: 4
#define ETH0_ARP_MAX_RETRY      4

//     <o>Resend Timeout in seconds <1-10>
//     <i>A timeout to resend the ARP Request
//     <i>Default: 2
#define ETH0_ARP_RESEND_TOUT    2

//     <q>Send Notification on Address changes
//     <i>When this option is enabled, the embedded host
//     <i>will send a Gratuitous ARP notification at startup,
//     <i>or when the device IP address has changed.
//     <i>Default: Disabled
#define ETH0_ARP_NOTIFY         0
//   </h>

//   <e>IGMP Group Management
//   <i>Enable or disable Internet Group Management Protocol
#define ETH0_IGMP_ENABLE        0

//     <o>Membership Table size <2-50>
//     <i>Number of Groups this host can join
//     <i>Default: 5
#define ETH0_IGMP_TAB_SIZE      5
//   </e>

//   <q>NetBIOS Name Service
//   <i>When this option is enabled, the embedded host can be
//   <i>accessed by his name on the local LAN using NBNS protocol.
//   <i>You need to modify also the number of UDP Sockets,
//   <i>because NBNS protocol uses one UDP socket to run.
#define ETH0_NBNS_ENABLE        1

//   <e>Dynamic Host Configuration
//   <i>When this option is enabled, local IP address, Net Mask
//   <i>and Default Gateway are obtained automatically from
//   <i>the DHCP Server on local LAN.
//   <i>You need to modify also the number of UDP Sockets,
//   <i>because DHCP protocol uses one UDP socket to run.
#define ETH0_DHCP_ENABLE        1

//     <s.40>Vendor Class Identifier
//     <i>This value is optional. If specified, it is added
//     <i>to DHCP request message, identifying vendor type.
//     <i>Default: ""
#define ETH0_DHCP_VCID          ""

//     <q>Bootfile Name
//     <i>This value is optional. If enabled, the Bootfile Name
//     <i>(option 67) is also requested from DHCP server.
//     <i>Default: disabled
#define ETH0_DHCP_BOOTFILE      0

//     <q>NTP Servers
//     <i>This value is optional. If enabled, a list of NTP Servers
//     <i>(option 42) is also requested from DHCP server.
//     <i>Default: disabled
#define ETH0_DHCP_NTP_SERVERS   0
//   </e>



// <h>Thread Priority
//	 <o>ETH0_THREAD_PRIORITY<0 - 10>
#define ETH0_THREAD_PRIORITY osPriorityAboveNormal
//	</h>

//	<h>Stack Size
//		<o>ETH0_THREAD_STACK_SIZE<0 - 2048>
#define ETH0_THREAD_STACK_SIZE 512
//	</h>

//	<e>IP Enables
//		<q>ETH0_IP4_ENABLE
#define ETH0_IP4_ENABLE 1
//		<q>ETH0_IP6_ENABLE
#define ETH0_IP6_ENABLE 0
//	</e>

#define NET_START_SERVICE 1
		


// </h>

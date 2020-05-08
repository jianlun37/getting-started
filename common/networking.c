#include "networking.h"

#include "nx_api.h"
#include "nx_secure_tls_api.h"
#include "nxd_dhcp_client.h"
#include "nxd_dns.h"

#define THREADX_IP_STACK_SIZE 2048
#define THREADX_PACKET_COUNT 40
#define THREADX_PACKET_SIZE 1536
#define THREADX_POOL_SIZE ((THREADX_PACKET_SIZE + sizeof(NX_PACKET)) * THREADX_PACKET_COUNT)
#define THREADX_ARP_CACHE_SIZE 512

#define THREADX_IPV4_ADDRESS IP_ADDRESS(0, 0, 0, 0)
#define THREADX_IPV4_MASK IP_ADDRESS(255, 255, 255, 0)

// Define the stack/cache for ThreadX.
static UCHAR threadx_ip_stack[THREADX_IP_STACK_SIZE];
static UCHAR threadx_ip_pool[THREADX_POOL_SIZE];
static UCHAR threadx_arp_cache_area[THREADX_ARP_CACHE_SIZE];

NX_PACKET_POOL main_pool;
NX_IP ip_0;
NX_DNS dns_client;
NX_DHCP dhcp_client;

// Print IPv4 address
static void print_address(CHAR* preable, ULONG address)
{
    printf("\t%s: %d.%d.%d.%d\r\n",
        preable,
        (u_int8_t)(address >> 24),
        (u_int8_t)(address >> 16 & 0xFF),
        (u_int8_t)(address >> 8 & 0xFF),
        (u_int8_t)(address & 0xFF));
}

static UINT dhcp_wait()
{
    UINT status;
    ULONG actual_status;
    ULONG ip_address;
    ULONG network_mask;
    ULONG gateway_address;

    printf("Initializing DHCP\r\n");

    // Create the DHCP instance.
    status = nx_dhcp_create(&dhcp_client, &ip_0, "azure_iot");

    // Start the DHCP Client.
    status = nx_dhcp_start(&dhcp_client);

    // Wait until address is solved.
    status = nx_ip_status_check(&ip_0, NX_IP_ADDRESS_RESOLVED, &actual_status, NX_WAIT_FOREVER);
    if (status != NX_SUCCESS)
    {
        // DHCP Failed...  no IP address!
        printf("Can't resolve address\r\n");
        return status;
    }

   // Get IP address and gateway address
    nx_ip_address_get(&ip_0, &ip_address, &network_mask);
    nx_ip_gateway_address_get(&ip_0, &gateway_address);

    // Output IP address and gateway address
    print_address("IP address", ip_address);
    print_address("Mask", network_mask);
    print_address("Gateway", gateway_address);

    printf("SUCCESS: DHCP initialized\r\n\r\n");

    return status;
}

static UINT dns_create()
{
    UINT status;
    ULONG dns_server_address[3] = { 0 };
    UINT dns_server_address_size = 12;

    printf("Initializing DNS client\r\n");

    // Create a DNS instance for the Client. Note this function will create
    // the DNS Client packet pool for creating DNS message packets intended
    // for querying its DNS server.
    status = nx_dns_create(&dns_client, &ip_0, (UCHAR *)"DNS Client");
    if (status != NX_SUCCESS)
    {
        return status;
    }

    // Use the packet pool here
    status = nx_dns_packet_pool_set(&dns_client, ip_0.nx_ip_default_packet_pool);
    if (status != NX_SUCCESS)
    {
        nx_dns_delete(&dns_client);
        return(status);
    }

    // Retrieve DNS server address
    nx_dhcp_interface_user_option_retrieve(&dhcp_client, 0, NX_DHCP_OPTION_DNS_SVR, (UCHAR*)dns_server_address, &dns_server_address_size); 
    
    // Add an IPv4 server address to the Client list
    status = nx_dns_server_add(&dns_client, dns_server_address[0]);
    if (status != NX_SUCCESS)
    {
        nx_dns_delete(&dns_client);
        return status;
    }
    
    // Output DNS Server address
    print_address("DNS address", dns_server_address[0]);

    printf("SUCCESS: DNS client initialized\r\n\r\n");

    return NX_SUCCESS;
}

bool network_init(VOID (*ip_link_driver)(struct NX_IP_DRIVER_STRUCT *))
{
    UINT status;

    // Initialize the NetX system.
    nx_system_initialize();

    // Create a packet pool.
    status = nx_packet_pool_create(&main_pool, "NetX Packet Pool",
        THREADX_PACKET_SIZE, 
        threadx_ip_pool, THREADX_POOL_SIZE);
    if (status != NX_SUCCESS)
    {
        printf("THREADX platform initialize fail: PACKET POOL CREATE FAIL.\r\n");
        return false;
    }

    // Create an IP instance
    status = nx_ip_create(&ip_0, "NetX IP Instance 0", 
        THREADX_IPV4_ADDRESS, THREADX_IPV4_MASK,
        &main_pool, ip_link_driver, 
        (UCHAR*)threadx_ip_stack, THREADX_IP_STACK_SIZE, 1);
    if (status != NX_SUCCESS)
    {
        nx_packet_pool_delete(&main_pool);
        printf("THREADX platform initialize fail: IP CREATE FAIL.\r\n");
        return false;
    }

    // Enable ARP and supply ARP cache memory
    status = nx_arp_enable(&ip_0, (VOID*)threadx_arp_cache_area, THREADX_ARP_CACHE_SIZE);
    if (status != NX_SUCCESS)
    {
        nx_ip_delete(&ip_0);
        nx_packet_pool_delete(&main_pool);
        printf("THREADX platform initialize fail: ARP ENABLE FAIL.\r\n");
        return false;
    }

    // Enable TCP traffic
    status = nx_tcp_enable(&ip_0);
    if (status != NX_SUCCESS)
    {
        nx_ip_delete(&ip_0);
        nx_packet_pool_delete(&main_pool);
        printf("THREADX platform initialize fail: TCP ENABLE FAIL.\r\n");
        return false;
    }

    // Enable UDP traffic
    status = nx_udp_enable(&ip_0);
    if (status != NX_SUCCESS)
    {
        nx_ip_delete(&ip_0);
        nx_packet_pool_delete(&main_pool);
        printf("THREADX platform initialize fail: UDP ENABLE FAIL.\r\n");
        return false;
    }

    // Enable ICMP traffic
    status = nx_icmp_enable(&ip_0);
    if (status != NX_SUCCESS)
    {
        nx_ip_delete(&ip_0);
        nx_packet_pool_delete(&main_pool);
        printf("THREADX platform initialize fail: ICMP ENABLE FAIL.\r\n");
        return false;
    }
    
    status = dhcp_wait();
    if (status != NX_SUCCESS)
    {
        printf("Failed to create DHCP\r\n");
    }

    // Create DNS
    status = dns_create();
    if (status != NX_SUCCESS)
    {
        nx_dhcp_delete(&dhcp_client);
        nx_ip_delete(&ip_0);
        nx_packet_pool_delete(&main_pool);
        printf("THREADX platform initialize fail: DNS CREATE FAIL.\r\n");
        return false;
    }
    
    // Initialize TLS
    nx_secure_tls_initialize();
    
    return true;
}

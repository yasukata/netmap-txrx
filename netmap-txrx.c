#include <stdio.h>
#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>

#include <sys/poll.h>

#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

static int do_abort = 0;

/* Copied from netmap pkt-gen */
static void sigint_h(int sig)
{
	(void)sig;
	do_abort = 1;
	signal(SIGINT, SIG_DFL);
}

/* Copied from netmap pkt-gen */
static uint32_t
checksum(const void *data, uint16_t len, uint32_t sum)
{
        const uint8_t *addr = data;
	uint32_t i;

        /* Checksum all the pairs of bytes first... */
        for (i = 0; i < (len & ~1U); i += 2) {
                sum += (u_int16_t)ntohs(*((u_int16_t *)(addr + i)));
                if (sum > 0xFFFF)
                        sum -= 0xFFFF;
        }
	/*
	 * If there's a single byte left over, checksum it, too.
	 * Network byte order is big-endian, so the remaining byte is
	 * the high byte.
	 */
	if (i < len) {
		sum += addr[i] << 8;
		if (sum > 0xFFFF)
			sum -= 0xFFFF;
	}
	return sum;
}

/* Copied from netmap pkt-gen */
static uint16_t
wrapsum(uint32_t sum)
{
	sum = ~sum & 0xFFFF;
	return (htons(sum));
}

static unsigned long make_packet(char *pkt_buf, char *payload, unsigned int payload_len)
{
	struct ether_header *eh;
	struct ip *ip;
	struct udphdr *udp;
	char *udp_payload;

	/* Destination MAC address */
	uint8_t dst_mac[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	/* Source MAC address */
	uint8_t src_mac[6] = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };

	/* Destination IP address */
	uint32_t dst_ip = (10 << 24) + (0 << 16) + (0 << 8) + (1 << 0) ;
	/* Source IP address */
	uint32_t src_ip = (10 << 24) + (0 << 16) + (0 << 8) + (2 << 0) ;

	/* Ethernet header */
	eh = (struct ether_header *) pkt_buf;
	eh->ether_type = htons(ETHERTYPE_IP);
	memcpy(eh->ether_dhost, dst_mac, 6);
	memcpy(eh->ether_shost, src_mac, 6);

	/* IP header */
	ip = (struct ip *) ((unsigned long) eh + sizeof(*eh));
	ip->ip_v = IPVERSION;
	ip->ip_hl = sizeof(*ip) >> 2;
	ip->ip_id = 0;
	ip->ip_tos = IPTOS_LOWDELAY;
	ip->ip_len = htons(payload_len + sizeof(*udp) + sizeof(*ip));
	ip->ip_id = 0;
	ip->ip_off = htons(IP_DF); /* Don't fragment */
	ip->ip_ttl = IPDEFTTL;
	ip->ip_p = IPPROTO_UDP;
	ip->ip_dst.s_addr = htonl(dst_ip);
	ip->ip_src.s_addr = htonl(src_ip);
	ip->ip_sum = wrapsum(checksum(&ip, sizeof(*ip), 0));

	/* UDP header */
	udp = (struct udphdr *) ((unsigned long) ip + sizeof(*ip));

	/* Copy payload to the packet buffer */
	udp_payload = (char *) ((unsigned long) udp + sizeof(*udp));
	memcpy(udp_payload, payload, payload_len);

	/* UDP header setup */
	udp->uh_sport = htons(10000);
	udp->uh_dport = htons(10001);
	udp->uh_ulen = htons(payload_len);
	udp->uh_sum = wrapsum(
	    checksum(&udp, sizeof(*udp),
            checksum(udp_payload, payload_len,
	    checksum(&ip->ip_src, 2 * sizeof(ip->ip_src),
		IPPROTO_UDP + (u_int32_t)ntohs(udp->uh_ulen)))));

	return payload_len + sizeof(*udp) + sizeof(*ip) + sizeof(*eh);
}

static void transmit_packets(struct nm_desc *nmd, unsigned int number_of_packets)
{
	struct netmap_ring *tx_ring;
	unsigned int j, limit;

	tx_ring = NETMAP_TXRING(nmd->nifp, 0);

	j = tx_ring->cur;

	if (nm_ring_space(tx_ring) < number_of_packets) {
		printf("tx ring does not have sufficient slots");
		limit = nm_ring_space(tx_ring);
	} else {
		limit = number_of_packets;
	}

	while (limit-- > 0) {
		struct netmap_slot *slot = &tx_ring->slot[j];
		char *txbuf = NETMAP_BUF(tx_ring, slot->buf_idx);
		unsigned long pkt_len;

		char payload[128];
		unsigned long i;

		snprintf(payload, sizeof(payload), "Hello packet I/O %d!", limit);

		/* Make a ethernet packet
		 *  - Prepare ether, IP and UDP headers
		 *  - Copy payload into the packet buffer
		 */
		pkt_len = make_packet(txbuf, payload, strlen(payload));

		/* Print the packet loaded at the "txbuf" pointer */
		printf("Packet content (%lu bytes) : ", pkt_len);
		for (i = 0; i < pkt_len; i++) {
			printf("%0x ", *(txbuf + i) & 0xff);
		}
		printf("\n\n");

		/* Set packet length to slot->len */
		slot->len = pkt_len;

		j = nm_ring_next(tx_ring, j);
	}

	/* Upadte head and cur indexes of netmap_ring */
	tx_ring->head = tx_ring->cur = j;

	/* Packets will be transmitted in this ioctl syscall */
	ioctl(nmd->fd, NIOCTXSYNC, NULL);
}

static void receive_packets(struct nm_desc *nmd)
{
	struct netmap_ring *rx_ring;
	unsigned int j, n;

	rx_ring = NETMAP_RXRING(nmd->nifp, 0);

	j = rx_ring->cur;

	n = nm_ring_space(rx_ring);

	while (n-- > 0) {
		struct netmap_slot *slot = &rx_ring->slot[j];
		char *rxbuf = NETMAP_BUF(rx_ring, slot->buf_idx);
		char *payload = (char *)((unsigned long) rxbuf
						+ sizeof(struct ether_header)
						+ sizeof(struct ip)
						+ sizeof(struct udphdr));
		unsigned long i;

		/* slot->len has length of the RX packet*/
		printf("Packet content (%d bytes) : ", slot->len);
		for (i = 0; i < slot->len; i++) {
			printf("%0x ", *(rxbuf + i) & 0xff);
		}
		printf("\n\n");

		printf("Received payload: %s\n", payload);

		j = nm_ring_next(rx_ring, j);
	}

	/* Upadte head and cur indexes of netmap_ring */
	rx_ring->head = rx_ring->cur = j;
}

int main(int argc, char **argv)
{
	int ch;
	struct nm_desc *nmd = NULL;
	char *ifname = NULL;
	int rx = 1;

	while ((ch = getopt(argc, argv, "i:f:")) != -1) {
		switch (ch) {
		case 'i':
			ifname = optarg;
			break;
		case 'f':
			if (!strncmp(optarg, "tx", 2)) {
				rx = 0;
			}
		default:
			break;
		}
	}

	if (ifname == NULL) {
		printf("Please specify an interface name by the -i option.");
		return 1;
	}

	nmd = nm_open(ifname, NULL, 0, NULL);
	if (nmd == NULL) {
		printf("nm_open failed");
		return 1;
	}

	signal(SIGINT, sigint_h);

	if (!rx) {
		/* Transmit */

		printf("Packet Info: \n");
		printf("MAC from aa:bb:cc:dd:ee:ff to ff:ff:ff:ff:ff:ff\n");
		printf("IP  from 10.0.0.2 to 10.0.0.1\n");
		printf("\n");

		while (!do_abort) {
			printf("Transmit 3 packets\n");
			transmit_packets(nmd, 3);
			sleep(1);
		}
	} else {
		/* Receive */

		struct pollfd pfd = { .fd = nmd->fd, .events = POLLIN };

		while (!do_abort) {
			int ret;
			printf("Receive packets...\n");
			/* Wait packet for 1 second */
			/* In this poll syscall, RX ring's indexes will be updated */
			ret = poll(&pfd, 1, 1000);
			if (ret < 0) {
				printf("poll() failed\n");
				break;
			} else if (ret == 0) {
				continue;
			}
			receive_packets(nmd);
			printf("\n");
		}
	}

	nm_close(nmd);

	printf("Exit process");

	return (0);
}

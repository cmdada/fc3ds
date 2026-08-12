#include "net/sntp.h"

#include <3ds.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define NTP_UNIX_DELTA 2208988800ULL

#define NTP_PORT       "123"
#define NTP_PACKET_LEN 48
#define NTP_TIMEOUT_MS 4000

#define CLOCK_SANE_FLOOR 1735689600LL
#define CLOCK_SANE_CEIL  2524608000LL

static FcClock s_clock;

time_t fcNow(void)
{
	return time(NULL) + (time_t)s_clock.offsetSec;
}

const FcClock *fcClockState(void)
{
	return &s_clock;
}

bool fcClockPlausible(void)
{
	int64_t t = (int64_t)time(NULL);
	return t > CLOCK_SANE_FLOOR && t < CLOCK_SANE_CEIL;
}

bool fcSntpSync(const char *host, FcClock *clock)
{
	struct addrinfo hints, *ai = NULL;
	int sock = -1;
	bool ok = false;

	memset(&hints, 0, sizeof hints);
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	if (getaddrinfo(host, NTP_PORT, &hints, &ai) != 0 || !ai) {
		snprintf(s_clock.error, sizeof s_clock.error, "cannot resolve %s", host);
		goto done;
	}

	sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (sock < 0) {
		snprintf(s_clock.error, sizeof s_clock.error, "socket failed");
		goto done;
	}

	unsigned char pkt[NTP_PACKET_LEN];
	memset(pkt, 0, sizeof pkt);
	pkt[0] = (0 << 6) | (4 << 3) | 3;

	uint64_t sentMs = osGetTime();

	if (sendto(sock, pkt, sizeof pkt, 0, ai->ai_addr, ai->ai_addrlen) != (ssize_t)sizeof pkt) {
		snprintf(s_clock.error, sizeof s_clock.error, "send failed");
		goto done;
	}

	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(sock, &rfds);
	struct timeval tv = { NTP_TIMEOUT_MS / 1000, (NTP_TIMEOUT_MS % 1000) * 1000 };

	int ready = select(sock + 1, &rfds, NULL, NULL, &tv);
	if (ready <= 0) {
		snprintf(s_clock.error, sizeof s_clock.error, "no reply from %s", host);
		goto done;
	}

	ssize_t got = recv(sock, pkt, sizeof pkt, 0);
	uint64_t recvMs = osGetTime();

	if (got != (ssize_t)NTP_PACKET_LEN) {
		snprintf(s_clock.error, sizeof s_clock.error, "short reply from %s", host);
		goto done;
	}

	if (pkt[1] == 0) {
		snprintf(s_clock.error, sizeof s_clock.error, "%s not synchronised", host);
		goto done;
	}

	uint32_t ntpSec = ((uint32_t)pkt[40] << 24) | ((uint32_t)pkt[41] << 16) |
	                  ((uint32_t)pkt[42] << 8)  | (uint32_t)pkt[43];
	if (ntpSec == 0) {
		snprintf(s_clock.error, sizeof s_clock.error, "empty timestamp");
		goto done;
	}

	int64_t rtt = (int64_t)(recvMs - sentMs);
	int64_t trueUnix = (int64_t)ntpSec - (int64_t)NTP_UNIX_DELTA + (rtt / 2000);

	s_clock.synced      = true;
	s_clock.offsetSec   = trueUnix - (int64_t)time(NULL);
	s_clock.roundTripMs = (int32_t)rtt;
	s_clock.error[0]    = '\0';
	snprintf(s_clock.server, sizeof s_clock.server, "%s", host);
	ok = true;

done:
	if (sock >= 0)
		close(sock);
	if (ai)
		freeaddrinfo(ai);
	if (clock)
		*clock = s_clock;
	return ok;
}

bool fcSntpSyncDefault(FcClock *clock)
{
	static const char *pool[] = {
		"pool.ntp.org",
		"time.cloudflare.com",
		"time.google.com",
	};

	for (size_t i = 0; i < sizeof pool / sizeof pool[0]; i++) {
		if (fcSntpSync(pool[i], clock))
			return true;
	}
	return false;
}

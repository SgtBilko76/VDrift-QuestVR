/* vd-join-probe: joins a VDrift server as a player without a headset, to
 * exercise the lobby path (HELLO, WELCOME, ROSTER, loading, START) and to
 * count snapshots. It sends zero inputs, so its car sits on the grid.
 *
 *   g++ -O1 -std=c++17 -I ../../../vdrift/src vd-join-probe.cpp -lenet -o vd-join-probe
 *   ./vd-join-probe 127.0.0.1 28600 Probe XS
 */

#include "net/netprotocol.h"
#include "physics/carinput.h"

#include <enet/enet.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

using namespace net;

int main(int argc, char ** argv)
{
	const char * host = argc > 1 ? argv[1] : "127.0.0.1";
	int port = argc > 2 ? atoi(argv[2]) : DEFAULT_PORT;
	const char * name = argc > 3 ? argv[3] : "Probe";
	const char * car = argc > 4 ? argv[4] : "XS";
	int seconds = argc > 5 ? atoi(argv[5]) : 120;

	if (enet_initialize() != 0) { fprintf(stderr, "enet init failed\n"); return 1; }
	ENetHost * client = enet_host_create(NULL, 1, CHANNELS, 0, 0);
	ENetAddress addr;
	enet_address_set_host(&addr, host);
	addr.port = port;
	ENetPeer * peer = enet_host_connect(client, &addr, CHANNELS, 0);
	if (!peer) { fprintf(stderr, "connect failed\n"); return 1; }

	bool loaded = false, racing = false;
	int snapshots = 0;
	unsigned last_tick = 0;
	int my_id = -1;
	auto start = std::chrono::steady_clock::now();
	auto last_input = start;

	while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < seconds)
	{
		ENetEvent ev;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		while (enet_host_service(client, &ev, 0) > 0)
		{
			if (ev.type == ENET_EVENT_TYPE_CONNECT)
			{
				printf("connected\n");
				Writer w;
				w.u8(MSG_HELLO);
				w.u16(PROTOCOL_VERSION);
				w.str(name);
				CarInfo ci;
				ci.name = car; ci.variant = car; ci.paint = "default"; ci.tire = "default"; ci.wheel = "default";
				ci.hsv[0] = 0.1f; ci.hsv[1] = 0.8f; ci.hsv[2] = 0.9f; ci.ailevel = 1.0f;
				w.carinfo(ci);
				DrivingAids aids; aids.tcs = false; aids.abs = false; w.aids(aids);
				enet_peer_send(peer, CHANNEL_CONTROL, enet_packet_create(w.buf.data(), w.buf.size(), ENET_PACKET_FLAG_RELIABLE));
			}
			else if (ev.type == ENET_EVENT_TYPE_RECEIVE)
			{
				Reader r(ev.packet->data, ev.packet->dataLength);
				uint8_t type = r.u8();
				if (type == MSG_WELCOME) { my_id = r.u8(); printf("welcome: player id %d\n", my_id); }
				else if (type == MSG_REJECT) { printf("rejected: %s\n", r.str().c_str()); return 2; }
				else if (type == MSG_ROSTER)
				{
					Roster ro = r.roster();
					static const char * const st[] = {"lobby", "loading", "racing", "finished"};
					printf("roster: %s %d lap(s) state=%s left=%.0fs cars=%zu\n", ro.track.c_str(), ro.laps,
						st[ro.state], ro.seconds_left, ro.cars.size());
					for (const auto & c : ro.cars)
						printf("   car %d: %s (%s) %s\n", c.carid, c.player_name.c_str(), c.info.name.c_str(),
							c.player == AI_PLAYER ? "bot" : (c.player == my_id ? "me" : "player"));
					if (ro.state == RS_LOADING && !loaded)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(500));
						Writer w; w.u8(MSG_READY);
						enet_peer_send(peer, CHANNEL_CONTROL, enet_packet_create(w.buf.data(), w.buf.size(), ENET_PACKET_FLAG_RELIABLE));
						loaded = true;
						printf("sent READY\n");
					}
					if (ro.state == RS_LOBBY) { loaded = false; racing = false; }
				}
				else if (type == MSG_START) { racing = true; printf("START at tick %u\n", r.u32()); }
				else if (type == MSG_SNAPSHOT)
				{
					last_tick = r.u32();
					unsigned n = r.u8();
					if (snapshots++ % 90 == 0)
					{
						printf("snapshot tick %u, %u cars, %zu bytes:", last_tick, n, ev.packet->dataLength);
						for (unsigned i = 0; i < n && r.ok(); ++i)
						{
							unsigned id = r.u8(); r.u32();
							std::vector<float> in; ReadInputs(r, in);
							std::string st = r.bytes();
							// VDrift's binary serializer writes big endian
							float f[15];
							for (int k = 0; k < 15 && st.size() >= 15 * 4; ++k)
							{
								unsigned char b[4] = {(unsigned char)st[k*4+3], (unsigned char)st[k*4+2], (unsigned char)st[k*4+1], (unsigned char)st[k*4]};
								memcpy(&f[k], b, 4);
							}
							if (st.size() >= 15 * 4)
								printf("  car%u (%.1f %.1f %.1f) v=%.1f", id, f[9], f[10], f[11],
									sqrtf(f[12]*f[12] + f[13]*f[13] + f[14]*f[14]));
						}
						printf("\n");
					}
				}
				else if (type == MSG_RACE_END) { printf("race end:\n%s", r.str().c_str()); }
				else if (type == MSG_TEXT) { printf("text: %s\n", r.str().c_str()); }
				enet_packet_destroy(ev.packet);
			}
			else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) { printf("disconnected\n"); return 0; }
		}
		if (racing && std::chrono::duration<double>(std::chrono::steady_clock::now() - last_input).count() > 0.05)
		{
			last_input = std::chrono::steady_clock::now();
			Writer w;
			w.u8(MSG_INPUT);
			w.u32(last_tick);
			std::vector<float> in(CarInput::INVALID, 0.0f);
			in[CarInput::THROTTLE] = 1.0f;   // drive off so the car state changes
			WriteInputs(w, in);
			enet_peer_send(peer, CHANNEL_STATE, enet_packet_create(w.buf.data(), w.buf.size(), ENET_PACKET_FLAG_UNSEQUENCED));
		}
	}
	printf("%d snapshots received\n", snapshots);
	enet_peer_disconnect(peer, 0);
	enet_host_flush(client);
	return 0;
}

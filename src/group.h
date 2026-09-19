// The outdoor election on the unit (fork #22; spec
// docs/superpowers/specs/2026-09-19-outdoor-election-design.md §7): owns the
// election state, feeds it the group's MQTT messages and carries out what its
// tick asks for. The rules themselves are lib/mhi_pure/mhi_group, host-tested.

#pragma once

#include <stdint.h>

void group_setup();      // once at boot
void group_connected();  // after every MQTT connect: clears the peer table, starts the 5 s grace period
void group_loop();       // every loop() pass: the election, and what it asks for, while connected

// Offered every MQTT message first. True when the topic was the group's (a
// record, a peer's connected topic, or a message on the old connected topic of
// a peer whose prefix changed); such a message never answers on cmd_received.
bool group_handle_message(const char* topic, const uint8_t* payload, unsigned int length);

// The system-value gate of output_P() (spec §6.4): past the grace period, role 1 and state 1.
bool group_may_publish_system();

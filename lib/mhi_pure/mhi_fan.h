// Named fan levels (fork #21 F6; spec §4.2). main.cpp maps these to/from the
// core's own fan bytes (0, 1, 2, 6 for the four levels, 7 for Auto),
// unchanged from today. Pure logic, no Arduino.
#pragma once

#define MHI_FAN_NONE 0  // mhi_fan_parse's failure value
#define MHI_FAN_AUTO 5

struct MhiFanNames {
  const char* levels[4];  // PAYLOAD_FAN_1..4
  const char* auto_name;  // PAYLOAD_FAN_AUTO
};

// 1..4 -> the level's name, MHI_FAN_AUTO -> auto_name, else NULL.
const char* mhi_fan_text(const MhiFanNames* names, int level);

// A set/Fan payload: one of the four level names, "1".."4", or auto_name.
// MHI_FAN_NONE when it is none of them (NULL and "" included).
int mhi_fan_parse(const MhiFanNames* names, const char* payload);

#pragma once
#include <math.h>
#include <stdint.h>

struct MistProfile { uint8_t percent; uint32_t durationMs; };

inline MistProfile mistProfile(float humidity) {
  if (!isfinite(humidity) || humidity < 0 || humidity >= 40) return {0, 0};
  if (humidity >= 35) return {25, 10000};
  if (humidity >= 30) return {50, 20000};
  return {100, 30000};
}

class MistCycle {
 public:
  void updateHumidity(float humidity, uint32_t now) {
    requested = mistProfile(humidity);
    sampledAt = now;
  }

  bool refresh(uint32_t now) {
    const uint8_t previous = duty;
    if (!requested.percent || now - sampledAt >= 6000) {
      duty = 0;
      resting = false;
    } else if (duty && now - changedAt >= durationMs) {
      duty = 0;
      resting = true;
      changedAt = now;
    } else if (!duty && (!resting || now - changedAt >= 5000)) {
      duty = requested.percent;
      durationMs = requested.durationMs;
      changedAt = now;
      resting = false;
    }
    return previous != duty;
  }

  uint8_t percent() const { return duty; }

 private:
  MistProfile requested{};
  uint32_t sampledAt = 0, changedAt = 0, durationMs = 0;
  uint8_t duty = 0;
  bool resting = false;
};

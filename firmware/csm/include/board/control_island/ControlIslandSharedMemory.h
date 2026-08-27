#pragma once

#include <stdint.h>

#include "board/control_island/ControlIslandContract.h"

namespace csm::board::control_island {

struct ControlReadResult {
  bool accepted = false;
  bool new_snapshot = false;
  uint16_t detail = 0;
  uint32_t sequence = 0;
  FinalControlSnapshotPayload payload = {};
};

struct HealthReadResult {
  bool accepted = false;
  bool new_snapshot = false;
  uint16_t detail = 0;
  uint32_t sequence = 0;
  ControlHealthPayload payload = {};
};

struct BringupReadResult {
  bool accepted = false;
  bool new_snapshot = false;
  uint16_t detail = 0;
  uint32_t sequence = 0;
  BringupTracePayload payload = {};
};

ControlIpcRegion* controlIpcRegion();
RawCanEntry* rawCanRingEntries();

void initializeControlIpcForM7(uint32_t m7_boot_id);
uint32_t initializeControlIpcForM4();
bool publishFinalControlSnapshot(FinalControlSnapshotPayload payload);
ControlReadResult readFinalControlSnapshot(uint32_t last_sequence);

bool publishControlHealth(ControlHealthPayload payload);
HealthReadResult readControlHealth(uint32_t last_sequence);
bool publishBringupTrace(const BringupTracePayload& payload);
BringupReadResult readBringupTrace(uint32_t last_sequence);

bool pushRawCanFromM4(const RawCanEntry& entry);
bool popRawCanForM7(RawCanEntry* entry);
uint32_t rawCanRingFill();

uint32_t controlObjectCrc32(const void* data, size_t size);

}  // namespace csm::board::control_island

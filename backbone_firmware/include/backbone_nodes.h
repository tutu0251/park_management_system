#pragma once

#include <stddef.h>
#include <stdint.h>

// Compile-time mapping between node_id and the nRF24 address that node listens on.
// Each node firmware must set NODE_RF_ADDR_NODE_INIT to match its entry here.

struct NodeEntry {
  uint16_t node_id;
  uint8_t addr[5];
};

// Default set. Adjust for your installation.
constexpr NodeEntry KNOWN_NODES[] = {
    {1, {'N', 'O', 'D', 'E', '1'}},
    {2, {'N', 'O', 'D', 'E', '2'}},
    {3, {'N', 'O', 'D', 'E', '3'}},
    {4, {'N', 'O', 'D', 'E', '4'}},
};

inline const NodeEntry* find_node_by_id(uint16_t node_id) {
  for (size_t i = 0; i < (sizeof(KNOWN_NODES) / sizeof(KNOWN_NODES[0])); ++i) {
    if (KNOWN_NODES[i].node_id == node_id) return &KNOWN_NODES[i];
  }
  return nullptr;
}


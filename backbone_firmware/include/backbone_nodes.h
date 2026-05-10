#pragma once
// =============================================================================
// backbone_nodes.h — compile-time table: logical node_id -> nRF24 RX address
// =============================================================================
// The backbone looks up a node's five-byte listening address to send PAY_RESP
// and CHECK_STATUS_REQ. Each node firmware must build the same pattern via
// node_cfg::node_listen_addr(..., node_id) so the last byte matches '1'..'9'.
// =============================================================================

#include <stddef.h>   // size_t for loop in find_node_by_id
#include <stdint.h>   // uint16_t, uint8_t

// One row: park machine identity plus the exact address that node's radio
// listens on (reading pipe 1 in node firmware).
struct NodeEntry {
  uint16_t node_id;   // Sent inside SWIPE_REQ; must be unique in this table.
  uint8_t addr[5];    // nRF24 address width; MSB-first ordering as RF24 uses.
};

// Default deployment: four nodes. Edit to add/remove machines or change bytes.
constexpr NodeEntry KNOWN_NODES[] = {
    {1, {'N', 'O', 'D', 'E', '1'}},
    {2, {'N', 'O', 'D', 'E', '2'}},
    {3, {'N', 'O', 'D', 'E', '3'}},
    {4, {'N', 'O', 'D', 'E', '4'}},
};

// Linear scan of KNOWN_NODES; fine for small fixed tables on AVR.
// Returns nullptr if no row matches (unknown node_id from a swipe).
inline const NodeEntry* find_node_by_id(uint16_t node_id) {
  for (size_t i = 0; i < (sizeof(KNOWN_NODES) / sizeof(KNOWN_NODES[0])); ++i) {
    if (KNOWN_NODES[i].node_id == node_id) return &KNOWN_NODES[i];
  }
  return nullptr;
}

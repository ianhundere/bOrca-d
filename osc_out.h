#pragma once
#include "base.h"

typedef struct Oosc_dev Oosc_dev;

typedef enum {
  Oosc_udp_create_error_ok = 0,
  Oosc_udp_create_error_getaddrinfo_failed = 1,
  Oosc_udp_create_error_couldnt_open_socket = 2,
} Oosc_udp_create_error;

Oosc_udp_create_error oosc_dev_create_udp(Oosc_dev **out_ptr,
                                          char const *dest_addr,
                                          char const *dest_port);
void oosc_dev_destroy(Oosc_dev *dev);

// Send a raw UDP datagram.
void oosc_send_datagram(Oosc_dev *dev, char const *data, Usz size);

// Send a list/array of 32-bit integers in OSC format to the specified "osc
// address" (a path like /foo) as a UDP datagram.
void oosc_send_int32s(Oosc_dev *dev, char const *osc_address, I32 const *vals,
                      Usz count);

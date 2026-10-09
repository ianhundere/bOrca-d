#include "osc_out.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>

struct Oosc_dev {
  int fd;
  // Just keep the whole list around, since juggling the strict-aliasing
  // problems with sockaddr_storage is not worth it.
  struct addrinfo *chosen;
  struct addrinfo *head;
};

Oosc_udp_create_error oosc_dev_create_udp(Oosc_dev **out_ptr,
                                          char const *dest_addr,
                                          char const *dest_port) {
  struct addrinfo hints = {0};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = 0;
  hints.ai_flags = AI_ADDRCONFIG;
  struct addrinfo *chosen = NULL;
  struct addrinfo *head = NULL;
  int err = getaddrinfo(dest_addr, dest_port, &hints, &head);
  if (err != 0) {
#if 0
    fprintf(stderr, "Failed to get address info, error: %d\n", errno);
#endif
    return Oosc_udp_create_error_getaddrinfo_failed;
  }
  // Special behavior: if no hostname was provided, we'll get loopback(s) from
  // getaddrinfo. Which is good. But on systems with both an ipv4 and ipv6
  // address, we might get the ipv6 address listed first. And the OSC server,
  // for example Plogue Bidule, might not support ipv6. And defaulting to the
  // ipv6 loopback wouldn't work, in that case. So if there's no hostname, and
  // we find an ipv4 address in the results, prefer to use that.
  //
  // Actually let's just prefer ipv4 completely for now
#if 0
  if (!dest_addr) {
#endif
  {
    for (struct addrinfo *a = head; a; a = a->ai_next) {
      if (a->ai_family != AF_INET)
        continue;
      chosen = a;
      break;
    }
  }
  if (!chosen)
    chosen = head;
#if 0
  for (struct addrinfo* a = head; a; a = a->ai_next) {
    char buff[INET6_ADDRSTRLEN];
    char const* str = NULL;
    if (a->ai_family == AF_INET) {
      str = inet_ntop(AF_INET, &((struct sockaddr_in*)a->ai_addr)->sin_addr,
                      buff, sizeof(buff));
    } else if (a->ai_family == AF_INET6) {
      str = inet_ntop(AF_INET6, &((struct sockaddr_in6*)a->ai_addr)->sin6_addr,
                      buff, sizeof(buff));
    }
    if (str) {
      fprintf(stderr, "ntop name: %s\n", str);
    } else {
      fprintf(stderr, "inet_ntop error\n");
    }
  }
#endif
  int udpfd =
      socket(chosen->ai_family, chosen->ai_socktype, chosen->ai_protocol);
  if (udpfd < 0) {
#if 0
    fprintf(stderr, "Failed to open UDP socket, error number: %d\n", errno);
#endif
    freeaddrinfo(head);
    return Oosc_udp_create_error_couldnt_open_socket;
  }
  Oosc_dev *dev = malloc(sizeof(Oosc_dev));
  dev->fd = udpfd;
  dev->chosen = chosen;
  dev->head = head;
  *out_ptr = dev;
  return Oosc_udp_create_error_ok;
}

void oosc_dev_destroy(Oosc_dev *dev) {
  close(dev->fd);
  freeaddrinfo(dev->head);
  free(dev);
}

void oosc_send_datagram(Oosc_dev *dev, char const *data, Usz size) {
  ssize_t res = sendto(dev->fd, data, size, 0, dev->chosen->ai_addr,
                       dev->chosen->ai_addrlen);
  (void)res;
  // TODO handle this in UI somehow
#if 0
  if (res < 0) {
    fprintf(stderr, "UDP message send failed\n");
    exit(1);
  }
#endif
}

static bool oosc_write_strn(char *restrict buffer, Usz buffer_size,
                            Usz *buffer_pos, char const *restrict in_str,
                            Usz in_str_len) {
  // no overflow check, should be fine
  Usz in_plus_null = in_str_len + 1;
  Usz null_pad = (4 - in_plus_null % 4) % 4;
  Usz needed = in_plus_null + null_pad;
  Usz cur_pos = *buffer_pos;
  if (cur_pos + needed >= buffer_size)
    return false;
  for (Usz i = 0; i < in_str_len; ++i) {
    buffer[cur_pos + i] = in_str[i];
  }
  buffer[cur_pos + in_str_len] = 0;
  cur_pos += in_plus_null;
  for (Usz i = 0; i < null_pad; ++i) {
    buffer[cur_pos + i] = 0;
  }
  *buffer_pos = cur_pos + null_pad;
  return true;
}

void oosc_send_int32s(Oosc_dev *dev, char const *osc_address, I32 const *vals,
                      Usz count) {
  char buffer[2048];
  Usz buf_pos = 0;
  if (!oosc_write_strn(buffer, sizeof(buffer), &buf_pos, osc_address,
                       strlen(osc_address)))
    return;
  Usz typetag_str_size = 1 + count + 1; // comma, 'i'... , null
  Usz typetag_str_null_pad = (4 - typetag_str_size % 4) % 4;
  if (buf_pos + typetag_str_size + typetag_str_null_pad > sizeof(buffer))
    return;
  buffer[buf_pos] = ',';
  ++buf_pos;
  for (Usz i = 0; i < count; ++i) {
    buffer[buf_pos + i] = 'i';
  }
  buffer[buf_pos + count] = 0;
  buf_pos += count + 1;
  for (Usz i = 0; i < typetag_str_null_pad; ++i) {
    buffer[buf_pos + i] = 0;
  }
  buf_pos += typetag_str_null_pad;
  Usz ints_size = count * sizeof(I32);
  if (buf_pos + ints_size > sizeof(buffer))
    return;
  for (Usz i = 0; i < count; ++i) {
    union {
      I32 i;
      U32 u;
    } pun;
    pun.i = vals[i];
    U32 u_ne = htonl(pun.u);
    memcpy(buffer + buf_pos, &u_ne, sizeof(u_ne));
    buf_pos += sizeof(u_ne);
  }
  oosc_send_datagram(dev, buffer, buf_pos);
}

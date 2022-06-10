#ifndef SAI_DROP_MARTIANS_P4_
#define SAI_DROP_MARTIANS_P4_

// Drop certain special-use (aka martian) addresses.

const ipv6_addr_t IPV6_MULTICAST_MASK =
  0xff00_0000_0000_0000_0000_0000_0000_0000;
const ipv6_addr_t IPV6_MULTICAST_VALUE =
  0xff00_0000_0000_0000_0000_0000_0000_0000;

#define IS_IPV6_MULTICAST(address) \
    (address & IPV6_MULTICAST_MASK == IPV6_MULTICAST_VALUE)

#define IS_IPV4_BROADCAST(address) \
    (address == 32w0xff_ff_ff_ff)

control drop_martians(in headers_t headers,
                      inout local_metadata_t local_metadata,
                      inout standard_metadata_t standard_metadata) {
  apply {
    // Drop the packet if:
    // - Src or dst IPv6 addresses are in multicast range; or
    // - Src or dst IPv4 addresses are the broadcast address.
    if ((headers.ipv6.isValid() &&
            (IS_IPV6_MULTICAST(headers.ipv6.src_addr) ||
             IS_IPV6_MULTICAST(headers.ipv6.dst_addr))) ||
        (headers.ipv4.isValid() &&
            (IS_IPV4_BROADCAST(headers.ipv4.src_addr) ||
             IS_IPV4_BROADCAST(headers.ipv4.dst_addr)))) {
        mark_to_drop(standard_metadata);
    }

    // TODO: Drop the rest of martian packets.
  }
}  // control drop_martians


#endif  // SAI_DROP_MARTIANS_P4_

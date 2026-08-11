#ifndef APPTRAVERSE_EXAMPLES_ANDROID_SYSTEM_DNS_RESOLVER_H_
#define APPTRAVERSE_EXAMPLES_ANDROID_SYSTEM_DNS_RESOLVER_H_

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include <cstring>
#include <string>
#include <vector>

#include "aether/all.h"

#include "apptraverse/object_macros.h"

namespace apptraverse::examples {

// Android c-ares often cannot read net.dns*; libc getaddrinfo still works
// through netd and is enough for Aether registration / cloud DNS.
class AndroidSystemDnsResolver final : public ae::DnsResolver {
  APPTRAVERSE_OBJECT(AndroidSystemDnsResolver, ae::DnsResolver, 0)

 protected:
  AndroidSystemDnsResolver() = default;

 public:
  explicit AndroidSystemDnsResolver(ae::ObjProp prop, ae::ObjPtr<ae::Aether> aether)
      : DnsResolver{prop}, aether_{std::move(aether)} {}

  AE_OBJECT_REFLECT(AE_MMBR(aether_))

  ae::ResolveSender Resolve(ae::NamedAddr const& name_address,
                            std::uint16_t port_hint,
                            ae::Protocol protocol_hint) override {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    int const status =
        ::getaddrinfo(name_address.name.c_str(), nullptr, &hints, &result);
    if (status != 0 || result == nullptr) {
      return ae::ex::just_error(1);
    }

    std::vector<ae::Endpoint> endpoints;
    for (addrinfo* node = result; node != nullptr; node = node->ai_next) {
      if (node->ai_family != AF_INET || node->ai_addr == nullptr) {
        continue;
      }
      auto const* sin = reinterpret_cast<sockaddr_in const*>(node->ai_addr);
      ae::IpV4Addr ipv4{};
      std::memcpy(&ipv4.ipv4_value, &sin->sin_addr.s_addr,
                  sizeof(ipv4.ipv4_value));
      endpoints.push_back(ae::Endpoint{{ipv4, port_hint}, protocol_hint});
    }
    ::freeaddrinfo(result);

    if (endpoints.empty()) {
      return ae::ex::just_error(1);
    }
    return ae::ex::just(std::move(endpoints));
  }

 private:
  ae::ObjPtr<ae::Aether> aether_;
};

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_ANDROID_SYSTEM_DNS_RESOLVER_H_

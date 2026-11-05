#include "minimal_app_android_embedded_profile.h"

#include "apptraverse/embedded_distilled_domain.h"

#include "embedded_distilled_minimal_app_supply.h"

namespace apptraverse::example::minimal {

MinimalAppEmbeddedProfile::MinimalAppEmbeddedProfile(
    std::filesystem::path const& user_dir)
    : user{user_dir},
      overlay{user, backing,
              EmbeddedSupplyConstantIdSet(
                  embedded_distilled::minimal::kEmbeddedGraph)} {
  InstallEmbeddedDistilledBacking(backing,
                                  embedded_distilled::minimal::kEmbeddedGraph);
}

}  // namespace apptraverse::example::minimal

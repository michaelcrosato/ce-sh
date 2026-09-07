// Stable identifiers. Scene objects keep the same id for their whole lifetime; ids are never reused.
#pragma once

#include <compare>
#include <cstdint>

namespace lc {

inline constexpr std::uint32_t kInvalidId = 0xFFFFFFFFu;

template <class Tag>
struct StrongId {
    std::uint32_t value = kInvalidId;

    constexpr bool IsValid() const { return value != kInvalidId; }
    constexpr auto operator<=>(const StrongId&) const = default;
};

using MeshId = StrongId<struct MeshIdTag>;
using InstanceId = StrongId<struct InstanceIdTag>;
using MaterialId = StrongId<struct MaterialIdTag>;

}  // namespace lc

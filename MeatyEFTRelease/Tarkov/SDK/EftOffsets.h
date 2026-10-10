#pragma once

#include <cstdint>

namespace sdk
{
#define EFT_FIELD(group, name, className, fieldName, fallback, isStatic, required) \
    namespace group { inline std::uint64_t name = fallback; }
#define EFT_TYPE(group, name, className, fallback, required) \
    namespace group { inline std::uint64_t name = fallback; }
#define EFT_ALIAS(group, name, targetGroup, targetName) \
    namespace group { inline std::uint64_t& name = targetGroup::targetName; }
#include "EftOffsetDefinitions.inc"
#undef EFT_ALIAS
#undef EFT_TYPE
#undef EFT_FIELD

    namespace Runtime
    {

        inline std::uint64_t TypeInfoTableRva = 0x5F589D8;
        inline std::uint64_t Il2CppClassStaticFields = 0xB8;
    }
}

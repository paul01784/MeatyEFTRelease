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
        struct Il2CppClassLayout
        {
            static constexpr std::uint32_t Image = 0x00;
            static constexpr std::uint32_t Name = 0x10;
            static constexpr std::uint32_t Namespace = 0x18;
            static constexpr std::uint32_t Parent = 0x58;
            static constexpr std::uint32_t Fields = 0x80;
            static constexpr std::uint32_t StaticFields = 0xA0;
            static constexpr std::uint32_t InstanceSize = 0xF8;
            static constexpr std::uint32_t FieldCount = 0x124;
        };

        inline std::uint64_t TypeInfoTableRva = 0x75DA668;
        inline std::uint64_t InGameStatusRva = 0x72CF648;
        inline std::uint64_t GamePlayerOwnerRva = 0x731CE40;
        inline std::uint64_t Il2CppClassStaticFields = Il2CppClassLayout::StaticFields;
    }
}

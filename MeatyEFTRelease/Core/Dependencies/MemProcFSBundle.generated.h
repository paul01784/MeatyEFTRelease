#pragma once

#include <array>
#include <cstdint>

namespace MemProcFSBundle
{
    struct ExpectedFile
    {
        const wchar_t* name;
        const char* sha256;
    };

    inline constexpr const char* Tag = "v5.19";
    inline constexpr const char* Name = "MemProcFS 5.19.4 / LeechCore 2.23.5";
    inline constexpr std::uint64_t VmmMajor = 5;
    inline constexpr std::uint64_t VmmMinor = 19;
    inline constexpr std::uint64_t VmmRevision = 4;
    inline constexpr std::array<ExpectedFile, 5> Files{{
        {L"vmm.dll", "8910de185be21844d827a08494ebb529ba9d1c49a1d1f4e3e9089e28d3df6441"},
        {L"leechcore.dll", "d1ee02255107258d279e713122d5d12e0407523695bdab221a6fc257deee5cb1"},
        {L"FTD3XX.dll", "3c0fc158fd4aa604c526d58ed8274ae830d53e69e01210cc5b5fa54ec5a9b7c0"},
        {L"FTD3XXWU.dll", "c507cb40f188740c4a7ba3aae4f848a305192b9b7c9e8e9264b1d66335824bfa"},
        {L"info.db", "7ff322ea13be0b0b519c85f9cc33cbb29eb44a795af7485c05e893a089c3386a"}
    }};
}
#pragma once

#include "MemProcFSBundle.generated.h"
#include "../../libs/vmmdll.h"

#include <filesystem>
#include <string>

namespace RuntimeDependencies
{
    struct ValidationResult
    {
        bool success = false;
        std::string message;
    };

    inline constexpr const char* BundleName = MemProcFSBundle::Name;

    [[nodiscard]] ValidationResult Validate(const std::filesystem::path& applicationDirectory);
    [[nodiscard]] bool Load(const std::filesystem::path& applicationDirectory, std::string& error);
    [[nodiscard]] bool ValidateVmmHandle(VMM_HANDLE handle, std::string& error);
    [[nodiscard]] std::string ExpectedHashForUpdaterPath(const std::string& relativePath);
}

#pragma once
#include <Windows.h>
#include <atomic>
#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
namespace SafeConfigFile {
inline std::atomic<unsigned long long> sequence{0};
inline std::filesystem::path UniqueSibling(const std::filesystem::path& path, const wchar_t* suffix) {
    auto result=path;
    result += std::wstring(suffix)+std::to_wstring(GetCurrentProcessId())+L"-"+
        std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(sequence.fetch_add(1));
    return result;
}
inline bool WriteAtomically(const std::filesystem::path& target, std::string_view content) {
    std::filesystem::path temporary;
    HANDLE file=INVALID_HANDLE_VALUE;
    bool success=false;
    try {
        if (!target.parent_path().empty()) std::filesystem::create_directories(target.parent_path());
        temporary=UniqueSibling(target,L".tmp-");
        file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) return false;
        size_t offset=0;
        bool complete=true;
        while(offset<content.size()) {
            const DWORD amount=static_cast<DWORD>((std::min)(content.size()-offset,size_t{1048576}));
            DWORD written=0;
            if(!WriteFile(file,content.data()+offset,amount,&written,nullptr) || written!=amount) {complete=false;break;}
            offset+=written;
        }
        if(complete) complete=FlushFileBuffers(file)!=0;
        const bool closed=CloseHandle(file)!=0;
        file=INVALID_HANDLE_VALUE;
        if(complete && closed)
            success=MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    } catch (...) {}
    if(file!=INVALID_HANDLE_VALUE) CloseHandle(file);
    if(!success && !temporary.empty()) {std::error_code error;std::filesystem::remove(temporary,error);}
    return success;
}
inline bool PreserveFailed(const std::filesystem::path& target, std::filesystem::path& backup) {
    try {
        std::error_code error;
        const bool exists=std::filesystem::exists(target,error);
        if(error) return false;
        if(!exists) {backup.clear();return true;}
        const auto destination=UniqueSibling(target,L".invalid-");
        if(!CopyFileW(target.c_str(),destination.c_str(),TRUE)) return false;
        backup=destination;
        return true;
    } catch (...) {return false;}
}
}

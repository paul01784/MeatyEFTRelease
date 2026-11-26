"""Exercise the production initialization read policy with synthetic failed/short reads.
Run in a VS x64 Native Tools prompt: python tests/run-player-init-tests.py
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'MeatyEFTRelease/Tarkov/GameWorld/Player/PlayerMemoryAccess.h').read_text(encoding='utf-8-sig')
test = r'''
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <span>
#include <string>
#include <vector>
enum class DmaCacheMode { Cached, Uncached };
namespace Utils { bool valid_pointer(uint64_t p) { return p > 0xffffff && p < 0x800000000000; } }
struct Memory {
    struct ScatterReadRequest { uint64_t address; void* buffer; size_t size; };
    enum class TryScatterReadResult { Success, Failed };
    int scatterCalls = 0, directCalls = 0;
    bool scatterOk = true, shortRead = false, directOk = true;
    uint64_t contents = 0x2000000;
    TryScatterReadResult TryReadScatter(const ScatterReadRequest* r, size_t n,
        DmaCacheMode, const char*, std::span<DWORD> bytes) {
        assert(n == 1 && r->size <= sizeof(contents));
        ++scatterCalls;
        memcpy(r->buffer, &contents, r->size);
        bytes[0] = scatterOk ? static_cast<DWORD>(r->size - (shortRead ? 1 : 0)) : 0;
        return scatterOk ? TryScatterReadResult::Success : TryScatterReadResult::Failed;
    }
    bool Read(uint64_t, void* buffer, size_t size, DmaCacheMode mode, const char*) {
        assert(mode == DmaCacheMode::Uncached && size <= sizeof(contents));
        ++directCalls;
        memcpy(buffer, &contents, size);
        return directOk;
    }
} mem;
'''
test += source[source.index('namespace PlayerMemoryAccess'):]
test += r'''
int main() {
    using namespace PlayerMemoryAccess;
    uint64_t value = 123;
    mem.scatterOk = false;
    assert(!tryRead(0x1000000, value));
    assert(value == 0 && mem.directCalls == 0); // Runtime must never gain recovery reads.
    InitializationReads outer;
    {
        InitializationScope scope(outer);
        initializationStage("profile");
        assert(tryReadPointer(0x1000000, value) && value == mem.contents);
        assert(outer.directRecovered == 1 && mem.directCalls == 1);
        mem.scatterOk = true;
        mem.shortRead = true;
        assert(tryReadPointer(0x1000000, value)); // Short success still needs exact recovery.
        assert(outer.directRecovered == 2);
        mem.shortRead = false;
        assert(tryReadPointer(0x1000000, value));
        assert(mem.directCalls == 2); // Complete scatter incurs no extra reads.
        InitializationReads inner;
        {
            InitializationScope nested(inner);
            initializationStage("movement context");
            mem.scatterOk = false;
            mem.directOk = false;
            assert(!tryRead(0x1000000, value) && value == 0);
            assert(inner.failedAddress == 0x1000000);
            assert(std::string(inner.failure) == "unreadable");
            assert(std::string(inner.stage) == "movement context");
        }
        assert(initializationReads == &outer);
        mem.scatterOk = true;
        mem.contents = 0; // Complete reads of null pointers remain invalid.
        assert(!tryReadPointer(0x1000000, value));
        assert(std::string(outer.failure) == "invalid_pointer");
        assert(std::string(outer.stage) == "profile");
    }
    assert(initializationReads == nullptr);
    mem.scatterOk = false;
    const int directCalls = mem.directCalls;
    assert(!tryRead(0x1000000, value) && mem.directCalls == directCalls);
    std::cout << "PASS: initialization-only recovery, exact lengths, failed-read clearing, pointer validation, scope restoration\n";
}
'''
with tempfile.TemporaryDirectory(prefix='meaty-player-init-') as temporary:
    target = Path(temporary)
    cpp, exe = target / 'init-test.cpp', target / 'init-test.exe'
    cpp.write_text(test, encoding='utf-8')
    subprocess.run(['cl', '/nologo', '/std:c++20', '/EHsc', '/W4', str(cpp), '/Fe:' + str(exe)], cwd=target, check=True)
    subprocess.run([str(exe)], check=True)

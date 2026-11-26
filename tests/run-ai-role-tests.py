"""Compile production role resolution with a simulated memory reader; no DMA needed.

Run from a VS x64 Native Tools prompt: python tests/run-ai-role-tests.py
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'MeatyEFTRelease/Tarkov/GameWorld/Player/ObservedPlayer.cpp').read_text(encoding='utf-8-sig')

def function(name):
    start = source.index(name)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

test = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <string>
enum class PlayerKind { Observed, Client, Local };
enum class PlayerType { AIScav, AIBoss, AIRaider };
enum class EPlayerSide { Savage, Usec };
enum class DmaCacheMode { Uncached };
struct AIRole {
    std::string Name;
    PlayerType Type;
    bool IsBlackDivision = false, IsCultist = false;
};
struct Player {
    PlayerKind kind = PlayerKind::Observed;
    PlayerKind getKind() const { return kind; }
    EPlayerSide playerSide = EPlayerSide::Savage;
    PlayerType type = PlayerType::AIScav;
    uint64_t instance = 0x1000000;
    bool isAi = true, isBTR = false, isDead = false, hasExfiled = false;
    bool isZombie = false, isBoss = false, isBlackDivision = false, isCultist = false;
    std::string voice, name = "Ai", aiRoleReadStatus = "not_requested";
    unsigned int aiRoleReadAttempts = 0;
};
namespace sdk::ObservedPlayerView { constexpr uint64_t Voice = 0x40; }
namespace PlayerMemoryAccess {
    bool pointerReadable = true;
    std::string stringValue;
    unsigned int reads = 0;
    bool tryReadPointer(uint64_t address, uint64_t& pointer, DmaCacheMode) {
        assert(address == 0x1000040);
        ++reads;
        pointer = pointerReadable ? 0x2000000 : 0;
        return pointerReadable;
    }
    std::string readString(uint64_t pointer) {
        assert(pointer == 0x2000000);
        ++reads;
        return stringValue;
    }
}
struct ObservedPlayer { static bool tryRefreshAiRole(Player&); };
'''
for name in ('bool containsIgnoreCase(', 'AIRole getAiRole(', 'bool ObservedPlayer::tryRefreshAiRole('):
    test += function(name) + '\n'
test += r'''
int main() {
    using namespace PlayerMemoryAccess;
    Player p;
    pointerReadable = false;
    assert(!ObservedPlayer::tryRefreshAiRole(p));
    assert(p.name == "Ai" && p.voice.empty());
    assert(p.aiRoleReadStatus == "voice_pointer_unreadable" && p.aiRoleReadAttempts == 1);
    pointerReadable = true;
    assert(!ObservedPlayer::tryRefreshAiRole(p));
    assert(p.aiRoleReadStatus == "voice_string_unreadable_or_empty" && p.aiRoleReadAttempts == 2);
    stringValue = "BossKnight";
    assert(ObservedPlayer::tryRefreshAiRole(p));
    assert(p.name == "Knight" && p.isBoss && p.type == PlayerType::AIBoss);
    assert(p.voice == stringValue && p.aiRoleReadStatus == "resolved" && p.aiRoleReadAttempts == 3);
    const auto priorReads = reads;
    pointerReadable = false;
    stringValue.clear();
    assert(!ObservedPlayer::tryRefreshAiRole(p));
    assert(reads == priorReads && p.name == "Knight" && p.isBoss);

    // All existing classifications still resolve, including special category flags.
    struct Case { const char* voice; const char* name; PlayerType type; bool boss, cultist, black, zombie; };
    const Case cases[] = {
        {"scav_1", "Scav", PlayerType::AIScav, false, false, false, false},
        {"USEC_2", "Usec", PlayerType::AIRaider, false, false, false, false},
        {"bear_1", "Bear", PlayerType::AIRaider, false, false, false, false},
        {"BossBully", "Reshala", PlayerType::AIBoss, true, false, false, false},
        {"SectantPriest", "Priest", PlayerType::AIBoss, true, true, false, false},
        {"SectantWarrior", "Cultist", PlayerType::AIRaider, false, true, false, false},
        {"blackdivision", "BlackDiv", PlayerType::AIBoss, true, false, true, false},
        {"Zombie", "Zombie", PlayerType::AIScav, false, false, false, true},
        {"new_voice", "new_voice", PlayerType::AIScav, false, false, false, false}
    };
    pointerReadable = true;
    for (auto c : cases) {
        Player candidate;
        stringValue = c.voice;
        assert(ObservedPlayer::tryRefreshAiRole(candidate));
        assert(candidate.name == c.name && candidate.type == c.type);
        assert(candidate.isBoss == c.boss && candidate.isCultist == c.cultist);
        assert(candidate.isBlackDivision == c.black && candidate.isZombie == c.zombie);
    }
    // No reads or edits for local players, PMCs, player Scavs, BTR, or retired entries.
    for (int guard = 0; guard < 7; ++guard) {
        Player candidate;
        if (guard == 0) candidate.kind = PlayerKind::Local;
        if (guard == 1) candidate.kind = PlayerKind::Client;
        if (guard == 2) candidate.isAi = false;
        if (guard == 3) candidate.playerSide = EPlayerSide::Usec;
        if (guard == 4) candidate.isBTR = true;
        if (guard == 5) candidate.isDead = true;
        if (guard == 6) candidate.hasExfiled = true;
        auto count = reads;
        assert(!ObservedPlayer::tryRefreshAiRole(candidate));
        assert(reads == count && candidate.aiRoleReadAttempts == 0 && candidate.name == "Ai");
    }
    std::cout << "PASS: failed identity reads recover; classifications and guards preserved; resolved roles stop reading\n";
}
'''
with tempfile.TemporaryDirectory(prefix='meaty-ai-role-') as temporary:
    target = Path(temporary)
    cpp, exe = target / 'role-test.cpp', target / 'role-test.exe'
    cpp.write_text(test, encoding='utf-8')
    subprocess.run(['cl', '/nologo', '/std:c++20', '/EHsc', '/W4', str(cpp), '/Fe:' + str(exe)], cwd=target, check=True)
    subprocess.run([str(exe)], check=True)

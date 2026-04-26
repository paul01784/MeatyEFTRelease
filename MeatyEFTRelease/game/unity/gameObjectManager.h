#pragma once
#include <cstdint>
#include <string>
#include <cstring>
#include "../../memory/Memory.h"
#include "../headers/unitysdk.h"
#include "../headers/sdk.h"


static const std::vector<std::string> MapNames =
{
    "default",
    "Labyrinth",
    "woods",
    "shoreline",
    "rezervbase",
    "laboratory",
    "interchange",
    "factory4_day",
    "factory4_night",
    "bigmap",
    "lighthouse",
    "tarkovstreets",
    "Sandbox",
    "Sandbox_high",
    "Sandbox_start"
};

// Case-insensitive comparison
bool iequals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return true;
}

// Check if map exists in vector
bool mapExists(const std::string& map)
{
    return std::any_of(MapNames.begin(), MapNames.end(),
        [&](const std::string& s) { return iequals(s, map); });
}

// Trim leading and trailing whitespace
std::string trims(const std::string& s)
{
    auto start = s.begin();
    while (start != s.end() && std::isspace(*start)) ++start;

    auto end = s.end();
    do { --end; } while (end != start && std::isspace(*end));

    return std::string(start, end + 1);
}

#pragma pack(push, 8)
struct LinkedListObject
{
    uint64_t PreviousObjectLink; // 0x0
    uint64_t NextObjectLink;     // 0x8
    uint64_t ThisObject;         // 0x10
};
#pragma pack(pop)

struct GameObjectManager
{
    uint64_t base() const
    {
        return mainGame.gameObjectManager;   // pull live value from mainGame instance
    }

    // Offsets
    static constexpr uint64_t OFFSET_LastActiveNode = 0x20;
    static constexpr uint64_t OFFSET_ActiveNodes = 0x28;

    // Accessors
    uint64_t LastActiveNode() const
    {
        return mem.Read<uint64_t>(base() + OFFSET_LastActiveNode);
    }

    uint64_t ActiveNodes() const
    {
        return mem.Read<uint64_t>(base() + OFFSET_ActiveNodes);
    }

    
    uint64_t GetObjectFromList(const std::string& objectName) const
    {
        // Read first node + last node
        LinkedListObject currentObject =
            mem.Read<LinkedListObject>(ActiveNodes());

        LinkedListObject lastObject =
            mem.Read<LinkedListObject>(LastActiveNode());

        // If the list is not empty
        if (currentObject.ThisObject != 0x0)
        {
            while (currentObject.ThisObject != 0x0 &&
                currentObject.ThisObject != lastObject.ThisObject)
            {
                // Read object name pointer
                uint64_t namePtr = mem.Read<uint64_t>(
                    currentObject.ThisObject + UnityOffsets::GameObject_NameOffset);

                // Read UTF-8 name
                std::string name =
                    mem.readUTF8String(namePtr, 64);

                std::cout << "Object Name : " << name << " @ 0x" << std::hex << currentObject.ThisObject << std::endl;;

                // Compare case-insensitive
                if (_stricmp(name.c_str(), objectName.c_str()) == 0)
                {
                    return currentObject.ThisObject;
                }

                // Move to next linked-list node
                currentObject =
                    mem.Read<LinkedListObject>(currentObject.NextObjectLink);
            }
        }

        return 0x0;
    }

    uint64_t GetGameWorldFromList(const std::string& objectName) const
    {
        // Read first node + last node
        LinkedListObject currentObject =
            mem.Read<LinkedListObject>(ActiveNodes());

        LinkedListObject lastObject =
            mem.Read<LinkedListObject>(LastActiveNode());

        // If the list is not empty
        if (currentObject.ThisObject != 0x0)
        {
            while (currentObject.ThisObject != 0x0 &&
                currentObject.ThisObject != lastObject.ThisObject)
            {
                // Read object name pointer
                uint64_t namePtr = mem.Read<uint64_t>(
                    currentObject.ThisObject + UnityOffsets::GameObject_NameOffset);

                // Read UTF-8 name
                std::string name =
                    mem.readUTF8String(namePtr, 64);

                // Compare case-insensitive
                if (_stricmp(name.c_str(), objectName.c_str()) == 0)
                {
                    // We have a Match, lets check its contents!

                    //localgameworld ptr
                    uint64_t localGameWorld = 0x0;
                    localGameWorld = mem.ReadChain(currentObject.ThisObject, { UnityOffsets::GameObject_ComponentsOffset, 0x18, UnityOffsets::Component_ObjectClassOffset });
                    if (Utils::valid_pointer(localGameWorld))
                    {
                        //storage 
                        uint64_t localPlayer = 0x0;
                        uint64_t mapPtr = 0x0;
                        std::string map = "";

                        //check map returned
                        mapPtr = mem.Read<uint64_t>(localGameWorld + sdk::GameWorld::Location);
                        if (!Utils::valid_pointer(mapPtr)) //check if we are offline mode
                        {
                            localPlayer = mem.Read<uint64_t>(localGameWorld + sdk::ClientLocalGameWorld::MainPlayer);
                            mapPtr = mem.Read<uint64_t>(localPlayer + sdk::Player::Location);
                            mainGame.onlineRaid = false;
                        }
                        else
                        {
                            //must be online
                            localPlayer = mem.Read<uint64_t>(localGameWorld + sdk::ClientLocalGameWorld::MainPlayer);
                            mainGame.onlineRaid = false;
                        }

                        map = mem.readUnicodeString(mapPtr + 0x14, mem.Read<int>(static_cast<SIZE_T>(mapPtr) + 0x10));
                        //std::cout << "Detected Poss Map :" << map.c_str() << std::endl;

                        if (mapExists(map.c_str())) // We are in a real map!
                        {
                            std::cout << "Detected Real Map :" << map.c_str() << std::endl;
                            mainGame.gameWorld = currentObject.ThisObject;
                            mainGame.localGameWorld = localGameWorld;
                            mainGame.localPlayerPtr = localPlayer;
                            mainGame.selectedLocation = map;

                            return currentObject.ThisObject;
                        }
                    }
                }

                // Move to next linked-list node
                currentObject =
                    mem.Read<LinkedListObject>(currentObject.NextObjectLink);
            }
        }

        return 0x0;
    }

    uint64_t GetGameWorldFromListFAST(const std::string& objectName, bool debugEnabled = false)
    {
        auto hex = [&](uint64_t v) {
            std::stringstream ss;
            ss << "0x" << std::hex << v;
            return ss.str();
            };

        auto dbg = [&](const std::string& msg) {
            if (debugEnabled)
                std::cout << "[DEBUG] " << msg << std::endl;
            };

        dbg("Starting GameWorld scan for: " + objectName);

        if (!mem.vHandle)
        {
            dbg("mem.vHandle invalid");
            return 0;
        }

        LinkedListObject headNode = mem.Read<LinkedListObject>(ActiveNodes());
        LinkedListObject tailNode = mem.Read<LinkedListObject>(LastActiveNode());

        dbg("headNode.ThisObject: " + hex(headNode.ThisObject));
        dbg("tailNode.ThisObject: " + hex(tailNode.ThisObject));

        if (!Utils::valid_pointer(headNode.ThisObject))
        {
            dbg("Head pointer invalid");
            return 0;
        }

        // STEP 1: Collect node addresses from linked list
        std::vector<uint64_t> nodeAddrs;
        nodeAddrs.reserve(512);

        uint64_t curr = headNode.NextObjectLink; // FIXED: skip sentinel head node
        size_t walkCount = 0;

        while (Utils::valid_pointer(curr) && curr != tailNode.ThisObject)
        {
            nodeAddrs.push_back(curr);
            walkCount++;

            curr = mem.Read<uint64_t>(curr + offsetof(LinkedListObject, NextObjectLink));

            if (walkCount > 10000)
            {
                dbg("Safety stop: too many nodes");
                break;
            }
        }

        dbg("WALK collected " + std::to_string(nodeAddrs.size()) + " nodes");

        if (nodeAddrs.empty())
        {
            dbg("No nodes collected");
            return 0;
        }

        const size_t count = nodeAddrs.size();

        // STEP 2: Scatter read LinkedListObjects
        std::vector<LinkedListObject> nodes(count);

        {
            auto h = mem.CreateScatterHandle();
            if (!h)
            {
                dbg("Failed CreateScatterHandle() for nodes");
                return 0;
            }

            for (size_t i = 0; i < count; i++)
                mem.AddScatterReadRequest(h, nodeAddrs[i], &nodes[i], sizeof(LinkedListObject));

            mem.ExecuteReadScatter(h);
            mem.CloseScatterHandle(h);
        }

        dbg("Scatter read LinkedListObjects complete");

        if (debugEnabled)
        {
            dbg("Node[0].ThisObject: " + hex(nodes[0].ThisObject));
            dbg("Node[0].Next: " + hex(nodes[0].NextObjectLink));
            dbg("Node[0].Prev: " + hex(nodes[0].PreviousObjectLink));
        }

        // STEP 3: Scatter read pointers to object names
        std::vector<uint64_t> namePtrs(count, 0);

        {
            auto h = mem.CreateScatterHandle();
            if (!h)
            {
                dbg("Failed CreateScatterHandle() for namePtrs");
                return 0;
            }

            for (size_t i = 0; i < count; i++)
            {
                if (!Utils::valid_pointer(nodes[i].ThisObject)) continue;
                mem.AddScatterReadRequest(
                    h,
                    nodes[i].ThisObject + UnityOffsets::GameObject_NameOffset,
                    &namePtrs[i],
                    sizeof(uint64_t)
                );
            }

            mem.ExecuteReadScatter(h);
            mem.CloseScatterHandle(h);
        }

        dbg("Scatter read namePtrs complete");


        // STEP 4: Read names & find "GameWorld"
        for (size_t i = 0; i < count; i++)
        {
            uint64_t np = namePtrs[i];
            if (!Utils::valid_pointer(np)) continue;

            std::string name = mem.readUTF8String(np, 64);

            //if (debugEnabled && !name.empty())
                //std::cout << "[DEBUG] Node[" << i << "] Name: " << name << std::endl;

            if (_stricmp(name.c_str(), objectName.c_str()) != 0)
                continue;

            dbg("MATCH FOUND at index " + std::to_string(i) + ": " + name);

            // Resolve GameWorld
            uint64_t gameWorld = mem.ReadChain(
                nodes[i].ThisObject,
                { UnityOffsets::GameObject_ComponentsOffset, 0x18, UnityOffsets::Component_ObjectClassOffset });

            dbg("gameWorld ptr: " + hex(gameWorld));

            if (!Utils::valid_pointer(gameWorld))
                continue;

            uint64_t mapPtr = mem.Read<uint64_t>(gameWorld + sdk::GameWorld::Location);
            uint64_t localPlayer = 0;

            if (!Utils::valid_pointer(mapPtr))
            {
                dbg("mapPtr invalid -> offline fallback");
                localPlayer = mem.Read<uint64_t>(gameWorld + sdk::ClientLocalGameWorld::MainPlayer);
                mapPtr = mem.Read<uint64_t>(localPlayer + sdk::Player::Location);
                mainGame.onlineRaid = false;
            }
            else
            {
                dbg("mapPtr valid -> online");
                localPlayer = mem.Read<uint64_t>(gameWorld + sdk::ClientLocalGameWorld::MainPlayer);
                mainGame.onlineRaid = true;
            }

            int len = mem.Read<int>(mapPtr + 0x10);
            std::string map = mem.readUnicodeString(mapPtr + 0x14, len);

            dbg("Detected Map: " + map);

            if (!mapExists(map.c_str()))
            {
                dbg("Map not recognized: " + map);
                continue;
            }

            dbg("Valid map! SUCCESS");

            mainGame.gameWorld = nodes[i].ThisObject;
            mainGame.localGameWorld = gameWorld;
            mainGame.localPlayerPtr = localPlayer;
            mainGame.selectedLocation = map;

            return nodes[i].ThisObject;
        }

        dbg("Completed scan: no match");
        return 0;
    }
};

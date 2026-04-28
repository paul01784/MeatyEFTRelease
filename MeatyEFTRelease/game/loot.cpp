#include "../app/includes.h"
#include <glm/glm.hpp>
#include "../game/headers/loot.h"
#include "../app/globals.h"
#include "../memory/Memory.h"
#include "../game/headers/utils.h"
#include "../game/headers/maingame.h"
#include "../game/headers/sdk.h"
#include "../game/headers/unityHelper.h"
#include "../app/market.h"
#include <map>
#include "headers/unitysdk.h"
#include "headers/transform.h"
#include "headers/questManager.h"
#include "headers/wishlist.h"
#include "headers/dogtag.h"
#include "headers/players.h"


//define storage container
std::vector<LootFilters> lootFilters;
std::vector<LootList> lootList;

loot Loot;

static const std::unordered_set<std::string> skipNames =
{
    "Compass",
    "ArmBand",
    "Pockets",
    "SecuredContainer"
};

LootList updateLootDetails(std::string bsgid, LootList& item)
{
    for (auto& ml : marketList)
    {
        if (ml.bsgid != bsgid.c_str())
            continue;

        item.longName = ml.name;
        item.shortName = ml.shortName;
        item.traderPrice = ml.traderPrice;
        item.avgMarketPrice = ml.marketPrice;

        return item;
    }
}
void loot::updateCorpseRequirements()
{
    auto now = std::chrono::steady_clock::now();

    if (now - this->lastCorpseEquipUpdate > std::chrono::seconds(20))
    {
        this->lastCorpseEquipUpdate = now;

        for (auto& loot : lootList)
        {
            if (!loot.isCorpse)
                continue;

            scanCorpseEquipment(loot.m_interactiveClass, loot, true);

            //get corpse value
            int corpseValue = 0;
            for (auto& corpseItems : loot.corpseEquip)
            {
                corpseValue += corpseItems.value;
            }
            loot.corpseValue = corpseValue;
        }
        
    }

    
}

void updateLootRequirments()
{
    // loot with value if enabled
    if (lootGlobals::enableValueLoot)
    {
        for (auto& loot : lootList)
        {
            if (loot.isItem)
            {
                if (loot.avgMarketPrice > lootGlobals::valueLootFrom)
                {

                    //we have a item and filter
                    loot.wanted = true;
                    loot.color = coloursGlobals::valueLootColour;
                }
            }
        }
    }

    // loot from quests
    if (lootGlobals::enableQuestLoot)
    {
        for (auto& questItems : masterItems)
        {
            if (questItems == "")
                continue;

            for (auto& loot : lootList)
            {
                if (loot.isItem || loot.isQuestItem)
                {
                    if (loot.bsgId.c_str() == questItems.c_str())
                    {

                        //we have a item and filter 
                        loot.wanted = true;
                        loot.color = coloursGlobals::questColour;
                        break;
                    }
                }
            }
        }
    }

    //loot from wishList
    if (lootGlobals::enableWishListLoot)
    {
        for (auto& wishListItem : wishListData)
        {
            for (auto& loot : lootList)
            {
                if (loot.isItem)
                {
                    if (loot.bsgId.c_str() == wishListItem.bsgId.c_str())
                    {

                        //we have a item and filter item matches lootlist item!
                        loot.wanted = true;
                        loot.color = coloursGlobals::wishListColour;
                        break;
                    }
                }
            }
        }
    }


    // loot filters
    for (auto& filter : lootFilters)
    {
        if (!filter.active)
            continue;

        for (size_t i = 0; i < filter.lootItems.size(); ++i)
        {
            //LOGS.logInfo(("loot item wanted" + filter.lootItems[i].bsgid).c_str());
            //loop over our current raid lootlist
            for (auto& loot : lootList)
            {
                if (loot.isItem)
                {
                    if (loot.bsgId.c_str() == filter.lootItems[i].bsgid.c_str())
                    {

                        //we have a item and filter item matches lootlist item!
                        loot.wanted = true;
                        loot.color = filter.filterColour;
                    }
                }
            }
        }
    }

    //update container requirements
    for (auto& loot : lootList)
    {
        if (!loot.isContainer)
            continue;

        if (loot.shortName == "Duffle Bag")
            if (Loot.drawDuffle)
                loot.wanted = true;

        if (loot.shortName == "Drawer")
            if (Loot.drawDrawer)
                loot.wanted = true;

        if (loot.shortName == "Drawer")
            if (Loot.drawDrawer)
                loot.wanted = true;

        if (loot.shortName == "Safe")
            if (Loot.drawSafe)
                loot.wanted = true;

        if (loot.shortName == "Weapon Box")
            if (Loot.drawWeaponBox)
                loot.wanted = true;

        if (loot.shortName == "Technical Crate")
            if (Loot.drawTechCrate)
                loot.wanted = true;

        if (loot.shortName == "Ration Crate")
            if (Loot.drawRationCrate)
                loot.wanted = true;

        if (loot.shortName == "Medical Crate")
            if (Loot.drawMedicalCrate)
                loot.wanted = true;

        if (loot.shortName == "Jacket")
            if (Loot.drawJacket)
                loot.wanted = true;

        if (loot.shortName == "Med Package")
            if (Loot.drawMedPackage)
                loot.wanted = true;

        if (loot.shortName == "Med Box")
            if (Loot.drawMedBox)
                loot.wanted = true;

        if (loot.shortName == "Toolbox")
            if (Loot.drawToolbox)
                loot.wanted = true;

        if (loot.shortName == "Grenade Box")
            if (Loot.drawGrenadeBox)
                loot.wanted = true;

        if (loot.shortName == "Buried Stash")
            if (Loot.drawBuriedStash)
                loot.wanted = true;

        if (loot.shortName == "Ground Cache")
            if (Loot.drawGroundCache)
                loot.wanted = true;

        if (loot.shortName == "Wooden Crate")
            if (Loot.drawWoodenCrate)
                loot.wanted = true;

        if (loot.shortName == "Suitcase")
            if (Loot.drawSuitcase)
                loot.wanted = true;

        if (loot.shortName == "Ammo Box")
            if (Loot.drawAmmoBox)
                loot.wanted = true;

        if (loot.shortName == "Dead Body")
            if (Loot.drawDeadBody)
                loot.wanted = true;

        if (loot.shortName == "PC Block")
            if (Loot.drawPCBlock)
                loot.wanted = true;

        if (loot.shortName == "Register")
            if (Loot.drawRegister)
                loot.wanted = true;

    }


}


std::string getContainerName(const std::string& bsgid) {

    // Check against all bsgid values and return corresponding names
    if (bsgid == "578f87a3245977356274f2cb") return "Duffle Bag";
    else if (bsgid == "578f87b7245977356274f2cd") return "Drawer";
    else if (bsgid == "578f8782245977354405a1e3") return "Safe";
    else if (bsgid == "5909d89086f77472591234a0") return "Weapon Box";
    else if (bsgid == "5909d7cf86f77470ee57d75a") return "Weapon Box";
    else if (bsgid == "5909d76c86f77471e53d2adf") return "Weapon Box";
    else if (bsgid == "5909d5ef86f77467974efbd8") return "Weapon Box";
    else if (bsgid == "5d6fd45b86f774317075ed43") return "Technical Crate";
    else if (bsgid == "5d6fd13186f77424ad2a8c69") return "Ration Crate";
    else if (bsgid == "5d6fe50986f77449d97f7463") return "Medical Crate";
    else if (bsgid == "578f8778245977358849a9b5") return "Jacket";
    else if (bsgid == "5937ef2b86f77408a47244b3") return "Jacket";
    else if (bsgid == "59387ac686f77401442ddd61") return "Jacket";
    else if (bsgid == "5909d4c186f7746ad34e805a") return "Med Package";
    else if (bsgid == "5909d24f86f77466f56e6855") return "Med Box";
    else if (bsgid == "5909d50c86f774659e6aaebe") return "Toolbox";
    else if (bsgid == "5909d36d86f774660f0bb900") return "Grenade Box";
    else if (bsgid == "5d6d2bb386f774785b07a77a") return "Buried Stash";
    else if (bsgid == "5d6d2b5486f774785c2ba8ea") return "Ground Cache";
    else if (bsgid == "578f87ad245977356274f2cc") return "Wooden Crate";
    else if (bsgid == "5c052cea86f7746b2101e8d8") return "Suitcase";
    else if (bsgid == "5909d45286f77465a8136dc6") return "Ammo Box";
    else if (bsgid == "5909e4b686f7747f5b744fa4") return "Dead Body";
    else if (bsgid == "59139c2186f77411564f8e42") return "PC Block";
    else if (bsgid == "578f879c24597735401e6bc6") return "Register";
    else if (bsgid == "6582e6c6edf14c4c6023adf2") return "Dead Body";
    else if (bsgid == "6582e6d7b14c3f72eb071420") return "Dead Body";
    else if (bsgid == "67614e3a6a90e4f10b0b140d") return "Xmas Loot";

    // Return the input bsgid if no match is found
    return bsgid;
}

void loot::clearCache()
{
    lootList.clear();

    this->lootListP = NULL;
    this->lootListPtr = NULL;
    this->lootCount = 0;
}

bool loot::buildPointers()
{

    //check if pointers already set
    if (Utils::valid_pointer(this->lootListP) &&
        Utils::valid_pointer(this->lootListPtr)
        )
    {
        return TRUE;
    }

    this->lootListP = mem.Read<uint64_t>(mainGame.localGameWorld + sdk::ClientLocalGameWorld::LootList);
    if (!Utils::valid_pointer(this->lootListP))
        return false;

    this->lootListPtr = mem.Read<uint64_t>(this->lootListP + 0x10);
    if (!Utils::valid_pointer(this->lootListPtr))
        return false;

    this->lootCount = mem.Read<int>(this->lootListP + 0x18);
    if (this->lootCount == 0 || this->lootCount > 100000)
        return false;

    return true;

}

bool loot::get_lootCount()
{
    this->lootCount = mem.Read<int>(this->lootListP + 0x18);
    if (this->lootCount == 0 || this->lootCount > 100000)
        return false;

    return true;

}

bool loot::buildLootBuffer()
{
    // Ensure lootCount is within a reasonable range and valid
    if (this->lootCount <= 0 || this->lootCount > 100000)
    {
        this->clearCache();
        return false;
    }

    // Check if lootListPtr is valid
    if (!Utils::valid_pointer(this->lootListPtr))
    {
        this->clearCache();
        return false;
    }

    loot_buffer.resize(this->lootCount);

    // Now read the memory into the vector's internal data buffer
    if (!mem.Read(this->lootListPtr + 0x20, loot_buffer.data(), sizeof(uint64_t) * this->lootCount))
    {
        
        return false;
    }
    return true;
}

bool loot::isPointerInVector_lootList(uint64_t ptr) const {
    return std::any_of(lootList.begin(), lootList.end(),
        [ptr](const LootList& item) {
            return item.instance == ptr;
        });
}

void loot::scanCorpseEquipment(uint64_t interactive, LootList& lootList, bool update)
{
    if (!Utils::valid_pointer(interactive))
        return;

    try
    {
        uint64_t itemBase = mem.Read<uint64_t>(interactive + sdk::InteractiveLootItem::Item);
        if (!Utils::valid_pointer(itemBase))
            return;

        uint64_t slotsPtr = mem.Read<uint64_t>(itemBase + sdk::LootItemMod::Slots);
        if (!Utils::valid_pointer(slotsPtr))
            return;

        auto slotsRead = UnityArray<uint64_t>(slotsPtr);
        if (slotsRead.count == 0)
            return;

        // Build into temp first so old data stays valid while scanning
        std::vector<corpseEquipment> newCorpseEquip;
        newCorpseEquip.reserve(slotsRead.count);

        bool isPMC = false;
        std::vector<PlayerCache>& cache = players.getCache();
        for (auto& player : cache)
        {
            if (interactive == player.P_CorpseClass)
            {
                if (player.isPlayer)
                    isPMC = true;

                if (player.isPlayer || player.isBoss)
                    lootList.longName = player.name;

                break;
            }
        }

        for (auto& slotPtr : slotsRead)
        {
            if (!Utils::valid_pointer(slotPtr))
                continue;

            uint64_t namePtr = mem.Read<uint64_t>(slotPtr + sdk::Slot::ID);
            if (!Utils::valid_pointer(namePtr))
                continue;

            const int nameLen = mem.Read<int>(static_cast<SIZE_T>(namePtr) + 0x10);
            auto name = mem.readUnicodeString(namePtr + 0x14, nameLen);
            std::string slotName = TrimEFT(name);

            if (skipNames.contains(slotName))
                continue;

            if (slotName == "")
                continue;

            if (isPMC && slotName == "Scabbard")
                continue;

            uint64_t containedItem = mem.Read<uint64_t>(slotPtr + sdk::Slot::ContainedItem);
            if (!Utils::valid_pointer(containedItem))
                continue;

            uint64_t inventorytemplate = mem.Read<uint64_t>(containedItem + sdk::LootItem::Template);
            if (!Utils::valid_pointer(inventorytemplate))
                continue;

            auto mongoId = mem.Read<MongoID>(inventorytemplate + sdk::ItemTemplate::_id);
            auto id = TrimEFT(mongoId.ReadString(mem));
            if (id.empty())
                continue;

            corpseEquipment corpseEq{};

            for (auto& ml : marketList)
            {
                if (ml.bsgid != id)
                    continue;

                corpseEq.equipmentName = ml.shortName;
                corpseEq.value = (ml.marketPrice == 0) ? ml.traderPrice : ml.marketPrice;
                break;
            }

            for (auto& filter : lootFilters)
            {
                if (!filter.active)
                    continue;

                bool found = false;

                for (size_t i = 0; i < filter.lootItems.size(); ++i)
                {
                    if (id == filter.lootItems[i].bsgid)
                    {
                        corpseEq.wanted = true;
                        found = true;
                        break;
                    }
                }

                if (found)
                    break;
            }

            if (!corpseEq.wanted)
            {
                for (auto& quest : masterItems)
                {
                    if (quest.c_str() == id.c_str())
                    {
                        corpseEq.wanted = true;
                        break;
                    }
                }
            }

            if (!corpseEq.wanted)
            {
                for (auto& wishlist : wishListData)
                {
                    if (wishlist.bsgId.c_str() == id.c_str())
                    {
                        corpseEq.wanted = true;
                        break;
                    }
                }
            }

            if (!corpseEq.wanted)
            {
                if (corpseEq.value > lootGlobals::valueLootFrom && lootGlobals::enableValueLoot)
                    corpseEq.wanted = true;
            }

            newCorpseEquip.emplace_back(std::move(corpseEq));
        }

        // only replace once full scan completed
        lootList.corpseEquip = std::move(newCorpseEquip);
    }
    catch (...)
    {
        std::cout << "[LootCorpse] exception while processing corpse\n";
    }
}

std::vector<LootList>& loot::getCacheLoot() {
    return this->lootList;
}

std::string GetQuestItemDisplayName(const std::string& itemId)
{
    if (itemId.empty())
        return "";

    for (const auto& task : tarkovDevTasksData)
    {
        for (const auto& obj : task.objectives)
        {
            // Match quest item id
            if (!obj.questItemId.empty() && obj.questItemId == itemId)
            {
                return task.qName;
            }

        }
    }

    return "";
}

void loot::lootTask()
{
    try {
        if (!Utils::valid_pointer(mainGame.localPlayerPtr))
            return;
        
        if (!radarGlobals::drawLoot)
        {
            return;
        }

        if (!this->buildPointers())
        {
            LOGS.logError("[LOOT] Pointer Build Error");
            return;
        }

        if (!this->get_lootCount())
        {
            LOGS.logError("[LOOT] Count Error");
            return;
        }

        if (!this->buildLootBuffer())
        {
            LOGS.logError("[LOOT] Loot Buffer Error");
            return;
        }

        //go over lootlist in memory and see if we have it already

        for (auto& lootMemory : loot_buffer)
        {
            if (!Utils::valid_pointer(lootMemory))
                continue;

            if (this->isPointerInVector_lootList(lootMemory))
                continue; // already in list, skip

            // do memory reads on ptr and add to list

            LootList item;

            item.instance = lootMemory;

            // Interactive Class
            auto monoBehaviour = mem.Read<std::uint64_t>(item.instance + 0x10); if (!Utils::valid_pointer(monoBehaviour)) continue; // MonoBehaviourOffset
            item.m_interactiveClass = mem.Read<std::uint64_t>(monoBehaviour + UnityOffsets::Component_ObjectClassOffset);  if (!Utils::valid_pointer(item.m_interactiveClass)) continue; // InteractiveClass (ObjectClass)

            // GameObject (used for name + components)
            auto gameObject = mem.Read<std::uint64_t>(monoBehaviour + UnityOffsets::Component_GameObjectOffset); if (!Utils::valid_pointer(gameObject)) continue;  // GameObject*

            // ClassName (type/class name for this loot object)
            item.m_objectClassName = ReadName(item.instance, sizeof(item.m_objectClassName));  // ClassName

            // ObjectName (GameObject name)
            auto pGameObjectName = mem.Read<std::uint64_t>(gameObject + UnityOffsets::GameObject_NameOffset); if (!Utils::valid_pointer(pGameObjectName)) continue;  // char*/string ptr
            item.gameObjectName = mem.readString(pGameObjectName, 64); // ObjectName

            // TransformInternal
            auto components = mem.Read<std::uint64_t>(gameObject + UnityOffsets::GameObject_ComponentsOffset); if (!Utils::valid_pointer(components)) continue;// Components list ptr
            item.m_pointerToTransform1 = mem.Read<std::uint64_t>(components + 0x8); if (!Utils::valid_pointer(item.m_pointerToTransform1)) continue;
            //item.m_pointerToTransform2 = mem.Read<uint64_t>(item.m_pointerToTransform1 + 0x20);

            //std::cout << "-------------------------------------------------------------" << std::endl;
            //std::cout << "monoBehaviour : 0x" << std::hex << monoBehaviour << std::endl;
            //std::cout << "m_interactiveClass : 0x" << std::hex << item.m_interactiveClass << std::endl;
            //std::cout << "m_objectClassName : " << item.m_objectClassName << std::endl;
            //std::cout << "pGameObjectName : 0x" << std::hex << pGameObjectName << std::endl;
            //std::cout << "gameObjectName : " << item.gameObjectName << std::endl;
            //std::cout << "components : 0x" << std::hex << components << std::endl;
            //std::cout << "m_pointerToTransform1 : 0x" << std::hex << item.m_pointerToTransform1 << std::endl;
            //std::cout << "m_pointerToTransform2 : 0x" << std::hex << item.m_pointerToTransform2 << std::endl;

            // Location
            UnityTransform transform(item.m_pointerToTransform1);
            item.worldLocation = transform.UpdatePosition();


            if (item.worldLocation.x == 0.f || item.worldLocation.y == 0.f || item.worldLocation.z == 0.f)
                continue;
            
            if (Utils::Text::containsIgnoreCase(item.gameObjectName, "script"))
            {
                continue;
            }

            

            if (item.m_objectClassName == "ObservedLootItem")
            {
                item.isContainer = FALSE;
                item.isCorpse = FALSE;


                uint64_t item_template = mem.ReadChain(item.m_interactiveClass, { sdk::InteractiveLootItem::Item, sdk::LootItem::Template }); if (!Utils::valid_pointer(item_template)) continue;

                auto nameptrMonogo = mem.Read<MongoID>(item_template + sdk::ItemTemplate::_id);
                item.bsgId = nameptrMonogo.ReadString(mem);

                if (item.bsgId.empty())
                    continue;

                bool isQuest = mem.Read<bool>(item_template + sdk::ItemTemplate::QuestItem);

                if (!isQuest)
                {
                    item.isItem = TRUE;
                    item.isQuestItem = FALSE;

                    updateLootDetails(item.bsgId.c_str(), item);
                }
                else
                {
                    item.isContainer = FALSE;
                    item.isCorpse = FALSE;
                    item.isItem = FALSE;
                    item.isQuestItem = TRUE;

                    std::string questName = GetQuestItemDisplayName(item.bsgId.c_str());

                    if (!questName.empty())
                        item.shortName = questName;
                    else
                        item.shortName = item.gameObjectName;

                    item.shortName = "PICKUP";
                }

                //push item to list
                lootList.push_back(item);

                //std::cout << "[Loot] Adding Loot Item : " << item.shortName.c_str() << std::endl;
            }
            if (item.m_objectClassName == "LootableContainer")
            {
                item.isContainer = TRUE;
                item.isCorpse = FALSE;
                item.isItem = FALSE;
                item.isQuestItem = FALSE;

                if (Utils::Text::containsIgnoreCase(item.gameObjectName, "loot_collider"))
                {
                    //Airdrop
                    item.shortName = "AirDrop";

                    
                }
                else
                {
                    uint64_t itemTemplate = mem.ReadChain(item.m_interactiveClass, { sdk::LootableContainer::ItemOwner, sdk::LootableContainerItemOwner::RootItem, sdk::LootItem::Template }); if (!Utils::valid_pointer(itemTemplate)) continue;

                    auto nameptrMonogo = mem.Read<MongoID>(itemTemplate + sdk::ItemTemplate::_id);
                    item.bsgId = nameptrMonogo.ReadString(mem);

                    if (item.bsgId.empty())
                        continue;

                    item.shortName = getContainerName(item.bsgId.c_str());
                }

                //push item to list
                lootList.push_back(item);
                //std::cout << "[Loot] Adding Loot Container : " << item.shortName.c_str() << std::endl;
            }
            if (item.m_objectClassName == "Corpse" || item.m_objectClassName == "ObservedCorpse")
            {
                item.isContainer = FALSE;
                item.isCorpse = TRUE;
                item.isItem = FALSE;
                item.isQuestItem = FALSE;

                item.bsgId = "";
                item.shortName = "Corpse";

                //scan corpse for loot
                scanCorpseEquipment(item.m_interactiveClass, item);

                //get corpse value
                int corpseValue = 0;
                for (auto& corpseItems : item.corpseEquip)
                {
                    corpseValue += corpseItems.value;
                }
                item.corpseValue = corpseValue;

                //push item to list
                lootList.push_back(item);

                //std::cout << "[Loot] Adding Loot Corpse : " << item.shortName << std::endl;
            }

            
            //std::cout << "-------------------------------------------------------------" << std::endl;
        }

        updateLootRequirments(); 
        updateCorpseRequirements();


        for (auto& loot : lootList)
        {

            //update all distances
            loot.distance = std::trunc(glm::distance(mainGame.localLocation, loot.worldLocation));

            if (loot.isCorpse)
            {
                if (espGlobals::drawCorpse)
                {
                    loot.wanted = true;
                    loot.color = coloursGlobals::playerCorpse;
                }
                else
                {
                    loot.wanted = false;
                }

                //get dogtag info
                g_dogTagCache.ReadFromCorpse(loot.m_interactiveClass);

                
            }

            if (loot.shortName == "AirDrop")
            {
                //update position
                UnityTransform transform(loot.m_pointerToTransform1);
                loot.worldLocation = transform.UpdatePosition();
            }


            if (loot.isItem || loot.isQuestItem)
            {

                bool updated = false;
                bool ignore = false;
                
                // stop edit if quest or wishlist etc
                for (auto& questItems : masterItems)
                {
                    if (strcmp(loot.bsgId.c_str(), questItems.c_str()) == 0)
                    {
                        updated = true; // we still want it
                        loot.wanted = true;
                        loot.color = coloursGlobals::questColour;
                        ignore = true;
                        break;
                    }
                }
                if (!ignore)
                {
                    for (auto& wishlistItems : wishListData)
                    {
                        if (strcmp(loot.bsgId.c_str(), wishlistItems.bsgId.c_str()) == 0)
                        {
                            updated = true; // we still want it
                            loot.wanted = true;
                            loot.color = coloursGlobals::wishListColour;
                            ignore = true;
                        }
                    }
                }
                if (!ignore)
                {
                    
                        if (loot.avgMarketPrice > lootGlobals::valueLootFrom)
                        {
                            updated = true; // we still want it
                            loot.wanted = true;
                            loot.color = coloursGlobals::valueLootColour;
                            ignore = true;
                        }
                    
                }


                for (auto& filter : lootFilters)
                {

                    if (!ignore)
                    {
                        for (size_t i = 0; i < filter.lootItems.size(); ++i)
                        {
                            //LOGS.logInfo("Checking " + std::string(filter.lootItems[i].shortName) + " " + std::string(filter.lootItems[i].bsgid));

                            if (strcmp(loot.bsgId.c_str(), filter.lootItems[i].bsgid.c_str()) == 0)
                            {

                                if (!filter.active)
                                {
                                    loot.wanted = false;
                                }
                                else
                                {
                                    
                                    updated = true;
                                    loot.wanted = true;
                                    loot.color = filter.filterColour;
                                }

                            }

                        }
                    }
                }

                if (!updated && loot.wanted)
                {
                    if (!loot.forceWanted)
                        loot.wanted = false;
                }

            }

        }

        //update container requirements
        for (auto& loot : lootList)
        {
            if (!loot.isContainer)
                continue;

            if (loot.shortName == "AirDrop")
                if (Loot.drawAirDrops)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Duffle Bag")
                if (Loot.drawDuffle)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Drawer")
                if (Loot.drawDrawer)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Drawer")
                if (Loot.drawDrawer)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Safe")
                if (Loot.drawSafe)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Weapon Box")
                if (Loot.drawWeaponBox)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Technical Crate")
                if (Loot.drawTechCrate)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Ration Crate")
                if (Loot.drawRationCrate)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Medical Crate")
                if (Loot.drawMedicalCrate)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Jacket")
                if (Loot.drawJacket)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Med Package")
                if (Loot.drawMedPackage)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Med Box")
                if (Loot.drawMedBox)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Toolbox")
                if (Loot.drawToolbox)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Grenade Box")
                if (Loot.drawGrenadeBox)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Buried Stash")
                if (Loot.drawBuriedStash)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Ground Cache")
                if (Loot.drawGroundCache)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Wooden Crate")
                if (Loot.drawWoodenCrate)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Suitcase")
                if (Loot.drawSuitcase)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Ammo Box")
                if (Loot.drawAmmoBox)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Dead Body")
                if (Loot.drawDeadBody)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "PC Block")
                if (Loot.drawPCBlock)
                    loot.wanted = true;
                else
                    loot.wanted = false;

            if (loot.shortName == "Register")
                if (Loot.drawRegister)
                    loot.wanted = true;
                else
                    loot.wanted = false;

        }

        //cleanup current list when things get looted
        std::vector<uint64_t> itemsToRemove;

        for (auto& loot : lootList)
        {
            bool found = false;
            for (auto& buffer : loot_buffer)
            {
                if (buffer == loot.instance)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
                itemsToRemove.push_back(loot.instance);
        }

        for (uint64_t itemToRemove : itemsToRemove) {
            auto it = std::remove_if(lootList.begin(), lootList.end(),
                [itemToRemove](const LootList& item) { return item.instance == itemToRemove; });

            lootList.erase(it, lootList.end());
        }

    }
    catch (const std::exception& e) {
        LOGS.logError("Exception caught in lootThread: " + std::string(e.what()) + ". Retrying...");
        this->clearCache();
    }
    catch (...) {
        LOGS.logError("Unknown exception caught in lootThread. Retrying...");
        this->clearCache();
    }

}


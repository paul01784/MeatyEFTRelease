#pragma once
#include "../../memory/Memory.h"
#include "../../app/debug.h"
#include "../../game/headers/utils.h"


#include <cstdint>
#include <vector>
#include <type_traits>
#include <span>

//MongoId

//struct MongoID
//{
//    std::uint32_t _timeStamp;            // 0x00
//    std::uint8_t  _pad0[0x08 - 0x04]{};
//    std::uint64_t _counter;              // 0x08
//    std::uint64_t _stringId;             // 0x10  (pointer to Unity/Mono string object)
//
//    __forceinline std::string ReadString(int maxChars = 128, bool useCache = true) const
//    {
//        if (_stringId == 0)
//            return {};
//
//        // length in UTF-16 chars
//        int charCount = mem.Read<int>(_stringId + 0x10);
//        if (charCount <= 0)
//            return {};
//
//        if (charCount > maxChars)
//            charCount = maxChars;
//
//        // IMPORTANT: char buffer begins at +0x14
//        // If your mem.readUnicodeString expects (addr, charCount) and internally reads from addr+0x14, use _stringId.
//        // If it expects a pointer to the UTF-16 buffer, pass (_stringId + 0x14).
//        return mem.readUnicodeString(_stringId + 0x14, charCount);
//    }
//};

struct MongoID
{
    std::uint32_t _timeStamp;
    std::uint8_t  _pad0[0x08 - 0x04]{};
    std::uint64_t _counter;
    std::uint64_t _stringId;

    template <typename MemoryT>
    std::string ReadString(MemoryT& memory, int maxChars = 128) const
    {
        if (_stringId == 0)
            return {};

        int charCount = memory.Read<int>(_stringId + 0x10);
        if (charCount <= 0)
            return {};

        if (charCount > maxChars)
            charCount = maxChars;

        return memory.readUnicodeString(_stringId + 0x14, charCount);
    }
};

static_assert(offsetof(MongoID, _timeStamp) == 0x0);
static_assert(offsetof(MongoID, _counter) == 0x8);
static_assert(offsetof(MongoID, _stringId) == 0x10);
static_assert(sizeof(MongoID) == 0x18);


//////

namespace OffsetsDict {
    constexpr size_t CountOffset = 0x40;
    constexpr size_t ArrOffset = 0x18;
    constexpr size_t ArrStartOffset = 0x20;
}

namespace OffsetsArray {
    constexpr size_t CountOffset = 0x18;
    constexpr size_t ArrBaseOffset = 0x20;
}

namespace OffsetsList
{
    inline constexpr std::uint32_t ItemsOffset = 0x10; // _items (T[] backing array)
    inline constexpr std::uint32_t SizeOffset = 0x18; // _size
}

template <typename TKey, typename TValue>
class UnityDictionary
{
    static_assert(std::is_trivially_copyable_v<TKey>, "TKey must be unmanaged/trivially copyable.");
    static_assert(std::is_trivially_copyable_v<TValue>, "TValue must be unmanaged/trivially copyable.");

public:
    // Matches C# constants
    static constexpr std::uint32_t CountOffset = 0x20;
    static constexpr std::uint32_t EntriesOffset = 0x18;
    static constexpr std::uint32_t EntriesStartOffset = 0x20;

#pragma pack(push, 8)
    struct MemDictEntry
    {
        std::uint64_t _pad00; // exactly like C# (ulong)
        TKey   Key;
        TValue Value;
    };
#pragma pack(pop)

    UnityDictionary() = default;

    explicit UnityDictionary(std::uintptr_t dictAddr)
    {
        init(dictAddr);
    }

    void init(std::uintptr_t dictAddr)
    {
        baseAddr = dictAddr;
        delete[] entries;
        entries = nullptr;
        count = 0;

        if (baseAddr == 0)
            return;

        // Read count exactly as C# does
        const int c = mem.Read<int>(baseAddr + CountOffset);
        if (c < 0 || c > 16384)
            throw std::out_of_range("UnityDictionary count out of range.");

        count = c;
        if (count == 0)
            return;

        // Read entries pointer and add start offset exactly as C# does
        std::uintptr_t entriesObj = mem.Read<std::uintptr_t>(baseAddr + EntriesOffset);
        if (!Utils::valid_pointer(entriesObj))
        {
            // entries pointer invalid: treat as empty
            count = 0;
            return;
        }

        const std::uintptr_t dictBase = entriesObj + EntriesStartOffset;

        // Allocate and single bulk read, exactly like ReadSpan()
        entries = new MemDictEntry[count];
        mem.Read(dictBase, entries, static_cast<std::size_t>(count) * sizeof(MemDictEntry));
    }

    ~UnityDictionary()
    {
        delete[] entries;
        entries = nullptr;
    }

    UnityDictionary(const UnityDictionary&) = delete;
    UnityDictionary& operator=(const UnityDictionary&) = delete;

    UnityDictionary(UnityDictionary&& other) noexcept
    {
        *this = std::move(other);
    }

    UnityDictionary& operator=(UnityDictionary&& other) noexcept
    {
        if (this == &other) return *this;
        delete[] entries;

        baseAddr = other.baseAddr;
        count = other.count;
        entries = other.entries;

        other.baseAddr = 0;
        other.count = 0;
        other.entries = nullptr;
        return *this;
    }

    int GetCount() const { return count; }

    struct Iterator
    {
        MemDictEntry* p{};
        MemDictEntry* e{};

        bool operator!=(const Iterator& o) const { return p != o.p; }
        Iterator& operator++() { ++p; return *this; }
        MemDictEntry& operator*() { return *p; }
    };

    Iterator begin() { return Iterator{ entries, entries ? (entries + count) : nullptr }; }
    Iterator end() { return Iterator{ entries ? (entries + count) : nullptr, entries ? (entries + count) : nullptr }; }

private:
    std::uintptr_t baseAddr{};
    int count{};
    MemDictEntry* entries{};
};


template <typename T>
struct UnityArray
{
    // Offsets (compile-time constants)
    static inline constexpr std::uint32_t CountOffset = OffsetsArray::CountOffset;
    static inline constexpr std::uint32_t ArrBaseOffset = OffsetsArray::ArrBaseOffset;

    // Per-instance state
    std::uintptr_t baseAddr = 0;   // Address of the Unity array object (or raw buffer, depending on ctor)
    int count = 0;
    T* elements = nullptr;

    UnityArray() = default;

    // Construct from a Unity/Mono array object: read count + element buffer
    explicit UnityArray(std::uintptr_t addr)
        : baseAddr(addr)
    {
        if (!baseAddr)
            return;

        // Read count from managed array object
        count = mem.Read<int>(baseAddr + CountOffset);

        // Your original guard
        if (count < 0 || count > 4096)
            throw std::out_of_range("UnityArray count out of allowed range.");

        if (count == 0)
            return;

        elements = new T[count]{};
        mem.Read(baseAddr + ArrBaseOffset, elements, static_cast<std::size_t>(count) * sizeof(T));
    }

    // Construct from a raw contiguous buffer (addr points directly to elements)
    UnityArray(std::uintptr_t addr, int elementCount)
        : baseAddr(addr), count(elementCount)
    {
        if (!baseAddr || count <= 0)
        {
            count = 0;
            return;
        }

        if (count > 4096)
            throw std::out_of_range("UnityArray count out of allowed range.");

        elements = new T[count]{};
        mem.Read(baseAddr, elements, static_cast<std::size_t>(count) * sizeof(T));
    }

    // Rule of 5: destructor + move support (copy disabled by default)
    ~UnityArray()
    {
        delete[] elements;
        elements = nullptr;
        count = 0;
        baseAddr = 0;
    }

    UnityArray(const UnityArray&) = delete;
    UnityArray& operator=(const UnityArray&) = delete;

    UnityArray(UnityArray&& other) noexcept
        : baseAddr(other.baseAddr), count(other.count), elements(other.elements)
    {
        other.baseAddr = 0;
        other.count = 0;
        other.elements = nullptr;
    }

    UnityArray& operator=(UnityArray&& other) noexcept
    {
        if (this != &other)
        {
            delete[] elements;

            baseAddr = other.baseAddr;
            count = other.count;
            elements = other.elements;

            other.baseAddr = 0;
            other.count = 0;
            other.elements = nullptr;
        }
        return *this;
    }

    struct Iterator
    {
        T* element = nullptr;

        bool operator!=(const Iterator& other) const { return element != other.element; }
        Iterator& operator++() { ++element; return *this; }
        T& operator*() { return *element; }
    };

    Iterator begin() { return Iterator{ elements }; }
    Iterator end() { return Iterator{ elements ? (elements + count) : nullptr }; }

    int GetCount() const { return count; }

    T& operator[](int index)
    {
        if (index < 0 || index >= count)
            throw std::out_of_range("UnityArray index out of range.");
        return elements[index];
    }

    const T& operator[](int index) const
    {
        if (index < 0 || index >= count)
            throw std::out_of_range("UnityArray index out of range.");
        return elements[index];
    }

    std::uintptr_t Base() const { return baseAddr; }

    void SetBase(std::uintptr_t startAddr)
    {
        if constexpr (requires(T & t, std::uintptr_t addr) { t.base = addr; })
        {
            for (int i = 0; i < count; ++i)
                elements[i].base = startAddr + (static_cast<std::uintptr_t>(i) * sizeof(T));
        }
    }
};

static std::string ReadName(uint64_t objectClass, int length = 128, bool useCache = true)
{
    try
    {
        uint64_t namePtr = mem.ReadChain(objectClass, { 0x0, 0x10 }, useCache);
        return mem.readUTF8String(namePtr, length);
    }
    catch (const std::exception& ex) {
        LOGS.logError("Exception caught in ReadName Unity Struct: " + std::string(ex.what()) + ".");
    }

}

template <typename T>
struct UnityHashSet
{
    static_assert(std::is_trivially_copyable_v<T>,
        "UnityHashSet<T> requires T to be trivially copyable");

    static constexpr std::uint64_t CountOffset = 0x38;
    static constexpr std::uint64_t ArrOffset = 0x18;
    static constexpr std::uint64_t ArrStartOffset = 0x20;

#pragma pack(push, 4)
    struct MemHashEntry
    {
        std::int32_t hashCode;
        std::int32_t next;
        T value;

        __forceinline operator T() const noexcept { return value; }
    };
#pragma pack(pop)

    std::int32_t count{ 0 };
    std::vector<MemHashEntry> entries;

    UnityHashSet() = default;
    explicit UnityHashSet(std::int32_t c) : count(c), entries(static_cast<size_t>(c)) {}

    bool empty() const noexcept { return count <= 0; }
    std::int32_t size() const noexcept { return count; }

    template <typename MemoryT>
    static UnityHashSet Create(std::uint64_t addr, MemoryT& mem)
    {
        const std::int32_t c = mem.template Read<std::int32_t>(addr + CountOffset);

        if (c < 0)
            throw std::out_of_range("UnityHashSet count < 0");
        if (c > 16384)
            throw std::out_of_range("UnityHashSet count > 16384");

        UnityHashSet hs(c);
        if (c == 0)
            return hs;

        const std::uint64_t arrPtr = mem.template Read<std::uint64_t>(addr + ArrOffset);
        if (arrPtr == 0)
            throw std::runtime_error("UnityHashSet array pointer is null");

        const std::uint64_t base = arrPtr + ArrStartOffset;

        // Read each entry
        for (std::int32_t i = 0; i < c; ++i)
        {
            const std::uint64_t entryAddr = base + (static_cast<std::uint64_t>(i) * sizeof(MemHashEntry));
            hs.entries[static_cast<size_t>(i)] = mem.template Read<MemHashEntry>(entryAddr);
        }

        return hs;
    }
};

template <typename T>
struct MonoList
{
    static constexpr std::uint32_t CountOffset = 0x18;
    static constexpr std::uint32_t ArrOffset = 0x10;
    static constexpr std::uint32_t ArrStartOffset = 0x20;

    std::uint64_t baseAddr = 0;   // List<T> object
    std::uint64_t dataBase = 0;   // pointer to first element
    int count = 0;

    MonoList() = default;

    explicit MonoList(std::uint64_t addr)
        : baseAddr(addr)
    {
        if (!baseAddr)
            return;

        count = mem.Read<int>(baseAddr + CountOffset);
        if (count < 0 || count > 16384)
            throw std::out_of_range("MonoList count out of range");

        if (count == 0)
            return;

        const std::uint64_t arrayObj =
            mem.Read<std::uint64_t>(baseAddr + ArrOffset);

        dataBase = arrayObj + ArrStartOffset;
    }

    T operator[](int index) const
    {
        if (index < 0 || index >= count)
            throw std::out_of_range("MonoList index out of range");

        return mem.Read<T>(dataBase + static_cast<std::uint64_t>(index) * sizeof(T));
    }
};

template<typename T>
class UnityList
{
    static_assert(std::is_trivially_copyable_v<T>,
        "UnityList<T> requires unmanaged / trivially copyable types.");

public:
    // ---- Unity offsets ----
    static constexpr uint32_t ArrOffset = 0x10;
    static constexpr uint32_t CountOffset = 0x18;
    static constexpr uint32_t ArrStartOffset = 0x20;

    static constexpr int MaxCount = 16384;

    // ---- constructors ----
    UnityList() = default;

    explicit UnityList(int count)
    {
        resize(count);
    }

    explicit UnityList(uint64_t addr)
    {
        *this = Create(addr);
    }

    static UnityList<T> Create(uint64_t addr)
    {
        UnityList<T> list;

        if (!addr)
            return list;

        int count = mem.Read<int>(addr + CountOffset);

        if (count < 0 || count > MaxCount)
            return list;

        if (count == 0)
            return list;

        list.resize(count);

        const uint64_t arrPtr = mem.Read<uint64_t>(addr + ArrOffset);
        if (!arrPtr)
        {
            list.clear();
            return list;
        }

        const uint64_t base = arrPtr + ArrStartOffset;

        // Bulk read entire backing array
        mem.Read(base, list.m_data.data(), sizeof(T) * count);

        return list;
    }

    
    int count() const noexcept
    {
        return static_cast<int>(m_data.size());
    }

    bool empty() const noexcept
    {
        return m_data.empty();
    }

    T* data() noexcept
    {
        return m_data.data();
    }

    const T* data() const noexcept
    {
        return m_data.data();
    }

    std::span<T> span() noexcept
    {
        return m_data;
    }

    std::span<const T> span() const noexcept
    {
        return m_data;
    }

    
    T& operator[](size_t i) noexcept
    {
        return m_data[i];
    }

    const T& operator[](size_t i) const noexcept
    {
        return m_data[i];
    }

    
    template<typename T>
    auto begin(UnityList<T>& list) noexcept
    {
        return list.begin();
    }

    template<typename T>
    auto end(UnityList<T>& list) noexcept
    {
        return list.end();
    }

    template<typename T>
    auto begin(const UnityList<T>& list) noexcept
    {
        return list.begin();
    }

    template<typename T>
    auto end(const UnityList<T>& list) noexcept
    {
        return list.end();
    }

    
    auto begin() noexcept { return m_data.begin(); }
    auto end()   noexcept { return m_data.end(); }

    auto begin() const noexcept { return m_data.begin(); }
    auto end()   const noexcept { return m_data.end(); }

private:
    void resize(int count)
    {
        if (count <= 0)
        {
            m_data.clear();
            return;
        }
        m_data.resize(static_cast<size_t>(count));
    }

    void clear()
    {
        m_data.clear();
    }

private:
    std::vector<T> m_data;
};
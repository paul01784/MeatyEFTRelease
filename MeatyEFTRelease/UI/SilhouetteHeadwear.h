#pragma once
#include <cctype>
#include <string>
#include <string_view>

namespace BodySilhouette
{
    enum class Headwear { Automatic, Unknown, Bare, Helmet, Cap };
    inline Headwear classifyHeadwear(std::string_view name)
    {
        std::string lower(name);
        for(char& c:lower) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto word=[&](std::string_view term)
        {
            size_t at=0;
            while((at=lower.find(term,at))!=std::string::npos)
            {
                const auto letter=[](char c){return std::isalnum(static_cast<unsigned char>(c))!=0;};
                const size_t end=at+term.size();
                if((at==0||!letter(lower[at-1])) && (end==lower.size()||!letter(lower[end]))) return true;
                ++at;
            }
            return false;
        };
        if(word("helmet")) return Headwear::Helmet;
        if(word("cap")) return Headwear::Cap;
        // Empty or unrecognized labels may be unresolved equipment, not empty slots.
        return Headwear::Unknown;
    }
}

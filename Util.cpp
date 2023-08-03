#include "Util.h"
#include "GLOBAL.h"

std::string Util::getEnumString(Keys key) 
{
    for (const auto& keyPair : GLOBAL::keysString) {
        if (static_cast<int>(key) == keyPair.second) {
            return keyPair.first;
        }
    }
    return ""; // Return empty string if the enum value is not found in the vector
}




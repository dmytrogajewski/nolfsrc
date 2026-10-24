#include "Stdafx.h"
#include "KeyFixes.h"
#include "engine/portable/string.h"
#include <algorithm>
#include <cstring>
#include <string>

void ConvertKeyToCurrentKeyboard(char* output,char* binding,int capacity)
{
    if (!output || capacity <= 0) return;
    const std::string ownedBinding=binding ? binding : "";
    binding=const_cast<char*>(ownedBinding.c_str());
    output[0]=0;
    auto copy=[&](const char* label) {
        const auto count=std::min<std::size_t>(std::strlen(label),std::size_t(capacity-1));
        std::memmove(output,label,count); output[count]=0;
    };
    if (g_pLTClient && g_pLTClient->GetDeviceObjects && g_pLTClient->FreeDeviceObjects) {
        auto* objects=g_pLTClient->GetDeviceObjects(DEVICETYPE_KEYBOARD);
        for (auto* object=objects; object; object=object->m_pNext) {
            if (nolf::ascii_compare(object->m_ObjectName,binding) == 0 && object->m_DisplayName[0]) {
                copy(object->m_DisplayName); g_pLTClient->FreeDeviceObjects(objects); return;
            }
        }
        g_pLTClient->FreeDeviceObjects(objects);
    }
    copy(binding);
}

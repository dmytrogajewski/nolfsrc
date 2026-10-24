#pragma once
class LTCommandVar;
class ConsoleBinding {
public:
    char* m_pName;
    float* m_pFloat;
    int* m_pInt;
    LTCommandVar** m_ppVar;
    char** m_ppString;
};
ConsoleBinding* dsi_GetConsoleBindings();
int dsi_GetNumConsoleBindings();

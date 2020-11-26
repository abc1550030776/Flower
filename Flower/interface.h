#pragma once
#include <set>
bool BuildDstIndex(const char* fileName);

bool SearchFile(const char* fileName, const char* searchTarget, unsigned int targetLen, std::set<unsigned long long>* set);

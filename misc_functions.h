#ifndef MISC_FUNCTIONS__H
#define MISC_FUNCTIONS__H
#include <string>

long strtolong(const std::string& str);
long long strtolonglong(const std::string& str);
std::string inttostr(int i);
std::string hex_decode(const std::string &in);

#endif

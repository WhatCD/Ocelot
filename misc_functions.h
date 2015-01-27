#ifndef MISC_FUNCTIONS__H
#define MISC_FUNCTIONS__H
#include <string>

int32_t strtoint32(const std::string& str);
int64_t strtoint64(const std::string& str);
std::string inttostr(int i);
std::string hex_decode(const std::string &in);
std::string bintohex(const std::string &in);

#endif

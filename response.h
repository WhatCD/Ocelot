#include <string>

std::string response(const std::string &body, bool gzip, bool html);
std::string response_head(bool gzip, bool html);
std::string error(const std::string &err);
std::string warning(const std::string &msg);

#ifndef RESPONSE_H
#define REPSONSE_H

#include <string>
#include "ocelot.h"

const std::string response(const std::string &body, client_opts_t &client_opts);
const std::string response_head(size_t content_length, client_opts_t &client_opts);
const std::string error(const std::string &err, client_opts_t &client_opts);
const std::string warning(const std::string &msg);
#endif

#pragma once
#include <string>
#include <map>

struct ParsedEmail {
    std::string from;
    std::string to;
    std::string subject;
    std::string body;
};

ParsedEmail parse_email(const std::string& raw_email);
std::string extract_email_address(const std::string& header_value);

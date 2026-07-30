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
std::string base64_decode(const std::string& input);
std::string quoted_printable_decode(const std::string& input);
std::string decode_content(const std::string& headers, const std::string& content);
std::string extract_html_body(const std::string& body);
std::string extract_text_body(const std::string& body);
std::string clean_mime_body(const std::string& body);

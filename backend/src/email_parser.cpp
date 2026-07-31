#include "email_parser.h"
#include <sstream>
#include <algorithm>
#include <regex>

// Base64 decode
static const std::string B64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_decode(const std::string& input) {
    std::string result;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[B64_CHARS[i]] = i;

    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
        if (T[c] == -1) continue;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}

// Quoted-printable decode
std::string quoted_printable_decode(const std::string& input) {
    std::string result;
    for (size_t i = 0; i < input.size(); i++) {
        if (input[i] == '=' && i + 2 < input.size()) {
            // Soft line break: =\r\n or =\n
            if (input[i+1] == '\r' && input[i+2] == '\n') {
                i += 2;
            } else if (input[i+1] == '\n') {
                i += 1;
            } else {
                // Only decode =XX where XX are UPPERCASE hex (standard QP)
                char h1 = input[i+1], h2 = input[i+2];
                bool isHex = ((h1>='0'&&h1<='9')||(h1>='A'&&h1<='F')) &&
                             ((h2>='0'&&h2<='9')||(h2>='A'&&h2<='F'));
                if (isHex) {
                    std::string hex = input.substr(i+1, 2);
                    char c = (char)std::stoi(hex, nullptr, 16);
                    result.push_back(c);
                    i += 2;
                } else {
                    result.push_back(input[i]);
                }
            }
        } else {
            result.push_back(input[i]);
        }
    }
    return result;
}

std::string extract_email_address(const std::string& header_value) {
    std::regex email_regex("<([^>]+)>");
    std::smatch match;
    if (std::regex_search(header_value, match, email_regex)) {
        return match[1].str();
    }
    std::regex bare_regex("[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}");
    if (std::regex_search(header_value, match, bare_regex)) {
        return match[0].str();
    }
    return header_value;
}

// Decode content based on Content-Transfer-Encoding header
std::string decode_content(const std::string& headers, const std::string& content) {
    std::string lower_headers = headers;
    std::transform(lower_headers.begin(), lower_headers.end(), lower_headers.begin(), ::tolower);
    
    if (lower_headers.find("content-transfer-encoding: base64") != std::string::npos) {
        return base64_decode(content);
    } else if (lower_headers.find("content-transfer-encoding: quoted-printable") != std::string::npos) {
        return quoted_printable_decode(content);
    }
    return content;
}

std::string extract_html_body(const std::string& body) {
    // Find HTML part in MIME message - capture headers + content
    std::regex html_regex("(Content-Type: text/html[^\\r\\n]*(?:\\r?\\n(?!\\r?\\n)[^\\r\\n]*)*)\\r?\\n\\r?\\n([\\s\\S]*?)(?:--[0-9a-zA-Z_+=/-]+|$)");
    std::smatch match;
    if (std::regex_search(body, match, html_regex)) {
        std::string headers = match[1].str();
        std::string html = match[2].str();
        // Trim trailing whitespace
        while (!html.empty() && (html.back() == '\r' || html.back() == '\n' || html.back() == ' ')) {
            html.pop_back();
        }
        return decode_content(headers, html);
    }
    return "";
}

std::string extract_text_body(const std::string& body) {
    // Find text/plain part in MIME message - capture headers + content
    std::regex text_regex("(Content-Type: text/plain[^\\r\\n]*(?:\\r?\\n(?!\\r?\\n)[^\\r\\n]*)*)\\r?\\n\\r?\\n([\\s\\S]*?)(?:--[0-9a-zA-Z_+=/-]+|$)");
    std::smatch match;
    if (std::regex_search(body, match, text_regex)) {
        std::string headers = match[1].str();
        std::string text = match[2].str();
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
            text.pop_back();
        }
        return decode_content(headers, text);
    }
    return "";
}

std::string clean_mime_body(const std::string& body) {
    // If it's a MIME message, extract the content
    if (body.find("Content-Type:") != std::string::npos) {
        // Try HTML first
        std::string html = extract_html_body(body);
        if (!html.empty()) return html;
        
        // Then try text
        std::string text = extract_text_body(body);
        if (!text.empty()) return text;
    }
    
    // If not MIME, return as-is
    return body;
}

ParsedEmail parse_email(const std::string& raw_email) {
    ParsedEmail result;
    std::istringstream stream(raw_email);
    std::string line;
    bool in_body = false;
    std::ostringstream body_stream;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (in_body) {
            body_stream << line << "\n";
            continue;
        }

        if (line.empty()) {
            in_body = true;
            continue;
        }

        if (line[0] == ' ' || line[0] == '\t') {
            continue;
        }

        std::string lower_line = line;
        std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(), ::tolower);

        if (lower_line.substr(0, 5) == "from:") {
            result.from = line.substr(5);
            size_t start = result.from.find_first_not_of(" \t");
            if (start != std::string::npos) result.from = result.from.substr(start);
        } else if (lower_line.substr(0, 3) == "to:") {
            result.to = line.substr(3);
            size_t start = result.to.find_first_not_of(" \t");
            if (start != std::string::npos) result.to = result.to.substr(start);
        } else if (lower_line.substr(0, 8) == "subject:") {
            result.subject = line.substr(8);
            size_t start = result.subject.find_first_not_of(" \t");
            if (start != std::string::npos) result.subject = result.subject.substr(start);
        }
    }

    result.body = body_stream.str();
    while (!result.body.empty() && result.body.back() == '\n') {
        result.body.pop_back();
    }

    // Clean MIME content
    result.body = clean_mime_body(result.body);

    return result;
}

// Remove <style>, <script>, <head> blocks
std::string strip_style_blocks(const std::string& html) {
    std::string result = html;
    std::regex style_re("<style[^>]*>[\\s\\S]*?</style>", std::regex::icase);
    result = std::regex_replace(result, style_re, "");
    std::regex script_re("<script[^>]*>[\\s\\S]*?</script>", std::regex::icase);
    result = std::regex_replace(result, script_re, "");
    std::regex head_re("<head>[\\s\\S]*?</head>", std::regex::icase);
    result = std::regex_replace(result, head_re, "");
    // Remove conditional comments
    std::regex mso_re("<!--[\\s\\S]*?-->");
    result = std::regex_replace(result, mso_re, "");
    return result;
}

// Strip all HTML tags, decode common entities, convert <br>/<p>/<div> to newlines
std::string strip_html_tags(const std::string& html) {
    std::string result = strip_style_blocks(html);
    
    // Convert block elements to newlines BEFORE stripping tags
    std::regex br_re("<br\\s*/?>", std::regex::icase);
    result = std::regex_replace(result, br_re, "\n");
    std::regex p_re("</p>", std::regex::icase);
    result = std::regex_replace(result, p_re, "\n\n");
    std::regex div_re("</div>", std::regex::icase);
    result = std::regex_replace(result, div_re, "\n");
    std::regex tr_re("<tr[^>]*>", std::regex::icase);
    result = std::regex_replace(result, tr_re, "\n");
    std::regex li_re("</li>", std::regex::icase);
    result = std::regex_replace(result, li_re, "\n");
    
    // Strip all remaining HTML tags
    std::regex tag_re("<[^>]*>");
    result = std::regex_replace(result, tag_re, "");
    
    // Decode common HTML entities
    auto replace_all = [](std::string& s, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.length(), to);
            pos += to.length();
        }
    };
    replace_all(result, "&amp;", "&");
    replace_all(result, "&lt;", "<");
    replace_all(result, "&gt;", ">");
    replace_all(result, "&quot;", "\"");
    replace_all(result, "&#39;", "'");
    replace_all(result, "&nbsp;", " ");
    
    // Clean up whitespace
    // Collapse multiple spaces
    std::regex multi_space("[ \\t]{2,}");
    result = std::regex_replace(result, multi_space, " ");
    // Remove leading whitespace from lines
    std::regex lead_space("\\n[ \\t]+");
    result = std::regex_replace(result, lead_space, "\n");
    // Collapse 3+ newlines to 2
    std::regex multi_newline("\\n{3,}");
    result = std::regex_replace(result, multi_newline, "\n\n");
    
    // Trim
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
        result.pop_back();
    }
    while (!result.empty() && (result.front() == '\n' || result.front() == '\r' || result.front() == ' ')) {
        result.erase(0, 1);
    }
    
    return result;
}

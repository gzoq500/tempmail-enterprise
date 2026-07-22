#include "email_parser.h"
#include <sstream>
#include <algorithm>
#include <regex>

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

std::string extract_html_body(const std::string& body) {
    // Find HTML part in MIME message
    std::regex html_regex("Content-Type: text/html[^\\r\\n]*\\r?\\n\\r?\\n([\\s\\S]*?)(?:--[0-9a-f]+|$)");
    std::smatch match;
    if (std::regex_search(body, match, html_regex)) {
        std::string html = match[1].str();
        // Trim trailing whitespace
        while (!html.empty() && (html.back() == '\r' || html.back() == '\n' || html.back() == ' ')) {
            html.pop_back();
        }
        return html;
    }
    return "";
}

std::string extract_text_body(const std::string& body) {
    // Find text/plain part in MIME message
    std::regex text_regex("Content-Type: text/plain[^\\r\\n]*\\r?\\n\\r?\\n([\\s\\S]*?)(?:--[0-9a-f]+|$)");
    std::smatch match;
    if (std::regex_search(body, match, text_regex)) {
        std::string text = match[1].str();
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
            text.pop_back();
        }
        return text;
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

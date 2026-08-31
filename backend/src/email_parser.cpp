#include "email_parser.h"
#include <sstream>
#include <algorithm>
#include <regex>
#include <vector>
#include <cctype>

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

// Decode MIME encoded words: =?charset?encoding?data?=
// Supports B (base64) and Q (quoted-printable) encoding
std::string decode_mime_header(const std::string& input) {
    std::string result;
    size_t pos = 0;
    while (pos < input.size()) {
        // Find =?charset?encoding?data?=
        size_t start = input.find("=?", pos);
        if (start == std::string::npos) {
            result += input.substr(pos);
            break;
        }
        // Add literal text before encoded word
        result += input.substr(pos, start - pos);
        
        size_t q1 = input.find('?', start + 2);
        if (q1 == std::string::npos) { result += input.substr(start); break; }
        
        size_t q2 = input.find('?', q1 + 1);
        if (q2 == std::string::npos) { result += input.substr(start); break; }
        
        size_t end = input.find("?=", q2 + 1);
        if (end == std::string::npos) { result += input.substr(start); break; }
        
        std::string encoding = input.substr(q1 + 1, q2 - q1 - 1);
        std::string data = input.substr(q2 + 1, end - q2 - 1);
        
        // Convert encoding to uppercase for comparison
        std::string enc_upper = encoding;
        std::transform(enc_upper.begin(), enc_upper.end(), enc_upper.begin(), ::toupper);
        
        if (enc_upper == "B") {
            result += base64_decode(data);
        } else if (enc_upper == "Q") {
            // Q encoding: underscores are spaces, =XX are hex
            std::string decoded;
            for (size_t i = 0; i < data.size(); i++) {
                if (data[i] == '_') {
                    decoded += ' ';
                } else if (data[i] == '=' && i + 2 < data.size()) {
                    const char h1 = data[i + 1], h2 = data[i + 2];
                    const bool valid_hex = std::isxdigit(static_cast<unsigned char>(h1)) &&
                                           std::isxdigit(static_cast<unsigned char>(h2));
                    if (valid_hex) {
                        decoded += static_cast<char>(std::stoi(data.substr(i + 1, 2), nullptr, 16));
                        i += 2;
                    } else {
                        decoded += data[i];
                    }
                } else {
                    decoded += data[i];
                }
            }
            result += decoded;
        } else {
            result += data; // unknown encoding, keep as-is
        }
        
        pos = end + 2;
        // Skip whitespace between adjacent encoded words (RFC 2047)
        // An encoded word followed by whitespace followed by another encoded word
        // should have the whitespace consumed entirely
        size_t ws_start = pos;
        while (pos < input.size() && (input[pos] == ' ' || input[pos] == '\t' || input[pos] == '\r' || input[pos] == '\n')) {
            pos++;
        }
        // If next token is NOT an encoded word, restore whitespace
        if (pos >= input.size() || input.substr(pos, 2) != "=?") {
            result += input.substr(ws_start, pos - ws_start);
        }
    }
    return result;
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

// Extract a MIME boundary case-insensitively from an entity's headers.
static std::string extract_boundary(const std::string& headers) {
    std::string lower = headers;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    auto pos = lower.find("boundary=");
    if (pos == std::string::npos) return "";
    pos += 9;
    char quote = 0;
    if (pos < headers.size() && (headers[pos] == '"' || headers[pos] == '\'')) {
        quote = headers[pos++];
    }
    const size_t start = pos;
    while (pos < headers.size()) {
        if (quote && headers[pos] == quote) break;
        if (!quote && (headers[pos] == ';' || headers[pos] == '\r' || headers[pos] == '\n' ||
                       std::isspace(static_cast<unsigned char>(headers[pos])))) break;
        ++pos;
    }
    return headers.substr(start, pos - start);
}

// Split MIME body by boundary and extract parts
struct MimePart {
    std::string headers;
    std::string content;
};

static std::vector<MimePart> split_mime_parts(const std::string& body, const std::string& boundary) {
    std::vector<MimePart> parts;
    std::string delim = "--" + boundary;
    size_t pos = body.find(delim);
    if (pos == std::string::npos) return parts;
    
    pos += delim.size();
    // Skip CRLF after delimiter
    if (pos < body.size() && body[pos] == '\r') pos++;
    if (pos < body.size() && body[pos] == '\n') pos++;
    
    while (pos < body.size()) {
        size_t next = body.find(delim, pos);
        if (next == std::string::npos) break;
        
        std::string part = body.substr(pos, next - pos);
        // Trim trailing CRLF before boundary
        while (!part.empty() && (part.back() == '\r' || part.back() == '\n')) part.pop_back();
        
        // Split headers and content
        size_t header_end = part.find("\r\n\r\n");
        if (header_end == std::string::npos) header_end = part.find("\n\n");
        
        MimePart mp;
        if (header_end != std::string::npos) {
            mp.headers = part.substr(0, header_end);
            size_t content_start = part[header_end] == '\r' ? header_end + 4 : header_end + 2;
            mp.content = part.substr(content_start);
        } else {
            mp.content = part;
        }
        parts.push_back(mp);
        
        pos = next + delim.size();
        if (pos < body.size() && body[pos] == '\r') pos++;
        if (pos < body.size() && body[pos] == '\n') pos++;
        // Check for closing boundary
        if (pos + 1 < body.size() && body[pos] == '-' && body[pos+1] == '-') break;
    }
    return parts;
}

// Split an entity into headers/body, accepting both CRLF and LF-only input.
static MimePart split_mime_entity(const std::string& entity) {
    MimePart out;
    size_t end = entity.find("\r\n\r\n");
    size_t skip = 4;
    if (end == std::string::npos) {
        end = entity.find("\n\n");
        skip = 2;
    }
    if (end == std::string::npos) {
        out.content = entity;
    } else {
        out.headers = entity.substr(0, end);
        out.content = entity.substr(end + skip);
    }
    return out;
}

// Recursively traverse multipart MIME entities. Real mail commonly nests
// multipart/alternative inside multipart/mixed; a one-level parser leaks inner
// boundaries, part headers and attachment bytes into the displayed body.
static std::string extract_part_recursive(const std::string& entity,
                                          const std::string& wanted_type,
                                          int depth = 0) {
    if (depth > 12) return "";  // bounded recursion for hostile MIME trees
    const MimePart current = split_mime_entity(entity);
    std::string lower_headers = current.headers;
    std::transform(lower_headers.begin(), lower_headers.end(), lower_headers.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const std::string boundary = extract_boundary(current.headers);
    if (lower_headers.find("content-type: multipart/") != std::string::npos && !boundary.empty()) {
        const auto parts = split_mime_parts(current.content, boundary);
        for (const auto& part : parts) {
            const std::string child = part.headers + "\r\n\r\n" + part.content;
            std::string found = extract_part_recursive(child, wanted_type, depth + 1);
            if (!found.empty()) return found;
        }
        return "";
    }

    if (lower_headers.find("content-type: " + wanted_type) != std::string::npos) {
        return decode_content(current.headers, current.content);
    }
    return "";
}

static std::string extract_part_by_boundary(const std::string& body, const std::string& type) {
    return extract_part_recursive(body, type);
}

std::string extract_html_body(const std::string& body) {
    // Try boundary-based extraction first (most robust)
    std::string result = extract_part_by_boundary(body, "text/html");
    if (!result.empty()) return result;
    
    // Simple string-based fallback (no regex = no hang on large bodies)
    // Look for Content-Type: text/html header followed by blank line
    std::string lower = body;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    size_t pos = lower.find("content-type: text/html");
    if (pos != std::string::npos) {
        // Find the blank line after headers
        size_t blank = body.find("\n\n", pos);
        if (blank == std::string::npos) blank = body.find("\r\n\r\n", pos);
        if (blank != std::string::npos) {
            std::string html = body.substr(blank + 2);
            // Trim trailing whitespace
            while (!html.empty() && (html.back() == '\r' || html.back() == '\n' || html.back() == ' ')) html.pop_back();
            if (!html.empty()) return html;
        }
    }
    
    // Body starts directly with HTML
    if (body.find("<html") != std::string::npos || body.find("<!DOCTYPE") != std::string::npos || body.find("<body") != std::string::npos) {
        return body;
    }
    
    return "";
}

std::string extract_text_body(const std::string& body) {
    // Try boundary-based extraction first (most robust)
    std::string result = extract_part_by_boundary(body, "text/plain");
    if (!result.empty()) return result;
    
    // Simple string-based fallback (no regex)
    std::string lower = body;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    size_t pos = lower.find("content-type: text/plain");
    if (pos != std::string::npos) {
        size_t blank = body.find("\n\n", pos);
        if (blank == std::string::npos) blank = body.find("\r\n\r\n", pos);
        if (blank != std::string::npos) {
            std::string text = body.substr(blank + 2);
            while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) text.pop_back();
            if (!text.empty()) return text;
        }
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
            result.from = decode_mime_header(result.from);
        } else if (lower_line.substr(0, 3) == "to:") {
            result.to = line.substr(3);
            size_t start = result.to.find_first_not_of(" \t");
            if (start != std::string::npos) result.to = result.to.substr(start);
            result.to = decode_mime_header(result.to);
        } else if (lower_line.substr(0, 8) == "subject:") {
            result.subject = line.substr(8);
            size_t start = result.subject.find_first_not_of(" \t");
            if (start != std::string::npos) result.subject = result.subject.substr(start);
            result.subject = decode_mime_header(result.subject);
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

// Remove <style>, <script>, <head> blocks — simple string scan, no regex
std::string strip_style_blocks(const std::string& html) {
    std::string result;
    result.reserve(html.size());
    size_t i = 0;
    while (i < html.size()) {
        // Check for blocks to skip
        const char* skip_tags[] = {"<style", "<script", "<head", "<!--"};
        const char* end_tags[] = {"</style>", "</script>", "</head>", "-->"};
        bool skipped = false;
        for (int t = 0; t < 4; t++) {
            size_t tag_len = strlen(skip_tags[t]);
            if (i + tag_len <= html.size() && strncasecmp(html.c_str() + i, skip_tags[t], tag_len) == 0) {
                size_t end_pos = html.find(end_tags[t], i + tag_len);
                if (end_pos != std::string::npos) {
                    i = end_pos + strlen(end_tags[t]);
                    skipped = true;
                    break;
                }
            }
        }
        if (!skipped) {
            result += html[i];
            i++;
        }
    }
    return result;
}

// Strip all HTML tags, decode common entities, convert <br>/<p>/<div> to newlines
std::string strip_html_tags(const std::string& html) {
    std::string result;
    result.reserve(html.size());
    
    bool in_tag = false;
    bool in_style = false;
    bool in_script = false;
    
    for (size_t i = 0; i < html.size(); i++) {
        // Check for tag start
        if (html[i] == '<') {
            // Check for style/script blocks
            if (i + 6 < html.size() && strncasecmp(html.c_str() + i, "<style", 6) == 0) { in_style = true; in_tag = true; continue; }
            if (i + 7 < html.size() && strncasecmp(html.c_str() + i, "<script", 7) == 0) { in_script = true; in_tag = true; continue; }
            if (i + 7 < html.size() && strncasecmp(html.c_str() + i, "</style", 7) == 0) { in_style = false; in_tag = true; continue; }
            if (i + 8 < html.size() && strncasecmp(html.c_str() + i, "</script", 8) == 0) { in_script = false; in_tag = true; continue; }
            
            // Convert <br>, </p>, </div>, </tr>, </li> to newlines
            if (i + 3 < html.size() && (strncasecmp(html.c_str() + i, "<br", 3) == 0)) { result += '\n'; in_tag = true; continue; }
            if (i + 3 < html.size() && strncasecmp(html.c_str() + i, "</p", 3) == 0) { result += '\n'; in_tag = true; continue; }
            if (i + 5 < html.size() && strncasecmp(html.c_str() + i, "</div", 5) == 0) { result += '\n'; in_tag = true; continue; }
            if (i + 3 < html.size() && strncasecmp(html.c_str() + i, "<tr", 3) == 0) { result += '\n'; in_tag = true; continue; }
            if (i + 4 < html.size() && strncasecmp(html.c_str() + i, "</li", 4) == 0) { result += '\n'; in_tag = true; continue; }
            
            in_tag = true;
            continue;
        }
        
        if (html[i] == '>') { in_tag = false; continue; }
        if (in_tag || in_style || in_script) continue;
        
        // Decode HTML entities
        if (html[i] == '&') {
            if (html.compare(i, 5, "&amp;") == 0) { result += '&'; i += 4; }
            else if (html.compare(i, 4, "&lt;") == 0) { result += '<'; i += 3; }
            else if (html.compare(i, 4, "&gt;") == 0) { result += '>'; i += 3; }
            else if (html.compare(i, 6, "&quot;") == 0) { result += '"'; i += 5; }
            else if (html.compare(i, 5, "&#39;") == 0) { result += '\''; i += 4; }
            else if (html.compare(i, 6, "&nbsp;") == 0) { result += ' '; i += 5; }
            else { result += html[i]; }
            continue;
        }
        
        result += html[i];
    }
    
    // Trim
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) result.pop_back();
    while (!result.empty() && (result.front() == '\n' || result.front() == '\r' || result.front() == ' ')) result.erase(0, 1);
    
    return result;
}

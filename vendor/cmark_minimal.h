// cmark - CommonMark C library (minimal implementation for Markdown to HTML)
// This is a minimal implementation that handles the most common Markdown features.
// For full CommonMark compliance, you'd use the full cmark library.

// Usage: 
//   std::string html = cmark_minimal::MarkdownToHtml(markdown_str);
//   Then inject HTML into RichEdit or WebView2.

#ifndef CMARK_MINIMAL_H
#define CMARK_MINIMAL_H

#include <string>
#include <sstream>
#include <vector>
#include <cctype>

namespace cmark_minimal {

inline std::string escape_html(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            default: result += c; break;
        }
    }
    return result;
}

inline std::string inline_format(const std::string& text) {
    std::string result;
    size_t i = 0;
    size_t len = text.size();
    
    while (i < len) {
        // Inline code
        if (text[i] == '`' && i + 1 < len) {
            size_t end = text.find('`', i + 1);
            if (end != std::string::npos) {
                result += "<code>";
                result += escape_html(text.substr(i + 1, end - i - 1));
                result += "</code>";
                i = end + 1;
                continue;
            }
        }
        
        // Bold (**)
        if (text[i] == '*' && i + 1 < len && text[i+1] == '*') {
            size_t end = text.find("**", i + 2);
            if (end != std::string::npos) {
                result += "<b>";
                result += inline_format(text.substr(i + 2, end - i - 2));
                result += "</b>";
                i = end + 2;
                continue;
            }
        }
        
        // Italic (*)
        if (text[i] == '*' && i + 1 < len && text[i+1] != '*') {
            size_t end = text.find('*', i + 1);
            if (end != std::string::npos && end > i) {
                result += "<i>";
                result += inline_format(text.substr(i + 1, end - i - 1));
                result += "</i>";
                i = end + 1;
                continue;
            }
        }
        
        // Italic (_)
        if (text[i] == '_' && i + 1 < len && text[i+1] != '_') {
            size_t end = text.find('_', i + 1);
            if (end != std::string::npos && end > i) {
                result += "<i>";
                result += inline_format(text.substr(i + 1, end - i - 1));
                result += "</i>";
                i = end + 1;
                continue;
            }
        }
        
        // Links [text](url)
        if (text[i] == '[' && i + 1 < len) {
            size_t close = text.find(']', i + 1);
            if (close != std::string::npos && close + 1 < len && text[close+1] == '(') {
                size_t paren_end = text.find(')', close + 2);
                if (paren_end != std::string::npos) {
                    std::string link_text = inline_format(text.substr(i + 1, close - i - 1));
                    std::string url = escape_html(text.substr(close + 2, paren_end - close - 2));
                    result += "<a href=\"";
                    result += url;
                    result += "\">";
                    result += link_text;
                    result += "</a>";
                    i = paren_end + 1;
                    continue;
                }
            }
        }
        
        // Images ![alt](src)
        if (text[i] == '!' && i + 1 < len && text[i+1] == '[') {
            size_t close = text.find(']', i + 2);
            if (close != std::string::npos && close + 1 < len && text[close+1] == '(') {
                size_t paren_end = text.find(')', close + 2);
                if (paren_end != std::string::npos) {
                    std::string alt = text.substr(i + 1, close - i - 1);
                    std::string src = escape_html(text.substr(close + 2, paren_end - close - 2));
                    result += "<img src=\"";
                    result += src;
                    result += "\" alt=\"";
                    result += escape_html(alt);
                    result += "\">";
                    i = paren_end + 1;
                    continue;
                }
            }
        }
        
        result += text[i];
        i++;
    }
    
    return result;
}

inline std::string MarkdownToHtml(const std::string& markdown) {
    std::string html;
    std::istringstream stream(markdown);
    std::string line;
    
    bool in_code_block = false;
    bool in_ul = false;
    bool in_ol = false;
    bool in_thead = false;
    bool in_tbody = false;
    std::string code_block_content;
    std::string code_block_lang;
    std::vector<std::string> ul_items;
    std::vector<std::string> ol_items;
    std::vector<std::string> table_header;
    std::vector<std::vector<std::string>> table_rows;
    
    auto flush_ul = [&]() {
        if (in_ul && !ul_items.empty()) {
            html += "<ul>\n";
            for (auto& item : ul_items) {
                html += "  <li>";
                html += inline_format(item);
                html += "</li>\n";
            }
            html += "</ul>\n";
            ul_items.clear();
            in_ul = false;
        }
    };
    
    auto flush_ol = [&]() {
        if (in_ol && !ol_items.empty()) {
            html += "<ol>\n";
            size_t idx = 0;
            for (auto& item : ol_items) {
                html += "  <li value=\"" + std::to_string(++idx) + "\">";
                html += inline_format(item);
                html += "</li>\n";
            }
            html += "</ol>\n";
            ol_items.clear();
            in_ol = false;
        }
    };
    
    auto flush_table = [&]() {
        if (!table_header.empty() && !table_rows.empty()) {
            html += "<table>\n";
            html += "  <thead><tr>\n";
            for (auto& cell : table_header) {
                html += "    <th>" + inline_format(cell) + "</th>\n";
            }
            html += "  </tr></thead>\n";
            html += "  <tbody>\n";
            for (auto& row : table_rows) {
                html += "    <tr>\n";
                for (size_t i = 0; i < row.size(); i++) {
                    html += "      <td>" + inline_format(row[i]) + "</td>\n";
                }
                html += "    </tr>\n";
            }
            html += "  </tbody>\n";
            html += "</table>\n";
            table_header.clear();
            table_rows.clear();
        }
    };
    
    auto flush_code_block = [&]() {
        if (in_code_block) {
            if (!code_block_lang.empty()) {
                html += "<pre class=\"code-block\"><code class=\"language-";
                html += code_block_lang;
                html += "\">";
            } else {
                html += "<pre class=\"code-block\"><code>";
            }
            html += escape_html(code_block_content);
            if (code_block_lang.empty()) {
                html += "</code></pre>\n";
            } else {
                html += "</code></pre>\n";
            }
            code_block_content.clear();
            code_block_lang.clear();
            in_code_block = false;
        }
    };
    
    // Helper to split a line by pipe character, respecting backticks
    auto split_pipe = [](const std::string& s, std::vector<std::string>& out) -> bool {
        out.clear();
        size_t start = 0;
        size_t pipe = s.find('|', start);
        while (pipe != std::string::npos) {
            std::string cell = s.substr(start, pipe - start);
            // Trim whitespace
            size_t first = cell.find_first_not_of(' ');
            size_t last = cell.find_last_not_of(' ');
            if (first != std::string::npos) {
                cell = cell.substr(first, last - first + 1);
            }
            out.push_back(cell);
            start = pipe + 1;
            pipe = s.find('|', start);
        }
        std::string last = s.substr(start);
        size_t first = last.find_first_not_of(' ');
        size_t last_pos = last.find_last_not_of(' ');
        if (first != std::string::npos) {
            last = last.substr(first, last_pos - first + 1);
        }
        out.push_back(last);
        return true;
    };
    
    while (std::getline(stream, line)) {
        // Strip trailing whitespace
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        
        // Empty line - flush everything
        if (line.empty()) {
            flush_code_block();
            flush_ul();
            flush_ol();
            flush_table();
            continue;
        }
        
        // Fenced code block
        if (line.size() >= 3 && (line[0] == '`' && line[1] == '`' && line[2] == '`')) {
            flush_ul();
            flush_ol();
            flush_table();
            
            if (in_code_block) {
                flush_code_block();
            } else {
                in_code_block = true;
                // Detect language
                size_t space_pos = line.find(' ');
                if (space_pos != std::string::npos) {
                    code_block_lang = line.substr(3, space_pos - 3);
                }
                continue;
            }
            continue;
        }
        
        if (in_code_block) {
            if (!code_block_content.empty()) code_block_content += "\n";
            code_block_content += line;
            continue;
        }
        
        // Headings
        size_t hash_count = 0;
        while (hash_count < line.size() && line[hash_count] == '#') {
            hash_count++;
        }
        if (hash_count > 0 && hash_count <= 6 && hash_count < line.size() && line[hash_count] == ' ') {
            flush_code_block();
            flush_ul();
            flush_ol();
            flush_table();
            std::string heading_text = line.substr(hash_count + 1);
            int level = static_cast<int>(hash_count);
            html += "<h" + std::to_string(level) + ">" + inline_format(heading_text) + "</h" + std::to_string(level) + ">\n";
            continue;
        }
        
        // Horizontal rule
        std::string hr_check = line;
        // Remove spaces
        hr_check.erase(std::remove_if(hr_check.begin(), hr_check.end(), ::isspace), hr_check.end());
        if (hr_check == "---" || hr_check == "***" || hr_check == "___") {
            flush_code_block();
            flush_ul();
            flush_ol();
            flush_table();
            html += "<hr>\n";
            continue;
        }
        
        // Blockquote
        if (line[0] == '>') {
            flush_ul();
            flush_ol();
            flush_table();
            std::string quote = line.substr(1);
            if (!quote.empty() && quote[0] == ' ') quote = quote.substr(1);
            html += "<blockquote>" + inline_format(quote) + "</blockquote>\n";
            continue;
        }
        
        // Tables - must have pipes and not be a separator
        bool has_pipe = line.find('|') != std::string::npos;
        if (has_pipe) {
            std::vector<std::string> cells;
            split_pipe(line, cells);
            
            if (cells.size() >= 2) {
                // Check if separator row (|---|---|)
                bool is_separator = true;
                for (auto& cell : cells) {
                    std::string check = cell;
                    for (auto& c : check) {
                        if (c != '-' && c != ':' && c != '-') {
                            // Allow leading/trailing colons
                        }
                    }
                    std::string stripped;
                    for (size_t ci = 0; ci < cell.size(); ci++) {
                        char c = cell[ci];
                        if (c != '-' && c != ':' && c != ' ') {
                            is_separator = false;
                            break;
                        }
                    }
                    if (!is_separator) break;
                }
                
                if (is_separator) {
                    continue;
                }
                
                // It's a table row
                if (table_header.empty()) {
                    // First row is header
                    table_header = cells;
                } else {
                    table_rows.push_back(cells);
                }
                continue;
            }
        }
        
        // Unordered list item
        if ((line.size() >= 2 && (line[0] == '*' || line[0] == '-' || line[0] == '+') && line[1] == ' ')) {
            std::string item = line.substr(2);
            flush_code_block();
            flush_ol();
            flush_table();
            ul_items.push_back(item);
            in_ul = true;
            continue;
        }
        
        // Ordered list item
        if (line.size() >= 2 && std::isdigit(static_cast<unsigned char>(line[0])) && line[1] == '.') {
            std::string item = line.substr(2);
            if (!item.empty() && item[0] == ' ') item = item.substr(1);
            flush_code_block();
            flush_ul();
            flush_table();
            ol_items.push_back(item);
            in_ol = true;
            continue;
        }
        
        // If we hit a non-list, non-table line, flush pending lists/tables
        flush_code_block();
        flush_ul();
        flush_ol();
        flush_table();
        
        // Regular text - apply inline formatting
        html += inline_format(line) + "<br>\n";
    }
    
    // Flush any remaining content
    flush_code_block();
    flush_ul();
    flush_ol();
    flush_table();
    
    return html;
}

} // namespace cmark_minimal

#endif // CMARK_MINIMAL_H

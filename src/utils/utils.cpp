#include <vector>
#include <string>

auto split(const std::string& str, char delimiter) -> std::vector<std::string> {
    std::vector<std::string> tokens;
    size_t start = 0;
    size_t end = str.find(delimiter);

    while (end != std::string::npos) {
        tokens.push_back(str.substr(start, end - start));
        start = end + 1; // Move past the delimiter
        end = str.find(delimiter, start);
    }
    tokens.push_back(str.substr(start)); // Add the last token

    return tokens;
}

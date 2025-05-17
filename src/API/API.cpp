#include "API.h"

std::vector<std::pair<String, String>> handleRequestConfig(const String& path, const std::vector<std::pair<String, String>>& params);
std::vector<std::pair<String, String>> handleRequestHomepage(const String& path, const std::vector<std::pair<String, String>>& params);
std::vector<std::pair<String, String>> handleRequestStatus(const String& path, const std::vector<std::pair<String, String>>& params);

std::vector<std::pair<String, String>> handleRequest(const String& path, const std::vector<std::pair<String, String>>& params) {
    if (path.startsWith("/config")) {
        return handleRequestConfig(path, params);
    }
    if (path.startsWith("/homepage")) {
        return handleRequestHomepage(path, params);
    }

    if (path.startsWith("/status")) {
        return handleRequestStatus(path, params);
    }

    return { { "error", "Unknown endpoint" } };
}

static std::vector<std::pair<String, String>> parseQueryString(const String& query) {
    std::vector<std::pair<String, String>> result;
    int start = 0;
    while (start < query.length()) {
        int eq = query.indexOf('=', start);
        int amp = query.indexOf('&', start);
        if (eq == -1 && amp == -1) {
            result.emplace_back(query.substring(start), "");
            break;
        } else if (eq != -1 && (amp == -1 || eq < amp)) {
            String key = query.substring(start, eq);
            String value;
            if (amp != -1) {
                value = query.substring(eq + 1, amp);
                start = amp + 1;
            } else {
                value = query.substring(eq + 1);
                start = query.length();
            }
            result.emplace_back(key, value);
        } else {
            result.emplace_back(query.substring(start, amp), "");
            start = amp + 1;
        }
    }
    return result;
}

std::vector<std::pair<String, String>> handleRequestHTML(const String& fullPath) {
    int q = fullPath.indexOf('?');
    String path = q >= 0 ? fullPath.substring(0, q) : fullPath;
    String query = q >= 0 ? fullPath.substring(q + 1) : "";
    auto params = parseQueryString(query);
    return handleRequest(path, params);
}

std::vector<std::pair<String, String>> handleRequestCLI(const String& input) {
    int space = input.indexOf(' ');
    String path = space >= 0 ? input.substring(0, space) : input;
    std::vector<std::pair<String, String>> params;

    int pos = space + 1;
    while (pos < input.length()) {
        int next = input.indexOf(' ', pos);
        String pair = (next >= 0) ? input.substring(pos, next) : input.substring(pos);
        int eq = pair.indexOf('=');
        if (eq >= 0) {
            params.emplace_back(pair.substring(0, eq), pair.substring(eq + 1));
        } else {
            params.emplace_back(pair, "");
        }
        if (next == -1) break;
        pos = next + 1;
    }

    return handleRequest(path, params);
}

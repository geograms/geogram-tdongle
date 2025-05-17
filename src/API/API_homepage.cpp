#include "API.h"
#include <Preferences.h>

// === Field declarations (key, description, default) ===
static const std::vector<std::tuple<String, String, String>> introFields = {
    { "intro_logo", "Logo image path", "/images/logo.png" },
    { "intro_banner", "Banner image path", "/images/banner.png" },
    { "intro_title", "Intro title text", "Welcome!" },
    { "intro_description", "Intro description text", "This server is running on ESP32." }
};

static const std::vector<std::tuple<String, String, String>> footerFields = {
    { "footer_copyright", "Footer copyright text", "(c) 2025 <a href=\"https://ecogram.app\">ecogram</a>" }
};

static const std::vector<std::tuple<String, String, String>> customizationFields = {
    { "custom_bg", "Background color", "#0e1b0e" },
    { "custom_text", "Text color", "#cfeac8" },
    { "custom_accent", "Accent color", "#a9dc9c" },
    { "custom_link", "Link color", "#b1e4b3" },
    { "custom_card", "Card background color", "#1b2e1b" },
    { "custom_border", "Border color", "#406343" }
};

// === JSON string escaper ===
static String jsonEscape(const String& s) {
    String out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') continue;
        else out += c;
    }
    return out;
}

// === JSON section builders ===
static String buildFlatSection(Preferences& prefs, const String& name, const std::vector<std::tuple<String, String, String>>& fields) {
    String section = "\"" + name + "\":{";
    bool first = true;

    for (const auto& entry : fields) {
        const String& key = std::get<0>(entry);
        const String& desc = std::get<1>(entry);
        const String& def = std::get<2>(entry);
        String val = prefs.getString(key.c_str(), def);

        if (!first) section += ",";
        section += "\"" + key + "\":\"" + jsonEscape(val) + "\",";
        section += "\"" + key + "_description\":\"" + jsonEscape(desc) + "\"";
        first = false;
    }

    section += "}";
    return section;
}

static String buildIntroLinks(Preferences& prefs) {
    String links = "\"links\":[";
    bool first = true;
    for (int i = 0; i < 10; ++i) {
        String keyTitle = "link_" + String(i) + "_title";
        String keyUrl   = "link_" + String(i) + "_url";

        if (!prefs.isKey(keyTitle.c_str()) || !prefs.isKey(keyUrl.c_str())) continue;

        String title = prefs.getString(keyTitle.c_str(), "");
        String url = prefs.getString(keyUrl.c_str(), "");
        if (title == "" || url == "") continue;

        if (!first) links += ",";
        links += "{\"title\":\"" + jsonEscape(title) + "\",\"url\":\"" + jsonEscape(url) + "\"}";
        first = false;
    }
    links += "]";
    return links;
}

static String buildIntroJson(Preferences& prefs) {
    String title = prefs.getString("intro_title", "Welcome!");
    String description = prefs.getString("intro_description", "This server is running on ESP32.");
    if (title == "" && description == "") return "";
    String base = buildFlatSection(prefs, "intro", introFields);
    int end = base.lastIndexOf('}');
    String links = buildIntroLinks(prefs);
    return base.substring(0, end) + "," + links + "}";
}

// === Placeholder section builders ===
static String buildAppsJson()     { return ""; }
static String buildNewsJson()     { return ""; }
static String buildGalleryJson()  { return ""; }
static String buildBlogJson()     { return ""; }
static String buildStatusJson()   { return ""; }

// === Main handler ===
std::vector<std::pair<String, String>> handleRequestHomepage(const String& path, const std::vector<std::pair<String, String>>& params) {
    std::vector<std::pair<String, String>> response;

    if (path == "/homepage") {
        Preferences prefs;
        prefs.begin("homepage", true);
        String json = "{";

        auto append = [&](const String& part) {
            if (part.length()) {
                if (json.length() > 1) json += ",";
                json += part;
            }
        };

        append(buildIntroJson(prefs));
        append(buildAppsJson());
        append(buildNewsJson());
        append(buildGalleryJson());
        append(buildBlogJson());
        append(buildStatusJson());
        append(buildFlatSection(prefs, "footer", footerFields));
        append(buildFlatSection(prefs, "customization", customizationFields));

        json += "}";
        prefs.end();

        response.emplace_back("json", json);
    } else {
        response.emplace_back("error", "invalid path");
    }

    return response;
}

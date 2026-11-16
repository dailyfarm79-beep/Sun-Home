#include "modules.hpp"
#include "lexer.hpp"

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef SUN_STDLIB_DIR
#define SUN_STDLIB_DIR "stdlib"
#endif

namespace sun {
namespace {

namespace fs = std::filesystem;

struct Import {
    std::string path;
    std::string alias;
    bool standard = false;
};

struct LoadedModule {
    std::string source;
    std::map<std::string, std::string> exports;
    bool math = false;
    bool memory = false;
    bool io = false;
    bool json = false;
    bool sys = false;
};

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r");
    return value.substr(first, last - first + 1);
}

bool identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool identifier_char(char c) {
    return identifier_start(c) || (c >= '0' && c <= '9');
}

bool has_export_prefix(const std::string& text) {
    return text.size() >= 6 && text.compare(0, 6, "export") == 0 &&
           (text.size() == 6 || text[6] == ' ' || text[6] == '\t');
}

std::string mangle_path(const fs::path& path) {
    std::string result = "sun_module";
    for (const auto c : path.generic_string()) {
        result.push_back(identifier_char(c) ? c : '_');
    }
    return result;
}

std::string rewrite_names(const std::string& source,
                          const std::map<std::string, std::string>& names,
                          const std::map<std::string, std::map<std::string, std::string>>& aliases) {
    std::ostringstream result;
    std::istringstream input(source);
    std::string line;
    bool in_cpp = false;
    while (std::getline(input, line)) {
        const auto stripped = trim(line);
        if (in_cpp) {
            result << line << '\n';
            if (stripped == "endcpp") in_cpp = false;
            continue;
        }
        if (stripped == "inline cpp:") {
            in_cpp = true;
            result << line << '\n';
            continue;
        }

        for (std::size_t i = 0; i < line.size();) {
            if (line[i] == '"') {
                const auto start = i++;
                bool escaped = false;
                while (i < line.size()) {
                    const auto c = line[i++];
                    if (escaped) escaped = false;
                    else if (c == '\\') escaped = true;
                    else if (c == '"') break;
                }
                result << line.substr(start, i - start);
                continue;
            }
            if (!identifier_start(line[i])) {
                result << line[i++];
                continue;
            }
            const auto start = i++;
            while (i < line.size() && identifier_char(line[i])) ++i;
            const auto word = line.substr(start, i - start);
            auto previous = start;
            while (previous > 0 && (line[previous - 1] == ' ' || line[previous - 1] == '\t')) {
                --previous;
            }
            if (previous > 0 && line[previous - 1] == '.') {
                result << word;
                continue;
            }
            auto lookahead = i;
            while (lookahead < line.size() && (line[lookahead] == ' ' || line[lookahead] == '\t')) {
                ++lookahead;
            }
            if (lookahead < line.size() && line[lookahead] == '.') {
                auto member_start = lookahead + 1;
                while (member_start < line.size() &&
                       (line[member_start] == ' ' || line[member_start] == '\t')) ++member_start;
                if (member_start < line.size() && identifier_start(line[member_start])) {
                    auto member_end = member_start + 1;
                    while (member_end < line.size() && identifier_char(line[member_end])) ++member_end;
                    const auto module = aliases.find(word);
                    if (module != aliases.end()) {
                        const auto member = line.substr(member_start, member_end - member_start);
                        const auto target = module->second.find(member);
                        if (target != module->second.end()) {
                            result << target->second;
                            i = member_end;
                            continue;
                        }
                        auto call_start = member_end;
                        while (call_start < line.size() &&
                               (line[call_start] == ' ' || line[call_start] == '\t')) {
                            ++call_start;
                        }
                        if (call_start < line.size() && line[call_start] == '(') {
                            throw std::runtime_error(
                                "module alias '" + word + "' does not export function '" +
                                member + "'");
                        }
                    }
                }
            }
            const auto renamed = names.find(word);
            result << (renamed == names.end() ? word : renamed->second);
        }
        result << '\n';
    }
    return result.str();
}

std::vector<std::string> lines_of(const std::string& source) {
    std::vector<std::string> lines;
    std::istringstream input(source);
    std::string line;
    while (std::getline(input, line)) lines.push_back(line);
    return lines;
}

std::string function_name(const std::string& line) {
    auto text = trim(line);
    if (has_export_prefix(text)) text = trim(text.substr(6));
    if (keyword_at_start(text) != Keyword::def) return {};
    const auto start = std::string("def ").size();
    const auto open = text.find('(', start);
    if (open == std::string::npos) return {};
    return trim(text.substr(start, open - start));
}

bool is_block_start(const std::string& text) {
    const auto keyword = keyword_at_start(text);
    return !function_name(text).empty() || keyword == Keyword::try_ ||
           keyword == Keyword::if_ || keyword == Keyword::while_ ||
           keyword == Keyword::for_ || keyword == Keyword::match_ ||
           keyword == Keyword::with_;
}

LoadedModule load_module(const fs::path& module_path, const fs::path& project_root,
                         std::set<fs::path>& loading, std::map<fs::path, LoadedModule>& cache) {
    auto canonical = fs::weakly_canonical(module_path);
    if (const auto found = cache.find(canonical); found != cache.end()) {
        auto reused = found->second;
        reused.source.clear();
        return reused;
    }
    if (!loading.insert(canonical).second) {
        throw std::runtime_error("cyclic local module import: '" + canonical.string() + "'");
    }
    std::ifstream file(canonical);
    if (!file) throw std::runtime_error("cannot open local module '" + canonical.string() + "'");
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const auto original = buffer.str();
    const auto lines = lines_of(original);

    std::vector<Import> imports;
    bool math = false;
    bool memory = false;
    bool io = false;
    bool json = false;
    bool sys = false;
    std::size_t first_content = 0;
    while (first_content < lines.size()) {
        const auto line = trim(lines[first_content]);
        if (line.empty() || line[0] == '#') {
            ++first_content;
            continue;
        }
        if (line.rfind("import ", 0) == 0) {
            const auto spec = trim(line.substr(7));
            if (spec == "math") math = true;
            else if (spec == "memory") memory = true;
            else if (spec == "io") io = true;
            else if (spec == "json") json = true;
            else if (spec == "sys") imports.push_back({"sys", "sys", true});
            else if (spec.rfind("local/", 0) == 0) {
                const auto as = spec.find(" as ");
                if (as == std::string::npos) {
                    throw std::runtime_error("local imports require an alias: import local/path as name");
                }
                auto alias = trim(spec.substr(as + 4));
                if (alias.size() >= 2 && alias.front() == '(' && alias.back() == ')') {
                    alias = trim(alias.substr(1, alias.size() - 2));
                }
                const auto local_path = trim(spec.substr(6, as - 6));
                if (alias.empty() || !identifier_start(alias.front())) {
                    throw std::runtime_error("invalid local module alias in '" + line + "'");
                }
                for (const auto c : alias) {
                    if (!identifier_char(c)) {
                        throw std::runtime_error("invalid local module alias in '" + line + "'");
                    }
                }
                imports.push_back({local_path, alias});
            } else {
                throw std::runtime_error("unsupported import '" + spec + "'");
            }
            ++first_content;
            continue;
        }
        break;
    }

    std::map<std::string, std::map<std::string, std::string>> aliases;
    std::ostringstream dependencies;
    for (const auto& import : imports) {
        LoadedModule dependency;
        if (import.standard) {
            sys = sys || import.path == "sys";
            dependency = load_module(fs::path(SUN_STDLIB_DIR) / (import.path + ".su"),
                                     project_root, loading, cache);
        } else {
            fs::path relative(import.path);
            if (relative.is_absolute() || !relative.extension().empty()) {
                throw std::runtime_error("local module imports use relative names without extensions");
            }
            for (const auto& part : relative) {
                if (part == "." || part == ".." || part.empty()) {
                    throw std::runtime_error("local module path may not contain '.' or '..'");
                }
                for (const auto c : part.string()) {
                    if (!identifier_char(c)) {
                        throw std::runtime_error("local module path must contain identifier names");
                    }
                }
            }
            const auto dependency_path = canonical.parent_path() / relative;
            dependency = load_module(dependency_path.string() + ".su", project_root,
                                     loading, cache);
        }
        if (!aliases.emplace(import.alias, dependency.exports).second) {
            throw std::runtime_error("duplicate local module alias '" + import.alias + "'");
        }
        math = math || dependency.math;
        memory = memory || dependency.memory;
        io = io || dependency.io;
        json = json || dependency.json;
        sys = sys || dependency.sys;
        dependencies << dependency.source;
    }

    std::map<std::string, std::string> names;
    LoadedModule loaded;
    const auto prefix = mangle_path(fs::relative(canonical, project_root));
    int declaration_depth = 0;
    bool declarations_in_cpp = false;
    bool has_explicit_exports = false;
    std::set<std::string> explicit_exports;
    std::vector<std::string> module_functions;
    for (std::size_t i = first_content; i < lines.size(); ++i) {
        const auto text = trim(lines[i]);
        if (declarations_in_cpp) {
            if (text == "endcpp") declarations_in_cpp = false;
            continue;
        }
        if (text == "inline cpp:") {
            declarations_in_cpp = true;
            continue;
        }
        const bool is_explicit_export = has_export_prefix(text);
        if (is_explicit_export) {
            if (declaration_depth != 0) {
                throw std::runtime_error("exported functions must be declared at module scope");
            }
            if (function_name(text).empty()) {
                throw std::runtime_error("module exports must use 'export def name(...)'");
            }
            has_explicit_exports = true;
        }
        if (text == "end") {
            if (declaration_depth > 0) --declaration_depth;
            continue;
        }
        const auto name = declaration_depth == 0 ? function_name(text) : "";
        if (!name.empty()) {
            const auto renamed = prefix + "_" + name;
            names.emplace(name, renamed);
            module_functions.push_back(name);
            if (is_explicit_export) explicit_exports.insert(name);
        }
        if (is_block_start(text)) ++declaration_depth;
    }
    for (const auto& name : module_functions) {
        if (!has_explicit_exports || explicit_exports.find(name) != explicit_exports.end()) {
            loaded.exports.emplace(name, names.at(name));
        }
    }

    int function_depth = 0;
    bool in_cpp = false;
    for (std::size_t i = first_content; i < lines.size(); ++i) {
        const auto text = trim(lines[i]);
        if (in_cpp) {
            if (text == "endcpp") in_cpp = false;
            continue;
        }
        if (text == "inline cpp:") {
            in_cpp = true;
            continue;
        }
        const bool is_explicit_export = has_export_prefix(text);
        if (function_depth == 0) {
            if (text.empty() || text[0] == '#') continue;
            if (function_name(text).empty() && is_explicit_export) {
                throw std::runtime_error("module exports must use 'export def name(...)'");
            }
            if (!function_name(text).empty()) {
                function_depth = 1;
                continue;
            }
            throw std::runtime_error("local modules may contain imports, functions, and global inline C++ only");
        }
        if (is_explicit_export) {
            throw std::runtime_error("exported functions must be declared at module scope");
        }
        if (is_block_start(text)) ++function_depth;
        else if (text == "end") --function_depth;
    }
    if (in_cpp) throw std::runtime_error("local module is missing 'endcpp'");
    if (function_depth != 0) throw std::runtime_error("local module function is missing 'end'");

    std::string body;
    bool in_module_cpp = false;
    for (std::size_t i = first_content; i < lines.size(); ++i) {
        auto line = lines[i];
        const auto text = trim(line);
        if (!in_module_cpp && has_export_prefix(text) &&
            !function_name(text).empty()) {
            const auto modifier = line.find("export");
            auto modifier_end = modifier + std::string("export").size();
            while (modifier_end < line.size() &&
                   (line[modifier_end] == ' ' || line[modifier_end] == '\t')) {
                ++modifier_end;
            }
            line.erase(modifier, modifier_end - modifier);
        } else if (text == "inline cpp:") {
            in_module_cpp = true;
        } else if (in_module_cpp && text == "endcpp") {
            in_module_cpp = false;
        }
        body += line + '\n';
    }
    loaded.source = dependencies.str() + rewrite_names(body, names, aliases);
    loaded.math = math;
    loaded.memory = memory;
    loaded.io = io;
    loaded.json = json;
    loaded.sys = sys;
    loading.erase(canonical);
    cache.emplace(canonical, loaded);
    return loaded;
}

} // namespace

std::string load_project_source(const std::filesystem::path& source_path) {
    const auto canonical = fs::weakly_canonical(source_path);
    std::ifstream file(canonical);
    if (!file) throw std::runtime_error("cannot open source file '" + source_path.string() + "'");
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const auto root_source = buffer.str();
    const auto lines = lines_of(root_source);

    std::vector<Import> imports;
    bool math = false;
    bool memory = false;
    bool io = false;
    bool json = false;
    bool sys = false;
    std::size_t first_content = 0;
    while (first_content < lines.size()) {
        const auto line = trim(lines[first_content]);
        if (line.empty() || line[0] == '#') {
            ++first_content;
            continue;
        }
        if (line.rfind("import ", 0) == 0) {
            const auto spec = trim(line.substr(7));
            if (spec == "math") math = true;
            else if (spec == "memory") memory = true;
            else if (spec == "io") io = true;
            else if (spec == "json") json = true;
            else if (spec == "sys") imports.push_back({"sys", "sys", true});
            else if (spec.rfind("local/", 0) == 0) {
                const auto as = spec.find(" as ");
                if (as == std::string::npos) {
                    throw std::runtime_error("local imports require an alias: import local/path as name");
                }
                auto alias = trim(spec.substr(as + 4));
                if (alias.size() >= 2 && alias.front() == '(' && alias.back() == ')') {
                    alias = trim(alias.substr(1, alias.size() - 2));
                }
                imports.push_back({trim(spec.substr(6, as - 6)), alias});
            } else {
                throw std::runtime_error("unsupported import '" + spec + "'");
            }
            ++first_content;
            continue;
        }
        break;
    }

    std::set<fs::path> loading;
    std::map<fs::path, LoadedModule> cache;
    std::map<std::string, std::map<std::string, std::string>> aliases;
    std::ostringstream modules;
    for (const auto& import : imports) {
        LoadedModule module;
        if (import.standard) {
            sys = sys || import.path == "sys";
            module = load_module(fs::path(SUN_STDLIB_DIR) / (import.path + ".su"),
                                 canonical.parent_path(), loading, cache);
        } else {
            fs::path relative(import.path);
            if (relative.is_absolute() || !relative.extension().empty()) {
                throw std::runtime_error("local module imports use relative names without extensions");
            }
            for (const auto& part : relative) {
                if (part == "." || part == ".." || part.empty()) {
                    throw std::runtime_error("local module path may not contain '.' or '..'");
                }
                for (const auto c : part.string()) {
                    if (!identifier_char(c)) {
                        throw std::runtime_error("local module path must contain identifier names");
                    }
                }
            }
            module = load_module(canonical.parent_path() /
                                     fs::path(relative.string() + ".su"),
                                 canonical.parent_path(), loading, cache);
        }
        if (!aliases.emplace(import.alias, module.exports).second) {
            throw std::runtime_error("duplicate local module alias '" + import.alias + "'");
        }
        math = math || module.math;
        memory = memory || module.memory;
        io = io || module.io;
        json = json || module.json;
        sys = sys || module.sys;
        modules << module.source;
    }

    std::string main_body;
    for (std::size_t i = first_content; i < lines.size(); ++i) main_body += lines[i] + '\n';
    const auto rewritten_main = rewrite_names(main_body, {}, aliases);
    std::ostringstream result;
    if (math) result << "import math\n";
    if (memory) result << "import memory\n";
    if (io) result << "import io\n";
    if (json) result << "import json\n";
    if (sys) result << "import sys\n";
    result << modules.str() << rewritten_main;
    return result.str();
}

} // namespace sun

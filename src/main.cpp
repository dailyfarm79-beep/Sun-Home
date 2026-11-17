#include "compiler.hpp"
#include "modules.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

namespace fs = std::filesystem;

struct Options {
    fs::path source;
    fs::path output;
    bool compile = false;
    bool llvm = false;
    bool emit_llvm = false;
    bool check = false;
    bool output_explicit = false;
    bool compiler_explicit = false;
    std::string compiler = "g++";
};

std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') quoted += "'\\''";
        else quoted += c;
    }
    quoted += '\'';
    return quoted;
}

void print_usage(std::ostream& out) {
    out << "Usage: sun <source.su> [-o <output.cpp>] [--compile | --check] "
           "[--compiler <path>] [--emit-llvm | --llvm]\n";
}

Options parse_options(int argc, char** argv) {
    if (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        print_usage(std::cout);
        std::exit(0);
    }
    if (argc < 2) throw std::runtime_error("missing source file");
    Options options;
    options.source = argv[1];
    options.output = options.source;
    options.output.replace_extension(".cpp");
    for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-o") {
            if (++i >= argc) throw std::runtime_error("'-o' requires an output path");
            options.output = argv[i];
            options.output_explicit = true;
        } else if (argument == "--compile") {
            options.compile = true;
        } else if (argument == "--emit-llvm") {
            options.emit_llvm = true;
        } else if (argument == "--llvm") {
            options.emit_llvm = true;
            options.llvm = true;
            options.compile = true;
        } else if (argument == "--check") {
            options.check = true;
        } else if (argument == "--compiler") {
            if (++i >= argc) throw std::runtime_error("'--compiler' requires a compiler path");
            options.compiler = argv[i];
            options.compiler_explicit = true;
        } else if (argument == "-h" || argument == "--help") {
            print_usage(std::cout);
            std::exit(0);
        } else {
            throw std::runtime_error("unknown option '" + argument + "'");
        }
    }
    if (options.source.extension() != ".su") {
        throw std::runtime_error("source file must have the .su extension");
    }
    if (options.check && options.compile) {
        throw std::runtime_error("'--check' cannot be combined with '--compile'");
    }
    if (options.check && options.output_explicit) {
        throw std::runtime_error("'-o' cannot be used with '--check'");
    }
    if (options.check && options.compiler_explicit) {
        throw std::runtime_error("'--compiler' cannot be used with '--check'");
    }
    if (options.check && (options.emit_llvm || options.llvm)) {
        throw std::runtime_error("'--check' cannot be combined with LLVM output options");
    }
    if (options.emit_llvm && options.compiler_explicit) {
        throw std::runtime_error("'--compiler' cannot be used with LLVM output options");
    }
    if (options.emit_llvm && !options.output_explicit) {
        options.output.replace_extension(".ll");
    }
    if (options.output == "-") {
        if (options.compile) {
            throw std::runtime_error("cannot compile when output is sent to stdout");
        }
    } else if (fs::absolute(options.source).lexically_normal() ==
               fs::absolute(options.output).lexically_normal()) {
        throw std::runtime_error("output path must not overwrite the source file");
    }
    return options;
}

std::string diagnostic_help(const sun::ParseError& error) {
    if (!error.help.empty()) return error.help;
    const std::string message = error.what();
    if (message.find("missing 'end'") != std::string::npos) {
        return "Add 'end' to close the function, condition, loop, or match block.";
    }
    if (message.find("requires 'import") != std::string::npos) {
        return "Add the required import near the top of the source file.";
    }
    if (message.find("must have type") != std::string::npos ||
        message.find("type mismatch") != std::string::npos ||
        message.find("cannot initialize") != std::string::npos) {
        return "Make the expression's type match the expected type; use number(value) only when an int should become a number.";
    }
    if (message.find("expected") != std::string::npos) {
        return "Check the punctuation and delimiters around this expression.";
    }
    return {};
}

void print_parse_error(const sun::ParseError& error, const std::string& source,
                       const fs::path& source_path) {
    std::cerr << "error: " << error.what() << '\n';
    std::istringstream lines(source);
    std::string text;
    bool displayed_source_line = false;
    for (std::size_t number = 1; number <= error.line && std::getline(lines, text); ++number) {
        if (number != error.line) continue;
        const auto first = text.find_first_not_of(" \t\r");
        auto column = first == std::string::npos ? text.size() : first;
        std::size_t underline_length = 1;
        const std::string message = error.what();
        if (message.rfind("unknown ", 0) == 0) {
            const auto quote = message.find('\'');
            const auto end_quote = quote == std::string::npos
                                       ? std::string::npos
                                       : message.find('\'', quote + 1);
            if (end_quote != std::string::npos) {
                const auto subject = message.substr(quote + 1, end_quote - quote - 1);
                const auto subject_column = text.find(subject);
                if (!subject.empty() && subject_column != std::string::npos) {
                    column = subject_column;
                    underline_length = subject.size();
                }
            }
        }
        const auto width = std::to_string(number).size();
        std::cerr << " --> " << source_path.string() << ':' << number << ':' << column + 1 << '\n'
                  << "  " << std::string(width, ' ') << " |\n"
                  << "  " << std::setw(static_cast<int>(width)) << number << " | " << text << '\n'
                  << "  " << std::string(width, ' ') << " | "
                  << std::string(column, ' ') << std::string(underline_length, '^') << '\n';
        const auto help = diagnostic_help(error);
        if (!help.empty()) std::cerr << "  = help: " << help << '\n';
        displayed_source_line = true;
    }
    if (!displayed_source_line) {
        const auto width = std::to_string(error.line).size();
        std::cerr << " --> " << source_path.string() << ':' << error.line << ":1\n"
                  << "  " << std::string(width, ' ') << " |\n"
                  << "  " << std::setw(static_cast<int>(width)) << error.line << " | \n"
                  << "  " << std::string(width, ' ') << " | ^\n";
        const auto help = diagnostic_help(error);
        if (!help.empty()) std::cerr << "  = help: " << help << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string source_text;
    try {
        const auto options = parse_options(argc, argv);
        source_text = sun::load_project_source(options.source);
        std::istringstream input(source_text);
        const auto program = sun::parse(input);
        if (options.check) return 0;

        const auto generated = options.emit_llvm
                                   ? sun::transpile_llvm(program)
                                   : sun::transpile(program);
        if (options.output == "-") {
            std::cout << generated;
        } else {
            std::ofstream output(options.output);
            if (!output) {
                throw std::runtime_error("cannot write output file '" + options.output.string() + "'");
            }
            output << generated;
            if (!output) throw std::runtime_error("failed while writing C++ output");
        }

        if (options.compile) {
            if (options.emit_llvm) {
                auto executable = options.source;
                executable.replace_extension("");
                const std::string ir_path = options.output.string();
                const std::string command =
                    "clang++ -x ir " + shell_quote(ir_path) +
                    " -o " + shell_quote(executable.string());
                const int result = std::system(command.c_str());
                if (result != 0) {
                    throw std::runtime_error("LLVM compilation failed (exit code " +
                                             std::to_string(result) + ")");
                }
                std::cerr << "Compiled executable: " << executable.string() << '\n';
            } else {
                const char* environment_compiler = std::getenv("CXX");
                const auto compiler = environment_compiler && !options.compiler_explicit
                                          ? std::string(environment_compiler)
                                          : options.compiler;
                auto executable = options.source;
                executable.replace_extension("");
                const std::string command = shell_quote(compiler) +
                                            " -std=c++17 -Wall -Wextra -pedantic " +
                                            shell_quote(options.output.string()) + " -o " +
                                            shell_quote(executable.string());
                const int result = std::system(command.c_str());
                if (result != 0) {
                    throw std::runtime_error("C++ compilation failed (exit code " +
                                             std::to_string(result) + ")");
                }
                std::cerr << "Compiled executable: " << executable.string() << '\n';
            }
        }
        return 0;
    } catch (const sun::ParseError& error) {
        print_parse_error(error, source_text, argv[1]);
    } catch (const std::exception& error) {
        std::cerr << "sun: error: " << error.what() << '\n';
        if (argc < 2) print_usage(std::cerr);
    }
    return 1;
}

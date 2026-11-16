#include "stdlib_io.hpp"

namespace sun {

std::string io_runtime_cpp() {
    return R"cpp(
struct SunFile {
    std::string path;
    std::string mode;
    std::fstream stream;
};

std::map<std::int64_t, std::unique_ptr<SunFile>> sun_io_files;
std::int64_t sun_io_next_handle = 1;

SunFile& sun_io_file(std::int64_t handle) {
    const auto found = sun_io_files.find(handle);
    if (found == sun_io_files.end()) {
        throw SunRuntimeError("io file handle is closed or invalid");
    }
    return *found->second;
}

std::int64_t sun_io_open(const std::string& path, const std::string& mode) {
    if (mode != "r" && mode != "w" && mode != "a" &&
        mode != "rb" && mode != "pw") {
        throw SunRuntimeError("io.open mode must be 'r', 'w', 'a', 'rb', or 'pw'");
    }
    auto file = std::make_unique<SunFile>();
    file->path = path;
    file->mode = mode;
    auto flags = std::ios::openmode{};
    if (mode == "r") flags = std::ios::in;
    else if (mode == "rb") flags = std::ios::in | std::ios::binary;
    else if (mode == "w") flags = std::ios::out | std::ios::trunc;
    else if (mode == "a") flags = std::ios::out | std::ios::app;
    else flags = std::ios::in | std::ios::out | std::ios::binary;
    file->stream.open(path, flags);
    if (!file->stream) {
        throw SunRuntimeError("io.open could not open file '" + path + "'");
    }
    if (sun_io_next_handle <= 0 ||
        sun_io_next_handle == std::numeric_limits<std::int64_t>::max()) {
        throw SunRuntimeError("io.open ran out of file handles");
    }
    const auto handle = sun_io_next_handle++;
    sun_io_files.emplace(handle, std::move(file));
    return handle;
}

std::string sun_io_read(std::int64_t handle) {
    auto& file = sun_io_file(handle);
    if (file.mode != "r" && file.mode != "rb") {
        throw SunRuntimeError("io.read requires a file opened in 'r' or 'rb' mode");
    }
    std::string result((std::istreambuf_iterator<char>(file.stream)),
                       std::istreambuf_iterator<char>());
    if (file.stream.bad()) {
        throw SunRuntimeError("io.read failed while reading file '" + file.path + "'");
    }
    return result;
}

void sun_io_write(std::int64_t handle, const std::string& contents) {
    auto& file = sun_io_file(handle);
    if (file.mode != "w" && file.mode != "a") {
        throw SunRuntimeError("io.write requires a file opened in 'w' or 'a' mode");
    }
    file.stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    file.stream.flush();
    if (!file.stream) {
        throw SunRuntimeError("io.write failed while writing file '" + file.path + "'");
    }
}

void sun_io_write_line(std::int64_t handle, std::int64_t line,
                       const std::string& contents) {
    auto& file = sun_io_file(handle);
    if (file.mode != "pw") {
        throw SunRuntimeError("io.pw requires a file opened in 'pw' mode");
    }
    if (line < 1) throw SunRuntimeError("io.pw line numbers start at 1");
    if (contents.find_first_of("\r\n") != std::string::npos) {
        throw SunRuntimeError("io.pw contents must be a single line");
    }
    std::ifstream input(file.path, std::ios::binary);
    if (!input) throw SunRuntimeError("io.pw could not read file '" + file.path + "'");
    const std::string original((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    if (input.bad()) throw SunRuntimeError("io.pw failed while reading file '" + file.path + "'");

    std::vector<std::pair<std::size_t, std::size_t>> lines;
    std::size_t start = 0;
    for (std::size_t index = 0; index < original.size(); ++index) {
        if (original[index] == '\n') {
            const auto end = index > start && original[index - 1] == '\r' ? index - 1 : index;
            lines.emplace_back(start, end);
            start = index + 1;
        }
    }
    if (start < original.size()) lines.emplace_back(start, original.size());
    if (static_cast<std::uint64_t>(line) > lines.size()) {
        throw SunRuntimeError("io.pw line number is out of bounds");
    }
    const auto [begin, end] = lines[static_cast<std::size_t>(line - 1)];
    std::string updated = original.substr(0, begin) + contents + original.substr(end);
    file.stream.close();
    file.stream.open(file.path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file.stream) throw SunRuntimeError("io.pw could not write file '" + file.path + "'");
    file.stream.write(updated.data(), static_cast<std::streamsize>(updated.size()));
    file.stream.flush();
    if (!file.stream) throw SunRuntimeError("io.pw failed while writing file '" + file.path + "'");
    file.stream.close();
    file.stream.clear();
    file.stream.open(file.path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.stream) throw SunRuntimeError("io.pw could not reopen file '" + file.path + "'");
}

void sun_io_close(std::int64_t handle) {
    const auto found = sun_io_files.find(handle);
    if (found == sun_io_files.end()) {
        throw SunRuntimeError("io file handle is closed or invalid");
    }
    const auto path = found->second->path;
    found->second->stream.close();
    if (found->second->stream.fail()) {
        sun_io_files.erase(found);
        throw SunRuntimeError("io.close failed for file '" + path + "'");
    }
    sun_io_files.erase(found);
}

void sun_io_close_noexcept(std::int64_t handle) noexcept {
    const auto found = sun_io_files.find(handle);
    if (found == sun_io_files.end()) return;
    found->second->stream.close();
    sun_io_files.erase(found);
}

class SunFileGuard {
public:
    explicit SunFileGuard(std::int64_t handle) : handle_(handle) {}
    SunFileGuard(const SunFileGuard&) = delete;
    SunFileGuard& operator=(const SunFileGuard&) = delete;
    ~SunFileGuard() noexcept { sun_io_close_noexcept(handle_); }
    std::int64_t handle() const { return handle_; }

private:
    std::int64_t handle_;
};

std::string sun_io_read_text(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw SunRuntimeError("io.read_text could not open file '" + path + "'");
    }
    std::string contents((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    if (file.bad()) {
        throw SunRuntimeError("io.read_text failed while reading file '" + path + "'");
    }
    return contents;
}

void sun_io_write_text(const std::string& path, const std::string& contents) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw SunRuntimeError("io.write_text could not open file '" + path + "'");
    }
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!file) {
        throw SunRuntimeError("io.write_text failed while writing file '" + path + "'");
    }
}

void sun_io_append_text(const std::string& path, const std::string& contents) {
    std::ofstream file(path, std::ios::binary | std::ios::app);
    if (!file) {
        throw SunRuntimeError("io.append_text could not open file '" + path + "'");
    }
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!file) {
        throw SunRuntimeError("io.append_text failed while writing file '" + path + "'");
    }
}

bool sun_io_exists(const std::string& path) {
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) {
        throw SunRuntimeError("io.exists failed for '" + path + "': " + error.message());
    }
    return exists;
}

std::vector<std::string> sun_io_read_lines(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw SunRuntimeError("io.read_lines could not open file '" + path + "'");
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) lines.push_back(line);
    if (file.bad()) {
        throw SunRuntimeError("io.read_lines failed while reading file '" + path + "'");
    }
    return lines;
}

bool sun_io_is_directory(const std::string& path) {
    std::error_code error;
    const bool is_directory = std::filesystem::is_directory(path, error);
    if (error) {
        throw SunRuntimeError("io.is_directory failed for '" + path + "': " + error.message());
    }
    return is_directory;
}

bool sun_io_remove(const std::string& path) {
    std::error_code error;
    const bool removed = std::filesystem::remove(path, error);
    if (error) {
        throw SunRuntimeError("io.remove failed for '" + path + "': " + error.message());
    }
    return removed;
}

bool sun_io_create_directories(const std::string& path) {
    std::error_code error;
    const bool created = std::filesystem::create_directories(path, error);
    if (error) {
        throw SunRuntimeError("io.create_directories failed for '" + path + "': " +
                              error.message());
    }
    return created;
}

bool sun_io_createdir(const std::string& path, const std::string& exist) {
    if (exist != "ok" && exist != "no") {
        throw SunRuntimeError("io.createdir exist must be 'ok' or 'no'");
    }

    std::error_code error;
    const bool already_exists = std::filesystem::exists(path, error);
    if (error) {
        throw SunRuntimeError("io.createdir failed for '" + path + "': " +
                              error.message());
    }
    if (already_exists) {
        const bool is_directory = std::filesystem::is_directory(path, error);
        if (error) {
            throw SunRuntimeError("io.createdir failed for '" + path + "': " +
                                  error.message());
        }
        if (!is_directory) {
            throw SunRuntimeError("io.createdir path exists and is not a directory: '" +
                                  path + "'");
        }
        if (exist == "no") {
            throw SunRuntimeError("io.createdir directory already exists: '" + path + "'");
        }
        return false;
    }

    const bool created = std::filesystem::create_directories(path, error);
    if (error) {
        if (exist == "ok") {
            std::error_code directory_error;
            if (std::filesystem::is_directory(path, directory_error) &&
                !directory_error) {
                return false;
            }
        }
        throw SunRuntimeError("io.createdir could not create directory '" + path +
                              "': " + error.message());
    }
    if (created) return true;

    const bool is_directory = std::filesystem::is_directory(path, error);
    if (error) {
        throw SunRuntimeError("io.createdir failed for '" + path + "': " +
                              error.message());
    }
    if (!is_directory) {
        throw SunRuntimeError("io.createdir path exists and is not a directory: '" +
                              path + "'");
    }
    if (exist == "no") {
        throw SunRuntimeError("io.createdir directory already exists: '" + path + "'");
    }
    return false;
}

std::int64_t sun_io_file_size(const std::string& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        throw SunRuntimeError("io.file_size failed for '" + path + "': " + error.message());
    }
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max())) {
        throw SunRuntimeError("io.file_size is too large for int");
    }
    return static_cast<std::int64_t>(size);
}
)cpp";
}

} // namespace sun

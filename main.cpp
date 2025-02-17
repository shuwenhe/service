#include <httplib.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <iostream>

namespace fs = std::filesystem;
using namespace httplib;

class VideoServer {
private:
    fs::path base_path_;
    const size_t CHUNK_SIZE = 8192;

    std::string get_mime_type(const fs::path& path) {
        std::string ext = path.extension().string();
        if (ext == ".mp4") return "video/mp4";
        return "application/octet-stream";
    }

    fs::path translate_path(const std::string& path) {
        std::string clean_path = path;
        if (!clean_path.empty() && clean_path[0] == '/') {
            clean_path = clean_path.substr(1);
        }
        return (base_path_ / clean_path).lexically_normal();
    }

    void handle_range_request(const fs::path& filepath, const std::string& range_header,
                            uintmax_t filesize, const std::string& content_type,
                            Response& res) {
        std::regex range_regex(R"(bytes=(\d*)-(\d*))");
        std::smatch matches;
        if (!std::regex_match(range_header, matches, range_regex)) {
            res.status = 400;
            return;
        }

        size_t start = matches[1].str().empty() ? 0 : std::stoull(matches[1].str());
        size_t end = matches[2].str().empty() ? filesize - 1 : std::stoull(matches[2].str());
        end = std::min(end, filesize - 1);
        size_t length = end - start + 1;

        res.status = 206;
        res.set_header("Content-Type", content_type);
        res.set_header("Content-Length", std::to_string(length));
        res.set_header("Content-Range", 
            "bytes " + std::to_string(start) + "-" + 
            std::to_string(end) + "/" + std::to_string(filesize));
        res.set_header("Accept-Ranges", "bytes");

        auto file = std::make_shared<std::ifstream>(filepath, std::ios::binary);
        res.set_content_provider(
            length,
            content_type,
            [file, start](size_t offset, size_t chunk_length, DataSink& sink) {
                static thread_local std::vector<char> buffer(8192);
                if (!file->seekg(start + offset)) return false;
                size_t to_read = std::min(chunk_length, buffer.size());
                file->read(buffer.data(), to_read);
                size_t bytes_read = file->gcount();
                if (bytes_read == 0) return false;
                return sink.write(buffer.data(), bytes_read);
            }
        );
    }

public:
    explicit VideoServer(const std::string& base_path) 
        : base_path_(fs::absolute(base_path)) {}

    void operator()(const Request& req, Response& res) {
        if (req.method == "OPTIONS") {
            res.set_header("Allow", "GET, HEAD, OPTIONS");
            res.status = 204;
            return;
        }

        auto filepath = translate_path(req.path);
        if (!fs::exists(filepath) || fs::is_directory(filepath)) {
            res.status = 404;
            return;
        }

        const auto filesize = fs::file_size(filepath);
        const auto content_type = get_mime_type(filepath);

        if (req.has_header("Range")) {
            handle_range_request(filepath, req.get_header_value("Range"), 
                               filesize, content_type, res);
        } else {
            res.set_header("Content-Type", content_type);
            res.set_header("Content-Length", std::to_string(filesize));
            res.set_header("Accept-Ranges", "bytes");
            res.set_content_provider(
                filesize,
                content_type,
                [filepath](size_t offset, size_t chunk_length, DataSink& sink) {
                    static thread_local std::vector<char> buffer(8192);
                    std::ifstream file(filepath, std::ios::binary);
                    if (!file.seekg(offset)) return false;
                    size_t to_read = std::min(chunk_length, buffer.size());
                    file.read(buffer.data(), to_read);
                    return sink.write(buffer.data(), file.gcount());
                }
            );
        }
    }
};

int main(int argc, char* argv[]) {
    Server svr;
    auto handler = std::make_shared<VideoServer>("/videos");
    svr.Get(".*", [handler](const Request& req, Response& res) { (*handler)(req, res); });
    svr.listen("0.0.0.0", 8080);
    return 0;
}

#include <httplib.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace fs = std::filesystem;
using namespace httplib;

class VideoStreamHandler {
public:
    explicit VideoStreamHandler(const std::string& base_path) 
        : base_path_(fs::absolute(base_path)) {
        init_mime_types();
    }

    void operator()(const Request& req, Response& res) {
        try {
            handle_request(req, res);
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content("Internal Server Error: " + std::string(e.what()), "text/plain");
        }
    }

private:
    fs::path base_path_;
    std::unordered_map<std::string, std::string> mime_types_;
    std::mutex file_mutex_;

    void init_mime_types() {
        mime_types_ = {
            {".mp4", "video/mp4"},
            {".webm", "video/webm"},
            {".mkv", "video/x-matroska"},
            {".mov", "video/quicktime"},
            {".avi", "video/x-msvideo"},
            {".m4v", "video/mp4"},
            {".f4v", "video/mp4"}
        };
    }

    void handle_request(const Request& req, Response& res) {
        try {
            // Log incoming request
            std::cout << "\n[Request Headers]\n"
                      << "Method: " << req.method << "\n"
                      << "Path: " << req.path << "\n"
                      << "Headers:\n";
            for (const auto& [key, val] : req.headers) {
                std::cout << "  " << key << ": " << val << "\n";
            }

            if (req.method == "OPTIONS") {
                res.status = 204;
                log_response(res);
                return;
            }

            auto filepath = translate_path(req.path);
            std::cout << "filepath: " << filepath << std::endl;
            
            if (!validate_path(filepath, res)) {
                log_response(res);
                return;
            }

            const auto filesize = fs::file_size(filepath);
            const auto content_type = get_mime_type(filepath);

            if (req.has_header("Range")) {
                handle_range_request(filepath, req.get_header_value("Range"), 
                                   filesize, content_type, res);
            } else {
                serve_full_file(filepath, filesize, content_type, res);
            }

            // Log response details
            log_response(res);
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content("Internal Server Error: " + std::string(e.what()), "text/plain");
            log_response(res);
        }
    }

    fs::path translate_path(const std::string& path) const {
        std::string clean_path = path;
        if (clean_path.find("..") != std::string::npos) {
            throw std::invalid_argument("Invalid path");
        }
        if (clean_path[0] == '/') {
            clean_path = clean_path.substr(1);
        }
        return (base_path_ / clean_path).lexically_normal();
    }

    bool validate_path(const fs::path& filepath, Response& res) const {
        if (!fs::exists(filepath)) {
            res.status = 404;
            res.set_content("File not found", "text/plain");
            return false;
        }
        if (fs::is_directory(filepath)) {
            res.status = 404;
            res.set_content("Directories not supported", "text/plain");
            return false;
        }
        return true;
    }

    std::string get_mime_type(const fs::path& filepath) const {
        auto ext = filepath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return mime_types_.count(ext) ? mime_types_.at(ext) : "application/octet-stream";
    }

    void handle_range_request(const fs::path& filepath, const std::string& range_header,
                            uintmax_t filesize, const std::string& content_type,
                            Response& res) {
        std::regex re("bytes=(\\d*)-(\\d*)");
        std::smatch match;
        if (!std::regex_match(range_header, match, re)) {
            res.status = 400;
            res.set_content("Invalid Range header", "text/plain");
            return;
        }

        size_t start = match[1].str().empty() ? 0 : std::stoull(match[1].str());
        size_t end = match[2].str().empty() ? filesize - 1 : std::stoull(match[2].str());

        if (start >= filesize || start > end) {
            res.status = 416;
            res.set_header("Content-Range", "bytes */" + std::to_string(filesize));
            return;
        }
        end = std::min(end, filesize - 1);

        // Set critical headers only once
        res.set_header("Content-Range", 
                      "bytes " + std::to_string(start) + "-" + 
                      std::to_string(end) + "/" + std::to_string(filesize));
        res.set_header("Accept-Ranges", "bytes");
        res.set_header("Content-Type", content_type);
        res.set_header("Content-Length", std::to_string(end - start + 1));

        res.status = 206;

        // Directly stream bytes like Python version
        stream_file_direct(filepath, start, end, res);
    }

    void stream_file_direct(const fs::path& filepath, size_t start, size_t end, 
                          Response& res) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            res.status = 500;
            res.set_content("Failed to open file", "text/plain");
            return;
        }

        file.seekg(start);
        const size_t chunk_size = 8192;  // Match Python's chunk size
        size_t length = end - start + 1;
        size_t bytes_sent = 0;

        std::vector<char> buffer(chunk_size);
        while (bytes_sent < length) {
            size_t to_read = std::min(chunk_size, length - bytes_sent);
            file.read(buffer.data(), to_read);
            size_t bytes_read = file.gcount();
            
            if (bytes_read == 0) break;  // EOF or error
            
            res.body.append(buffer.data(), bytes_read);
            bytes_sent += bytes_read;
        }

        if (bytes_sent != length) {
            res.status = 500;
            res.set_content("File read error", "text/plain");
            return;
        }
    }

    void serve_full_file(const fs::path& filepath, uintmax_t filesize,
                        const std::string& content_type, Response& res) {
        // Add Content-Length for full file responses
        res.set_header("Content-Length", std::to_string(filesize));
        res.set_header("Accept-Ranges", "bytes");
        stream_file_direct(filepath, 0, filesize - 1, res);
    }

    void log_response(const Response& res) const {
        std::cout << "[Response]\n"
                  << "Status: " << res.status << "\n"
                  << "Headers:\n";
        for (const auto& [key, val] : res.headers) {
            std::cout << "  " << key << ": " << val << "\n";
        }
        std::cout << "Body size: " << res.body.size() << " bytes\n"
                  << "-----------------------------\n";
    }
};

int main(int argc, char* argv[]) {
    try {
        std::string base_path = "/videos";
        int port = 8080;

        // Proper argument parsing
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--port" && i + 1 < argc) {
                port = std::stoi(argv[++i]);
            } else if (arg == "--path" && i + 1 < argc) {
                base_path = argv[++i];
            } else {
                std::cerr << "Unknown option or missing value: " << arg << "\n";
                return 1;
            }
        }

        Server server;
        auto handler = std::make_shared<VideoStreamHandler>(base_path);
        
        // Use lambda to avoid copy issues
        server.Get(".*", [handler](const Request& req, Response& res) {
            (*handler)(req, res);
        });
        
        server.Options(".*", [](const Request&, Response& res) {
            res.set_header("Allow", "GET, HEAD, OPTIONS");
        });

        std::cout << "Serving videos from " << fs::absolute(base_path) 
                 << " on port " << port << "\n";
        std::cout << "Access videos at http://localhost:" << port << "/\n";

        server.listen("0.0.0.0", port);
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
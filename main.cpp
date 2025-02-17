#include <httplib.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <iostream>
#include <iomanip>

namespace fs = std::filesystem;
using namespace httplib;

class VideoServer {
private:
    fs::path base_path_;
    const size_t CHUNK_SIZE = 8192;

    void log_request(const Request& req) {
        std::cout << "\n" << std::string(50, '=') << "\n"
                  << "📥 REQUEST\n"
                  << std::string(50, '=') << "\n"
                  << "Method: " << req.method << "\n"
                  << "Path: " << req.path << "\n\n"
                  << "Headers:\n";
        for (const auto& [key, val] : req.headers) {
            std::cout << "  " << key << ": " << val << "\n";
        }
        std::cout << std::string(50, '=') << "\n\n";
    }

    void log_response(int status, const Headers& headers, size_t body_size) {
        std::cout << "\n" << std::string(50, '=') << "\n"
                  << "📤 RESPONSE\n"
                  << std::string(50, '=') << "\n"
                  << "Status: " << status << "\n\n"
                  << "Headers:\n";
        for (const auto& [key, val] : headers) {
            std::cout << "  " << key << ": " << val << "\n";
        }
        std::cout << "Body size: " << body_size << " bytes\n"
                  << std::string(50, '=') << "\n\n";
    }

    std::string get_mime_type(const fs::path& path) {
        std::string ext = path.extension().string();
        if (ext == ".mp4") return "video/mp4";
        if (ext == ".webm") return "video/webm";
        if (ext == ".mkv") return "video/x-matroska";
        if (ext == ".mov") return "video/quicktime";
        return "application/octet-stream";
    }

    fs::path translate_path(const std::string& path) {
        std::string clean_path = path;
        if (!clean_path.empty() && clean_path[0] == '/') {
            clean_path = clean_path.substr(1);
        }
        return (base_path_ / clean_path).lexically_normal();
    }



public:
    explicit VideoServer(const std::string& base_path) 
        : base_path_(fs::absolute(base_path)) {}

    void operator()(const Request& req, Response& res) {
        log_request(req);

        if (req.method == "OPTIONS") {
            res.set_header("Allow", "GET, HEAD, OPTIONS");
            res.status = 204;
            return;
        }

        auto filepath = translate_path(req.path);
        std::cout << "filepath: " << filepath << std::endl;
            // Check if file exists
        struct stat st;
        if (stat(filepath.c_str(), &st) == 0) {
            // File exists
            size_t filesize = st.st_size;

            std::ifstream file(filepath, std::ios::binary);
            if (!file.is_open()) {
                res.status = 500;
                res.body = "Error opening file.";
                return;
            }

            std::string range_header = req.get_header_value("Range");
            if (!range_header.empty()) {
                // Handle Range request
                std::regex range_regex("bytes=(\\d*)-(\\d*)");
                std::smatch match;
                if (std::regex_search(range_header, match, range_regex)) {
                    long start = std::stol(match[1].str());
                    long end = match[2].str().empty() ? filesize - 1 : std::stol(match[2].str());

                    if (start > end || start >= filesize) {
                        res.status = 416;
                        res.body = "Requested range not satisfiable.";
                        return;
                    }

                    long length = end - start + 1;
                    
                    res.status = 206;  // Partial Content
                    res.set_header("Content-Type", "video/mp4");
                    res.set_header("Content-Length", std::to_string(length));
                    res.set_header("Content-Range", "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(filesize));
                    res.set_header("Accept-Ranges", "bytes");

                    // Log response
                    log_response(206, res.headers, length);

                    // Send partial content
                    file.seekg(start, std::ios::beg);
                    char buffer[8192];
                    size_t bytes_sent = 0;

                    while (bytes_sent < length) {
                        size_t chunk_size = std::min(sizeof(buffer), static_cast<size_t>(length - bytes_sent));
                        file.read(buffer, chunk_size);
                        res.body.append(buffer, chunk_size);
                        bytes_sent += chunk_size;
                    }

                    return;
                } else {
                    res.status = 400;
                    res.body = "Invalid Range header.";
                    return;
                }
            } else {
                // Serve the whole file if no Range header
                res.status = 200;
                res.set_header("Content-Type", "video/mp4");
                res.set_header("Content-Length", std::to_string(filesize));
                res.set_header("Accept-Ranges", "bytes");

                // Log response
                log_response(200, res.headers, filesize);

                // Send entire content
                char buffer[8192];
                while (file.read(buffer, sizeof(buffer))) {
                    res.body.append(buffer, file.gcount());
                }

                return;
            }
        }

        res.status = 404;
        res.body = "File not found.";
        return;
    }
};

int main() {
    Server svr;
    auto handler = std::make_shared<VideoServer>("/videos");
    
    svr.Get(".*", [handler](const Request& req, Response& res) { 
        (*handler)(req, res); 
    });

    std::cout << "Serving videos from " << fs::absolute("/videos") << " on port 8080\n";
    std::cout << "Access videos at http://localhost:8080/\n";

    svr.listen("0.0.0.0", 8080);
    return 0;
}

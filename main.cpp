#include <httplib.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#include <memory>
#include <algorithm>

namespace fs = std::filesystem;
using namespace httplib;

class VideoServer {
private:
    fs::path base_path_;
    static constexpr size_t CHUNK_SIZE = 8192;  // 8KB chunks

    // Return current timestamp string.
    std::string get_current_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&time);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    // Write a log entry both to console and to service.log.
    void write_log(const std::string& log_entry) {
        std::cout << log_entry << std::endl;
        std::ofstream log_file("service.log", std::ios::app);
        if (log_file.is_open()) {
            log_file << log_entry << std::endl;
        } else {
            std::cerr << "Failed to open service.log for writing." << std::endl;
        }
    }

    // Log the incoming request.
    void log_request(const Request& req) {
        std::string log_entry = "\n" + std::string(50, '=') + "\n"
                                + "📥 REQUEST\n"
                                + std::string(50, '=') + "\n"
                                + "Timestamp: " + get_current_timestamp() + "\n"
                                + "Method: " + req.method + "\n"
                                + "Path: " + req.path + "\n\n"
                                + "Headers:\n";
        for (const auto& [key, val] : req.headers) {
            log_entry += "  " + key + ": " + val + "\n";
        }
        log_entry += std::string(50, '=') + "\n\n";
        write_log(log_entry);
    }

    // Log the response details.
    void log_response(int status, const Headers& headers, size_t body_size) {
        std::string log_entry = "\n" + std::string(50, '=') + "\n"
                                + "📤 RESPONSE\n"
                                + std::string(50, '=') + "\n"
                                + "Timestamp: " + get_current_timestamp() + "\n"
                                + "Status: " + std::to_string(status) + "\n\n"
                                + "Headers:\n";
        for (const auto& [key, val] : headers) {
            log_entry += "  " + key + ": " + val + "\n";
        }
        log_entry += "Body size: " + std::to_string(body_size) + " bytes\n"
                     + std::string(50, '=') + "\n\n";
        write_log(log_entry);
    }

    // Determine MIME type based on file extension.
    std::string get_mime_type(const fs::path& path) {
        std::string ext = path.extension().string();
        if (ext == ".mp4") return "video/mp4";
        if (ext == ".webm") return "video/webm";
        if (ext == ".mkv") return "video/x-matroska";
        if (ext == ".mov") return "video/quicktime";
        if (ext == ".avi") return "video/x-msvideo";
        return "application/octet-stream";
    }

    // Translate the URL path into a filesystem path.
    fs::path translate_path(const std::string& path) {
        std::string clean_path = path;
        if (!clean_path.empty() && clean_path[0] == '/') {
            clean_path = clean_path.substr(1);
        }
        return (base_path_ / clean_path).lexically_normal();
    }

    // --- RANGE REQUEST HANDLER ---
    void handle_range_request(const fs::path& filepath, const std::string& range_header,
                              uintmax_t filesize, const std::string& content_type,
                              Response& res) {
        std::regex range_regex(R"(bytes=(\d*)-(\d*))");
        std::smatch matches;
        if (!std::regex_match(range_header, matches, range_regex)) {
            res.status = 400;
            res.set_content("Invalid Range header", "text/plain");
            return;
        }

        size_t start = 0;
        if (!matches[1].str().empty()) {
            start = std::stoull(matches[1].str());
        }

        size_t end = filesize - 1;
        if (!matches[2].str().empty()) {
            end = std::stoull(matches[2].str());
        }

        if (start >= filesize || start > end) {
            res.status = 416;
            res.set_header("Content-Range", "bytes */" + std::to_string(filesize));
            res.set_content("Requested range not satisfiable", "text/plain");
            return;
        }

        end = std::min(end, filesize - 1);
        size_t length = end - start + 1;

        // Set necessary headers.
        res.status = 206;
        res.set_header("Accept-Ranges", "bytes");
        res.set_header("Content-Range", "bytes " + std::to_string(start) + "-" +
                                           std::to_string(end) + "/" + std::to_string(filesize));
        res.set_header("Content-Length", std::to_string(length));
        res.set_header("Content-Type", content_type);
        // Optionally force connection close so that AVPlayer re-requests data.
        res.set_header("Connection", "close");

        // Open the file once.
        auto file_ptr = std::make_shared<std::ifstream>(filepath, std::ios::binary);
        if (!file_ptr || !file_ptr->is_open()) {
            res.status = 500;
            res.set_content("Failed to open file", "text/plain");
            return;
        }
        // Seek to the beginning of the requested range.
        file_ptr->clear();
        file_ptr->seekg(start, std::ios::beg);

        // Use a persistent stream in the content provider lambda.
        res.set_content_provider(
            length,
            content_type,
            [file_ptr, start](size_t offset, size_t chunk_length, DataSink& sink) {
                static thread_local std::vector<char> buffer(CHUNK_SIZE);
                // Ensure we clear any eof flags.
                file_ptr->clear();
                // Position the stream to the current offset.
                file_ptr->seekg(start + offset, std::ios::beg);
                size_t to_read = std::min(chunk_length, buffer.size());
                file_ptr->read(buffer.data(), to_read);
                size_t bytes_read = file_ptr->gcount();
                if (bytes_read == 0) return false;
                return sink.write(buffer.data(), bytes_read);
            }
        );

        log_response(res.status, res.headers, length);
    }

    // --- FULL FILE HANDLER ---
    void serve_full_file(const fs::path& filepath, uintmax_t filesize,
                         const std::string& content_type, Response& res) {
        res = Response();
        res.set_header("Content-Type", content_type);
        res.set_header("Content-Length", std::to_string(filesize));
        res.set_header("Accept-Ranges", "bytes");
        // Optionally force connection close.
        res.set_header("Connection", "close");
        res.status = 200;

        auto file_ptr = std::make_shared<std::ifstream>(filepath, std::ios::binary);
        if (!file_ptr || !file_ptr->is_open()) {
            res.status = 500;
            res.set_content("Failed to open file", "text/plain");
            return;
        }

        res.set_content_provider(
            filesize,
            content_type,
            [file_ptr](size_t offset, size_t chunk_length, DataSink& sink) {
                static thread_local std::vector<char> buffer(CHUNK_SIZE);
                file_ptr->clear();
                file_ptr->seekg(offset, std::ios::beg);
                size_t to_read = std::min(chunk_length, buffer.size());
                file_ptr->read(buffer.data(), to_read);
                size_t bytes_read = file_ptr->gcount();
                if (bytes_read == 0) return false;
                return sink.write(buffer.data(), bytes_read);
            }
        );

        log_response(res.status, res.headers, filesize);
    }

public:
    explicit VideoServer(const std::string& base_path)
        : base_path_(fs::absolute(base_path)) {}

    // Main request handler. (Also handles HEAD requests internally.)
    void operator()(const Request& req, Response& res) {
        log_request(req);
        res = Response();

        if (req.method == "OPTIONS") {
            res.set_header("Allow", "GET, HEAD, OPTIONS");
            res.status = 204;
            return;
        }

        auto filepath = translate_path(req.path);
        std::cout << "filepath: " << filepath << std::endl;

        if (!fs::exists(filepath)) {
            res.status = 404;
            res.set_content("File not found", "text/plain");
            return;
        }
        if (fs::is_directory(filepath)) {
            res.status = 404;
            res.set_content("Directories not supported", "text/plain");
            return;
        }

        uintmax_t filesize = fs::file_size(filepath);
        std::string content_type = get_mime_type(filepath);
        bool is_head = (req.method == "HEAD");

        if (req.has_header("Range")) {
            handle_range_request(filepath, req.get_header_value("Range"),
                                 filesize, content_type, res);
            if (is_head) {
                // For HEAD, clear body.
                res.body.clear();
            }
        } else {
            if (is_head) {
                res.set_header("Content-Type", content_type);
                res.set_header("Content-Length", std::to_string(filesize));
                res.set_header("Accept-Ranges", "bytes");
                res.status = 200;
                log_response(res.status, res.headers, 0);
            } else {
                serve_full_file(filepath, filesize, content_type, res);
            }
        }
    }
};

int main(int argc, char* argv[]) {
    try {
        std::string base_path = "/videos";
        int port = 8080;

        // Simple command-line parsing.
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--port" && i + 1 < argc) {
                port = std::stoi(argv[++i]);
            } else if (arg == "--path" && i + 1 < argc) {
                base_path = argv[++i];
            }
        }

        Server server;
        auto handler = std::make_shared<VideoServer>(base_path);

        // Register GET and OPTIONS. (HEAD requests are handled by the same GET handler.)
        server.Get(".*", [handler](const Request& req, Response& res) {
            (*handler)(req, res);
        });
        server.Options(".*", [handler](const Request& req, Response& res) {
            (*handler)(req, res);
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

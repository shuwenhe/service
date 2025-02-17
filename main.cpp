#include <httplib.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <unordered_map>
#include <mutex>

namespace fs = std::filesystem;

class VideoStreamHandler {
public:
    VideoStreamHandler(const std::string& base_path) 
        : base_path_(fs::absolute(base_path)) {
        init_mime_types();
    }

    void handle_request(const httplib::Request& req, httplib::Response& res) {
        try {
            std::cout << "Handling request for path: " << req.path << std::endl;
            
            // Add CORS headers for browser compatibility
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, HEAD, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Range, Accept-Ranges, Content-Range");
            res.set_header("Cross-Origin-Resource-Policy", "cross-origin");
            res.set_header("Cross-Origin-Embedder-Policy", "require-corp");
            res.set_header("Cross-Origin-Opener-Policy", "same-origin");
            
            // Handle OPTIONS request for CORS preflight
            if (req.method == "OPTIONS") {
                res.status = 204;
                return;
            }

            // Translate and validate the requested path
            auto filepath = translate_path(req.path);
            std::cout << "Resolved path: " << filepath << std::endl;
            
            if (!validate_path(filepath, res)) {
                std::cout << "Path validation failed for: " << filepath << std::endl;
                return;
            }

            // Get file info
            const auto filesize = fs::file_size(filepath);
            const auto content_type = get_mime_type(filepath);
            std::cout << "File size: " << filesize << ", Content-Type: " << content_type << std::endl;

            // Check for Range header
            if (auto range_header = req.get_header_value("Range"); !range_header.empty()) {
                std::cout << "Range request: " << range_header << std::endl;
                handle_range_request(filepath, range_header, filesize, content_type, res);
            } else {
                std::cout << "Full file request" << std::endl;
                serve_full_file(filepath, filesize, content_type, res);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error handling request: " << e.what() << std::endl;
            res.status = 500;
            res.set_content("Internal Server Error: " + std::string(e.what()), "text/plain");
        }
    }

private:
    fs::path base_path_;
    std::unordered_map<std::string, std::string> mime_types_;

    void init_mime_types() {
        mime_types_ = {
            {".mp4", "video/mp4"},
            {".webm", "video/webm"},
            {".mkv", "video/x-matroska"},
            {".mov", "video/quicktime"},
            {".avi", "video/x-msvideo"},
            // Add more specific MIME types for MP4 variants
            {".m4v", "video/mp4"},
            {".f4v", "video/mp4"},
            {".m4a", "audio/mp4"}
        };
    }

    fs::path translate_path(const std::string& path) const {
        // Replace starts_with with string comparison since we're using C++17
        std::string normalized = path.compare(0, 1, "/") == 0 ? path.substr(1) : path;
        return (base_path_ / normalized).lexically_normal();
    }

    bool validate_path(const fs::path& filepath, httplib::Response& res) const {
        if (!fs::exists(filepath)) {
            res.status = 404;
            res.set_content("File not found", "text/plain");
            return false;
        }
        if (!fs::is_regular_file(filepath)) {
            res.status = 403;
            res.set_content("Directory access forbidden", "text/plain");
            return false;
        }
        return true;
    }

    std::string get_mime_type(const fs::path& filepath) const {
        auto ext = filepath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        // Default to video/mp4 for .mp4 files regardless of variant
        if (ext == ".mp4") {
            return "video/mp4";
        }
        
        return mime_types_.count(ext) ? mime_types_.at(ext) : "application/octet-stream";
    }

    void handle_range_request(const fs::path& filepath, const std::string& range_header,
                            size_t filesize, const std::string& content_type,
                            httplib::Response& res) {
        // Parse range header (format: "bytes=start-end")
        std::regex range_regex(R"(bytes=(\d*)-(\d*))");
        std::smatch matches;
        if (!std::regex_match(range_header, matches, range_regex)) {
            res.status = 400;
            res.set_content("Invalid Range header", "text/plain");
            return;
        }

        // Parse range values
        size_t start = matches[1].str().empty() ? 0 : std::stoull(matches[1].str());
        size_t end = matches[2].str().empty() ? filesize - 1 : std::stoull(matches[2].str());

        // Validate range
        if (start >= filesize) {
            res.status = 416; // Range Not Satisfiable
            res.set_header("Content-Range", "bytes */" + std::to_string(filesize));
            return;
        }
        end = std::min(end, filesize - 1);

        // Set response headers for partial content
        res.status = 206;
        const size_t content_length = end - start + 1;
        
        // Set all headers at once
        if (res.get_header_value("Content-Type").empty()) {
            res.set_header("Content-Type", content_type);
        }
        res.set_header("Accept-Ranges", "bytes");
        res.set_header("Content-Length", std::to_string(content_length));
        res.set_header("Content-Range", 
            "bytes " + std::to_string(start) + "-" + 
            std::to_string(end) + "/" + std::to_string(filesize));
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_header("Access-Control-Expose-Headers", "Content-Length, Content-Range");

        // Stream the content
        stream_file_range(filepath, start, end, res);
    }

    void serve_full_file(const fs::path& filepath, size_t filesize,
                        const std::string& content_type, httplib::Response& res) {
        // Set all headers at once
        res.set_header("Content-Type", content_type);
        res.set_header("Accept-Ranges", "bytes");
        res.set_header("Content-Length", std::to_string(filesize));
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_header("Access-Control-Expose-Headers", "Content-Length, Content-Range");

        stream_file_range(filepath, 0, filesize - 1, res);
    }

    void stream_file_range(const fs::path& filepath, size_t start, size_t end,
                          httplib::Response& res) {
        static const size_t CHUNK_SIZE = 8192;

        // Open file before setting content provider
        auto file = std::make_shared<std::ifstream>(filepath, std::ios::binary);
        if (!file->good()) {
            res.status = 500;
            res.set_content("Failed to open file", "text/plain");
            return;
        }

        // Seek to start position
        file->seekg(start);

        // Create buffer that will live throughout the streaming
        auto buffer = std::make_shared<std::vector<char>>(CHUNK_SIZE);

        // Use content provider without setting additional headers
        res.body.clear(); // Clear any existing body
        res.set_content_provider(
            end - start + 1,  // Content length
            res.get_header_value("Content-Type"),  // Use existing content type
            [file, buffer, start, end](size_t offset, size_t length, httplib::DataSink& sink) {
                if (!file->good()) return false;
                
                // Calculate how much to read
                const size_t current_pos = start + offset;
                const size_t remaining = end - current_pos + 1;
                const size_t to_read = std::min(buffer->size(), std::min(length, remaining));
                
                if (to_read == 0) return false;

                // Read chunk
                file->read(buffer->data(), to_read);
                const size_t bytes_read = file->gcount();
                
                if (bytes_read == 0) return false;

                // Write chunk
                return sink.write(buffer->data(), bytes_read);
            },
            [file, buffer](bool /*success*/) {
                file->close();
            });
    }
};

class VideoServer {
public:
    VideoServer(int port, const std::string& base_path)
        : handler_(base_path), port_(port) {
        
        server_.Get(".*", [this](const httplib::Request& req, httplib::Response& res) {
            handler_.handle_request(req, res);
        });
    }

    void start() {
        std::cout << "Starting video server on port " << port_ << "\n";
        std::cout << "Press Ctrl+C to stop\n";
        server_.listen("0.0.0.0", port_);
    }

private:
    VideoStreamHandler handler_;
    httplib::Server server_;
    int port_;
};

int main(int argc, char* argv[]) {
    try {
        int port = 8080;
        std::string base_path = "/videos";

        // Parse command line args
        for (int i = 1; i < argc; i += 2) {
            if (std::string(argv[i]) == "--port" && i + 1 < argc) {
                port = std::stoi(argv[i + 1]);
            } else if (std::string(argv[i]) == "--path" && i + 1 < argc) {
                base_path = argv[i + 1];
            }
        }

        VideoServer server(port, base_path);
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
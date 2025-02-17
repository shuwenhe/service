#include <filesystem>
#include <iostream>
#include <httplib.h>

namespace fs = std::filesystem;

class VideoStreamServer {
public:
    VideoStreamServer(int port, const std::string& base_path)
        : port_(port), base_path_(fs::absolute(base_path)) {
        init_server();
    }

    void start() {
        std::cout << "Starting video server on port " << port_ << "\n";
        std::cout << "Serving content from: " << base_path_ << "\n";
        server_.listen("0.0.0.0", port_);
    }

private:
    void init_server() {
        server_.set_error_handler([](const auto& req, auto& res) {
            std::cerr << "Error " << res.status << " for " << req.path << "\n";
        });

        server_.Get(".*", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                const auto full_path = resolve_path(req.path);
                
                if (!fs::exists(full_path)) {
                    res.status = 404;
                    res.set_content("File not found", "text/plain");
                    return;
                }

                if (!fs::is_regular_file(full_path)) {
                    res.status = 400;
                    res.set_content("Invalid request", "text/plain");
                    return;
                }

                const auto file_size = fs::file_size(full_path);
                res.set_header("Content-Type", get_mime_type(full_path));
                res.set_header("Accept-Ranges", "bytes");
                res.set_header("Cache-Control", "no-store");

                res.set_chunked_content_provider(
                    get_mime_type(full_path),
                    [this, full_path, file_size](size_t offset, httplib::DataSink& sink) {
                        std::ifstream file(full_path, std::ios::binary);
                        if (!file) return false;

                        if (offset >= static_cast<size_t>(file_size)) {
                            return false;
                        }

                        file.seekg(offset);
                        const auto remaining = static_cast<size_t>(file_size) - offset;
                        const size_t chunk_size = std::min(remaining, static_cast<size_t>(8192));

                        std::vector<char> buffer(chunk_size);
                        file.read(buffer.data(), chunk_size);
                        
                        if (file.gcount() > 0) {
                            sink.write(buffer.data(), static_cast<size_t>(file.gcount()));
                        }

                        return true;
                    },
                    nullptr // Resource releaser
                );
            } catch (const std::exception& e) {
                res.status = 500;
                res.set_content("Server error", "text/plain");
                std::cerr << "Error: " << e.what() << "\n";
            }
        });
    }

    std::string resolve_path(const std::string& request_path) const {
        // URL decode and sanitize path
        std::string decoded_path = url_decode(request_path);
        
        // Remove query parameters and fragments
        const size_t query_start = decoded_path.find('?');
        if (query_start != std::string::npos) {
            decoded_path = decoded_path.substr(0, query_start);
        }

        // Build filesystem path
        fs::path request_fs_path = decoded_path;
        if (request_fs_path.is_absolute()) {
            request_fs_path = request_fs_path.relative_path();
        }
        
        std::cout << "Resolved path: " << (base_path_ / request_fs_path).lexically_normal() << "\n";
        return (base_path_ / request_fs_path).lexically_normal();
    }

    std::string get_mime_type(const std::string& path) const {
        const std::string ext = fs::path(path).extension().string();
        
        // Common video MIME types
        static const std::unordered_map<std::string, std::string> mime_types = {
            {".mp4", "video/mp4"},
            {".m4v", "video/mp4"},
            {".mov", "video/quicktime"},
            {".mkv", "video/x-matroska"},
            {".webm", "video/webm"}
        };

        return mime_types.count(ext) ? mime_types.at(ext) : "video/mp4";
    }

    bool stream_file_chunk(const std::string& path, size_t offset, 
                         httplib::DataSink& sink) const {
        static std::mutex file_mutex;
        std::lock_guard<std::mutex> lock(file_mutex);
        
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return false;

        const auto file_size = file.tellg();
        if (offset >= static_cast<size_t>(file_size)) {
            return false; // Invalid range
        }

        file.seekg(offset);
        const size_t remaining = file_size - offset;
        const size_t chunk_size = std::min<size_t>(remaining, 8192);

        std::vector<char> buffer(chunk_size);
        file.read(buffer.data(), chunk_size);
        
        return sink.write(buffer.data(), file.gcount());
    }

    std::string url_decode(const std::string& encoded) const {
        std::string decoded;
        for (size_t i = 0; i < encoded.size(); ++i) {
            if (encoded[i] == '%' && i + 2 < encoded.size()) {
                const int hex_val = std::stoi(encoded.substr(i + 1, 2), nullptr, 16);
                decoded += static_cast<char>(hex_val);
                i += 2;
            } else if (encoded[i] == '+') {
                decoded += ' ';
            } else {
                decoded += encoded[i];
            }
        }
        return decoded;
    }

    int port_;
    fs::path base_path_;
    httplib::Server server_;
};

int main(int argc, char* argv[]) {
    try {
        int port = 8080;
        std::string base_path = "/videos";

        // Parse command line arguments
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--port" && i + 1 < argc) {
                port = std::stoi(argv[++i]);
            } else if (arg == "--path" && i + 1 < argc) {
                base_path = argv[++i];
            }
        }

        VideoStreamServer server(port, base_path);
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
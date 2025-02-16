#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <filesystem>
#include <regex>
#include <sstream>
#include <vector>
#include <map>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <arpa/inet.h>
#endif

namespace fs = std::filesystem;

class VideoServer {
private:
    const int PORT;
    const std::string BASE_PATH;
    int server_fd;

    std::string getMimeType(const std::string& path) {
        std::string ext = fs::path(path).extension().string();
        if (ext == ".mp4") return "video/mp4";
        if (ext == ".m4v") return "video/mp4";
        if (ext == ".mov") return "video/quicktime";
        return "video/mp4";
    }

    std::string getFilePath(const std::string& path) {
        // Remove URL parameters if any
        std::string cleanPath = path;
        size_t paramPos = cleanPath.find('?');
        if (paramPos != std::string::npos) {
            cleanPath = cleanPath.substr(0, paramPos);
        }

        // Decode URL-encoded characters
        cleanPath = urlDecode(cleanPath);

        // Remove leading '/' and join with base path
        if (!cleanPath.empty() && cleanPath[0] == '/') {
            cleanPath = cleanPath.substr(1);
        }
        
        return fs::absolute(BASE_PATH + "/" + cleanPath).string();
    }

    std::string urlDecode(const std::string& encoded) {
        std::string decoded;
        for (size_t i = 0; i < encoded.length(); ++i) {
            if (encoded[i] == '%' && i + 2 < encoded.length()) {
                int value;
                std::stringstream ss;
                ss << std::hex << encoded.substr(i + 1, 2);
                ss >> value;
                decoded += static_cast<char>(value);
                i += 2;
            } else if (encoded[i] == '+') {
                decoded += ' ';
            } else {
                decoded += encoded[i];
            }
        }
        return decoded;
    }

    std::map<std::string, std::string> parseHeaders(const std::string& request) {
        std::map<std::string, std::string> headers;
        std::istringstream stream(request);
        std::string line;
        
        // Skip first line (request line)
        std::getline(stream, line);
        
        while (std::getline(stream, line) && line != "\r") {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string value = line.substr(colon + 2, line.length() - colon - 3);
                headers[key] = value;
            }
        }
        return headers;
    }

    void sendHeaders(int client_sock, int status, const std::string& contentType, 
                    size_t start, size_t end, size_t filesize) {
        std::string statusText;
        switch (status) {
            case 200: statusText = "OK"; break;
            case 206: statusText = "Partial Content"; break;
            case 404: statusText = "Not Found"; break;
            case 416: statusText = "Requested Range Not Satisfiable"; break;
            default: statusText = "Internal Server Error";
        }

        std::stringstream headers;
        headers << "HTTP/1.1 " << status << " " << statusText << "\r\n"
                << "Content-Type: " << contentType << "\r\n"
                << "Accept-Ranges: bytes\r\n"
                << "Content-Length: " << (end - start + 1) << "\r\n"
                << "Content-Range: bytes " << start << "-" << end << "/" << filesize << "\r\n"
                << "Connection: keep-alive\r\n"
                << "Cache-Control: no-cache\r\n"
                << "\r\n";

        std::string headerStr = headers.str();
        send(client_sock, headerStr.c_str(), headerStr.length(), 0);
    }

    void handleVideoRequest(int client_sock, const std::string& path, 
                          const std::map<std::string, std::string>& headers) {
        std::string filepath = getFilePath(path);
        
        if (!fs::exists(filepath)) {
            sendHeaders(client_sock, 404, "text/plain", 0, 0, 0);
            return;
        }

        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            sendHeaders(client_sock, 500, "text/plain", 0, 0, 0);
            return;
        }

        // Get file size
        file.seekg(0, std::ios::end);
        size_t filesize = file.tellg();
        file.seekg(0, std::ios::beg);

        size_t start = 0;
        size_t end = filesize - 1;
        int status = 200;

        // Handle range request
        auto rangeIt = headers.find("Range");
        if (rangeIt != headers.end()) {
            std::regex rangeRegex(R"(bytes=(\d*)-(\d*))");
            std::smatch matches;
            if (std::regex_match(rangeIt->second, matches, rangeRegex)) {
                if (!matches[1].str().empty()) {
                    start = std::stoull(matches[1].str());
                }
                if (!matches[2].str().empty()) {
                    end = std::stoull(matches[2].str());
                }

                if (start >= filesize) {
                    sendHeaders(client_sock, 416, "text/plain", 0, 0, filesize);
                    return;
                }

                end = std::min(end, filesize - 1);
                status = 206;
            }
        }

        // Send headers
        sendHeaders(client_sock, status, getMimeType(filepath), start, end, filesize);

        // Send file content
        file.seekg(start);
        std::vector<char> buffer(8192);
        size_t remaining = end - start + 1;

        while (remaining > 0 && file) {
            size_t toRead = std::min(buffer.size(), remaining);
            file.read(buffer.data(), toRead);
            size_t bytesRead = file.gcount();
            if (bytesRead == 0) break;

            send(client_sock, buffer.data(), bytesRead, 0);
            remaining -= bytesRead;
        }

        file.close();
    }

public:
    VideoServer(int port, const std::string& basePath) 
        : PORT(port), BASE_PATH(basePath) {}

    void start() {
        #ifdef _WIN32
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                throw std::runtime_error("WSAStartup failed");
            }
        #endif

        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            throw std::runtime_error("Socket creation failed");
        }

        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, 
            (const char*)&opt, sizeof(opt))) {
            throw std::runtime_error("Setsockopt failed");
        }

        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(PORT);

        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            throw std::runtime_error("Bind failed");
        }

        if (listen(server_fd, 3) < 0) {
            throw std::runtime_error("Listen failed");
        }

        std::cout << "Video server started on port " << PORT << std::endl;
        std::cout << "Serving videos from: " << BASE_PATH << std::endl;

        while (true) {
            struct sockaddr_in client_addr;
            socklen_t addrlen = sizeof(client_addr);
            
            int client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
            if (client_sock < 0) {
                std::cerr << "Accept failed" << std::endl;
                continue;
            }

            // Get client request
            std::vector<char> buffer(4096);
            std::string request;
            
            while (true) {
                int bytesRead = recv(client_sock, buffer.data(), buffer.size(), 0);
                if (bytesRead <= 0) break;
                request.append(buffer.data(), bytesRead);
                if (request.find("\r\n\r\n") != std::string::npos) break;
            }

            if (!request.empty()) {
                // Parse request line
                std::istringstream requestStream(request);
                std::string requestLine;
                std::getline(requestStream, requestLine);
                
                std::istringstream requestLineStream(requestLine);
                std::string method, path, protocol;
                requestLineStream >> method >> path >> protocol;

                // Handle GET request
                if (method == "GET") {
                    auto headers = parseHeaders(request);
                    handleVideoRequest(client_sock, path, headers);
                }
            }

            #ifdef _WIN32
                closesocket(client_sock);
            #else
                close(client_sock);
            #endif
        }
    }

    ~VideoServer() {
        #ifdef _WIN32
            closesocket(server_fd);
            WSACleanup();
        #else
            close(server_fd);
        #endif
    }
};

int main(int argc, char* argv[]) {
    try {
        int port = 8080;
        std::string basePath = "/videos";

        // Parse command line arguments
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--port" && i + 1 < argc) {
                port = std::stoi(argv[++i]);
            } else if (arg == "--path" && i + 1 < argc) {
                basePath = argv[++i];
            }
        }

        VideoServer server(port, basePath);
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
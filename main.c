#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <regex.h>
#include <getopt.h>
#include <libgen.h>
#include <errno.h>

#define BUFFER_SIZE 8192
#define MAX_HEADER_SIZE 4096
#define MAX_PATH_SIZE 1024

typedef struct {
    char* base_path;
} VideoServer;

// Helper functions
void log_request(const char* method, const char* path, const char* headers) {
    printf("\n%.*s\n", 50, "==================================================");
    printf("📥 REQUEST\n");
    printf("%.*s\n", 50, "==================================================");
    printf("Method: %s\n", method);
    printf("Path: %s\n\n", path);
    printf("Headers:\n%s", headers);
    printf("%.*s\n\n", 50, "==================================================");
}

void log_response(int status, const char* headers, size_t body_size) {
    printf("\n%.*s\n", 50, "==================================================");
    printf("📤 RESPONSE\n");
    printf("%.*s\n", 50, "==================================================");
    printf("Status: %d\n\n", status);
    printf("Headers:\n%s", headers);
    printf("Body size: %zu bytes\n", body_size);
    printf("%.*s\n\n", 50, "==================================================");
}

char* get_mime_type(const char* path) {
    char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    
    if (strcmp(ext, ".mp4") == 0) return "video/mp4";
    if (strcmp(ext, ".webm") == 0) return "video/webm";
    if (strcmp(ext, ".mkv") == 0) return "video/x-matroska";
    if (strcmp(ext, ".mov") == 0) return "video/quicktime";
    if (strcmp(ext, ".avi") == 0) return "video/x-msvideo";
    
    return "application/octet-stream";
}

void handle_range_request(int client_sock, const char* filepath, const char* range_header, 
                         off_t filesize, const char* content_type) {
    regex_t regex;
    regmatch_t matches[3];
    char pattern[] = "bytes=([0-9]*)-([0-9]*)";
    
    if (regcomp(&regex, pattern, REG_EXTENDED) != 0) {
        // Send 400 Bad Request
        char response[MAX_HEADER_SIZE];
        snprintf(response, sizeof(response),
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 22\r\n\r\n"
                "Invalid Range header\r\n");
        send(client_sock, response, strlen(response), 0);
        return;
    }

    if (regexec(&regex, range_header, 3, matches, 0) != 0) {
        // Send 400 Bad Request
        char response[MAX_HEADER_SIZE];
        snprintf(response, sizeof(response),
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 22\r\n\r\n"
                "Invalid Range header\r\n");
        send(client_sock, response, strlen(response), 0);
        regfree(&regex);
        return;
    }

    // Parse range values
    char range_str[256];
    strncpy(range_str, range_header + matches[1].rm_so, 
            matches[1].rm_eo - matches[1].rm_so);
    range_str[matches[1].rm_eo - matches[1].rm_so] = '\0';

    off_t start = 0;
    off_t end = filesize - 1;

    if (strlen(range_str) > 0) {
        start = atoll(range_str);
    }

    if (matches[2].rm_so != matches[2].rm_eo) {
        char end_str[256];
        strncpy(end_str, range_header + matches[2].rm_so,
                matches[2].rm_eo - matches[2].rm_so);
        end_str[matches[2].rm_eo - matches[2].rm_so] = '\0';
        end = atoll(end_str);
    }

    regfree(&regex);

    if (start > end || start >= filesize) {
        // Send 416 Range Not Satisfiable
        char headers[MAX_HEADER_SIZE];
        snprintf(headers, sizeof(headers),
            "HTTP/1.1 206 Partial Content\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %lld\r\n"
            "Content-Range: bytes %lld-%lld/%lld\r\n"
            "Accept-Ranges: bytes\r\n"
            "\r\n",
            content_type, length, start, end, filesize);
        send(client_sock, headers, strlen(headers), 0);
        return;
    }

    off_t length = end - start + 1;

    // Send 206 Partial Content
    char response[MAX_HEADER_SIZE];
    snprintf(response, sizeof(response),
            "HTTP/1.1 206 Partial Content\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %lld\r\n"
            "Content-Range: bytes %lld-%lld/%lld\r\n"
            "Accept-Ranges: bytes\r\n\r\n",
            content_type,
            (long long)length,
            (long long)start,
            (long long)end,
            (long long)filesize);

    send(client_sock, response, strlen(response), 0);

    // Stream file content
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        perror("Failed to open file");
        return;
    }

    fseeko(file, start, SEEK_SET);
    char buffer[BUFFER_SIZE];
    size_t bytes_sent = 0;

    while (bytes_sent < length) {
        size_t to_read = BUFFER_SIZE;
        if (bytes_sent + to_read > length) {
            to_read = length - bytes_sent;
        }

        size_t bytes_read = fread(buffer, 1, to_read, file);
        if (bytes_read == 0) break;

        send(client_sock, buffer, bytes_read, 0);
        bytes_sent += bytes_read;
    }

    fclose(file);

    // Log response
    char headers[MAX_HEADER_SIZE];
    snprintf(headers, sizeof(headers),
            "  Content-Type: %s\n"
            "  Content-Length: %lld\n"
            "  Content-Range: bytes %lld-%lld/%lld\n"
            "  Accept-Ranges: bytes\n",
            content_type,
            (long long)length,
            (long long)start,
            (long long)end,
            (long long)filesize);
    
    log_response(206, headers, bytes_sent);
}

void serve_full_file(int client_sock, const char* filepath, off_t filesize, 
                    const char* content_type) {
    char response[MAX_HEADER_SIZE];
    snprintf(response, sizeof(response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %lld\r\n"
            "Accept-Ranges: bytes\r\n\r\n",
            content_type,
            (long long)filesize);

    send(client_sock, response, strlen(response), 0);

    FILE* file = fopen(filepath, "rb");
    if (!file) {
        perror("Failed to open file");
        return;
    }

    char buffer[BUFFER_SIZE];
    size_t bytes_sent = 0;

    while (1) {
        size_t bytes_read = fread(buffer, 1, BUFFER_SIZE, file);
        if (bytes_read == 0) break;

        send(client_sock, buffer, bytes_read, 0);
        bytes_sent += bytes_read;
    }

    fclose(file);

    // Log response
    char headers[MAX_HEADER_SIZE];
    snprintf(headers, sizeof(headers),
            "  Content-Type: %s\n"
            "  Content-Length: %lld\n"
            "  Accept-Ranges: bytes\n",
            content_type,
            (long long)filesize);
    
    log_response(200, headers, bytes_sent);
}

void handle_client(int client_sock, VideoServer* server) {
    char buffer[MAX_HEADER_SIZE];
    ssize_t bytes_received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_received <= 0) {
        close(client_sock);
        return;
    }
    
    buffer[bytes_received] = '\0';

    // Parse request
    char method[32], path[MAX_PATH_SIZE], version[32];
    sscanf(buffer, "%s %s %s", method, path, version);

    // Log request
    log_request(method, path, buffer);

    if (strcmp(method, "OPTIONS") == 0) {
        char response[] = "HTTP/1.1 204 No Content\r\n"
                         "Allow: GET, HEAD, OPTIONS\r\n\r\n";
        send(client_sock, response, strlen(response), 0);
        close(client_sock);
        return;
    }

    // Translate path
    char filepath[MAX_PATH_SIZE];
    snprintf(filepath, sizeof(filepath), "%s/%s", 
             server->base_path, path[0] == '/' ? path + 1 : path);

    printf("filepath: %s\n", filepath);

    // Check if file exists
    struct stat st;
    if (stat(filepath, &st) != 0) {
        char response[] = "HTTP/1.1 404 Not Found\r\n"
                         "Content-Type: text/plain\r\n"
                         "Content-Length: 13\r\n\r\n"
                         "File not found";
        send(client_sock, response, strlen(response), 0);
        close(client_sock);
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        char response[] = "HTTP/1.1 404 Not Found\r\n"
                         "Content-Type: text/plain\r\n"
                         "Content-Length: 25\r\n\r\n"
                         "Directories not supported";
        send(client_sock, response, strlen(response), 0);
        close(client_sock);
        return;
    }

    const char* content_type = get_mime_type(filepath);
    char* range_header = strstr(buffer, "Range: ");

    if (range_header) {
        char* range_end = strchr(range_header, '\r');
        if (range_end) *range_end = '\0';
        handle_range_request(client_sock, filepath, range_header + 7, st.st_size, content_type);
    } else {
        serve_full_file(client_sock, filepath, st.st_size, content_type);
    }

    close(client_sock);
}

int main(int argc, char* argv[]) {
    int port = 8080;
    char* base_path = "/videos";

    // Parse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "p:d:")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            case 'd':
                base_path = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s [-p port] [-d base_path]\n", argv[0]);
                exit(1);
        }
    }

    VideoServer server = {
        .base_path = base_path
    };

    // Create socket
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Failed to create socket");
        exit(1);
    }

    // Enable address reuse
    int opt_val = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val));

    // Bind
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port)
    };

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to bind");
        exit(1);
    }

    // Listen
    if (listen(server_sock, 10) < 0) {
        perror("Failed to listen");
        exit(1);
    }

    char abs_path[MAX_PATH_SIZE];
    realpath(base_path, abs_path);
    printf("Serving videos from %s on port %d\n", abs_path, port);
    printf("Access videos at http://localhost:%d/\n", port);

    // Accept connections
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_sock < 0) {
            perror("Failed to accept");
            continue;
        }

        handle_client(client_sock, &server);
    }

    close(server_sock);
    return 0;
}
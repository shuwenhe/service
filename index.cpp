#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

const int PORT = 8080;

// Define a map to store the web pages.
std::map<std::string, std::string> web_pages;

// Function to load HTML pages from files.
void loadWebPages() {
    web_pages["/"] = "index.html";
    web_pages["/about"] = "about.html";
    // Add more pages here as needed.
}

// Function to serve a web page.
void servePage(int client_socket, const std::string& page_path) {
    std::ifstream page_file(page_path);
    if (!page_file) {
        std::cerr << "Error: Unable to open " << page_path << std::endl;
        return;
    }

    std::stringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: text/html\r\n\r\n";

    std::string line;
    while (std::getline(page_file, line)) {
        response << line;
    }

    send(client_socket, response.str().c_str(), response.str().size(), 0);
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Create a socket.
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        std::cerr << "Error: Unable to create socket." << std::endl;
        return 1;
    }

    // Configure the server address structure.
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind the server socket.
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Error: Unable to bind." << std::endl;
        return 1;
    }

    // Listen for incoming connections.
    if (listen(server_socket, 5) < 0) {
        std::cerr << "Error: Unable to listen." << std::endl;
        return 1;
    }

    loadWebPages(); // Load web pages into the map.

    std::cout << "Server is running on port " << PORT << std::endl;

    while (true) {
        // Accept a client connection.
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            std::cerr << "Error: Unable to accept connection." << std::endl;
            return 1;
        }

        char buffer[1024];
        recv(client_socket, buffer, sizeof(buffer), 0);

        std::istringstream request(buffer);
        std::string request_line;
        getline(request, request_line);

        std::istringstream iss(request_line);
        std::string method, page;
        iss >> method >> page;

        if (method == "GET" && web_pages.find(page) != web_pages.end()) {
            // Serve the requested page.
            servePage(client_socket, web_pages[page]);
        } else {
            // Handle 404 Not Found.
            std::string not_found_response = "HTTP/1.1 404 Not Found\r\n\r\n";
            send(client_socket, not_found_response.c_str(), not_found_response.size(), 0);
        }

        close(client_socket);
    }

    close(server_socket);
    return 0;
}
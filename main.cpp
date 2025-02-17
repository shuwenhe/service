#include <iostream>
#include <fstream>
#include <string>
#include <regex>
#include <sstream>
#include <sys/stat.h> // For stat
#include <unistd.h>   // For access

#include "oatpp/web/server/HttpServer.hpp"
#include "oatpp/network/tcp/server/TcpServer.hpp"
#include "oatpp/web/protocol/http/v1/Request.hpp"
#include "oatpp/web/protocol/http/v1/Response.hpp"
#include "oatpp/core/macro/component.hpp"
#include "oatpp/core/base/Environment.hpp"
#include "oatpp/core/base/CommandLineArguments.hpp"

OATPP_COMPONENT(LoggerComponent)(oatpp::日志::Logger);

class VideoServer : public oatpp::web::server::HttpRequestHandler {
private:
  std::string m_basePath;

public:
  VideoServer(const std::string& basePath) : m_basePath(basePath) {}

  std::shared_ptr<oatpp::web::protocol::http::v1::Response> handle(const std::shared_ptr<oatpp::web::protocol::http::v1::Request>& request) override {
    auto logger = OATPP_COMPONENT(LoggerComponent)::getLogger();

    logger->info("==================================================");
    logger->info("📥 REQUEST");
    logger->info("==================================================");
    logger->info("Method: {}", request->getMethod());
    logger->info("Path: {}", request->getPath());
    logger->info("");
    logger->info("Headers:");
    for (const auto& header : request->getHeaders()) {
      logger->info("  {}: {}", header.first, header.second);
    }
    logger->info("==================================================\n");

    if (request->getMethod() == "OPTIONS") {
      auto response = oatpp::web::protocol::http::v1::Response::createShared(oatpp::web::protocol::http::v1::Status::CODE_204_NO_CONTENT);
      response->putHeader("Allow", "GET, HEAD, OPTIONS");
      return response;
    }

    std::string filePath = translatePath(request->getPath());
    logger->info("filepath: {}", filePath);

    struct stat fileInfo;
    if (stat(filePath.c_str(), &fileInfo) != 0) {
      return oatpp::web::protocol::http::v1::Response::createShared(oatpp::web::protocol::http::v1::Status::CODE_404_NOT_FOUND, "File not found");
    }

    if (S_ISDIR(fileInfo.st_mode)) {
      return oatpp::web::protocol::http::v1::Response::createShared(oatpp::web::protocol::http::v1::Status::CODE_404_NOT_FOUND, "Directories not supported");
    }

    std::string contentType = getContentType(filePath);
    long long fileSize = fileInfo.st_size;

    auto rangeHeader = request->getHeader("Range");
    if (rangeHeader.has_value()) {
      return handleRangeRequest(request, filePath, rangeHeader.value(), fileSize, contentType);
    } else {
      return serveFullFile(filePath, fileSize, contentType);
    }
  }


  std::shared_ptr<oatpp::web::protocol::http::v1::Response> handleRangeRequest(const std::shared_ptr<oatpp::web::protocol::http::v1::Request>& request, const std::string& filePath, const std::string& rangeHeader, long long fileSize, const std::string& contentType) {
    auto logger = OATPP_COMPONENT(LoggerComponent)::getLogger();

    std::regex re(R"(bytes=(\d*)-(\d*))");
    std::smatch matches;
    if (!std::regex_search(rangeHeader, matches, re)) {
      return oatpp::web::protocol::http::v1::Response::createShared(oatpp::web::protocol::http::v1::Status::CODE_400_BAD_REQUEST, "Invalid Range header");
    }

    long long start = 0;
    if (!matches[1].str().empty()) {
      start = std::stoll(matches[1].str());
    }

    long long end = fileSize - 1;
    if (!matches[2].str().empty()) {
      end = std::stoll(matches[2].str());
    }

    if (start > end || start >= fileSize) {
      auto response = oatpp::web::protocol::http::v1::Response::createShared(oatpp::web::protocol::http::v1::Status::CODE_416_REQUESTED_RANGE_NOT_SATISFIABLE, "Requested range not satisfiable");
      response->putHeader("Content-Range", "bytes */" + std::to_string(fileSize));
      return response;
    }

    long long length = end - start + 1;

    auto response = oatpp::web::protocol::http::v1::Response::createShared(oatpp::web::protocol::http::v1::Status::CODE_206_PARTIAL_CONTENT);
    response->putHeader("Content-Type", contentType);
    response->putHeader("Content-Length", std::to_string(length));
    response->putHeader("Content-Range", "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(fileSize));
    response->putHeader("Accept-Ranges", "bytes");

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
      return oatpp::web::protocol::http::v1::Response::createShared(oatpp::web::protocol::http::v1::Status::CODE_500_INTERNAL_SERVER_ERROR, "Failed to open file");
    }
    file.seekg(start);

    std::vector<char> buffer(8192);
    long long bytesSent = 0;

    while (bytesSent < length) {
      long long toRead = std::min((long long)buffer.size(), length - bytesSent);
      file.read(buffer.data(), toRead);
      auto bytesRead = file.gcount();
      if (bytesRead <= 0) break;

      response->writeBody(buffer.data(), bytesRead);
      bytesSent += bytesRead;
    }
    file.close();

    logger->info("==================================================");
    logger->info("📤 RESPONSE");
    logger->info("==================================================");
    logger->info("Status: {}", response->getStatusCode());
    logger->info("");
    logger->info("Headers:");
    for (const auto& header : response->getHeaders()) {
      logger->info("  {}: {}", header.first, header.second);
    }
    logger->info("Body size: {} bytes\n", bytesSent);
    logger->info("==================================================\n");

    return response;
  }

  std::shared_ptr<oatpp::web::protocol::http::v1::Response> serveFullFile(const std::string& filePath, long long fileSize, const std::string& contentType) {
    auto logger = OATPP_COMPONENT(LoggerComponent)::getLogger();

    auto response = oatpp::web::protocol::http::v1::Response::createShared(oatpp::web::protocol::http::v1::Status::CODE_200_OK);
    response->putHeader("Content-Type", contentType);
    response->putHeader("Content-Length", std::to_string(fileSize));
    response->putHeader("Accept-Ranges", "bytes");

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
      return oatpp::web::protocol::http::v1::Response::createShared(oatpp::web::protocol::http::v1::Status::CODE_500_INTERNAL_SERVER_ERROR, "Failed to open file");
    }

    std::vector<char> buffer(8192);
    long long bytesSent = 0;

    while (file.good()) {
      file.read(buffer.data(), buffer.size());
      auto bytesRead = file.gcount();

      response->writeBody(buffer.data(), bytesRead);
      bytesSent += bytesRead;
    }

    file.close();

    logger->info("==================================================");
    logger->info("📤 RESPONSE");
    logger->info("==================================================");
    logger->info("Status: {}", response
// Minimal HTTPS GET client that keeps its connection open between requests.
#pragma once

#include <cstddef>
#include <string>

#include "esp_err.h"
#include "esp_http_client.h"

struct HttpResponse {
  esp_err_t error = ESP_FAIL;  // transport-level result; ESP_OK means a response was received
  int status = 0;              // HTTP status of the final response (after redirects)
  std::string body;
};

class HttpClient {
 public:
  HttpClient(int timeoutMs, std::size_t maxBodyBytes);
  ~HttpClient();
  HttpClient(const HttpClient&) = delete;
  HttpClient& operator=(const HttpClient&) = delete;

  // Follows redirects. A body larger than maxBodyBytes fails with ESP_ERR_NO_MEM rather than
  // being truncated, so callers never parse half a document.
  HttpResponse get(const char* url);

 private:
  static esp_err_t onEvent(esp_http_client_event_t* event);
  void close();

  const int timeoutMs_;
  const std::size_t maxBodyBytes_;
  esp_http_client_handle_t handle_ = nullptr;
  std::string body_;
  bool overflow_ = false;
};

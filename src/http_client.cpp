#include "http_client.h"

#include <utility>

#include "esp_crt_bundle.h"
#include "esp_log.h"

namespace {

const char* TAG = "http";

// The Apps Script endpoint answers with a redirect whose Location header is ~500 bytes,
// and that URL is then sent back as the next request line; the 512-byte defaults overflow.
constexpr int kHeaderBufferBytes = 4096;

bool isRedirect(int status) { return status >= 300 && status < 400; }

}  // namespace

HttpClient::HttpClient(int timeoutMs, std::size_t maxBodyBytes)
    : timeoutMs_(timeoutMs), maxBodyBytes_(maxBodyBytes) {}

HttpClient::~HttpClient() { close(); }

void HttpClient::close() {
  if (handle_ != nullptr) {
    esp_http_client_cleanup(handle_);
    handle_ = nullptr;
  }
}

esp_err_t HttpClient::onEvent(esp_http_client_event_t* event) {
  auto* self = static_cast<HttpClient*>(event->user_data);
  if (event->event_id != HTTP_EVENT_ON_DATA) {
    return ESP_OK;
  }
  // Redirect responses carry an HTML body of their own that must not end up in the result.
  if (isRedirect(esp_http_client_get_status_code(event->client))) {
    return ESP_OK;
  }
  if (self->body_.size() + event->data_len > self->maxBodyBytes_) {
    self->overflow_ = true;
    return ESP_OK;
  }
  self->body_.append(static_cast<const char*>(event->data), event->data_len);
  return ESP_OK;
}

HttpResponse HttpClient::get(const char* url) {
  HttpResponse response;
  body_.clear();
  overflow_ = false;

  if (handle_ == nullptr) {
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = timeoutMs_;
    config.event_handler = &HttpClient::onEvent;
    config.user_data = this;
    config.buffer_size = kHeaderBufferBytes;
    config.buffer_size_tx = kHeaderBufferBytes;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.keep_alive_enable = true;
    handle_ = esp_http_client_init(&config);
    if (handle_ == nullptr) {
      ESP_LOGE(TAG, "esp_http_client_init failed");
      response.error = ESP_ERR_NO_MEM;
      return response;
    }
  } else {
    esp_http_client_set_url(handle_, url);
  }

  response.error = esp_http_client_perform(handle_);
  if (response.error != ESP_OK) {
    ESP_LOGW(TAG, "GET failed: %s", esp_err_to_name(response.error));
    // The connection state is unknown after a failure; start clean next time.
    close();
    return response;
  }

  response.status = esp_http_client_get_status_code(handle_);
  if (overflow_) {
    ESP_LOGW(TAG, "response body exceeds %u bytes", static_cast<unsigned>(maxBodyBytes_));
    response.error = ESP_ERR_NO_MEM;
    return response;
  }
  response.body = std::move(body_);
  body_.clear();
  return response;
}

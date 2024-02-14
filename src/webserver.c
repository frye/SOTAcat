#include "esp_log.h"
#include "globals.h"
#include "webserver.h"


asset_entry_t asset_map[] = {
    {"/", index_html_srt, index_html_end, "text/html", 60}, // 1 minute cache
    {"/index.html", index_html_srt, index_html_end, "text/html", 60},
    { "/style.css", style_css_srt, style_css_end, "text/css", 60 },
    { "/script.js",  script_js_srt,  script_js_end, "text/javascript", 60 },
    {"/sclogo.png", sclogo_png_srt, sclogo_png_end, "image/png", 0}, // Cache forever
    {"/favicon.ico", favicon_ico_srt, favicon_ico_end, "image/x-icon", 0},
    {NULL, NULL, NULL, NULL, 0}};


// Arrays for GET, PUT, POST handlers
const api_handler_t get_handlers[] = {
    {"batteryPercent", handler_batteryPercent_get},
    {"batteryVoltage", handler_batteryVoltage_get},
    {"frequency", handler_frequency_get},
    {"mode", handler_mode_get},
    {"rxBandwidth", handler_rxBandwidth_get},
    {NULL, NULL} // Sentinel to mark end of array
};

const api_handler_t put_handlers[] = {
    {"frequency", handler_frequency_put},
    {"mode", handler_mode_put},
    {"rxBandwidth", handler_rxBandwidth_put},
    {NULL, NULL} // Sentinel
};

const api_handler_t post_handlers[] = {
    {"prepareft8", handler_prepareft8_post},
    {"ft8", handler_ft8_post},
    {"cancelft8", handler_cancelft8_post},
    {NULL, NULL} // Sentinel
};

long last_known_frequency = 0;

int find_and_execute_handler(const char *api_name, const api_handler_t *handlers, httpd_req_t *req)
{
    // Ignore any query string if there is one:
    size_t compare_length = strcspn(api_name, "?");

    for (int i = 0; handlers[i].api_name != NULL; i++)
    {
        if (strncmp(api_name, handlers[i].api_name, compare_length) == 0)
        {
            return handlers[i].handler_func(req);
        }
    }
    ESP_LOGI(TAG, "find_and_execute_handler(): Handler not found for API: %s", api_name);
    httpd_resp_send_404(req);
    return ESP_FAIL; // Handler not found
}


esp_err_t dynamic_file_handler(httpd_req_t *req)
{
    const char *requested_path = req->uri;

    bool found_file = false;
    int ii = 0;

    for (ii = 0; asset_map[ii].uri != NULL; ii++)
    {
        if (strcmp(requested_path, asset_map[ii].uri) == 0)
        {
            found_file = true;
            break;
        }
    }

    if (!found_file)
        return ESP_FAIL;

    httpd_resp_set_type(req, asset_map[ii].asset_type);

    char cache_header[64];
    if (asset_map[ii].cache_time > 0)
    {
        snprintf(cache_header, sizeof(cache_header), "max-age=%ld", asset_map[ii].cache_time);
    }
    else // cache forever
    {
        snprintf(cache_header, sizeof(cache_header), "max-age=31536000"); // 1 year
    }
    httpd_resp_set_hdr(req, "Cache-Control", cache_header);

    httpd_resp_send(
        req, 
        (const char *)asset_map[ii].asset_start, 
        asset_map[ii].asset_end - asset_map[ii].asset_start - 1); // -1 to exclude the NULL terminator
        
    return ESP_OK;
}



esp_err_t my_http_request_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Call to my_http_request_handler() with URI: %s", req->uri);
    const char *requested_uri = req->uri;

    // 1. Check for REST API calls
    if (starts_with(requested_uri, "/api/v1/"))
    {
        const char *api_name = requested_uri + sizeof("/api/v1/") - 1; // Correct the offset

        if (req->method == HTTP_GET)
            return find_and_execute_handler(api_name, get_handlers, req);
        if (req->method == HTTP_PUT)
            return find_and_execute_handler(api_name, put_handlers, req);
        if (req->method == HTTP_POST)
            return find_and_execute_handler(api_name, post_handlers, req);

        return ESP_FAIL; // Method not supported
    }

    // 2. Check for Web Page Assets
    if (starts_with(requested_uri, "/"))
        return dynamic_file_handler(req);

    // 3. Default / Not Found - should not be possible to reach this code.  Not found errors would happen in the dynamic_file_handler in step 2.
    return ESP_FAIL;
}

// httpd_uri_match_func_t custom_uri_matcher(httpd_req_t *r)
// bool custom_uri_matcher(httpd_req_t *r)
bool custom_uri_matcher(const char *uri1, const char *uri2, unsigned int uri_len)
{
    // Since we want a catch-all, we always match:
    return true;
}

void start_webserver()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    config.max_uri_handlers = 6;
    config.uri_match_fn = custom_uri_matcher; // Configure to use your matcher

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t uri_api = {
            .uri = "/", // Not used: we match all URIs based on the custom_uri_matcher
            .method = HTTP_GET,
            .handler = my_http_request_handler // The universal routing handler
        };
        httpd_register_uri_handler(server, &uri_api);
        uri_api.method = HTTP_PUT;
        httpd_register_uri_handler(server, &uri_api);
        uri_api.method = HTTP_POST;
        httpd_register_uri_handler(server, &uri_api);

        ESP_LOGI(TAG, "Completed defining Webserver callbacks");
    }
}

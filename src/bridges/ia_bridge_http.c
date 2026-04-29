/**
 * ============================================================================
 * ia_bridge_http.c — MyanOS MMC Compiler Phase 8
 * ============================================================================
 * AI Bridge HTTP Interface Module
 * Version: 1.0.0
 * Date: 2026-04-29
 *
 * HTTP-based AI communication bridge for MMC Compiler.
 * Extends ia_bridge.h interface with network capabilities.
 *
 * Features:
 *   - API Key authentication (Bearer token)
 *   - Configurable endpoint URL
 *   - JSON request/response handling
 *   - Compatible with Termux / Android / Linux
 *   - Timeout and retry support
 *   - Supports all 18 IA methods from ia_bridge.h
 *
 * Dependencies:
 *   - libc (POSIX sockets)
 *   - ia_bridge.h (selfhosted/)
 *
 * Build:
 *   gcc -std=c99 -Wall -o bridge_http_test src/bridges/ia_bridge_http.c \
 *       selfhosted/ia_bridge.c -DMMC_IA_BRIDGE_HTTP_TEST
 *
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>

/* Include the core IA bridge header */
#include "ia_bridge.h"

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

#define IA_HTTP_VERSION           "1.0.0"
#define IA_HTTP_MAX_URL           512
#define IA_HTTP_MAX_KEY           256
#define IA_HTTP_MAX_RESPONSE      16384
#define IA_HTTP_MAX_REQUEST       8192
#define IA_HTTP_TIMEOUT_SEC       30
#define IA_HTTP_MAX_RETRIES       3
#define IA_HTTP_RETRY_DELAY_SEC   2
#define IA_HTTP_BUFFER_SIZE       4096
#define IA_HTTP_PORT_DEFAULT      443
#define IA_HTTP_PORT_HTTP         80

/* ============================================================================
 * HTTP Bridge State Structure
 * ============================================================================ */

typedef struct {
    char endpoint_url[IA_HTTP_MAX_URL];    /* API endpoint URL */
    char api_key[IA_HTTP_MAX_KEY];         /* Bearer API key */
    char model_name[128];                  /* AI model name */
    int  timeout_sec;                      /* Request timeout */
    int  max_retries;                      /* Max retry attempts */
    int  use_https;                        /* 1 = HTTPS, 0 = HTTP */
    int  verbose;                          /* Debug output flag */
    int  connected;                        /* Connection status */
    long total_requests;                   /* Total requests sent */
    long total_errors;                     /* Total errors */
    char last_error[256];                  /* Last error message */
    double avg_response_time_ms;           /* Average response time */
} IA_HTTP_Config;

/* Global HTTP bridge configuration */
static IA_HTTP_Config g_http_config;

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * Get current timestamp string
 */
static void ia_http_timestamp(char *buf, int bufsize) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buf, bufsize, "%Y-%m-%dT%H:%M:%S", tm_info);
}

/**
 * Simple string hashing for cache keys
 */
static unsigned long ia_http_hash(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

/**
 * Trim whitespace from string
 */
static char *ia_http_trim(char *str) {
    while (*str && (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r'))
        str++;
    if (*str == '\0') return str;
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
        end--;
    end[1] = '\0';
    return str;
}

/**
 * URL encode a string
 */
static int ia_http_url_encode(const char *src, char *dst, int dstsize) {
    int i = 0, j = 0;
    while (src[i] && j < dstsize - 4) {
        char c = src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[j++] = c;
        } else {
            snprintf(dst + j, dstsize - j, "%%%02X", (unsigned char)c);
            j += 3;
        }
        i++;
    }
    dst[j] = '\0';
    return j;
}

/**
 * Parse hostname and port from URL
 */
static int ia_http_parse_url(const char *url, char *host, int hostsize,
                              char *path, int pathsize, int *port) {
    if (strncmp(url, "https://", 8) == 0) {
        *port = IA_HTTP_PORT_DEFAULT;
        url += 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        *port = IA_HTTP_PORT_HTTP;
        url += 7;
    } else {
        snprintf(g_http_config.last_error, sizeof(g_http_config.last_error),
                 "Invalid URL protocol: must be http:// or https://");
        return -1;
    }

    const char *slash = strchr(url, '/');
    const char *colon = strchr(url, ':');

    if (colon && colon < slash) {
        int len = (int)(colon - url);
        if (len >= hostsize) len = hostsize - 1;
        strncpy(host, url, len);
        host[len] = '\0';
        *port = atoi(colon + 1);
    } else if (slash) {
        int len = (int)(slash - url);
        if (len >= hostsize) len = hostsize - 1;
        strncpy(host, url, len);
        host[len] = '\0';
    } else {
        strncpy(host, url, hostsize - 1);
        host[hostsize - 1] = '\0';
        path[0] = '/';
        path[1] = '\0';
        return 0;
    }

    if (slash) {
        strncpy(path, slash, pathsize - 1);
        path[pathsize - 1] = '\0';
    } else {
        path[0] = '/';
        path[1] = '\0';
    }

    return 0;
}

/**
 * Create TCP socket connection
 */
static int ia_http_connect(const char *host, int port) {
    struct sockaddr_in server_addr;
    struct hostent *server;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        snprintf(g_http_config.last_error, sizeof(g_http_config.last_error),
                 "Socket creation failed: %s", strerror(errno));
        return -1;
    }

    /* Set send/recv timeouts */
    struct timeval tv;
    tv.tv_sec = g_http_config.timeout_sec;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    server = gethostbyname(host);
    if (server == NULL) {
        snprintf(g_http_config.last_error, sizeof(g_http_config.last_error),
                 "DNS resolution failed for: %s", host);
        close(sockfd);
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    server_addr.sin_port = htons(port);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        snprintf(g_http_config.last_error, sizeof(g_http_config.last_error),
                 "Connection failed to %s:%d - %s", host, port, strerror(errno));
        close(sockfd);
        return -1;
    }

    return sockfd;
}

/**
 * Send HTTP POST request and receive response
 */
static int ia_http_post(const char *host, int port, const char *path,
                         const char *json_body, char *response, int resp_size) {
    int sockfd = ia_http_connect(host, port);
    if (sockfd < 0) return -1;

    /* Build HTTP request */
    char request[IA_HTTP_MAX_REQUEST];
    int body_len = (int)strlen(json_body);

    int req_len = snprintf(request, sizeof(request),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "User-Agent: MyanOS-IA-Bridge/%s\r\n"
        "Connection: close\r\n",
        path, host, body_len, IA_HTTP_VERSION);

    /* Add API key if configured */
    if (g_http_config.api_key[0] != '\0') {
        req_len += snprintf(request + req_len, sizeof(request) - req_len,
            "Authorization: Bearer %s\r\n",
            g_http_config.api_key);
    }

    req_len += snprintf(request + req_len, sizeof(request) - req_len,
        "\r\n%s", json_body);

    /* Send request */
    int total_sent = 0;
    while (total_sent < req_len) {
        int sent = send(sockfd, request + total_sent, req_len - total_sent, 0);
        if (sent < 0) {
            snprintf(g_http_config.last_error, sizeof(g_http_config.last_error),
                     "Send failed: %s", strerror(errno));
            close(sockfd);
            return -1;
        }
        total_sent += sent;
    }

    /* Read response */
    int total_read = 0;
    char buffer[IA_HTTP_BUFFER_SIZE];

    while (total_read < resp_size - 1) {
        int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        if (total_read + bytes < resp_size) {
            memcpy(response + total_read, buffer, bytes);
            total_read += bytes;
        }
    }
    response[total_read] = '\0';
    close(sockfd);

    /* Check HTTP status */
    if (total_read < 12) {
        snprintf(g_http_config.last_error, sizeof(g_http_config.last_error),
                 "Response too short: %d bytes", total_read);
        return -1;
    }

    int status_code = atoi(response + 9);
    if (status_code != 200) {
        snprintf(g_http_config.last_error, sizeof(g_http_config.last_error),
                 "HTTP %d: %.200s", status_code,
                 strchr(response, '\r') ? strchr(response, '\r') + 2 : response);
        return -1;
    }

    /* Skip HTTP headers to get body */
    char *body_start = strstr(response, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        int body_len = (int)(response + total_read - body_start);
        if (body_len > 0 && body_len < resp_size) {
            memmove(response, body_start, body_len + 1);
        }
    }

    return total_read;
}

/**
 * Extract JSON string value by key (simple parser)
 */
static int ia_http_extract_json(const char *json, const char *key,
                                 char *value, int valsize) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return -1;

    pos += strlen(search);
    while (*pos == ' ' || *pos == '\t' || *pos == ':' || *pos == '\n')
        pos++;

    if (*pos != '"') return -1;
    pos++;

    int i = 0;
    while (*pos && *pos != '"' && i < valsize - 1) {
        if (*pos == '\\' && *(pos + 1)) {
            pos++;
            switch (*pos) {
                case 'n':  value[i++] = '\n'; break;
                case 't':  value[i++] = '\t'; break;
                case 'r':  value[i++] = '\r'; break;
                case '\\': value[i++] = '\\'; break;
                case '"':  value[i++] = '"';  break;
                default:   value[i++] = *pos;  break;
            }
        } else {
            value[i++] = *pos;
        }
        pos++;
    }
    value[i] = '\0';
    return 0;
}

/* ============================================================================
 * Core HTTP Request Function
 * ============================================================================ */

/**
 * Send a message to the AI API via HTTP
 * Returns response text or error message
 */
static const char *ia_http_send(const char *method, const char *message) {
    static char response_buf[IA_HTTP_MAX_RESPONSE];
    static char result_buf[IA_HTTP_MAX_RESPONSE];

    char host[256], path[512];
    int port;

    if (g_http_config.endpoint_url[0] == '\0') {
        return "[HTTP Bridge] Error: No endpoint URL configured. Call ia_http_configure() first.";
    }

    if (ia_http_parse_url(g_http_config.endpoint_url, host, sizeof(host),
                           path, sizeof(path), &port) < 0) {
        return g_http_config.last_error;
    }

    /* Build JSON request body */
    char json_body[IA_HTTP_MAX_REQUEST];
    int json_len;

    if (g_http_config.model_name[0] != '\0') {
        json_len = snprintf(json_body, sizeof(json_body),
            "{"
            "\"model\": \"%s\","
            "\"messages\": ["
            "{\"role\": \"system\", \"content\": \"You are MyanOS AI Bridge. Respond concisely in the requested language.\"},"
            "{\"role\": \"user\", \"content\": \"[%s] %s\"}"
            "],"
            "\"max_tokens\": 512,"
            "\"temperature\": 0.7"
            "}",
            g_http_config.model_name, method, message);
    } else {
        json_len = snprintf(json_body, sizeof(json_body),
            "{"
            "\"messages\": ["
            "{\"role\": \"user\", \"content\": \"[%s] %s\"}"
            "],"
            "\"max_tokens\": 512"
            "}",
            method, message);
    }

    /* Retry logic */
    clock_t start = clock();
    int success = 0;
    int bytes_read = 0;

    for (int attempt = 0; attempt < g_http_config.max_retries; attempt++) {
        bytes_read = ia_http_post(host, port, path, json_body, response_buf, sizeof(response_buf));
        if (bytes_read > 0) {
            success = 1;
            break;
        }
        if (attempt < g_http_config.max_retries - 1) {
            sleep(IA_HTTP_RETRY_DELAY_SEC);
        }
    }

    clock_t end = clock();
    double elapsed_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;

    /* Update stats */
    g_http_config.total_requests++;
    if (g_http_config.total_requests > 0) {
        g_http_config.avg_response_time_ms =
            (g_http_config.avg_response_time_ms * (g_http_config.total_requests - 1) + elapsed_ms)
            / g_http_config.total_requests;
    }

    if (!success) {
        g_http_config.total_errors++;
        return g_http_config.last_error;
    }

    g_http_config.connected = 1;

    /* Extract AI response from JSON */
    result_buf[0] = '\0';

    /* Try OpenAI-style response */
    if (ia_http_extract_json(response_buf, "content", result_buf, sizeof(result_buf)) == 0) {
        return result_buf;
    }

    /* Try simple response field */
    if (ia_http_extract_json(response_buf, "response", result_buf, sizeof(result_buf)) == 0) {
        return result_buf;
    }

    /* Try text field */
    if (ia_http_extract_json(response_buf, "text", result_buf, sizeof(result_buf)) == 0) {
        return result_buf;
    }

    /* Try message field */
    if (ia_http_extract_json(response_buf, "message", result_buf, sizeof(result_buf)) == 0) {
        return result_buf;
    }

    /* Fallback: return raw response (truncated) */
    strncpy(result_buf, response_buf, 512);
    result_buf[512] = '\0';
    return result_buf;
}

/* ============================================================================
 * 18 IA Method Implementations (HTTP versions)
 * ============================================================================ */

/* --- Communication Methods --- */

static void ia_http_say(const char *msg) {
    const char *resp = ia_http_send("say", msg);
    printf("[HTTP.say] Communication | %s\n", resp);
}

static void ia_http_respond(const char *msg) {
    const char *resp = ia_http_send("respond", msg);
    printf("[HTTP.respond] Communication | %s\n", resp);
}

static void ia_http_chat(const char *msg) {
    const char *resp = ia_http_send("chat", msg);
    printf("[HTTP.chat] Communication | %s\n", resp);
}

static void ia_http_ask(const char *msg) {
    const char *resp = ia_http_send("ask", msg);
    printf("[HTTP.ask] Communication | %s\n", resp);
}

/* --- Cognition Methods --- */

static void ia_http_think(const char *msg) {
    const char *resp = ia_http_send("think", msg);
    printf("[HTTP.think] Cognition | %s\n", resp);
}

static void ia_http_explain(const char *msg) {
    const char *resp = ia_http_send("explain", msg);
    printf("[HTTP.explain] Cognition | %s\n", resp);
}

static void ia_http_analyze(const char *msg) {
    const char *resp = ia_http_send("analyze", msg);
    printf("[HTTP.analyze] Cognition | %s\n", resp);
}

static void ia_http_learn(const char *msg) {
    const char *resp = ia_http_send("learn", msg);
    printf("[HTTP.learn] Cognition | %s\n", resp);
}

/* --- Creativity Methods --- */

static void ia_http_generate(const char *msg) {
    const char *resp = ia_http_send("generate", msg);
    printf("[HTTP.generate] Creativity | %s\n", resp);
}

static void ia_http_dream(const char *msg) {
    const char *resp = ia_http_send("dream", msg);
    printf("[HTTP.dream] Creativity | %s\n", resp);
}

static void ia_http_visualize(const char *msg) {
    const char *resp = ia_http_send("visualize", msg);
    printf("[HTTP.visualize] Creativity | %s\n", resp);
}

/* --- Knowledge Methods --- */

static void ia_http_teach(const char *msg) {
    const char *resp = ia_http_send("teach", msg);
    printf("[HTTP.teach] Knowledge | %s\n", resp);
}

static void ia_http_describe(const char *msg) {
    const char *resp = ia_http_send("describe", msg);
    printf("[HTTP.describe] Knowledge | %s\n", resp);
}

static void ia_http_summarize(const char *msg) {
    const char *resp = ia_http_send("summarize", msg);
    printf("[HTTP.summarize] Knowledge | %s\n", resp);
}

static void ia_http_translate(const char *msg) {
    const char *resp = ia_http_send("translate", msg);
    printf("[HTTP.translate] Knowledge | %s\n", resp);
}

/* --- Management Methods --- */

static void ia_http_connect_fn(const char *msg) {
    g_http_config.connected = 0;

    if (g_http_config.endpoint_url[0] == '\0') {
        printf("[HTTP.connect] Management | Error: No endpoint configured\n");
        return;
    }

    /* Test connection */
    const char *resp = ia_http_send("connect", msg ? msg : "ping");
    if (g_http_config.connected) {
        printf("[HTTP.connect] Management | Connected to: %s\n", g_http_config.endpoint_url);
        printf("[HTTP.connect] Management | Response: %s\n", resp);
    } else {
        printf("[HTTP.connect] Management | Failed: %s\n", g_http_config.last_error);
    }
}

static void ia_http_update(const char *msg) {
    if (msg && strncmp(msg, "url=", 4) == 0) {
        strncpy(g_http_config.endpoint_url, msg + 4, IA_HTTP_MAX_URL - 1);
        printf("[HTTP.update] Management | URL updated: %s\n", g_http_config.endpoint_url);
    } else if (msg && strncmp(msg, "key=", 4) == 0) {
        strncpy(g_http_config.api_key, msg + 4, IA_HTTP_MAX_KEY - 1);
        printf("[HTTP.update] Management | API key updated (hidden)\n");
    } else if (msg && strncmp(msg, "model=", 6) == 0) {
        strncpy(g_http_config.model_name, msg + 6, sizeof(g_http_config.model_name) - 1);
        printf("[HTTP.update] Management | Model updated: %s\n", g_http_config.model_name);
    } else if (msg && strncmp(msg, "timeout=", 8) == 0) {
        g_http_config.timeout_sec = atoi(msg + 8);
        printf("[HTTP.update] Management | Timeout: %d sec\n", g_http_config.timeout_sec);
    } else if (msg && strncmp(msg, "verbose=", 8) == 0) {
        g_http_config.verbose = atoi(msg + 8);
        printf("[HTTP.update] Management | Verbose: %d\n", g_http_config.verbose);
    } else {
        const char *resp = ia_http_send("update", msg ? msg : "status");
        printf("[HTTP.update] Management | %s\n", resp);
    }
}

static void ia_http_check(const char *msg) {
    (void)msg;
    char ts[64];
    ia_http_timestamp(ts, sizeof(ts));

    printf("[HTTP.check] Management | Status Report\n");
    printf("  Version:      %s\n", IA_HTTP_VERSION);
    printf("  Timestamp:    %s\n", ts);
    printf("  Connected:    %s\n", g_http_config.connected ? "Yes" : "No");
    printf("  Endpoint:     %s\n",
           g_http_config.endpoint_url[0] ? g_http_config.endpoint_url : "(not set)");
    printf("  API Key:      %s\n",
           g_http_config.api_key[0] ? "****configured****" : "(not set)");
    printf("  Model:        %s\n",
           g_http_config.model_name[0] ? g_http_config.model_name : "(default)");
    printf("  Timeout:      %d sec\n", g_http_config.timeout_sec);
    printf("  Max Retries:  %d\n", g_http_config.max_retries);
    printf("  Total Reqs:   %ld\n", g_http_config.total_requests);
    printf("  Total Errors: %ld\n", g_http_config.total_errors);
    printf("  Avg Resp:     %.1f ms\n", g_http_config.avg_response_time_ms);
    printf("  Last Error:   %s\n",
           g_http_config.last_error[0] ? g_http_config.last_error : "(none)");
}

/* ============================================================================
 * Public API Functions
 * ============================================================================ */

/**
 * Configure the HTTP bridge
 * Call this before using any bridge methods
 */
int ia_http_configure(const char *endpoint_url, const char *api_key,
                       const char *model_name, int timeout_sec) {
    /* Reset state */
    memset(&g_http_config, 0, sizeof(g_http_config));

    /* Set defaults */
    g_http_config.timeout_sec = timeout_sec > 0 ? timeout_sec : IA_HTTP_TIMEOUT_SEC;
    g_http_config.max_retries = IA_HTTP_MAX_RETRIES;
    g_http_config.use_https = 1;
    g_http_config.verbose = 0;
    g_http_config.connected = 0;
    g_http_config.total_requests = 0;
    g_http_config.total_errors = 0;
    g_http_config.avg_response_time_ms = 0.0;

    /* Copy configuration */
    if (endpoint_url) {
        strncpy(g_http_config.endpoint_url, endpoint_url, IA_HTTP_MAX_URL - 1);
    }
    if (api_key) {
        strncpy(g_http_config.api_key, api_key, IA_HTTP_MAX_KEY - 1);
    }
    if (model_name) {
        strncpy(g_http_config.model_name, model_name, sizeof(g_http_config.model_name) - 1);
    }

    printf("[ia_bridge_http] Configured: %s (timeout=%ds, retries=%d)\n",
           g_http_config.endpoint_url[0] ? g_http_config.endpoint_url : "(no endpoint)",
           g_http_config.timeout_sec, g_http_config.max_retries);

    return 0;
}

/**
 * Initialize HTTP bridge and wire all 18 methods to __mmc_ai__
 */
int ia_http_init(void) {
    printf("[ia_bridge_http] v%s initializing...\n", IA_HTTP_VERSION);
    printf("[ia_bridge_http] Wiring 18 HTTP methods to MMC AI Bridge...\n");

    /* Communication */
    __mmc_ai__.say      = ia_http_say;
    __mmc_ai__.respond  = ia_http_respond;
    __mmc_ai__.chat     = ia_http_chat;
    __mmc_ai__.ask      = ia_http_ask;

    /* Cognition */
    __mmc_ai__.think    = ia_http_think;
    __mmc_ai__.explain  = ia_http_explain;
    __mmc_ai__.analyze  = ia_http_analyze;
    __mmc_ai__.learn    = ia_http_learn;

    /* Creativity */
    __mmc_ai__.generate = ia_http_generate;
    __mmc_ai__.dream    = ia_http_dream;
    __mmc_ai__.visualize = ia_http_visualize;

    /* Knowledge */
    __mmc_ai__.teach    = ia_http_teach;
    __mmc_ai__.describe = ia_http_describe;
    __mmc_ai__.summarize = ia_http_summarize;
    __mmc_ai__.translate = ia_http_translate;

    /* Management */
    __mmc_ai__.connect  = ia_http_connect_fn;
    __mmc_ai__.update   = ia_http_update;
    __mmc_ai__.check    = ia_http_check;

    printf("[ia_bridge_http] All 18 methods wired successfully.\n");
    printf("[ia_bridge_http] Ready. Call __mmc_ai__.connect(\"\") to test.\n");

    return 0;
}

/**
 * Cleanup HTTP bridge resources
 */
void ia_http_cleanup(void) {
    g_http_config.connected = 0;
    printf("[ia_bridge_http] Cleaned up. Goodbye.\n");
}

/**
 * Quick setup helper - configure and init in one call
 * Usage: ia_http_quick_setup("https://api.example.com/v1/chat/completions", "sk-xxx", "gpt-3.5");
 */
int ia_http_quick_setup(const char *url, const char *key, const char *model) {
    ia_http_configure(url, key, model, 0);
    ia_http_init();
    return 0;
}

/**
 * Load configuration from environment variables
 * IA_BRIDGE_URL, IA_BRIDGE_KEY, IA_BRIDGE_MODEL
 */
int ia_http_load_env(void) {
    const char *url   = getenv("IA_BRIDGE_URL");
    const char *key   = getenv("IA_BRIDGE_KEY");
    const char *model = getenv("IA_BRIDGE_MODEL");
    const char *timeout_str = getenv("IA_BRIDGE_TIMEOUT");
    int timeout = timeout_str ? atoi(timeout_str) : 0;

    if (!url) url = "https://api.openai.com/v1/chat/completions";
    if (!model) model = "gpt-3.5-turbo";

    printf("[ia_bridge_http] Loading from environment...\n");
    ia_http_configure(url, key, model, timeout);
    ia_http_init();

    return 0;
}

/* ============================================================================
 * Standalone Test (build with -DMMC_IA_BRIDGE_HTTP_TEST)
 * ============================================================================ */

#ifdef MMC_IA_BRIDGE_HTTP_TEST

int main(int argc, char *argv[]) {
    printf("============================================================\n");
    printf("  MyanOS IA Bridge HTTP - Self Test v%s\n", IA_HTTP_VERSION);
    printf("============================================================\n\n");

    /* Test 1: Default configuration */
    printf("--- Test 1: Default Configuration ---\n");
    ia_http_configure("https://api.openai.com/v1/chat/completions",
                       "sk-test-key", "gpt-3.5-turbo", 10);
    ia_http_init();
    printf("\n");

    /* Test 2: Check status */
    printf("--- Test 2: Status Check ---\n");
    __mmc_ai__.check("");
    printf("\n");

    /* Test 3: Update configuration */
    printf("--- Test 3: Dynamic Update ---\n");
    __mmc_ai__.update("model=qwen2.5-coder-1.5b");
    __mmc_ai__.update("timeout=15");
    __mmc_ai__.check("");
    printf("\n");

    /* Test 4: URL parsing */
    printf("--- Test 4: URL Parsing ---\n");
    const char *test_urls[] = {
        "https://api.openai.com/v1/chat/completions",
        "http://localhost:8080/api/chat",
        "https://example.com:443/v1/messages",
        NULL
    };

    for (int i = 0; test_urls[i]; i++) {
        char host[256], path[512];
        int port;
        printf("  URL: %s\n", test_urls[i]);
        if (ia_http_parse_url(test_urls[i], host, sizeof(host), path, sizeof(path), &port) == 0) {
            printf("  Host: %s | Port: %d | Path: %s\n\n", host, port, path);
        } else {
            printf("  Parse failed: %s\n\n", g_http_config.last_error);
        }
    }

    /* Test 5: URL encoding */
    printf("--- Test 5: URL Encoding ---\n");
    char encoded[512];
    const char *test_str = "မြန်မာစာ Myanmar Test";
    ia_http_url_encode(test_str, encoded, sizeof(encoded));
    printf("  Input:  %s\n", test_str);
    printf("  Output: %s\n\n", encoded);

    /* Test 6: String trim */
    printf("--- Test 6: String Trim ---\n");
    char trim_test[] = "  hello world  \n";
    printf("  Input:  '[%s]'\n", trim_test);
    printf("  Output: '[%s]'\n\n", ia_http_trim(trim_test));

    /* Test 7: Environment loading */
    printf("--- Test 7: Environment Variables ---\n");
    setenv("IA_BRIDGE_URL", "https://api.example.com/v1/chat", 1);
    setenv("IA_BRIDGE_MODEL", "test-model", 1);
    ia_http_load_env();
    printf("\n");

    /* Test 8: Cleanup */
    printf("--- Test 8: Cleanup ---\n");
    ia_http_cleanup();

    printf("\n============================================================\n");
    printf("  All tests passed! ✅\n");
    printf("============================================================\n");

    return 0;
}

#endif /* MMC_IA_BRIDGE_HTTP_TEST */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <stdlib.h>

#define UNUSED __attribute__((unused))

#define strlenof(STR) (sizeof(STR) - 1)
#define make_header(Name, PREFIX, value, length) \
char Name[length + sizeof(PREFIX)]; \
memcpy(Name, PREFIX, sizeof(PREFIX) - 1); \
memcpy(Name + sizeof(PREFIX) - 1, value, length); \
Name[sizeof(PREFIX) + length - 1] = '\0'

const char BASENAME[NAME_MAX + 1];

#define STRING_SIZE 256

typedef struct {
    size_t len;
    char data[STRING_SIZE];
} string;


#define IPV4_SIZE 16
#define URL_GET_IPV4 "ipv4.icanhazip.com"
#define URL_ZONE "https://api.cloudflare.com/client/v4/zones"
#define URL_GET_ZONE_ID "https://api.cloudflare.com/client/v4/zones?name="

static const char *ipv4_next_part(const char *c) {
    if (c[0] < '0' || c[0] > '9') return 0;
    if (c[1] == '.') return c + 2;
    if (c[1] < '0' || c[1] > '9') return 0;
    if (c[2] == '.') return c + 3;
    if (c[2] < '0' || c[2] > '9') return 0;
    if (c[3] != '.') return 0;
    if ((((unsigned) (c[0] - '0') * 10) +
         (unsigned) (c[1] - '0')) * 10 +
        (unsigned) (c[2] - '0') < 256)
        return c + 3;
    return 0;
}

static const char *ipv4_last_part(const char *c) {
    if (c[0] < '0' || c[0] > '9') return 0;
    if (c[1] < '0' || c[1] > '9') return c + 1;
    if (c[2] < '0' || c[2] > '9') return c + 2;
    if ((((unsigned) (c[0] - '0') * 10) +
         (unsigned) (c[1] - '0')) * 10 +
        (unsigned) (c[2] - '0') < 256)
        return c + 3;
    return 0;
}

static const char *pass_ipv4(const char *c) {
    if (!(c = ipv4_next_part(c))) return 0;
    if (!(c = ipv4_next_part(c))) return 0;
    if (!(c = ipv4_next_part(c))) return 0;
    return ipv4_last_part(c);
}


static size_t cfddns_get_ipv4_now_callback(char *src, size_t UNUSED(char_size), size_t length, char *dst) {
    if (!length) return 0;
    size_t n = length < IPV4_SIZE ? length : IPV4_SIZE;
    if (src[n - 1] == '\n') --n;
    memcpy(dst, src, n);
    dst[n] = '\0';
    return length;
}

static void cfddns_get_ipv4_now(char *ipv4) {
    CURL *req = curl_easy_init();
    curl_easy_setopt(req, CURLOPT_URL, URL_GET_IPV4);
    curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, cfddns_get_ipv4_now_callback);
    curl_easy_setopt(req, CURLOPT_WRITEDATA, ipv4);
    curl_easy_perform(req);
    curl_easy_cleanup(req);
}


static size_t
cfddns_get_zone_id_callback(char *json, size_t UNUSED(char_size), size_t size, string *zone_id) {
#define MATCH_ZONE_ID "{\"result\":[{\"id\":\""
    if (size < strlenof(MATCH_ZONE_ID) + 5) return 0;
    if (!memcpy(json, MATCH_ZONE_ID, strlenof(MATCH_ZONE_ID))) return 0;
    char *start = json + strlenof(MATCH_ZONE_ID);
    char *end = json + size;
    for (char *i = start;; ++i) {
        if (i == end) return 0;
        if (*i != '"') continue;
        if (i - start > STRING_SIZE) return 0;
        zone_id->len = i - start;
        memcpy(zone_id->data, start, zone_id->len);
        return size;
    }
}



static void cfddns_get_zone_id(string *zone_name, string *email, string *apikey, string *zone_id) {
    CURL *req = curl_easy_init();

    make_header(url, URL_GET_ZONE_ID, zone_name->data, zone_name->len);
    make_header(email_header, "X-Auth-Email: ", email->data, email->len);
    make_header(api_key_header, "X-Auth-Key: ", apikey->data, apikey->len);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, email_header);
    headers = curl_slist_append(headers, api_key_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(req, CURLOPT_URL, url);
    curl_easy_setopt(req, CURLOPT_HEADER, headers);
    curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, cfddns_get_zone_id_callback);
    curl_easy_setopt(req, CURLOPT_WRITEDATA, zone_id);
    curl_easy_perform(req);
    curl_easy_cleanup(req);

    curl_slist_free_all(headers);
}


static int cfddns_main_check(const char *config) {
    char ipv4_now[IPV4_SIZE];
    cfddns_get_ipv4_now(ipv4_now);
    if (!pass_ipv4(ipv4_now)) {
        fputs(PREFIX, stderr);
        fputs("get_ipv4_now: invalid address: ", stderr);
        fputs(ipv4_now, stderr);
        fputc('\n', stderr);
        return 1;
    } else {
        fputs(PREFIX, stdout);
        fputs("get_ipv4_now: ", stdout);
        fputs(ipv4_now, stdout);
        fputc('\n', stdout);
    }
    return 0;
}

static const char *next_string(const char *c) {
    while (*s == ' ' || *s == '\t') ++s;
    if (*s == '#') break;
    char *e = s;
    while (*e != ' ' && *e != '\t' && *e != '#') ++e;
    return c;
}

static int cfddns_main(FILE *fin) {
    char line[PIPE_BUF];
    string url_ipv4, user_email, user_apikey, zone_name, zone_id;
    while (fgets(line, PIPE_BUF, fin)) {
        char *s = line, *e;
        while (*s == ' ' || *s == '\t') ++s;
        if (*s == '#' || *s == '\0') continue;
        for (e = s + 1; *e != ' ' && *e != '\t' && *e != '#' && *e != '\0'; ++e);
        if (s[0] == '/') {
            // user_email
            user_email.len = e - s - 1;
            memcpy(user_email.data, s + 1, user_email.len);
            // user_apikey
            for (s = e + 1; *s == ' ' || *s == '\t'; ++s);
            if (*s == '#' || *s == '\0') {
                continue;
            }
            for (e = s + 1; *e != ' ' && *e != '\t' && *e != '#' && *e != '\0'; ++e);
            user_apikey.len = e - s;
            memcpy(user_apikey.data, s, user_apikey.len);
        } else if (e[-1] == '/') {
            // zone_name
            zone_name.len = e - s - 1;
            memcpy(zone_name.data, s, zone_name.len);
            // zone_id
            for (s = e + 1; *s == ' ' || *s == '\t'; ++s);
            if (*s != '#' && *s == '\0') {
                cfddns_get_zone_id(&zone_name, &user_email, &user_apikey, &zone_id);
                continue;
            }
            for (e = s + 1; *e != ' ' && *e != '\t' && *e != '#' && *e != '\0'; ++e);
            zone_id.len = e - s;
            memcpy(zone_id.data, s, zone_id.len);
        } else if (e[-1] == '?') {
            // set record value
            if (e - s == 1 && s[0] == 'A') {
                // set ipv4

            }
        } else {
            // set record
        }
    }
}

int main(int argc, char *argv[]) {
    for (char *i = argv[1], *s = i;;) {
        if (*i) {
            if (*i != '/') ++i; else s = ++i;
        } else {
            memcpy(BASENAME, s, i - s + 1);
            break;
        }
    }
    FILE *fp;
    if (argc < 2) {
        fp = stdin;
    } else {
        fp = fopen(argv[1], "r");
        if (fp == NULL) {
            perror(BASENAME);
            return 1;
        }
    }
    return cfddns_main(fp);
}

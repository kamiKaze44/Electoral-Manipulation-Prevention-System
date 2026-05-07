#include "mongoose.h"
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

sqlite3 *db;

// --- JSON Escape Helper ---
// Prevents JSON from breaking when descriptions contain quotes, newlines, etc.
void json_escape(const char *src, char *dst, size_t dst_size) {
    if (!src) { dst[0] = '\0'; return; }
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j < dst_size - 2; i++) {
        if      (src[i] == '"')  { dst[j++] = '\\'; dst[j++] = '"';  }
        else if (src[i] == '\\') { dst[j++] = '\\'; dst[j++] = '\\'; }
        else if (src[i] == '\n') { dst[j++] = '\\'; dst[j++] = 'n';  }
        else if (src[i] == '\r') { dst[j++] = '\\'; dst[j++] = 'r';  }
        else if (src[i] == '\t') { dst[j++] = '\\'; dst[j++] = 't';  }
        else                     { dst[j++] = src[i]; }
    }
    dst[j] = '\0';
}

// --- Database Initialization ---
void init_db()
{
    char *err_msg = 0;
    int rc = sqlite3_open("elecguard.db", &db);

    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    const char *sql = "CREATE TABLE IF NOT EXISTS reports("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                      "type TEXT, desc TEXT, loc TEXT, "
                      "lat REAL, lng REAL, status TEXT, date TEXT, reporter TEXT);";

    sqlite3_exec(db, sql, 0, 0, &err_msg);
}

// --- Event Handler ---
static void ev_handler(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev == MG_EV_HTTP_MSG)
    {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;

        // 1. API: /api/reports (Handle GET all AND POST new)
        if (mg_match(hm->uri, mg_str("/api/reports"), NULL))
        {

            // --- POST: Save New Report ---
            if (strncmp(hm->method.buf, "POST", 4) == 0)
            {
                char type[100] = "", desc[2048] = "", loc[200] = "", date[50] = "", reporter[100] = "Anonymous";
                char lat_str[50] = "0", lng_str[50] = "0";

                mg_http_get_var(&hm->body, "type", type, sizeof(type));
                mg_http_get_var(&hm->body, "desc", desc, sizeof(desc));
                mg_http_get_var(&hm->body, "loc", loc, sizeof(loc));
                mg_http_get_var(&hm->body, "date", date, sizeof(date));
                mg_http_get_var(&hm->body, "reporter", reporter, sizeof(reporter));
                mg_http_get_var(&hm->body, "lat", lat_str, sizeof(lat_str));
                mg_http_get_var(&hm->body, "lng", lng_str, sizeof(lng_str));

                // Use "Anonymous" if reporter field was left empty
                if (strlen(reporter) == 0) {
                    strncpy(reporter, "Anonymous", sizeof(reporter) - 1);
                }

                // Insert into SQLite
                sqlite3_stmt *stmt;
                const char *sql = "INSERT INTO reports (type, desc, loc, lat, lng, status, date, reporter) VALUES (?, ?, ?, ?, ?, 'Pending', ?, ?)";
                sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

                sqlite3_bind_text(stmt, 1, type, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, desc, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, loc, -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(stmt, 4, atof(lat_str));
                sqlite3_bind_double(stmt, 5, atof(lng_str));
                sqlite3_bind_text(stmt, 6, date, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 7, reporter, -1, SQLITE_TRANSIENT);

                int step_result = sqlite3_step(stmt);
                sqlite3_finalize(stmt);

                if (step_result == SQLITE_DONE) {
                    mg_http_reply(c, 200, "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n", "{\"status\":\"success\"}");
                } else {
                    mg_http_reply(c, 500, "Content-Type: application/json\r\n", "{\"status\":\"error\",\"message\":\"Database insert failed\"}");
                }
            }

            // --- GET: Fetch All Reports ---
            else
            {
                sqlite3_stmt *stmt;
                sqlite3_prepare_v2(db, "SELECT * FROM reports ORDER BY id DESC", -1, &stmt, NULL);

                // Increased buffer size to handle many reports safely
                char *json = (char *)malloc(65536);
                if (!json) {
                    mg_http_reply(c, 500, "Content-Type: application/json\r\n", "{\"error\":\"Memory error\"}");
                    return;
                }
                strcpy(json, "[");
                int first = 1;
                size_t json_len = 1;

                while (sqlite3_step(stmt) == SQLITE_ROW)
                {
                    // Escape all string fields to prevent broken JSON
                    char esc_type[200], esc_desc[4096], esc_loc[400], esc_status[100], esc_date[100], esc_reporter[200];
                    json_escape((const char*)sqlite3_column_text(stmt, 1), esc_type,     sizeof(esc_type));
                    json_escape((const char*)sqlite3_column_text(stmt, 2), esc_desc,     sizeof(esc_desc));
                    json_escape((const char*)sqlite3_column_text(stmt, 3), esc_loc,      sizeof(esc_loc));
                    json_escape((const char*)sqlite3_column_text(stmt, 6), esc_status,   sizeof(esc_status));
                    json_escape((const char*)sqlite3_column_text(stmt, 7), esc_date,     sizeof(esc_date));
                    json_escape((const char*)sqlite3_column_text(stmt, 8), esc_reporter, sizeof(esc_reporter));

                    char item[5120];
                    snprintf(item, sizeof(item),
                             "%s{\"id\":%d,\"type\":\"%s\",\"desc\":\"%s\",\"loc\":\"%s\",\"lat\":%f,\"lng\":%f,\"status\":\"%s\",\"date\":\"%s\",\"reporter\":\"%s\"}",
                             first ? "" : ",",
                             sqlite3_column_int(stmt, 0),
                             esc_type, esc_desc, esc_loc,
                             sqlite3_column_double(stmt, 4),
                             sqlite3_column_double(stmt, 5),
                             esc_status, esc_date, esc_reporter);

                    // Safety check: make sure we don't overflow the buffer
                    size_t item_len = strlen(item);
                    if (json_len + item_len + 2 < 65536) {
                        strcat(json, item);
                        json_len += item_len;
                    }
                    first = 0;
                }
                strcat(json, "]");
                sqlite3_finalize(stmt);
                mg_http_reply(c, 200, "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n", "%s", json);
                free(json);
            }
        }

        // 2. API: POST /api/reports/*/status — Update report status (MUST be before the /* route)
        else if (mg_match(hm->uri, mg_str("/api/reports/*/status"), NULL))
        {
            if (strncmp(hm->method.buf, "POST", 4) == 0)
            {
                // Extract the ID from the URL: /api/reports/7/status -> 7
                int id = atoi(hm->uri.buf + 13);

                char new_status[50] = "";
                mg_http_get_var(&hm->body, "status", new_status, sizeof(new_status));

                // Validate: only allow known status values
                int valid = (strcmp(new_status, "Pending") == 0 ||
                             strcmp(new_status, "Under Review") == 0 ||
                             strcmp(new_status, "Resolved") == 0);

                if (!valid || id <= 0) {
                    mg_http_reply(c, 400, "Content-Type: application/json\r\n", "{\"error\":\"Invalid status or ID\"}");
                }
                else
                {
                    sqlite3_stmt *stmt;
                    sqlite3_prepare_v2(db, "UPDATE reports SET status = ? WHERE id = ?", -1, &stmt, NULL);
                    sqlite3_bind_text(stmt, 1, new_status, -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(stmt, 2, id);

                    int result = sqlite3_step(stmt);
                    sqlite3_finalize(stmt);

                    if (result == SQLITE_DONE) {
                        mg_http_reply(c, 200, "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n", "{\"status\":\"updated\"}");
                    } else {
                        mg_http_reply(c, 500, "Content-Type: application/json\r\n", "{\"error\":\"Update failed\"}");
                    }
                }
            }
            else
            {
                mg_http_reply(c, 405, "Content-Type: application/json\r\n", "{\"error\":\"Method not allowed\"}");
            }
        }

        // 3. API: Get SINGLE Report by ID
        else if (mg_match(hm->uri, mg_str("/api/reports/*"), NULL))
        {
            int id = atoi(hm->uri.buf + 13);

            sqlite3_stmt *stmt;
            sqlite3_prepare_v2(db, "SELECT * FROM reports WHERE id = ?", -1, &stmt, NULL);
            sqlite3_bind_int(stmt, 1, id);

            if (sqlite3_step(stmt) == SQLITE_ROW)
            {
                char esc_type[200], esc_desc[4096], esc_loc[400], esc_status[100], esc_date[100], esc_reporter[200];
                json_escape((const char*)sqlite3_column_text(stmt, 1), esc_type,     sizeof(esc_type));
                json_escape((const char*)sqlite3_column_text(stmt, 2), esc_desc,     sizeof(esc_desc));
                json_escape((const char*)sqlite3_column_text(stmt, 3), esc_loc,      sizeof(esc_loc));
                json_escape((const char*)sqlite3_column_text(stmt, 6), esc_status,   sizeof(esc_status));
                json_escape((const char*)sqlite3_column_text(stmt, 7), esc_date,     sizeof(esc_date));
                json_escape((const char*)sqlite3_column_text(stmt, 8), esc_reporter, sizeof(esc_reporter));

                char json[5120];
                snprintf(json, sizeof(json),
                         "{\"id\":%d,\"type\":\"%s\",\"desc\":\"%s\",\"loc\":\"%s\",\"lat\":%f,\"lng\":%f,\"status\":\"%s\",\"date\":\"%s\",\"reporter\":\"%s\"}",
                         sqlite3_column_int(stmt, 0),
                         esc_type, esc_desc, esc_loc,
                         sqlite3_column_double(stmt, 4),
                         sqlite3_column_double(stmt, 5),
                         esc_status, esc_date, esc_reporter);

                mg_http_reply(c, 200, "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n", "%s", json);
            }
            else
            {
                mg_http_reply(c, 404, "Content-Type: application/json\r\n", "{\"error\":\"Not found\"}");
            }
            sqlite3_finalize(stmt);
        }

        // 4. Static Files (Serve everything in /public)
        else
        {
            struct mg_http_serve_opts opts = {.root_dir = "public"};
            if (mg_match(hm->uri, mg_str("/"), NULL))
            {
                mg_http_serve_file(c, hm, "public/index.html", &opts);
            }
            else
            {
                mg_http_serve_dir(c, hm, &opts);
            }
        }
    }
}

int main(void)
{
    init_db();
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);

    mg_http_listen(&mgr, "http://localhost:5000", ev_handler, NULL);

    printf("Starting ElecGuard API with SQLite Database on http://localhost:5000\n");
    for (;;)
        mg_mgr_poll(&mgr, 1000);

    sqlite3_close(db);
    mg_mgr_free(&mgr);
    return 0;
}
#include "mongoose.h"
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
    #include <direct.h>
#else
    #include <unistd.h>
#endif

sqlite3 *db;
char g_frontend_path[512];

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

                // Read form data variables sent by report.html
                mg_http_get_var(&hm->body, "type", type, sizeof(type));
                mg_http_get_var(&hm->body, "desc", desc, sizeof(desc));
                mg_http_get_var(&hm->body, "loc", loc, sizeof(loc));
                mg_http_get_var(&hm->body, "date", date, sizeof(date));
                mg_http_get_var(&hm->body, "reporter", reporter, sizeof(reporter));
                mg_http_get_var(&hm->body, "lat", lat_str, sizeof(lat_str));
                mg_http_get_var(&hm->body, "lng", lng_str, sizeof(lng_str));

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

                sqlite3_step(stmt);
                sqlite3_finalize(stmt);

                mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"success\"}");
            }

            // --- GET: Fetch All Reports ---
            else
            {
                sqlite3_stmt *stmt;
                sqlite3_prepare_v2(db, "SELECT * FROM reports ORDER BY id DESC", -1, &stmt, NULL);

                char json[8192] = "[";
                int first = 1;

                while (sqlite3_step(stmt) == SQLITE_ROW)
                {
                    if (!first)
                        strcat(json, ",");
                    char item[1024];
                    snprintf(item, sizeof(item),
                             "{\"id\":%d, \"type\":\"%s\", \"desc\":\"%s\", \"loc\":\"%s\", \"lat\":%f, \"lng\":%f, \"status\":\"%s\", \"date\":\"%s\", \"reporter\":\"%s\"}",
                             sqlite3_column_int(stmt, 0), sqlite3_column_text(stmt, 1), sqlite3_column_text(stmt, 2),
                             sqlite3_column_text(stmt, 3), sqlite3_column_double(stmt, 4), sqlite3_column_double(stmt, 5),
                             sqlite3_column_text(stmt, 6), sqlite3_column_text(stmt, 7), sqlite3_column_text(stmt, 8));
                    strcat(json, item);
                    first = 0;
                }
                strcat(json, "]");
                sqlite3_finalize(stmt);
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json);
            }
        }

        // 2. API: Get SINGLE Report by ID
        else if (mg_match(hm->uri, mg_str("/api/reports/*"), NULL))
        {
            int id = atoi(hm->uri.buf + 13);

            sqlite3_stmt *stmt;
            sqlite3_prepare_v2(db, "SELECT * FROM reports WHERE id = ?", -1, &stmt, NULL);
            sqlite3_bind_int(stmt, 1, id);

            if (sqlite3_step(stmt) == SQLITE_ROW)
            {
                char json[1024];
                snprintf(json, sizeof(json),
                         "{\"id\":%d, \"type\":\"%s\", \"desc\":\"%s\", \"loc\":\"%s\", \"lat\":%f, \"lng\":%f, \"status\":\"%s\", \"date\":\"%s\", \"reporter\":\"%s\"}",
                         sqlite3_column_int(stmt, 0), sqlite3_column_text(stmt, 1), sqlite3_column_text(stmt, 2),
                         sqlite3_column_text(stmt, 3), sqlite3_column_double(stmt, 4), sqlite3_column_double(stmt, 5),
                         sqlite3_column_text(stmt, 6), sqlite3_column_text(stmt, 7), sqlite3_column_text(stmt, 8));
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json);
            }
            else
            {
                mg_http_reply(c, 404, "Content-Type: application/json\r\n", "{\"error\":\"Not found\"}");
            }
            sqlite3_finalize(stmt);
        }

        // 3. Static Files (Serve everything in /public)
        // 4. Static Files — serve from Frontend folder
        else
        {
            struct mg_http_serve_opts opts = {.root_dir = g_frontend_path};
            if (mg_match(hm->uri, mg_str("/"), NULL))
            {
                char index_path[600];
                snprintf(index_path, sizeof(index_path), "%s/index.html", g_frontend_path);
                mg_http_serve_file(c, hm, index_path, &opts);
            }
            else
            {
                mg_http_serve_dir(c, hm, &opts);
            }
        }
    }
}
void fix_slashes(char *path) {
    for (int i = 0; path[i]; i++)
        if (path[i] == '\\') path[i] = '/';
}
int main(void)
{
    // Build absolute path to Frontend folder which sits next to C backend
    char cwd[512];
    char raw_path[512];
    #ifdef _WIN32
        _getcwd(cwd, sizeof(cwd));
        snprintf(raw_path, sizeof(raw_path), "%s/../Frontend", cwd);
        _fullpath(g_frontend_path, raw_path, sizeof(g_frontend_path));
        fix_slashes(g_frontend_path);  // add this line
    #else
        getcwd(cwd, sizeof(cwd));
        snprintf(raw_path, sizeof(raw_path), "%s/../Frontend", cwd);
        realpath(raw_path, g_frontend_path);
    #endif

    printf("Serving frontend from: %s\n", g_frontend_path);

    init_db();
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);

    mg_http_listen(&mgr, "http://localhost:5000", ev_handler, NULL);

    printf("ElecGuard running on http://localhost:5000\n");
    for (;;)
        mg_mgr_poll(&mgr, 1000);

    sqlite3_close(db);
    mg_mgr_free(&mgr);
    return 0;
}
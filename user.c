#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql/mysql.h>
#include <json-c/json.h>
#include "cnf.h"
#include "httplib.h"

struct json_object* get_user_data() {
    MYSQL *conn;
    MYSQL_RES *res;
    MYSQL_ROW row;

    conn = mysql_init(NULL);
    if (!mysql_real_connect(conn, host, user, pwd, db, port, NULL, 0)) {
        struct json_object *error_json = json_object_new_object();
        json_object_object_add(error_json, "status", json_object_new_string("error"));
        json_object_object_add(error_json, "message", json_object_new_string(mysql_error(conn)));
        return error_json;
    }

    if (mysql_query(conn, "SELECT id, username, password, phone, created_at, updated_at FROM shuwen.`user`;")) {
        fprintf(stderr, "error query database: %s\n", mysql_error(conn));
        return NULL;
    }

    res = mysql_store_result(conn);
    struct json_object *result = json_object_new_object();
    struct json_object *users = json_object_new_array();

    while ((row = mysql_fetch_row(res)) != NULL) {
        struct json_object *user = json_object_new_object();
        json_object_object_add(user, "id", json_object_new_string(row[0] ? row[0] : ""));
        json_object_object_add(user, "username", json_object_new_string(row[1] ? row[1] : ""));
        json_object_object_add(user, "password", json_object_new_string(row[2] ? row[2] : ""));
        json_object_object_add(user, "phone", json_object_new_string(row[3] ? row[3] : ""));
        json_object_object_add(user, "created_at", json_object_new_string(row[4] ? row[4] : ""));
        json_object_object_add(user, "updated_at", json_object_new_string(row[5] ? row[5] : ""));
        json_object_array_add(users, user);
    }

    json_object_object_add(result, "status", json_object_new_string("success"));
    json_object_object_add(result, "data", users);

    printf("Generated JSON: %s\n", json_object_to_json_string(result));

    mysql_free_result(res);
    mysql_close(conn);

    return result;
}

int main() {
    httplib::Server svr;

    svr.Get("/getUserData", [](const httplib::Request& req, httplib::Response& res) {
        struct json_object *data = get_user_data();
        const char *json_str = json_object_to_json_string(data);
        res.set_content(json_str, "application/json");
        json_object_put(data);
    });

    printf("Server started at http://localhost:8080\n");
    svr.listen("0.0.0.0", 8888);
    return 0;
}

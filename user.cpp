#include <iostream>
#include <mysql/mysql.h>
#include <json/json.h>

Json::Value get_user_data(){
	Json::Value json_data
	MYSQL *conn;
	MYSQL_RES *res;
	MYSQL_ROW row;

	conn = mysql_init(NULL);
	if(!mysql_real_connect(conn,"39.107.59.4","shuwen","Shuwen@_00..","shuwen",33061,NULL,0)){
		Json::Value error_json;
		error_json["status"] = "error";
		error_json["message"] = "Error connecting to database:" + std::string(mysql_error(conn));
		return error_json;
	}

	if(mysql_query(conn,"SELECT id, username, password, phone, created_at, updated_at FROM shuwen.`user`;")){
		std::cerr<<"error query database:"<<mysql_error(conn)<<std::endl;
		return 1;
	}

	res = mysql_use_result(conn);

	Json::Value user(Json::arrayValue);
	while((row = mysql_fetch_row(res)) != NULL){
		Json::Value user;
		user["id"] = row[0]?row[0]:"";
		user["username"] = row[1]?row[1]:"";
		user["password"] = row[2]?row[2]:"";
		user["phone"] = row[3]?row[3]:"";
		user["created_at"] = row[4]?row[4]:"";
		user["updated_at"] = row[5]?row[5]:"";
		user.append(user);
	}
	json_data["status"] = "success";
	json_data["data"] = user;

	mysql_free_result(res);
	mysql_close(conn);

	return json_data;
}

int main(){
	Json::Value data = get_user_data();
	std::cout<<data.toStyledString()<<std::endl;

	return 0;
}


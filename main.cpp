#include <iostream>
#include <mysql/mysql.h>
#include <json/json.h>
#include "cnf.h"
#include "httplib.h"

#define DATA_SIZE 100

Json::Value get_user_data(){
	Json::Value result;
	MYSQL *conn;
	MYSQL_RES *res;
	MYSQL_ROW row;

	conn = mysql_init(NULL);
	if(!mysql_real_connect(conn,host,user,pwd,db,port,NULL,0)){
		Json::Value error_json;
		error_json["status"] = "error";
		error_json["message"] = "Error connecting to database:" + std::string(mysql_error(conn));
		return error_json;
	}

	if(mysql_query(conn,"SELECT uid, wechat_user_id, account, pwd, real_name, sex, birthday, card_id, mark, label_id, group_id, nickname, avatar, phone, addres, cancel_time, create_time, last_time, last_ip, now_money, brokerage_price, status, spread_uid, spread_time, spread_limit, brokerage_level, user_type, promoter_time, is_promoter, main_uid, pay_count, pay_price, spread_count, spread_pay_count, spread_pay_price, integral, member_level, member_value, count_start, count_fans, count_content, is_svip, svip_endtime, svip_save_money FROM crmeb_merchant.`eb_user`;")){
		std::cerr<<"error query database:"<<mysql_error(conn)<<std::endl;
		return 1;
	}

	res = mysql_use_result(conn);

	Json::Value users(Json::arrayValue);
	while((row = mysql_fetch_row(res)) != NULL){
		Json::Value user;
		user["id"] = row[0]?row[0]:"";
		user["username"] = row[1]?row[1]:"";
		user["password"] = row[2]?row[2]:"";
		user["phone"] = row[3]?row[3]:"";
		user["created_at"] = row[4]?row[4]:"";
		user["updated_at"] = row[5]?row[5]:"";
		users.append(user);
	}
	result["status"] = "success";
	result["data"] = users;
	std::cout << "Generated JSON: " << result.toStyledString() << std::endl;

	mysql_free_result(res);
	mysql_close(conn);

	return result;
}

void generate_price(double *price,int size){

}

int main(){
	double price[DATA_SIZE];
	httplib::Server svr;
	svr.Get("/getUserData",[](const httplib::Request& req,httplib::Response& res){
			Json::Value data = get_user_data();
			res.set_content(data.toStyledString(),"application/json");
	});
	std::cout<<"Server started at http://localhost:8888"<<std::endl;
	svr.listen("0.0.0.0",8888);
	return 0;
}


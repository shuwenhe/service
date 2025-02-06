#include <iostream>
#include <mysql/mysql.h>
#include <json/json.h>
#include "cnf.h"
#include "httplib.h"

#define DATA_SIZE 100

const std::string videoFilePath = "123.mp4";

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
		Json::Value error_json;
		error_json["status"] = "error";
		error_json["message"] = "Error querying database:" + std::string(mysql_error(conn));
		return error_json;
	}

	res = mysql_use_result(conn);

	Json::Value users(Json::arrayValue);
	while((row = mysql_fetch_row(res)) != NULL){
		Json::Value user;
		user["uid"] = row[0]?row[0]:"";
		user["wechat_user_id"] = row[1]?row[1]:"";
		user["account"] = row[2]?row[2]:"";
		user["pwd"] = row[3]?row[3]:"";
		user["real_name"] = row[4]?row[4]:"";
		user["sex"] = row[5]?row[5]:"";
		user["birthday"] = row[6]?row[6]:"";
		user["card_id"] = row[7]?row[7]:"";
		user["mark"] = row[8]?row[8]:"";
		user["label_id"] = row[9]?row[9]:"";
		user["group_id"] = row[10]?row[10]:"";
		user["nickname"] = row[11]?row[11]:"";
		user["avatar"] = row[12]?row[12]:"";
		user["phone"] = row[13]?row[13]:"";
		user["addres"] = row[14]?row[14]:"";
		user["cancel_time"] = row[15]?row[15]:"";
		user["create_time"] = row[16]?row[16]:"";
		user["last_time"] = row[17]?row[17]:"";
		user["last_ip"] = row[18]?row[18]:"";
		user["now_money"] = row[19]?row[19]:"";
		user["brokerage_price"] = row[20]?row[20]:"";
		user["status"] = row[21]?row[21]:"";
		user["spread_uid"] = row[22]?row[22]:"";
		user["spread_time"] = row[23]?row[23]:"";
		user["spread_limit"] = row[24]?row[24]:"";
		user["brokerage_level"] = row[25]?row[8]:"";
		user["user_type"] = row[26]?row[26]:"";
		user["promoter_time"] = row[27]?row[27]:"";
		user["is_promoter"] = row[28]?row[28]:"";
		user["main_uid"] = row[29]?row[29]:"";
		user["pay_count"] = row[30]?row[30]:"";
		user["pay_price"] = row[31]?row[31]:"";
		user["spread_count"] = row[32]?row[32]:"";
		user["spread_pay_count"] = row[33]?row[33]:"";
		user["spread_pay_price"] = row[34]?row[34]:"";
		user["integral"] = row[35]?row[35]:"";
		user["member_level"] = row[36]?row[36]:"";
		user["member_value"] = row[37]?row[37]:"";
		user["count_start"] = row[38]?row[38]:"";
		user["count_fans"] = row[39]?row[39]:"";
		user["count_content"] = row[40]?row[40]:"";
		user["is_svip"] = row[41]?row[41]:"";
		user["svip_endtime"] = row[42]?row[42]:"";
		user["svip_save_money"] = row[43]?row[43]:"";
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


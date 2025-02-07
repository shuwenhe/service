#include <iostream>
#include <mysql/mysql.h>
#include <json/json.h>
#include "cnf.h"
#include "httplib.h"
#include <fstream>

#define DATA_SIZE 100

const std::string videoFilePath = "123.mp4";

void writeToResponse(httplib::Response& res,const char* buffer,size_t size){
	res.body.append(buffer,size);
}

void sendVideo(const httplib::Request& req,httplib::Response& res){
	std::ifstream videoFile(videoFilePath,std::ios::binary);
	if(!videoFile){
		res.status = 404;
		res.set_content("Video file not found","text/plain");
		return;
	}
	// 获取文件大小
	videoFile.seekg(0,std::ios::end);
	size_t fileSize = videoFile.tellg();
	videoFile.seekg(0,std::ios::beg);
	// 设置响应头
	res.set_header("Content-Type","video/mp4");
	res.set_header("Content-Length",std::to_string(fileSize).c_str());
	// 流式传输视频文件
	const size_t bufferSize = 1024 * 1024 * 1024; // 1MB
	char buffer[bufferSize];
	while (videoFile.read(buffer,bufferSize)){
		writeToResponse(res,buffer,bufferSize);
	}
	size_t remaining = videoFile.gcount();
	if (remaining > 0){
		writeToResponse(res,buffer,remaining);
	}
	videoFile.close();
}

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
		std::vector<std::string> columns = {
			"uid", "wechat_user_id", "account", "pwd", "real_name", "sex", "birthday", "card_id",
			"mark", "label_id", "group_id", "nickname", "avatar", "phone", "addres", "cancel_time",
            "create_time", "last_time", "last_ip", "now_money", "brokerage_price", "status",
            "spread_uid", "spread_time", "spread_limit", "brokerage_level", "user_type",
            "promoter_time", "is_promoter", "main_uid", "pay_count", "pay_price", "spread_count",                "spread_pay_count", "spread_pay_price", "integral", "member_level", "member_value",
            "count_start", "count_fans", "count_content", "is_svip", "svip_endtime", "svip_save_money"
			};
		for (size_t i = 0; i < columns.size(); ++i) {
			user[columns[i]] = row[i] ? row[i] : "";
		}
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
	svr.Get("/getVideo",sendVideo); // 注册视频请求处理函数
	std::cout<<"Server started at http://localhost:8888"<<std::endl;
	svr.listen("0.0.0.0",8888);
	return 0;
}


#include <iostream>
#include <mysql/mysql.h>

int main(){
	MYSQL *conn;
	MYSQL_RES *res;
	MYSQL_ROW row;
	conn = mysql_init(NULL);
	if(!mysql_real_connect(conn,"39.107.59.4","shuwen","123456","shuwen",33061,NULL,0)){
		std::cerr<<"error connecting to database:"<<mysql_error(conn)<<std::endl;
		return 1;
	}

	if(mysql_query(conn,"SELECT id, username, password, phone, created_at, updated_at FROM shuwen.`user`;")){
		std::cerr<<"error query database:"<<mysql_error(conn)<<std::endl;
		return 1;
	}

	res = mysql_use_result(conn);

	std::cout<<"id\tusername\tpassword\tphone"<<std::endl;
	while((row = mysql_fetch_row(res)) != NULL){
		std::cout<<row[0]<<"\t"<<row[1]<<"\t"<<row[2]<<"\t"<<row[3]<<std::endl;
	}

}


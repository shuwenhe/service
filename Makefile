build:
	# g++ main.cpp -o service -I/usr/include/jsoncpp -ljsoncpp -lmysqlclient
	g++ main.cpp -o service -std=c++17 -lstdc++fs -lpthread -ldl
run: 
	./service
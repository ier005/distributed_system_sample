client:client.cc client.pb.cc
	g++ -g -o client client.cc client.pb.cc -pthread -lprotobuf --std=c++11
client.pb.cc: client.proto
	protoc --cpp_out=. ./client.proto
clean:
	rm client.pb.cc client.pb.h client

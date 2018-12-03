worker: worker.pb.cc main.cc
	c++ -o worker main.cc worker.pb.cc -lpthread -lprotobuf

worker.pb.cc: worker.proto
	protoc --cpp_out=. worker.proto

clean:
	rm worker.pb.h worker.pb.cc worker

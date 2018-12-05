worker: worker.pb.cc main.cc thread_pool.cc
	c++ -o worker main.cc worker.pb.cc thread_pool.cc -lpthread -lprotobuf

worker.pb.cc: worker.proto
	protoc --cpp_out=. worker.proto

clean:
	rm -f worker.pb.h worker.pb.cc worker

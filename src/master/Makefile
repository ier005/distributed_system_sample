LIBS = -lpthread -lprotobuf
TARGET = master

all: $(TARGET)

clean:
	rm -f $(TARGET)
	rm -f master.pb.cc master.pb.h
master.pb.cc: master.proto
	protoc --cpp_out=. master.proto
$(TARGET): master.cc master.pb.cc
	pkg-config --cflags --libs protobuf  # fails if protobuf is not installed
	c++ -g -o $@ master.cc workersmanager.cc taskqueue.cc master.pb.cc -lpthread -lprotobuf -std=gnu++11
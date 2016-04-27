all: hello

hello: hello.cpp
	g++ `pkg-config --cflags gtk+-3.0` -o hello hello.cpp `pkg-config --libs gtk+-3.0`


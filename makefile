all: hello

hello: hello.cpp iridescent-map.cpp
	g++ `pkg-config --cflags gtk+-3.0` -o hello hello.cpp iridescent-map.cpp `pkg-config --libs gtk+-3.0`


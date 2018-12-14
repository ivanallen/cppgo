main:main.cc context.h chan.h go.h
	g++ -g -std=c++11 -o main main.cc -lpthread

clean:
	rm -rf main

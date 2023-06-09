header=$(wildcard *.h ./src/*.h)
src=$(wildcard *.cpp ./src/*.cpp)
objs=$(patsubst %.cpp, %.o, $(src))

target=./out/webserver
$(target): $(objs) $(header)
	$(CXX) $(objs) -o $(target)
%.o: $.c
	$(CXX) -c $< -o $@
.PHONY:clean
clean:
	- rm -f $(objs) $(target)

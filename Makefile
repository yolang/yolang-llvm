all: yo

.PHONY clean:
	rm -f yo

yo: yo.cpp
	g++ ./yo.cpp `llvm-config --cppflags --ldflags --libs core jit native all` -o ./yo

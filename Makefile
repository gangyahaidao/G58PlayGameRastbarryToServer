CC = g++
CPPFLAGS = 
CFLAGS = -Wall -o2 
INC_PATH = -I/usr/local/include

LIBPATH = -L/usr/local/lib
LIBS = -lpthread -lserial

SRCS = $(wildcard *.cpp)
OBJS = $(patsubst %.cpp,%.o,$(SRCS))
TARGETS = mytask

all:$(TARGETS)

$(TARGETS):$(OBJS)
	$(CC) $^ -o $@ $(LIBPATH) $(LIBS)

$(OBJS):%.o : %.cpp
	$(CC) $(CFLAGS) -c $< -o $@ $(INC_PATH)

clean:
	$(RM) $(OBJS) $(TARGETS)

.PHONY: all clean

CC = g++
CFLAGS = -Iinclude -Wall -g
SRC = src/main.cpp src/modules/debug_module.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = my_cpp_project

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
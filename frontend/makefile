# Compiler
CC = g++

# Compiler flags
CFLAGS = -std=c++11 -pthread

# Executable name
TARGET = frontendserver

# Source files
SRCS = frontendserver.cc

# Rule to build the executable
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

# Clean rule
clean:
	rm -f $(TARGET)

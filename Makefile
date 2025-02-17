CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -I.
TARGET = video_service
SRC = main.cpp
LIBS = -lpthread -lstdc++fs

.PHONY: all build run stop clean help

all: help

build:
	@echo "Building service..."
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LIBS)

run: build
	@echo "Starting service..."
	@./$(TARGET) --path /videos --port 8080
		

stop:
	@echo "Stopping service..."
	@if [ -f service.pid ]; then \
		kill -TERM $$(cat service.pid) 2>/dev/null || true; \
		rm -f service.pid; \
	fi

clean:
	@echo "Cleaning build artifacts..."
	@rm -f $(TARGET) *.o

help:
	@echo "Service management commands:"
	@echo "  make build    - Compile the service"
	@echo "  make run      - Start the service in background"
	@echo "  make stop     - Stop the running service"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make help     - Show this help message"





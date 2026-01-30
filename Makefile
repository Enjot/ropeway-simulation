# Ropeway Simulation Makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -I./include
LDFLAGS =

# Debug build by default
DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CFLAGS += -g -O0 -DDEBUG
else
    CFLAGS += -O2 -DNDEBUG
endif

# Directories
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

# Source files
MAIN_SRC = $(SRC_DIR)/main.c $(SRC_DIR)/ipc_utils.c
CASHIER_SRC = $(SRC_DIR)/cashier.c $(SRC_DIR)/ipc_utils.c
TOURIST_SRC = $(SRC_DIR)/tourist.c $(SRC_DIR)/ipc_utils.c

# Targets
MAIN = $(BUILD_DIR)/main
CASHIER = $(BUILD_DIR)/cashier
TOURIST = $(BUILD_DIR)/tourist

# All executables
ALL_TARGETS = $(MAIN) $(CASHIER) $(TOURIST)

.PHONY: all clean run debug ipcs-clean

all: $(BUILD_DIR) $(ALL_TARGETS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(MAIN): $(MAIN_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CASHIER): $(CASHIER_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TOURIST): $(TOURIST_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)

# Run the simulation
run: all
	@echo "Running ropeway simulation..."
	@echo "Use Ctrl+C to stop"
	./$(MAIN)

# Run with debug logging
debug: all
	ROPEWAY_LOG_LEVEL=DEBUG ./$(MAIN)

# Clean up any leftover IPC resources
ipcs-clean:
	@echo "Cleaning up IPC resources..."
	@ipcs -m | grep $$(whoami) | awk '{print $$2}' | xargs -I {} ipcrm -m {} 2>/dev/null || true
	@ipcs -s | grep $$(whoami) | awk '{print $$2}' | xargs -I {} ipcrm -s {} 2>/dev/null || true
	@echo "Done"

# Run with small number of tourists for testing
test: all
	ROPEWAY_NUM_TOURISTS=10 ROPEWAY_STATION_CAPACITY=5 ./$(MAIN)

# Stress test
stress: all
	ROPEWAY_NUM_TOURISTS=1000 ROPEWAY_STATION_CAPACITY=100 ROPEWAY_LOG_LEVEL=INFO ./$(MAIN)

# Show help
help:
	@echo "Ropeway Simulation Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all        - Build all executables (default)"
	@echo "  clean      - Remove build directory"
	@echo "  run        - Build and run simulation"
	@echo "  debug      - Run with DEBUG logging"
	@echo "  test       - Run with 10 tourists (quick test)"
	@echo "  stress     - Run with 1000 tourists"
	@echo "  ipcs-clean - Clean up leftover IPC resources"
	@echo "  help       - Show this help"
	@echo ""
	@echo "Environment variables:"
	@echo "  ROPEWAY_NUM_TOURISTS     - Number of tourists (default: 100)"
	@echo "  ROPEWAY_STATION_CAPACITY - Station capacity (default: 50)"
	@echo "  ROPEWAY_LOG_LEVEL        - Log level: DEBUG, INFO, WARN, ERROR"

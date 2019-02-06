# Can take variables passed in for:
#
# MODE         "debug" or "release".
# TARGET       Name of the output executable.
# BIN_DIR      Output directory for executable.
# BUILD_DIR    Output directory for object files.
# SOURCE_DIR   Directory where source files are found.
# INCLUDE_DIR  Directory where header files are found.

CFLAGS := -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter
INCLUDE_DIR := include
SOURCE_DIR := src
BUILD_DIR := build
BIN_DIR := bin
TARGET := docopt

# Mode configuration.
ifeq ($(MODE),debug)
	CFLAGS += -O0 -DDEBUG -g
	OBJ_DIR := $(BUILD_DIR)/debug
else
	CFLAGS += -O3 -flto
	OBJ_DIR := $(BUILD_DIR)/release
endif

CFLAGS += -I$(INCLUDE_DIR)

# Files.
HEADERS := $(wildcard $(INCLUDE_DIR)/*.h)
SOURCES := $(wildcard $(SOURCE_DIR)/*.c)
OBJECTS := $(addprefix $(OBJ_DIR)/, $(notdir $(SOURCES:.c=.o)))

# Targets ---------------------------------------------------------------------

# Link the interpreter.
$(BIN_DIR)/$(TARGET): $(OBJECTS)
	@ printf "%8s %-40s %s\n" $(CC) $@ "$(CFLAGS)"
	@ mkdir -p $(BIN_DIR)
	@ $(CC) $(CFLAGS) $^ -o $@

# Compile object files.
$(OBJ_DIR)/%.o: $(SOURCE_DIR)/%.c $(HEADERS)
	@ printf "%8s %-40s %s\n" $(CC) $< "$(CFLAGS)"
	@ mkdir -p $(OBJ_DIR)
	@ $(CC) -c $(CFLAGS) -o $@ $<

clean:
	@ $(RM) -rf $(BUILD_DIR) $(BIN_DIR)

.PHONY: default clean

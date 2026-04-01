# LitemultiKV Makefile
# =====================
# Default (Reactor):  make
# Proactor (io_uring): make proactor

CC = gcc
CFLAGS = -Wall -I ./include -I ./NtyCo/core/
LDFLAGS = -lpthread -ldl -lm

# io_uring paths (for proactor mode)
LOCAL_PREFIX = ../local
IO_CFLAGS = -I $(LOCAL_PREFIX)/lib/include
IO_LDFLAGS = -L $(LOCAL_PREFIX)/lib -luring

# NtyCo library
NTYCO_DIR = ./NtyCo
NTYCO_LIB = -L $(NTYCO_DIR) -lntyco

# Source files by module
ENGINE_SRCS = src/engine/kvs_array.c \
              src/engine/kvs_rbtree.c \
              src/engine/kvs_hash.c \
              src/engine/kvs_skiptable.c \
              src/engine/kvs_vector.c

CACHE_SRCS  = src/cache/kvs_cache.c
PERSIST_SRCS = src/persist/kvs_rdb.c
COMMON_SRCS = src/common/kvstore.c
NETWORK_REACTOR = src/network/reactor.c
NETWORK_PROACTOR = src/network/proactor.c

ALL_SRCS = $(COMMON_SRCS) $(NETWORK_REACTOR) $(ENGINE_SRCS) $(CACHE_SRCS) $(PERSIST_SRCS)
OBJS = $(ALL_SRCS:.c=.o)

TESTCASE_SRC = tests/testcase.c

.PHONY: all reactor proactor clean test

all: reactor

# --- Build NtyCo library ---
$(NTYCO_DIR)/libntyco.a:
	make -C $(NTYCO_DIR)

# --- Reactor mode (default) ---
reactor: $(NTYCO_DIR)/libntyco.a $(OBJS)
	$(CC) -o kvstore $(OBJS) $(CFLAGS) $(LDFLAGS) $(NTYCO_LIB)
	$(CC) -o testcase $(TESTCASE_SRC) $(CFLAGS)
	@echo "=== Build complete: kvstore (reactor) ==="
	@echo "  Run: ./kvstore <port>"

# --- Proactor mode (io_uring) ---
PROACTOR_SRCS = $(COMMON_SRCS) $(NETWORK_PROACTOR) $(ENGINE_SRCS) $(CACHE_SRCS) $(PERSIST_SRCS)
PROACTOR_OBJS = $(PROACTOR_SRCS:.c=.o)

proactor: CFLAGS += $(IO_CFLAGS) -DNETWORK_SELECT=1
proactor: LDFLAGS += $(IO_LDFLAGS)
proactor: $(NTYCO_DIR)/libntyco.a $(PROACTOR_OBJS)
	$(CC) -o kvstore_proactor $(PROACTOR_OBJS) $(CFLAGS) $(LDFLAGS) $(NTYCO_LIB)
	@echo "=== Build complete: kvstore_proactor ==="
	@echo "  Run: LD_LIBRARY_PATH=../local/lib ./kvstore_proactor <port>"

# --- Pattern rule ---
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# --- Test ---
test:
	@echo "Start server first: ./kvstore 19999"
	@echo "Then run: python3 tests/test_comprehensive.py"

# --- Clean ---
clean:
	rm -f $(OBJS) $(PROACTOR_OBJS) kvstore kvstore_proactor testcase
	rm -f src/engine/*.o src/network/*.o src/cache/*.o src/persist/*.o src/common/*.o

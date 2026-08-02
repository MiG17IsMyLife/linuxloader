.PHONY: all win32 clean

BUILD_DIR = build-pacloader

all: win32

win32:
	cmake -S . -B $(BUILD_DIR) -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo
	cmake --build $(BUILD_DIR) --parallel

clean:
	cmake -E remove_directory $(BUILD_DIR)

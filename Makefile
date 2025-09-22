# 源文件
SRC = pylon_tool.c

# 目标文件
TARGET = py-tool

# 版本号
VERSION = 1.0.0

# 编译选项
CFLAGS = -Wall -O2 -DPYLON_TOOL_VERSION=\"$(VERSION)\"

# 链接选项（如果需要额外的库，可以在这里添加）
LDFLAGS =

# 默认目标
all: clean $(TARGET)

# 编译规则
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

# 清理目标
clean:
	rm -f $(TARGET)
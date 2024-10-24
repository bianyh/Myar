# 定义编译器
CC = gcc

# 定义编译选项，这里没有特别指定，可以根据需要添加
CFLAGS = -w

# 定义链接选项
LDFLAGS =

# 定义目标文件名
TARGET = myar

# 定义源文件
SOURCES = myar.c

# 使用patsubst函数将源文件列表转换为对象文件列表
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

# 默认目标
all: $(TARGET)
	@echo "complete!"
	@rm $(TARGET).o

# 链接目标文件生成最终的可执行文件
$(TARGET): $(OBJECTS)
	@$(CC) $(LDFLAGS) $(OBJECTS) -o $(TARGET) > /dev/null 2>&1

# 编译源文件生成对象文件，并且不生成.d文件和不显示编译信息
%.o: %.c
	@$(CC) $(CFLAGS) -c $< -o $@ > /dev/null 2>&1

# 清理生成的文件
clean:
	@rm -f $(TARGET) $(OBJECTS)

# 防止make命令回显命令
.PHONY: all clean
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <utime.h>

#define ARMAG "!<arch>\n"  // Magic string for ar files
#define AR_HDR_SIZE 60     // Header size for each entry in the archive
#define ARFMAG "`\n"       // End of header marker

typedef struct {
    char name[16];
    char date[12];
    char uid[6];
    char gid[6];
    char mode[8];
    char size[10];
    char end[2];
} ar_hdr;

// Function prototypes
void quick_add(const char *archive, const char *file);
void extract(const char *archive, const char *file, int restore_metadata);
void list(const char *archive, int verbose);
void delete_file(const char *archive, const char *file);
void append_old_files(const char *archive, int days);
void show_help();
void write_archive_header(int fd, struct stat *st, const char *filename);
void restore_file_metadata(const char *filename, struct stat *st);

// Utility function to pad archive size
void pad_if_needed(int fd, int size) {
    if (size % 2 != 0) {
        char padding = '\n';
        write(fd, &padding, 1);
    }
}

// Main function to parse commands
int main(int argc, char *argv[]) {
    if (argc < 3) {
        show_help();
        return 1;
    }

    const char *command = argv[1];
    const char *archive = argv[2];

    if (strcmp(command, "q") == 0 && argc == 4) {
        quick_add(archive, argv[3]);
    } else if (strcmp(command, "x") == 0 || strcmp(command, "xo") == 0) {
        extract(archive, argv[3], strcmp(command, "xo") == 0);
    } else if (strcmp(command, "t") == 0 || strcmp(command, "tv") == 0) {
        list(archive, strcmp(command, "tv") == 0);
    } else if (strcmp(command, "d") == 0 && argc == 4) {
        delete_file(archive, argv[3]);
    } else if (strcmp(command, "A") == 0 && argc == 4) {
        append_old_files(archive, atoi(argv[3]));
    } else if (strcmp(command, "h") == 0) {
        show_help();
    } else {
        show_help();
        return 1;
    }

    return 0;
}

void quick_add(const char *archive, const char *file) {
    // 打开归档文件，创建并附加内容，权限为0666
    int archive_fd = open(archive, O_RDWR | O_CREAT | O_APPEND, 0666);
    if (archive_fd < 0) {
        perror("Failed to open archive file"); // 打开失败时打印错误信息
        return;
    }

    // 检查归档是否为空，如果为空则写入ARMAG
    char magic[8];
    if (read(archive_fd, magic, 8) != 8 || strncmp(magic, ARMAG, 8) != 0) {
        lseek(archive_fd, 0, SEEK_SET); // 移动到文件开头
        write(archive_fd, ARMAG, 8);    // 写入ARMAG标记
    } else {
        lseek(archive_fd, 0, SEEK_END);  // 移动到文件末尾以附加内容
    }

    // 打开要添加的文件，读取权限
    int file_fd = open(file, O_RDONLY);
    if (file_fd < 0) {
        perror("Failed to open file to add"); // 打开失败时打印错误信息
        close(archive_fd); // 关闭归档文件
        return;
    }

    struct stat st;
    // 获取文件的状态信息
    if (fstat(file_fd, &st) < 0) {
        perror("Failed to stat file"); // 获取失败时打印错误信息
        close(file_fd); // 关闭文件描述符
        close(archive_fd);
        return;
    }

    // 写入文件头部
    write_archive_header(archive_fd, &st, file);

    // 写入文件内容
    char buffer[1024];
    ssize_t n;
    while ((n = read(file_fd, buffer, sizeof(buffer))) > 0) {
        write(archive_fd, buffer, n); // 将读取的内容写入归档文件
    }

    // 如果需要，填充归档文件以保持对齐
    pad_if_needed(archive_fd, st.st_size);

    // 关闭文件描述符
    close(file_fd);
    close(archive_fd);
}



// 移除字符串末尾的反斜杠
void remove_trailing_backslash(char *str) {
    char *end = str + strlen(str) - 1;
    while (end > str && *end == '/') {
        *end-- = '\0';
    }
}

void extract(const char *archive, const char *file, int restore_metadata) {
    int archive_fd = open(archive, O_RDONLY);
    if (archive_fd < 0) {
        perror("Failed to open archive file");
        return;
    }

    char magic[8];
    if (read(archive_fd, magic, 8) != 8 || strncmp(magic, ARMAG, 8) != 0) {
        fprintf(stderr, "Invalid archive format\n");
        close(archive_fd);
        return;
    }

    ar_hdr header;
    while (read(archive_fd, &header, AR_HDR_SIZE) == AR_HDR_SIZE) {
        header.name[15] = '\0';
        char *filename = header.name;
        while (*filename && *filename == ' ') filename++;
        char *end = filename + strlen(filename) - 1;
        while (end > filename && *end == ' ') end--;
        *(end + 1) = '\0';

        remove_trailing_backslash(filename); // 移除末尾的反斜杠

        if (strncmp(filename, file, strlen(file)) == 0) {
            int out_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (out_fd < 0) {
                perror("Failed to create output file");
                close(archive_fd);
                return;
            }

            char buffer[1024];
            ssize_t remaining = atoi(header.size);
            ssize_t n;
            while (remaining > 0 && (n = read(archive_fd, buffer, sizeof(buffer) < remaining ? sizeof(buffer) : remaining)) > 0) {
                write(out_fd, buffer, n);
                remaining -= n;
            }

                // 根据参数决定是否恢复文件的权限和时间
            if (restore_metadata) {
                struct stat st;
                st.st_mode = strtol(header.mode, NULL, 8); // 设置文件权限
                st.st_mtime = atoi(header.date); // 设置文件修改时间
                restore_file_metadata(filename, &st); // 调用函数恢复元数据
            }

            close(out_fd);
            break;
        }

        lseek(archive_fd, atoi(header.size) + (atoi(header.size) % 2), SEEK_CUR);
    }

    close(archive_fd);
}



// Convert date to a more readable format
void format_date(char *date_str, char *buffer) {
    time_t timestamp = (time_t)atol(date_str);
    struct tm *tm_info = localtime(&timestamp);
    strftime(buffer, 20, "%b %d %H:%M %Y", tm_info);
}

// Convert octal permissions to rw-r--r-- format
void octal_to_permissions(const char *octal_perm, char *perm_str) {
    // Extract the permission part (ignore file type)
    const char *perm_start = octal_perm;
    if(*(perm_start+4)!=' ')
        perm_start+=3;
    
    // Convert octal permissions to decimal
    mode_t mode = strtol(perm_start, NULL, 8);
    
    // Convert decimal permissions to string format
    perm_str[0] = (mode & 0400) ? 'r' : '-';
    perm_str[1] = (mode & 0200) ? 'w' : '-';
    perm_str[2] = (mode & 0100) ? 'x' : '-';

    perm_str[3] = (mode & 0040) ? 'r' : '-';
    perm_str[4] = (mode & 0020) ? 'w' : '-';
    perm_str[5] = (mode & 0010) ? 'x' : '-';

    perm_str[6] = (mode & 0004) ? 'r' : '-';
    perm_str[7] = (mode & 0002) ? 'w' : '-';
    perm_str[8] = (mode & 0001) ? 'x' : '-';
    perm_str[9] = '\0'; // String terminator
}

// List files in archive
void list(const char *archive, int verbose) {
    int archive_fd = open(archive, O_RDONLY);
    if (archive_fd < 0) {
        perror("Failed to open archive file");
        return;
    }

    char magic[8];
    if (read(archive_fd, magic, 8) != 8 || strncmp(magic, ARMAG, 8) != 0) {
        fprintf(stderr, "Invalid archive format\n");
        close(archive_fd);
        return;
    }

    ar_hdr header;
    char formatted_date[20];
    char permissions[12]; // Buffer to store permissions in rw-r--r-- format
    while (read(archive_fd, &header, AR_HDR_SIZE) == AR_HDR_SIZE) {
        header.name[15] = '\0';  // Ensure the name is null-terminated
        header.uid[5] = '\0';    // Ensure the uid is null-terminated
        header.gid[5] = '\0';    // Ensure the gid is null-terminated
        header.mode[7] = '\0';   // Ensure the mode is null-terminated
        header.size[9] = '\0';   // Ensure the size is null-terminated

        if (verbose) {
            format_date(header.date, formatted_date);
            octal_to_permissions(header.mode, permissions);
            printf("%s %s/%s %s %s %s\n",
                   permissions, header.uid, header.gid, header.size, formatted_date, header.name);
        } else {
            printf("%s\n", header.name);
        }

        int file_size = atoi(header.size);
        lseek(archive_fd, file_size + (file_size % 2), SEEK_CUR);  // Skip file content
    }

    close(archive_fd);
}



void delete_file(const char *archive, const char *file) {
    int archive_fd = open(archive, O_RDWR);
    if (archive_fd < 0) {
        perror("Failed to open archive file");
        return;
    }

    char magic[8];
    if (read(archive_fd, magic, 8) != 8 || strncmp(magic, ARMAG, 8) != 0) {
        fprintf(stderr, "Invalid archive format\n");
        close(archive_fd);
        return;
    }

    int found = 0; // 用于标记是否找到要删除的文件
    ar_hdr header;
    off_t offset = lseek(archive_fd, 0, SEEK_CUR); // 保存当前偏移量

    // 创建一个临时文件用于存储未删除的文件
    int temp_fd = open("temp_archive", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (temp_fd < 0) {
        perror("Failed to create temporary archive");
        close(archive_fd);
        return;
    }

    // 写入魔数到临时文件
    write(temp_fd, ARMAG, 8);

    // 读取每个文件头并决定是否写入临时文件
    while (read(archive_fd, &header, AR_HDR_SIZE) == AR_HDR_SIZE) {
        header.name[15] = '\0'; // 确保文件名以空字符结束

        int file_size = atoi(header.size); // 获取文件大小
        if (strncmp(header.name, file, strlen(file)) == 0) {
            found = 1; // 找到文件，标记为删除
            lseek(archive_fd, file_size + (file_size % 2), SEEK_CUR); // 跳过该文件内容
        } else {
            // 复制文件头和内容到临时文件
            write(temp_fd, &header, AR_HDR_SIZE);
            char buffer[1024];
            ssize_t n;
            while (file_size > 0 && (n = read(archive_fd, buffer, sizeof(buffer) < file_size ? sizeof(buffer) : file_size)) > 0) {
                write(temp_fd, buffer, n);
                file_size -= n;
            }
        }
    }

    if (!found) {
        fprintf(stderr, "File not found in archive\n");
    }

    close(archive_fd);
    close(temp_fd);

    // 替换原归档文件
    rename("temp_archive", archive);
}


void append_old_files(const char *archive, int days) {
    int archive_fd = open(archive, O_RDWR | O_CREAT | O_APPEND, 0666);
    if (archive_fd < 0) {
        perror("Failed to open archive file");
        return;
    }

    char magic[8];
    if (read(archive_fd, magic, 8) != 8 || strncmp(magic, ARMAG, 8) != 0) {
        lseek(archive_fd, 0, SEEK_SET); // 归档文件为空，重置到文件开头
        write(archive_fd, ARMAG, 8);    // 写入ARMAG标记
    } else {
        lseek(archive_fd, 0, SEEK_END); // 移动到文件末尾
    }

    time_t now = time(NULL);
    time_t cutoff_time = now - (days * 24 * 60 * 60); // 计算截止时间

    // 遍历当前目录中的所有文件
    DIR *dir = opendir(".");
    if (!dir) {
        perror("Failed to open directory");
        close(archive_fd);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) { // 只处理普通文件
            struct stat st;
            if (stat(entry->d_name, &st) == 0 && st.st_mtime < cutoff_time) {
                // 如果文件修改时间超过截止时间，进行添加
                quick_add(archive, entry->d_name);
            }
        }
    }

    closedir(dir);
    close(archive_fd);
}


// Show help information
void show_help() {
    printf("Usage: myar [command] [archive] [options]\n");
    printf("Commands:\n");
    printf("  q <file>      Quickly add a file to the archive.\n");
    printf("  x <file>      Extract a file from the archive.\n");
    printf("  t [-v]       List files in the archive.\n");
    printf("  d <file>     Delete a file from the archive.\n");
    printf("  A <days>     Append files older than N days.\n");
    printf("  h            Show help information.\n");
}

struct ar_hdr {
    char name[16];
    char date[12];
    char uid[6];
    char gid[6];
    char mode[8];
    char size[10];
    char end[2];
} __attribute__((packed));     // 确保结构体没有填充，大小正好为60字节

void write_archive_header(int fd, struct stat *st, const char *filename) {
    ar_hdr header;
    memset(&header, ' ', sizeof(header));  // 初始化为填充空格

    // 设置文件名，确保以空格填充到 16 个字符
    snprintf(header.name, sizeof(header.name), "%-16s", filename);
    // 设置修改时间，使用十进制表示，确保填充到 12 个字符
    snprintf(header.date, sizeof(header.date), "%-12ld", (long)st->st_mtime);
    // 设置用户ID，确保填充到 6 个字符
    snprintf(header.uid, sizeof(header.uid), "%-6d", st->st_uid);
    // 设置组ID，确保填充到 6 个字符
    snprintf(header.gid, sizeof(header.gid), "%-6d", st->st_gid);
    // 设置文件权限，确保以八进制形式填充到 8 个字符
    snprintf(header.mode, sizeof(header.mode), "%-8o", st->st_mode & 07777);
    // 设置文件大小，确保填充到 10 个字符
    snprintf(header.size, sizeof(header.size), "%-10ld", (long)st->st_size);
    // 设置结束标记
    strncpy(header.end, ARFMAG, sizeof(header.end));

    // 写入头部信息到归档文件
    write(fd, &header, AR_HDR_SIZE);


}

void restore_file_metadata(const char *filename, struct stat *st) {
    // 设置文件的权限
    if (chmod(filename, st->st_mode) < 0) {
        perror("Failed to set file permissions");
    }

    // 使用 utime 恢复访问和修改时间
    struct utimbuf new_times;
    new_times.actime = st->st_atime;  // 访问时间
    new_times.modtime = st->st_mtime;  // 修改时间
    if (utime(filename, &new_times) < 0) {
        perror("Failed to restore file times");
    }
}

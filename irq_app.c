#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#define KEY0VALUE       0xf0

int main(int argc, char *argv[])
{
        int ret = 0;
        int fd;
        int value;
        char *filename = NULL;

        if (argc != 2) {
                printf("Error Usage!\n"
                       "Usage %s filename 0:1\n"
                       ,argv[0]);
                ret = -1;
                goto error;
        }

        filename = argv[1];
        fd = open(filename, O_RDWR);
        if (fd == -1) {
                perror("open failed!\n");
                ret = -1;
                goto error;
        }

        while (1) {
                read(fd, &value, sizeof(value));
                if (value == KEY0VALUE) {
                        printf("KEY0 Press, value = %d\n", value);
                }
        }

error:
        close(fd);
        return ret;
}

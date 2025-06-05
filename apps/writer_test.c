#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fs.h>

#define ASSERT(cond, func)                               \
do {                                                     \
	if (!(cond)) {                                       \
		fprintf(stderr, "Function '%s' failed\n", func); \
		exit(EXIT_FAILURE);                              \
	}                                                    \
} while (0)

int main(int argc, char *argv[])
{
	int ret;
	char *diskname;
	int fd1;
	int fd2;
	int fd3;
	int fd4;
	char *data = malloc(5000);
	if (data == NULL){
		printf("Malloc failed!\n");
		exit(1);
	}
	memset(data, 'a', 5000);

	if (argc < 2) {
		printf("Usage: %s <diskimage>\n", argv[0]);
		exit(1);
	}

	/* Mount disk */
	diskname = argv[1];
	ret = fs_mount(diskname);
	ASSERT(!ret, "fs_mount");

	/* Create file and open */
	ret = fs_create("myfile");
	ASSERT(!ret, "fs_create");

	fd1 = fs_open("myfile");
	ASSERT(fd1 >= 0, "fs_open");

	/* Write some data */
	ret = fs_write(fd1, data, 4097);
	ASSERT(ret == 4097, "fs_write");

	ret = fs_write(fd1, data, 4095);
	ASSERT(ret == 4095, "fs_write");

	/* Create file and open */
	ret = fs_create("myfile2");
	ASSERT(!ret, "fs_create");

	fd2 = fs_open("myfile2");
	ASSERT(fd2 >= 0, "fs_open");

	/* Write some data */
	ret = fs_write(fd2, data, 4096);
	ASSERT(ret == 4096, "fs_write");

	/* Create file and open */
	ret = fs_create("myfile3");
	ASSERT(!ret, "fs_create");

	fd3 = fs_open("myfile3");
	ASSERT(fd3 >= 0, "fs_open");

	/* seek to start writing from middle of block */
	ret = fs_write(fd3, data, 5000);
	ASSERT(ret == 5000, "fs_write");
	ret = fs_lseek(fd3, 500);
	ASSERT(ret == 0, "fs_lseek");

	/* Write some data */
	ret = fs_write(fd3, data, 4096);
	ASSERT(ret == 4096, "fs_write");

	/* Create file and open */
	ret = fs_create("myfile4");
	ASSERT(!ret, "fs_create");

	fd4 = fs_open("myfile4");
	ASSERT(fd4 >= 0, "fs_open");

	/* seek to start writing from middle of block */
	ret = fs_write(fd4, data, 5000);
	ASSERT(ret == 5000, "fs_write");
	ret = fs_lseek(fd4, 1000);
	ASSERT(ret == 0, "fs_lseek");

	/* Write some data */
	ret = fs_write(fd4, data, 4096);
	ASSERT(ret == 4096, "fs_write");

	/* Close file and unmount */
	fs_close(fd1);
	fs_close(fd2);
	fs_close(fd3);
	fs_close(fd4);
	fs_umount();

	free(data);
	return 0;
}

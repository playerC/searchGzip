/**
Copyright (c) 2020 player

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <fcntl.h>
#include <assert.h>
#include <zlib.h>
#include <string.h>

const unsigned char G_GZIP_SIGN[] = { 0x1f, 0x8b, 0x08 ,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff
};

const int G_GZIP_SIGN_LENGTH = sizeof(G_GZIP_SIGN);

typedef struct {
	char * name;
	fpos_t filePos;
	long long byteIndex;
} CXGZFilePos_t;

typedef struct _CXList_t {
	void * value;
	struct _CXList_t * next;
	struct _CXList_t * tail;
	long long length;
} CXList_t;

/**
 * Copy from zlib code example.
 */
int CX_handleUnzipGzip(FILE *toGZFile, FILE *fromFile) {
	int ret;
	unsigned have;
	z_stream strm;
	unsigned char in[BUFSIZ];
	unsigned char out[BUFSIZ];

	/* allocate inflate state */
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = 0;
	strm.next_in = Z_NULL;
	ret = inflateInit2(&strm, 15 + 16);
	if (ret != Z_OK) {
		return ret;
	}

	for (;;) {

		strm.avail_in = fread(in, 1, BUFSIZ, fromFile);
		if (ferror(fromFile)) {
			(void) inflateEnd(&strm);
			return Z_ERRNO;
		}
		if (strm.avail_in == 0)
			break;
		strm.next_in = in;

		for (;;) {
			strm.avail_out = BUFSIZ;
			strm.next_out = out;
			ret = inflate(&strm, Z_NO_FLUSH);
			assert(ret != Z_STREAM_ERROR); /* state not clobbered */
			switch (ret) {
			case Z_NEED_DICT:
				ret = Z_DATA_ERROR; /* and fall through */
			case Z_DATA_ERROR:
			case Z_MEM_ERROR:
				(void) inflateEnd(&strm);
				return ret;
			}

			have = BUFSIZ - strm.avail_out;

			if (fwrite(out, 1, have, toGZFile) != have || ferror(toGZFile)) {
				(void) inflateEnd(&strm);
				return Z_ERRNO;
			}

			if (strm.avail_out != 0) {
				break;
			}
		} //for

		if (ret == Z_STREAM_END) {
			break;
		}
	} //for

	/* clean up and return */
	(void) inflateEnd(&strm);
	return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}


void printUsage(int argc, char **argv) {
	printf("Usage: searchGzip BIN_FILE \n", argv[0]);
	printf(
			"searchGzip looks for gzip file information in BIN_FILE and unzips\n"
			"it to the current folder.\n"
			"\n"
			"searchGzip Copyright (c) 2020 player\n"
			"\n"
			"This program is distributed in the hope that it will be useful,\n"
			"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
			"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
			"GNU General Public License for more details.\n"
			);
}


int CX_openFile(FILE** openedFile, char *path, char * mode) {
	FILE *file;
	file = fopen(path, mode);

	if (file == NULL) {
		perror(path);
		return -1;
	}

	*openedFile = file;

	return 0;
}

int CX_UnzipGZFilePosToFile(CXGZFilePos_t* theGZFilePos, FILE * theFile) {
	FILE * deFile;
	int ax = 0;

	ax = CX_openFile(&deFile, theGZFilePos->name, "wb");
	if (ax != 0) {
		return ax;
	}

	fsetpos(theFile, &theGZFilePos->filePos);
	ax = CX_handleUnzipGzip(deFile, theFile);

	fflush(deFile);
	fclose(deFile);

	return ax;
}

int CX_UnzipGZFileList(CXList_t *gzFilePosList, FILE * theFile) {
	CXGZFilePos_t * cur;
	CXList_t * it = gzFilePosList;
	int ax = 0;

	if (gzFilePosList->length < 1) {
		return 0;
	}

	for (it = gzFilePosList; it != NULL; it = it->next) {
		cur = (CXGZFilePos_t*) (it->value);
		ax = CX_UnzipGZFilePosToFile(cur, theFile);

		if (ax != 0) {
			if (ax == Z_DATA_ERROR) {
				printf("Unzip %s [ERROR] : Z_DATA_ERROR\n", cur->name);

			} else {
				printf("Unzip %s [ERROR] : %d\n", cur->name, ax);
			}
			continue;
		}

		printf("Unzip %s [ OK ]\n", cur->name);
	}

	return ax;
}

int CX_List_destory(CXList_t *gzFilePosList) {
	CXList_t * cur;
	CXList_t * prev = NULL;

	if (gzFilePosList == NULL || gzFilePosList->length < 1) {
		return 0;
	}

	for (cur = gzFilePosList; cur != NULL; cur = cur->next) {
		if (prev != NULL) {
			free(prev);
		}

		if (cur->value != NULL) {
			free(((CXGZFilePos_t*) (cur->value))->name);
			free(cur->value);
		}

		if (cur != gzFilePosList) {
			prev = cur;
		}
	}

	gzFilePosList->length = 0;
	return 0;
}

int CX_List_addFilePos(CXList_t *gzFilePosList, long long theBytePos,
		fpos_t theFilePos) {
	CXGZFilePos_t *gzFilePos = NULL;
	CXList_t * listNode = NULL;
	size_t nameSize;
	char buf[BUFSIZ];
	char *name;

	gzFilePos = (CXGZFilePos_t *) malloc(sizeof(CXGZFilePos_t));
	gzFilePos->byteIndex = theBytePos;
	gzFilePos->filePos = theFilePos;

	nameSize = snprintf(buf, BUFSIZ, "%lld", theBytePos);
	name = (char *) malloc(sizeof(char) * (nameSize + 1));
	memset(name, 0, nameSize + 1);

	gzFilePos->name = memcpy(name, buf, nameSize);

	if (gzFilePosList->length > 0) {
		listNode = (CXList_t *) malloc(sizeof(CXList_t));

		gzFilePosList->tail->next = listNode;
		gzFilePosList->tail = listNode;
	} else {
		listNode = gzFilePosList;
		listNode->tail = listNode;
	}

	listNode->value = gzFilePos;
	listNode->next = NULL;

	gzFilePosList->length++;
}

int CX_searchGzFile(CXList_t *gzFilePosList, FILE* theFile) {
	fpos_t startFilePos;
	long long bytePos = 0;
	long long startPos = 0;
	int si = 0;
	int c;

	for (;;) {

		c = fgetc(theFile);
		bytePos++;

		if (c == EOF) {
			return 0;
		}

		if (c < 0) {
			return c;
		}

		if (c == G_GZIP_SIGN[si] || G_GZIP_SIGN[si] == 0xff) {
			if (si == 0) {
				startPos = bytePos - 1;

				fseek(theFile, -1, SEEK_CUR);
				fgetpos(theFile, &startFilePos);
				fseek(theFile, 1, SEEK_CUR);
			}

			si++;
		} else {
			si = 0;
		}

		if (si == G_GZIP_SIGN_LENGTH) {
			si = 0;
			//add to list;
			CX_List_addFilePos(gzFilePosList, startPos, startFilePos);
		}
	}

	return 0;
}

int main(int argc, char **argv) {
	CXList_t gzFilePosList;
	FILE* theFile;
	char* theFilePath;
	int ax = 0;

	gzFilePosList.length = 0;
	gzFilePosList.tail = &gzFilePosList;

	if (argc != 2) {
		printUsage(argc, argv);
		return -1;
	}

	theFilePath = argv[1];

	ax = CX_openFile(&theFile, theFilePath, "rb");
	if (ax != 0) {
		goto destory;
	}

	ax = CX_searchGzFile(&gzFilePosList, theFile);

	if (ax != 0) {
		goto destory;
	}

	printf("Gzip file is located : %lld files.\n", gzFilePosList.length);

	printf("Unzip starting ...\n");

	ax = CX_UnzipGZFileList(&gzFilePosList, theFile);

	printf("Unzip task completed.\n");

	destory:

	CX_List_destory(&gzFilePosList);

	fclose(theFile);
	return ax;
}

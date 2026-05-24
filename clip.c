/*
 * clip file parser 
 * sources:
 * https://github.com/Inochi2D/clip-d/blob/main/SPEC.md
 * https://github.com/rasensuihei/cliputils/blob/master/README.md
 * https://github.com/dobrokot/clip_to_psd/blob/main/clip_to_psd.py
 */
#define NO_DUMP_BYTES 1
 
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#pragma warning (push)
#pragma warning (disable: 4295 4616 4761 5045)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
typedef unsigned __int8 uint8_t;
typedef unsigned __int64 uint64_t;
#define PRIu64 "llu"
#define PRIX64 "llX"
#include <winsock.h>
#define FILE_OPEN _wfopen
#define FILE_NAME_TYPE wchar_t
#define FILE_NAME_FORMAT_SPECIFIER "%ls"
#define FILE_OPEN_MODE L"rb"
#else
#include <stdint.h>
#include <inttypes.h>
#include <arpa/inet.h>
#define FILE_OPEN fopen
#define FILE_NAME_TYPE char
#define FILE_NAME_FORMAT_SPECIFIER "%s"
#define FILE_OPEN_MODE "rb"
#endif

/*
 * byte swap from big endian
 * https://stackoverflow.com/questions/3022552/is-there-any-standard-htonl-like-function-for-64-bits-integers-in-c
 */
#define htonll(x) ((1 == htonl(1)) ? (x) : ((uint64_t) htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32))
#define ntohll(x) ((1 == ntohl(1)) ? (x) : ((uint64_t) ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32))

/*
 * layout:
 * CSFCHUNK (24 bytes):
 * - 8 bytes for "CSFCHUNK" magic number
 * - 8 bytes for file length
 * - 8 bytes for offset (big endian)
 * CHNKHead (56 bytes):
 * - 8 bytes for "CHNKHead" magic number
 * - 48 bytes for unknowns
 * total 80 bytes (not including mandatory "CHNKExta" data)
 */

#define CLIP_CHUNK_MAGIC_SIZE 8

typedef enum clip_chunk_type
{
	CLIP_CHUNK_UNKNOWN_OR_ERROR,
	CLIP_CHUNK_CSFCHUNK,
	CLIP_CHUNK_CHNKHEAD,
	CLIP_CHUNK_CHNKEXTA,
	CLIP_CHUNK_CHNKSQLI,
	CLIP_CHUNK_CHNKFOOT
} clip_chunk_type;

#ifdef _MSC_VER
#pragma pack(1)
#endif
static const char clip_csfchunk_magic[CLIP_CHUNK_MAGIC_SIZE] = "CSFCHUNK";
static const char clip_chnkhead_magic[CLIP_CHUNK_MAGIC_SIZE] = "CHNKHead";
static const char clip_chnkexta_magic[CLIP_CHUNK_MAGIC_SIZE] = "CHNKExta";
static const char clip_chnksqli_magic[CLIP_CHUNK_MAGIC_SIZE] = "CHNKSQLi";
static const char clip_chnkfoot_magic[CLIP_CHUNK_MAGIC_SIZE] = "CHNKFoot";

/* "BlockDataBeginChunk" */
const char * clip_chnkexta_chunk_begin = "B\0l\0o\0c\0k\0D\0a\0t\0a\0B\0e\0g\0i\0n\0C\0h\0u\0n\0k\0";
/* "BlockDataEndChunk" */
const char * clip_chnkexta_chunk_end = "B\0l\0o\0c\0k\0D\0a\0t\0a\0E\0n\0d\0C\0h\0u\0n\0k\0";
/* "BlockStatus" */
const char * clip_chnkexta_chunk_status = "B\0l\0o\0c\0k\0S\0t\0a\0t\0u\0s\0";
/* "BlockCheckSum" */
const char * clip_chnkexta_chunk_checksum = "B\0l\0o\0c\0k\0C\0h\0e\0c\0k\0S\0u\0m\0";

/* file signature chunk */
typedef struct clip_csfchunk
{
	/* magic number, expected to be "CSFCHUNK" */
	char magic[CLIP_CHUNK_MAGIC_SIZE];
	/* the file size */
	uint64_t length;
	/* expected offset to CHNKHead? */
	uint64_t offset;
} clip_csfchunk;

/* the "CHNKHead" chunk*/
typedef struct clip_chnkhead
{
	/* magic number, expected to be "CHNKHead" */
	char magic[CLIP_CHUNK_MAGIC_SIZE];
	/* possibly length? */
	uint64_t unknown_0;
	uint64_t unknown_1;
	uint64_t unknown_2;
	uint64_t unknown_3;
	uint8_t unknown[16];
} clip_chnkhead;

typedef struct clip_chnkexta_base
{
	/* magic number, expected to be "CHNKExta" */
	char magic[CLIP_CHUNK_MAGIC_SIZE];
	/* data size */
	uint64_t size;
} clip_chnkexta_base;

/* "CHNKExta" chunk */
typedef struct clip_chnkexta
{
	clip_chnkexta_base base;
	/* can be any size */
	uint8_t * data;
} clip_chnkexta;

typedef struct clip_chnksqli_base
{
	/* magic number, expected to be "CHNKSQLi" */
	char magic[CLIP_CHUNK_MAGIC_SIZE];
	/* data size */
	uint64_t size;
} clip_chnksqli_base;

/* "CHNKSQLi" chunk */
typedef struct clip_chnksqli
{
	clip_chnksqli_base base;
	/* can be any size */
	uint8_t * data;
} clip_chnksqli;

/* "CHNKFoot" chunk */
typedef struct clip_chnkfoot
{
	/* magic number, expected to be "CHNKFoot" */
	char magic[CLIP_CHUNK_MAGIC_SIZE];
	/* seems to be always 0 */
	uint64_t unknown;
} clip_chnkfoot;

static void dump_bytes(const uint8_t * bytes, uint64_t count)
{
	uint64_t i;
	for (i = 0; i < count; i++)
	{
		printf("%02X", bytes[i]);
		if (i + 1 < count)
		{
			if ((i + 1) % 16 == 0)
			{
				printf("\n");
			}
			else
			{
				printf(" ");
			}
		}
	}
	printf("\n");
}

static clip_chunk_type detect_chunk(FILE * file)
{
	char tmp[CLIP_CHUNK_MAGIC_SIZE];
	long prev_offset;
	clip_chunk_type ret = CLIP_CHUNK_UNKNOWN_OR_ERROR;
	if (!file)
	{
		fprintf(stderr, "file must not be NULL\n");
		return CLIP_CHUNK_UNKNOWN_OR_ERROR;
	}
	prev_offset = ftell(file);
	if (prev_offset == -1)
	{
		perror("failed to retrieve previous file offset");
		return CLIP_CHUNK_UNKNOWN_OR_ERROR;
	}
	if (fread(tmp, 1, sizeof(tmp), file) != sizeof(tmp))
	{
		fprintf(stderr, "failed to read header magic from file\n");
		return CLIP_CHUNK_UNKNOWN_OR_ERROR;
	}
	if (fseek(file, prev_offset, SEEK_SET) != 0)
	{
		fprintf(stderr, "failed to seek file to previous position\n");
		return CLIP_CHUNK_UNKNOWN_OR_ERROR;
	}
	if (strncmp(clip_csfchunk_magic, tmp, sizeof(tmp)) == 0)
	{
		ret = CLIP_CHUNK_CSFCHUNK;
	}
	if (strncmp(clip_chnkhead_magic, tmp, sizeof(tmp)) == 0)
	{
		ret = CLIP_CHUNK_CHNKHEAD;
	}
	if (strncmp(clip_chnkexta_magic, tmp, sizeof(tmp)) == 0)
	{
		ret = CLIP_CHUNK_CHNKEXTA;
	}
	if (strncmp(clip_chnksqli_magic, tmp, sizeof(tmp)) == 0)
	{
		ret = CLIP_CHUNK_CHNKSQLI;
	}
	if (strncmp(clip_chnkfoot_magic, tmp, sizeof(tmp)) == 0)
	{
		ret = CLIP_CHUNK_CHNKFOOT;
	}
	return ret;
}

static clip_csfchunk * alloc_clip_csfchunk(FILE * file)
{
	clip_csfchunk * ret;
	if (!file)
	{
		fprintf(stderr, "file must not be NULL\n");
		return NULL;
	}
	ret = (clip_csfchunk *) malloc(sizeof(clip_csfchunk));
	if (!ret)
	{
		fprintf(stderr, "failed to allocate CSFCHUNK buffer\n");
		return NULL;
	}
	if (fread(ret, 1, sizeof(clip_csfchunk), file) != sizeof(clip_csfchunk))
	{
		fprintf(stderr, "failed to read CSFCHUNK data from file\n");
		free(ret);
		return NULL;
	}
	ret->length = htonll(ret->length);
	ret->offset = htonll(ret->offset);
	return ret;
}

static void free_clip_csfchunk(clip_csfchunk * csfchunk)
{
	free(csfchunk);
}

static void parse_clip_csfchunk(clip_csfchunk * csfchunk)
{
	printf("CSFCHUNK length: 0x%" PRIX64 "\n", csfchunk->length);
	printf("CSFCHUNK offset: 0x%" PRIX64 "\n", csfchunk->offset);
}

static clip_chnkhead * alloc_clip_chnkhead(FILE * file)
{
	clip_chnkhead * ret;
	if (!file)
	{
		fprintf(stderr, "file must not be NULL\n");
		return NULL;
	}
	ret = (clip_chnkhead *) malloc(sizeof(clip_chnkhead));
	if (!ret)
	{
		fprintf(stderr, "failed to allocate CHNKHead buffer\n");
		return NULL;
	}
	if (fread(ret, 1, sizeof(clip_chnkhead), file) != sizeof(clip_chnkhead))
	{
		fprintf(stderr, "failed to read CHNKHead data from file\n");
		free(ret);
		return NULL;
	}
	ret->unknown_0 = htonll(ret->unknown_0);
	ret->unknown_1 = htonll(ret->unknown_1);
	ret->unknown_2 = htonll(ret->unknown_2);
	ret->unknown_3 = htonll(ret->unknown_3);
	return ret;
}

static void free_clip_chnkhead(clip_chnkhead * chnkhead)
{
	free(chnkhead);
}

static void parse_clip_chnkhead(clip_chnkhead * chnkhead)
{
	printf("CHNKHead unknown 0 0x%" PRIX64 "\n", chnkhead->unknown_0);
	printf("CHNKHead unknown 1 0x%" PRIX64 "\n", chnkhead->unknown_1);
	printf("CHNKHead unknown 2 0x%" PRIX64 "\n", chnkhead->unknown_2);
	printf("CHNKHead unknown 3 0x%" PRIX64 "\n", chnkhead->unknown_3);
	printf("CHNKHead byte array:\n");
	dump_bytes(chnkhead->unknown, sizeof(chnkhead->unknown));
}

static clip_chnkexta * alloc_clip_chnkexta(FILE * file, uint64_t verify_file_size)
{
	clip_chnkexta * ret;
	uint64_t offset;
	if (!file)
	{
		fprintf(stderr, "file must not be NULL\n");
		return NULL;
	}
	offset = (uint64_t) ftell(file);
	ret = (clip_chnkexta *) malloc(sizeof(clip_chnkexta));
	if (!ret)
	{
		fprintf(stderr, "failed to allocate CHNKExta buffer\n");
		return NULL;
	}
	if (fread(ret, 1, sizeof(clip_chnkexta_base), file) != sizeof(clip_chnkexta_base))
	{
		fprintf(stderr, "failed to read CHNKExta header from file\n");
		free(ret);
		return NULL;
	}
	ret->base.size = htonll(ret->base.size);
	if (offset + sizeof(ret->base.magic) + sizeof(ret->base.size) + ret->base.size >= verify_file_size)
	{
		fprintf(stderr, "length exceeds file size\n");
		free(ret);
		return NULL;
	}
	ret->data = (uint8_t *) malloc((size_t) ret->base.size);
	if (!ret->data)
	{
		fprintf(stderr, "failed to allocate CHNKExta data buffer\n");
		free(ret);
		return NULL;
	}
	if (fread(ret->data, 1, (size_t) ret->base.size, file) != ret->base.size)
	{
		fprintf(stderr, "failed to read CHNKExta data from file\n");
		free(ret->data);
		free(ret);
		return NULL;
	}
	return ret;
}

static void free_clip_chnkexta(clip_chnkexta * chnkexta)
{
	free(chnkexta->data);
	free(chnkexta);
}

static void parse_clip_chnkexta(clip_chnkexta * chnkexta)
{
	printf("CHNKExta length %" PRIu64 " (0x%" PRIX64 ")\n", chnkexta->base.size, chnkexta->base.size);
#if NO_DUMP_BYTES != 1
	dump_bytes(chnkexta->data, chnkexta->base.size);
#endif
}

static clip_chnksqli * alloc_clip_chnksqli(FILE * file, uint64_t verify_file_size)
{
	clip_chnksqli * ret;
	uint64_t offset;
	if (!file)
	{
		fprintf(stderr, "file must not be NULL\n");
		return NULL;
	}
	offset = (uint64_t) ftell(file);
	ret = (clip_chnksqli *) malloc(sizeof(clip_chnksqli));
	if (!ret)
	{
		fprintf(stderr, "failed to allocate CHNKSQLi buffer\n");
		return NULL;
	}
	if (fread(ret, 1, sizeof(clip_chnksqli_base), file) != sizeof(clip_chnksqli_base))
	{
		fprintf(stderr, "failed to read CHNKSQLi header from file\n");
		free(ret);
		return NULL;
	}
	ret->base.size = htonll(ret->base.size);
	if (offset + sizeof(ret->base.magic) + sizeof(ret->base.size) + ret->base.size >= verify_file_size)
	{
		fprintf(stderr, "length exceeds file size\n");
		free(ret);
		return NULL;
	}
	ret->data = (uint8_t *) malloc((size_t) ret->base.size);
	if (!ret->data)
	{
		fprintf(stderr, "failed to allocate CHNKSQLi data buffer\n");
		free(ret);
		return NULL;
	}
	if (fread(ret->data, 1, (size_t) ret->base.size, file) != ret->base.size)
	{
		fprintf(stderr, "failed to read CHNKSQLi data from file\n");
		free(ret->data);
		free(ret);
		return NULL;
	}
	return ret;
}

static void free_clip_chnksqli(clip_chnksqli * chnksqli)
{
	free(chnksqli->data);
	free(chnksqli);
}

static void parse_clip_chnksqli(clip_chnksqli * chnksqli)
{
	printf("CHNKSQLi length %" PRIu64 " (0x%" PRIX64 ")\n", chnksqli->base.size, chnksqli->base.size);
#if NO_DUMP_BYTES != 1
	dump_bytes(chnksqli->data, chnksqli->base.size);
#endif
}

static clip_chnkfoot * alloc_clip_chnkfoot(FILE * file)
{
	clip_chnkfoot * ret;
	if (!file)
	{
		fprintf(stderr, "file must not be NULL\n");
		return NULL;
	}
	ret = (clip_chnkfoot *) malloc(sizeof(clip_chnkfoot));
	if (!ret)
	{
		fprintf(stderr, "failed to allocate CHNKFoot buffer\n");
		return NULL;
	}
	if (fread(ret, 1, sizeof(clip_chnkfoot), file) != sizeof(clip_chnkfoot))
	{
		fprintf(stderr, "failed to read CHNKFoot header from file\n");
		free(ret);
		return NULL;
	}
	ret->unknown = htonll(ret->unknown);
	return ret;
}

static void free_clip_chnkfoot(clip_chnkfoot * chnkfoot)
{
	free(chnkfoot);
}

static void parse_clip_chnkfoot(clip_chnkfoot * chnkfoot)
{
	printf("CHNKFoot unknown 0x%" PRIX64 "\n", chnkfoot->unknown);
}

#ifdef _WIN32
int wmain(int argc, wchar_t ** argv)
#else
int main(int argc, char ** argv)
#endif
{
	FILE * file;
	uint64_t file_size;
	uint64_t offset;
	int ret;
	if (argc < 2)
	{
		fprintf(stderr, "usage: " FILE_NAME_FORMAT_SPECIFIER " FILE\n", argv[0]);
		return 2;
	}
	file = FILE_OPEN(argv[1], FILE_OPEN_MODE);
	if (!file)
	{
		fprintf(stderr, "unable to open file " FILE_NAME_FORMAT_SPECIFIER "\n", argv[1]);
		return 1;
	}
	if (fseek(file, 0, SEEK_END) != 0)
	{
		fprintf(stderr, "failed to seek to end of file\n");
		ret = 1;
		goto done_error;
	}
	if (ftell(file) == -1L)
	{
		fprintf(stderr, "failed to get file size\n");
		ret = 1;
		goto done_error;
	}
	file_size = (uint64_t) ftell(file);
	rewind(file);
	for (;;)
	{
		static clip_csfchunk * csfchunk;
		static clip_chnkhead * chnkhead;
		static clip_chnkexta * chnkexta;
		static clip_chnksqli * chnksqli;
		static clip_chnkfoot * chnkfoot;
		offset = (uint64_t) ftell(file);
		if (offset == file_size)
		{
			printf("end of file reached\n");
			break;
		}
		if (offset == -1L)
		{
			fprintf(stderr, "failed to get file offset\n");
			ret = 1;
			goto done_error;
		}
		printf("current offset 0x%" PRIX64 "\n", offset);
		switch (detect_chunk(file))
		{
			case CLIP_CHUNK_CSFCHUNK:
				csfchunk = alloc_clip_csfchunk(file);
				if (csfchunk)
				{
					parse_clip_csfchunk(csfchunk);
					free_clip_csfchunk(csfchunk);
				}
				else
				{
					fprintf(stderr, "CSFCHUNK chunk allocation error\n");
					ret = 1;
					goto done_error;
				}
				break;
			case CLIP_CHUNK_CHNKHEAD:
				chnkhead = alloc_clip_chnkhead(file);
				if (chnkhead)
				{
					parse_clip_chnkhead(chnkhead);
					free_clip_chnkhead(chnkhead);
				}
				else
				{
					fprintf(stderr, "CHNKHead chunk allocation error\n");
					ret = 1;
					goto done_error;
				}
				break;
			case CLIP_CHUNK_CHNKEXTA:
				chnkexta = alloc_clip_chnkexta(file, file_size);
				if (chnkexta)
				{
					parse_clip_chnkexta(chnkexta);
					free_clip_chnkexta(chnkexta);
				}
				else
				{
					fprintf(stderr, "CHNKExta chunk allocation error\n");
					ret = 1;
					goto done_error;
				}
				break;
			case CLIP_CHUNK_CHNKSQLI:
				chnksqli = alloc_clip_chnksqli(file, file_size);
				if (chnksqli)
				{
					parse_clip_chnksqli(chnksqli);
					free_clip_chnksqli(chnksqli);
				}
				else
				{
					fprintf(stderr, "CHNKSQLi chunk allocation error\n");
					ret = 1;
					goto done_error;
				}
				break;
			case CLIP_CHUNK_CHNKFOOT:
				chnkfoot = alloc_clip_chnkfoot(file);
				if (chnkfoot)
				{
					parse_clip_chnkfoot(chnkfoot);
					free_clip_chnkfoot(chnkfoot);
				}
				else
				{
					fprintf(stderr, "CHNKFoot chunk allocation error\n");
					ret = 1;
					goto done_error;
				}
				break;
			case CLIP_CHUNK_UNKNOWN_OR_ERROR:
			default:
				fprintf(stderr, "unknown chunk or chunk detection error\n");
				ret = 1;
				goto done_error;
				break;
		}
	}
	ret = 0;
done_error:
	fclose(file);
	return ret;
}
#ifdef _MSC_VER
#pragma warning (pop)
#endif

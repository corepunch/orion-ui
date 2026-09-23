#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MISSING_OFFSET UINT32_MAX
#define HEADER_SIZE 4

static unsigned char *read_file(const char *path, size_t *size) {
	FILE *file = fopen(path, "rb");
	if (!file) { perror(path); return NULL; }
	if (fseek(file, 0, SEEK_END) || (*size = (size_t)ftell(file)) == (size_t)-1 || fseek(file, 0, SEEK_SET)) {
		perror(path); fclose(file); return NULL;
	}
	unsigned char *data = malloc(*size ? *size : 1);
	if (!data || fread(data, 1, *size, file) != *size) {
		perror(path); free(data); fclose(file); return NULL;
	}
	fclose(file);
	return data;
}

static uint32_t be32(const unsigned char *p) {
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int compare_offsets(const void *a, const void *b) {
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	return (x > y) - (x < y);
}

static size_t next_offset(const uint32_t *sorted, size_t count, uint32_t current, size_t file_size) {
	size_t lo = 0, hi = count;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (sorted[mid] <= current) lo = mid + 1;
		else hi = mid;
	}
	return lo < count ? sorted[lo] : file_size;
}

int main(int argc, char **argv) {
	if (argc != 3 && argc != 4 && argc != 5) {
		fprintf(stderr, "usage: %s OFFSETS CONTAINER [record-id [output-file]]\n", argv[0]);
		return 2;
	}
	size_t table_size = 0, file_size = 0;
	unsigned char *table = read_file(argv[1], &table_size);
	unsigned char *file = read_file(argv[2], &file_size);
	if (!table || !file || table_size % HEADER_SIZE) { fprintf(stderr, "invalid input\n"); free(table); free(file); return 1; }
	size_t count = table_size / HEADER_SIZE, present = 0, fant = 0;
	uint32_t *sorted = malloc(count * sizeof(*sorted));
	if (!sorted) { perror("malloc"); free(table); free(file); return 1; }
	for (size_t i = 0; i < count; i++) {
		uint32_t offset = be32(table + i * HEADER_SIZE);
		if (offset == MISSING_OFFSET) continue;
		if (offset >= file_size) { fprintf(stderr, "invalid offset at record %zu: %u\n", i, offset); free(sorted); free(table); free(file); return 1; }
		sorted[present++] = offset;
		if (file_size - offset >= HEADER_SIZE && !memcmp(file + offset, "FANT", HEADER_SIZE)) fant++;
	}
	qsort(sorted, present, sizeof(*sorted), compare_offsets);
	printf("entries=%zu present=%zu missing=%zu FANT=%zu container_bytes=%zu\n", count, present, count - present, fant, file_size);
	if (argc >= 4) {
		char *end = NULL;
		unsigned long id = strtoul(argv[3], &end, 10);
		if (!argv[3][0] || *end || id >= count) { fprintf(stderr, "invalid record id\n"); free(sorted); free(table); free(file); return 1; }
		uint32_t offset = be32(table + id * HEADER_SIZE);
		if (offset == MISSING_OFFSET) { fprintf(stderr, "record %lu is absent\n", id); free(sorted); free(table); free(file); return 1; }
		size_t size = next_offset(sorted, present, offset, file_size) - offset;
		if (size < HEADER_SIZE) { fprintf(stderr, "record %lu is too short\n", id); free(sorted); free(table); free(file); return 1; }
		printf("record=%lu offset=%u size=%zu signature=%02x%02x%02x%02x\n", id, offset, size, file[offset], file[offset+1], file[offset+2], file[offset+3]);
		if (argc == 5) {
			FILE *out = fopen(argv[4], "wb");
			if (!out) { perror(argv[4]); free(sorted); free(table); free(file); return 1; }
			int failed = fwrite(file + offset, 1, size, out) != size;
			failed |= fclose(out) != 0;
			if (failed) {
				perror(argv[4]); free(sorted); free(table); free(file); return 1;
			}
		}
	}
	free(sorted); free(table); free(file);
	return 0;
}

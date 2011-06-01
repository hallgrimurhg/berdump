/*
 * Copyright 2011 Hallgrimur H. Gunnarsson.  All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <vector>

static int format = 0;
static unsigned int depth = 0;
static int tagpath[128];

enum
{
	CLASS_MASK	= 0xC0,
	TYPE_MASK	= 0x20,
	TAG_MASK	= 0x1F,
	LEN_XTND	= 0x80,
	LEN_MASK	= 0x7F
};

struct tag
{
	int cls;
	int isPrimitive;
	int id;
	int tag;
	int nbytes;
};

struct length
{
	unsigned int length;
	unsigned int nbytes;
};

struct TLV
{
	struct tag tag;
	struct length length;
	unsigned int nbytes;
	unsigned int depth;
	unsigned char *value;
	std::vector<struct TLV *> children;
};

int readTag(FILE *fp, struct tag *tag)
{
	memset(tag, 0, sizeof(struct tag));

	int b = fgetc(fp);

	if (b == EOF)
		return 1;

	tag->nbytes = 1;

	tag->tag = b;
	tag->cls = b & CLASS_MASK;
	tag->isPrimitive = (b & TYPE_MASK) == 0;

	tag->id = b & TAG_MASK;

	if (tag->id == TAG_MASK)
	{
		// Long tag, encoded as a sequence of 7-bit values

		tag->id = 0;

		do
		{
			b = fgetc(fp);

			if (b == EOF)
				return 1;

			tag->nbytes++;
			tag->id = (tag->id << 7) | (b & LEN_MASK);

		} while ((b & LEN_XTND) == LEN_XTND);
	}

	return 0;
}

int readLen(FILE *fp, struct length *length)
{
	int b, i;

	memset(length, 0, sizeof(struct length));

	b = fgetc(fp);

	if (b == EOF)
		return 1;

	length->nbytes = 1;
	length->length = b;

	if ((length->length & LEN_XTND) == LEN_XTND)
	{
		int numoct = length->length & LEN_MASK;

		length->length = 0;

		if (numoct == 0)
			return 0;

		for (i = 0; i < numoct; i++)
		{
			b = fgetc(fp);

			if (b == EOF)
				return 1;

			length->length = (length->length << 8) | b;
			length->nbytes++;
		}
	}

	return 0;
}

int readTLV(FILE *fp, struct TLV *tlv, unsigned int limit)
{
	int n = 0;
	int i;

	memset(tlv, 0, sizeof(struct TLV));

	if (readTag(fp, &tlv->tag))
		return 1;

	tlv->nbytes += tlv->tag.nbytes;

	if (tlv->nbytes >= limit)
		return 1;

	if (readLen(fp, &tlv->length))
		return 1;

	tlv->nbytes += tlv->length.nbytes;
	tlv->depth = depth;

	int length = tlv->length.length;

	if (tlv->nbytes >= limit)
	{
		if (length == 0)
			return 0;

		return 1;
	}

	if (tlv->tag.isPrimitive)
	{
		// Primitive definite-length method

		if (length == 0)
			return 0;

		tlv->value = (unsigned char *)malloc(length);

		if (!fread(tlv->value, length, 1, fp))
			return 1;

		tlv->nbytes += length;

		return 0;
	}

	if (length > 0)
	{
		// Constructed definite-length method

		struct TLV *child;
		i = 0;

		while (i < length)
		{
			depth++;

			child = (struct TLV *)malloc(sizeof(struct TLV));

			if (readTLV(fp, child, length-i))
			{
				depth--;
				return 1;
			}

			depth--;

			i += child->nbytes;
			tlv->nbytes += child->nbytes;
			tlv->children.push_back(child);
		}

		return 0;
	}

	// Constructed indefinite-length method

	struct TLV *child;

	while (1)
	{
		depth++;

		child = (struct TLV *)malloc(sizeof(struct TLV));

		n = readTLV(fp, child, limit-tlv->nbytes);

		depth--;

		tlv->nbytes += child->nbytes;

		if (n == 1)
			return 1;

		if (child->tag.tag == 0 && child->length.length == 0)
			break;

		tlv->children.push_back(child);
	}

	return 0;
}

void print(struct TLV *tlv)
{
	unsigned int i;

	for (i = 0; i < tlv->depth; i++)
		printf("  ");

	if (tlv->value)
	{
		printf("[%d] ", tlv->tag.id);

		for (i = 0; i < tlv->length.length; i++)
		{
			printf("%02x", tlv->value[i]);
		}

		printf("\n");

	} else
	{
		printf("[%d] {\n", tlv->tag.id);

		for (i = 0; i < tlv->children.size(); i++)
			print(tlv->children[i]);

		for (i = 0; i < tlv->depth; i++)
			printf("  ");

		printf("}\n");
	}
}

void printCSV(struct TLV *tlv, int printPath)
{
	unsigned int i;

	if (tlv->tag.isPrimitive)
	{
		tagpath[tlv->depth] = tlv->tag.id;

		if (printPath)
		{
			for (i = 0; i <= tlv->depth; i++)
			{
				if (i == 0)
					printf("%d", tagpath[i]);
				else
					printf(",%d", tagpath[i]);
			}
		} else
		{
			printf("%d", tlv->tag.id);
		}

		printf("|");

		for (i = 0; i < tlv->length.length; i++)
		{
			printf("%02x", tlv->value[i]);
		}

		printf("\n");

	} else
	{
		tagpath[tlv->depth] = tlv->tag.id;

		if (printPath)
		{
			for (i = 0; i <= tlv->depth; i++)
			{
				if (i == 0)
					printf("%d", tagpath[i]);
				else
					printf(",%d", tagpath[i]);
			}
		} else
		{
			printf("%d", tlv->tag.id);
		}

		printf("|BEGIN\n");

		for (i = 0; i < tlv->children.size(); i++)
			printCSV(tlv->children[i], printPath);

		if (printPath)
		{
			for (i = 0; i <= tlv->depth; i++)
			{
				if (i == 0)
					printf("%d", tagpath[i]);
				else
					printf(",%d", tagpath[i]);
			}
		} else
		{
			printf("%d", tlv->tag.id);
		}

		printf("|END\n");
	}
}

void dump(FILE *fp)
{
	struct TLV root;

	while (1)
	{
		if (feof(fp) || ferror(fp))
			break;

		if (readTLV(fp, &root, 1048576))
			break;

		memset(tagpath, 0, sizeof(tagpath));

		switch (format)
		{
			case 0:
				print(&root);
				break;
			case 1:
				printCSV(&root, 0);
				break;
			case 2:
				printCSV(&root, 1);
				break;
		}
	}
}

static void usage(void)
{
	fprintf(stderr, "usage: berdump [options] <files>\n");
	fprintf(stderr, "options:\n");
	fprintf(stderr, "  -f <N> : output format\n");
	fprintf(stderr, "output formats:\n");
	fprintf(stderr, "  -f0	  : Pretty tree structure\n");
	fprintf(stderr, "  -f1	  : CSV tag|value\n");
	fprintf(stderr, "  -f2	  : CSV tag|value with full tag path\n");
}

int main(int argc, char *argv[])
{
	int c, i;

	while ((c = getopt(argc, argv, "f:h?")) != -1)
	{
		switch (c)
		{
			case 'f':
				format = atoi(optarg);
				break;
			case '?':
				if (optopt == 'c')
					fprintf (stderr, "Option -%c requires an argument.\n", optopt);
				else if (isprint (optopt))
					fprintf (stderr, "Unknown option `-%c'.\n", optopt);
				else
					fprintf (stderr,
							"Unknown option character `\\x%x'.\n",
							optopt);
				return 1;
			case 'h':
			default:
				usage();
				return 0;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1)
	{
		dump(stdin);
		return 0;
	}

	for (i = 0; i < argc; i++)
	{
		if (strlen(argv[i]) == 1 && argv[i][0] == '-')
		{
			dump(stdin);
			continue;
		}

		FILE *fp = fopen(argv[i], "r");

		if (fp == NULL)
		{
			fprintf(stderr, "error opening %s: %s\n", argv[i], strerror(errno));
			continue;
		}

		dump(fp);

		fclose(fp);
	}
}


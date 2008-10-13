#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fnmatch.h>

#include "logging.h"
#include "index.h"

/*
 * Index abstract data type (used only by depmod)
 */

struct index_node *index_create()
{
	struct index_node *node;

	node = NOFAIL(calloc(sizeof(struct index_node), 1));
	node->prefix = NOFAIL(strdup(""));
	node->first = INDEX_CHILDMAX;
	
	return node;
}

void index_destroy(struct index_node *node)
{
	int c;
	
	for (c = node->first; c <= node->last; c++) {
		struct index_node *child = node->children[c];
		
		if (child)
			index_destroy(child);
	}
	free(node->prefix);
	free(node);
}

static void index__checkstring(const char *str)
{
	int spaces = 0;
	int i;
	
	for (i = 0; str[i]; i++) {
		int ch = str[i];
		
		if (ch >= INDEX_CHILDMAX)
			fatal("Module index: bad character '%c'=0x%x - only 7-bit ASCII is supported:"
			      "\n%s\n", (char) ch, (int) ch, str);
		
		if (ch == ' ')
			spaces++;
	}
	
	if (!spaces)
		fatal("Module index: internal error - missing space (key/value separator)"
		      "\n%s\n", str);
}

int index_insert(struct index_node *node, const char *str)
{
	int i = 0; /* index within str */
	int ch;
	int duplicate = 0;
	
	index__checkstring(str);
	
	while(1) {
		int j; /* index within node->prefix */
	
		/* Ensure node->prefix is a prefix of &str[i].
		   If it is not already, then we must split node. */
		for (j = 0; node->prefix[j]; j++) {
			ch = node->prefix[j];
		
			if (ch != str[i+j]) {
				char *prefix = node->prefix;
				struct index_node *n;
				
				/* New child is copy of node with prefix[j+1..N] */
				n = NOFAIL(calloc(sizeof(struct index_node), 1));
				memcpy(n, node, sizeof(struct index_node));
				n->prefix = NOFAIL(strdup(&prefix[j+1]));
				
				/* Parent has prefix[0..j], child at prefix[j] */
				memset(node, 0, sizeof(struct index_node));
				prefix[j] = '\0';
				node->prefix = prefix;
				node->first = ch;
				node->last = ch;
				node->children[ch] = n;
				
				break;
			}
		}
		/* j is now length of node->prefix */
		i += j;
	
		ch = str[i];
		if(ch == '\0') {
			if (node->isendpoint)
				duplicate = 1;
			
			node->isendpoint = 1;
			return duplicate;
		}
		
		if (!node->children[ch]) {
			struct index_node *child;
		
			if (ch < node->first)
				node->first = ch;
			if (ch > node->last)
				node->last = ch;
			node->children[ch] = NOFAIL(calloc(sizeof(struct index_node), 1));
			
			child = node->children[ch];
			child->prefix = NOFAIL(strdup(&str[i+1]));
			child->isendpoint = 1;
			child->first = INDEX_CHILDMAX;
			
			return duplicate;
		}
		
		/* Descend into child node and continue */
		node = node->children[ch];
		i++;
	}
}

static int index__haschildren(const struct index_node *node)
{
	return node->first < INDEX_CHILDMAX;
}

/* Recursive post-order traversal

   Pre-order would make for better read-side buffering / readahead / caching.
   (post-order means you go backwards in the file as you descend the tree).
   However, index reading is already fast enough.
   Pre-order is simpler for writing, and depmod is already slow.
 */
static uint32_t index_write__node(const struct index_node *node, FILE *out)
{
 	uint32_t *child_offs = NULL;
 	int child_count = 0;
	long offset;
	
	if (!node)
		return 0;
	
	/* Write children and save their offsets */
	if (index__haschildren(node)) {
		const struct index_node *child;
		int i;
		
		child_count = node->last - node->first + 1;
		child_offs = NOFAIL(malloc(child_count * sizeof(uint32_t)));
		
		for (i = 0; i < child_count; i++) {
			child = node->children[node->first + i];
			child_offs[i] = htonl(index_write__node(child, out));
		}
	}
		
	/* Now write this node */
	offset = ftell(out);
	
	if (node->prefix[0]) {
		fputs(node->prefix, out);
		fputc('\0', out);
		offset |= INDEX_NODE_PREFIX;
	}
		
	if (child_count) {
		fputc(node->first, out);
		fputc(node->last, out);
		fwrite(child_offs, sizeof(uint32_t), child_count, out);
		free(child_offs);
		offset |= INDEX_NODE_CHILDS;
	}
	
	if (node->isendpoint)
		offset |= INDEX_NODE_ENDPOINT;
	
	return offset;
}

void index_write(const struct index_node *node, FILE *out)
{
	long initial_offset, final_offset;
	uint32_t u;
	
	u = htonl(INDEX_MAGIC);
	fwrite(&u, sizeof(u), 1, out);
	
	/* First word is reserved for the offset of the root node */
	initial_offset = ftell(out);
	u = 0;
	fwrite(&u, sizeof(uint32_t), 1, out);
	
	/* Dump trie */
	u = htonl(index_write__node(node, out));
	
	/* Update first word */
	final_offset = ftell(out);
	fseek(out, initial_offset, SEEK_SET);
	fwrite(&u, sizeof(uint32_t), 1, out);
	fseek(out, final_offset, SEEK_SET);
}


/*
 * Buffer abstract data type
 *
 * Used internally to store the current path during tree traversal.
 * They help build wildcard key strings to pass to fnmatch(),
 * as well as building values of matching keys.
 */

struct buffer {
	char *bytes;
	unsigned size;
	unsigned used;
};

static void buf__realloc(struct buffer *buf, unsigned size)
{
	if (size > buf->size) {
		buf->bytes = NOFAIL(realloc(buf->bytes, size));
		buf->size = size;
	}
}

static struct buffer *buf_create()
{
	struct buffer *buf;
	 
	buf = NOFAIL(calloc(sizeof(struct buffer), 1));
	buf__realloc(buf, 1024);
	return buf;
}

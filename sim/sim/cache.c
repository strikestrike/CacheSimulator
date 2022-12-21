/*
 * cache.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "cache.h"
#include "main.h"

#define MASK_ORIG 0xFFFFFFFF

/* cache configuration parameters */
static int cache_split = 0;
static int cache_usize = DEFAULT_CACHE_SIZE;
static int cache_isize = DEFAULT_CACHE_SIZE;
static int cache_dsize = DEFAULT_CACHE_SIZE;
static int cache_block_size = DEFAULT_CACHE_BLOCK_SIZE;
static int words_per_block = DEFAULT_CACHE_BLOCK_SIZE / WORD_SIZE;
static int cache_assoc = DEFAULT_CACHE_ASSOC;
static int cache_writeback = DEFAULT_CACHE_WRITEBACK;
static int cache_writealloc = DEFAULT_CACHE_WRITEALLOC;

/* cache model data structures */
static Pcache icache;
static Pcache dcache;
static cache c1;
static cache c2;
static cache_stat cache_stat_inst;
static cache_stat cache_stat_data;

/************************************************************/
void set_cache_param(param, value) int param;
int value;
{

	switch (param)
	{
	case CACHE_PARAM_BLOCK_SIZE:
		cache_block_size = value;
		words_per_block = value / WORD_SIZE;
		break;
	case CACHE_PARAM_USIZE:
		cache_split = FALSE;
		cache_usize = value;
		break;
	case CACHE_PARAM_ISIZE:
		cache_split = TRUE;
		cache_isize = value;
		break;
	case CACHE_PARAM_DSIZE:
		cache_split = TRUE;
		cache_dsize = value;
		break;
	case CACHE_PARAM_ASSOC:
		cache_assoc = value;
		break;
	case CACHE_PARAM_WRITEBACK:
		cache_writeback = TRUE;
		break;
	case CACHE_PARAM_WRITETHROUGH:
		cache_writeback = FALSE;
		break;
	case CACHE_PARAM_WRITEALLOC:
		cache_writealloc = TRUE;
		break;
	case CACHE_PARAM_NOWRITEALLOC:
		cache_writealloc = FALSE;
		break;
	default:
		printf("error set_cache_param: bad parameter value\n");
		exit(-1);
	}
}
void init_cache_aux(cache *c, int size)
{
	int bitsOffset, bitsSet; // No. of bits
	int auxMask;			 // generate the set mask
	/* initialize the cache, and cache statistics data structures */
	c->size = size;
	c->associativity = cache_assoc;
	c->n_sets = size / (cache_assoc * cache_block_size);
	c->LRU_head = (Pcache_line *)malloc(sizeof(Pcache_line) * c->n_sets);
	c->LRU_tail = (Pcache_line *)malloc(sizeof(Pcache_line) * c->n_sets);
	c->set_contents = (int *)malloc(sizeof(int) * c->n_sets);
	c->contents = 0;
	// calcul
	bitsSet = LOG2(c->n_sets);
	bitsOffset = LOG2(cache_block_size);

	auxMask = (1 << bitsSet) - 1;

	c->index_mask = auxMask << bitsOffset;
	c->index_mask_offset = bitsOffset;

	for (int i = 0; i < c->n_sets; i++)
	{
		c->LRU_head[i] = NULL;
		c->LRU_tail[i] = NULL;
		c->set_contents[i] = 0;
	}
}
/************************************************************/

/************************************************************/
void init_cache()
{

	// printf("Checando tipo de caché\n");
	if (cache_split)
	{
		//printf("init");
		init_cache_aux(&c1, cache_dsize);
		//printf("Cach 1 list...");
		init_cache_aux(&c2, cache_isize);
		//printf("Cache 2 list.\n");
	}
	else
	{
		init_cache_aux(&c1, cache_usize);
	}

	// printf("\n\nInicializando estadísticas...");
	cache_stat_data.accesses = 0;
	cache_stat_data.copies_back = 0;
	cache_stat_data.demand_fetches = 0;
	cache_stat_data.misses = 0;
	cache_stat_data.replacements = 0;
	cache_stat_inst.accesses = 0;
	cache_stat_inst.copies_back = 0;
	cache_stat_inst.demand_fetches = 0;
	cache_stat_inst.misses = 0;
	cache_stat_inst.replacements = 0;
	// printf("Listo\n\n");
}
/************************************************************/

/************************************************************/
void perform_access_instruction_load(
	cache *c,
	unsigned addr,
	int index,
	int block_size_in_words,
	unsigned int tag)
{
	cache_stat_inst.accesses++;

	if (c->LRU_head[index] == NULL)
	{ // empty list
		cache_stat_inst.misses++;
		cache_stat_inst.demand_fetches += block_size_in_words;
		Pcache_line new_item = malloc(sizeof(cache_line));
		new_item->tag = tag;
		new_item->dirty = 0;
		c->set_contents[index] = 1;
		insert(&c->LRU_head[index], &c->LRU_tail[index], new_item);
	}
	else
	{
		if (c->set_contents[index] == c->associativity)
		{
			Pcache_line cl = c->LRU_head[index];
			int tag_found = FALSE;
			for (int i = 0; i < c->set_contents[index]; i++)
			{
				if (cl->tag == tag)
				{
					tag_found = TRUE;
					break;
				}
				cl = cl->LRU_next;
			}
			if (tag_found)
			{
				delete (&c->LRU_head[index], &c->LRU_tail[index], cl);
				insert(&c->LRU_head[index], &c->LRU_tail[index], cl);
			}
			else
			{
				cache_stat_inst.demand_fetches += block_size_in_words;
				cache_stat_inst.misses++;
				cache_stat_inst.replacements++;
				Pcache_line new_item = malloc(sizeof(cache_line));
				new_item->tag = tag;
				new_item->dirty = 0;
				insert(&c->LRU_head[index], &c->LRU_tail[index], new_item);
				// insert
				if (c->LRU_tail[index]->dirty)
				{
					cache_stat_data.copies_back += block_size_in_words;
				}
				delete (&c->LRU_head[index], &c->LRU_tail[index], c->LRU_tail[index]);
				// delete duplicates
			}
		}
		else
		{
			int tag_found = FALSE;
			Pcache_line cl = c->LRU_head[index];

			for (int i = 0; i < c->set_contents[index]; i++)
			{
				if (cl->tag == tag)
				{
					tag_found = TRUE;
					break;
				}
				cl = cl->LRU_next;
			}

			if (tag_found)
			{
				delete (&c->LRU_head[index], &c->LRU_tail[index], cl);
				insert(&c->LRU_head[index], &c->LRU_tail[index], cl);
			}
			else
			{
				cache_stat_inst.demand_fetches += block_size_in_words;
				cache_stat_inst.misses++;
				Pcache_line new_item = malloc(sizeof(cache_line));
				new_item->tag = tag;
				new_item->dirty = 0;
				insert(&(c->LRU_head[index]), &(c->LRU_tail[index]), new_item);
				c->set_contents[index]++;
				// incremanting the counter of nodes
			}
		}
	}
}
/**************************************************************************************************************************************************************************************/
void perform_access_data_load(
	cache *c,
	unsigned addr,
	int index,
	int block_size_in_words,
	unsigned int tag)
{
	cache_stat_data.accesses++;
	if (c->LRU_head[index] == NULL)
	{
		cache_stat_data.misses++;
		cache_stat_data.demand_fetches += block_size_in_words;
		Pcache_line new_item = malloc(sizeof(cache_line));
		new_item->tag = tag;
		new_item->dirty = 0;
		c->set_contents[index] = 1;
		insert(&c->LRU_head[index], &c->LRU_tail[index], new_item);
	}
	else
	{

		if (c->set_contents[index] == c->associativity)
		{
			Pcache_line cl = c->LRU_head[index];
			int tag_found = FALSE;
			for (int i = 0; i < c->set_contents[index]; i++)
			{
				if (cl->tag == tag)
				{
					tag_found = TRUE;
					break;
				}
				cl = cl->LRU_next;
			}

			if (tag_found)
			{
				delete (&c->LRU_head[index], &c->LRU_tail[index], cl);
				insert(&c->LRU_head[index], &c->LRU_tail[index], cl);
			}
			else
			{
				cache_stat_data.demand_fetches += block_size_in_words;
				cache_stat_data.misses++;
				cache_stat_data.replacements++;
				Pcache_line new_item = malloc(sizeof(cache_line));
				new_item->tag = tag;
				new_item->dirty = 0;

				if (c->LRU_tail[index]->dirty)
				{
					cache_stat_data.copies_back += block_size_in_words;
				}
				delete (&c->LRU_head[index], &c->LRU_tail[index], c->LRU_tail[index]);
				// delete duplicate
				insert(&(c->LRU_head[index]), &(c->LRU_tail[index]), new_item);
				// insert the new element
			}
		}
		else
		{
			int tag_found = FALSE;
			Pcache_line cl = c->LRU_head[index];
			for (int i = 0; i < c->set_contents[index]; i++)
			{
				if (cl->tag == tag)
				{
					tag_found = TRUE;
					break;
				}
				cl = cl->LRU_next;
			}

			if (tag_found)
			{
				delete (&c->LRU_head[index], &c->LRU_tail[index], cl);
				insert(&c->LRU_head[index], &c->LRU_tail[index], cl);
			}
			else
			{
				cache_stat_data.demand_fetches += block_size_in_words;
				cache_stat_data.misses++;
				Pcache_line new_item = malloc(sizeof(cache_line));
				new_item->tag = tag;
				new_item->dirty = 0;
				insert(&(c->LRU_head[index]), &(c->LRU_tail[index]), new_item);
				// insert the element
				c->set_contents[index]++;
			}
		}
	}
}
/************************************************************************************************************************************************************************************/
void perform_access_data_store(
	cache *c,
	unsigned addr,
	int index,
	int block_size_in_words,
	unsigned int tag)
{
	cache_stat_data.accesses++;

	if (c->LRU_head[index] == NULL)
	{
		if (cache_writealloc == 0)
		{
			cache_stat_data.copies_back += 1;
			cache_stat_data.misses++;
		}

		else
		{
			cache_stat_data.misses++;
			cache_stat_data.demand_fetches += block_size_in_words;

			Pcache_line new_item = malloc(sizeof(cache_line));

			new_item->tag = tag;
			new_item->dirty = 1;
			// Implement the modifications to the cache memory
			if (cache_writeback == 0)
			{
				cache_stat_data.copies_back += 1;
				new_item->dirty = 0;
			}
			c->set_contents[index] = 1;
			insert(&c->LRU_head[index], &c->LRU_tail[index], new_item);
		}
	}
	else
	{
		if (c->set_contents[index] == c->associativity)
		{
			Pcache_line cl = c->LRU_head[index];
			int tag_found = FALSE;
			for (int i = 0; i < c->set_contents[index]; i++)
			{
				if (cl->tag == tag)
				{
					tag_found = TRUE;
					break;
				}
				cl = cl->LRU_next;
			}
			if (tag_found)
			{
				delete (&c->LRU_head[index], &c->LRU_tail[index], cl);
				insert(&c->LRU_head[index], &c->LRU_tail[index], cl);
				c->LRU_head[index]->dirty = 1;
				if (cache_writeback == 0)
				{
					cache_stat_data.copies_back += 1;
					c->LRU_head[index]->dirty = 0;
				}
			}
			else
			{
				if (cache_writealloc == 0)
				{
					cache_stat_data.copies_back += 1;
					cache_stat_data.misses++;
				}
				else
				{
					cache_stat_data.demand_fetches += block_size_in_words;
					cache_stat_data.misses++;
					cache_stat_data.replacements++;
					Pcache_line new_item = malloc(sizeof(cache_line));
					new_item->tag = tag;
					new_item->dirty = 1;

					if (c->LRU_tail[index]->dirty)
					{
						cache_stat_data.copies_back += block_size_in_words;
					}
					if (cache_writeback == 0)
					{
						cache_stat_data.copies_back += 1;
						new_item->dirty = 0;
					}
					delete (&c->LRU_head[index], &c->LRU_tail[index], c->LRU_tail[index]);
					insert(&(c->LRU_head[index]), &(c->LRU_tail[index]), new_item);
					// Insert the new element
				}
			}
		}
		else
		{
			int tag_found = FALSE;
			Pcache_line cl = c->LRU_head[index];
			for (int i = 0; i < c->set_contents[index]; i++)
			{
				if (cl->tag == tag)
				{
					tag_found = TRUE;
					break;
				}
				cl = cl->LRU_next;
			}

			if (tag_found)
			{
				delete (&c->LRU_head[index], &c->LRU_tail[index], cl);
				insert(&c->LRU_head[index], &c->LRU_tail[index], cl);
				c->LRU_head[index]->dirty = 1;
				if (cache_writeback == 0)
				{
					cache_stat_data.copies_back += 1;
					c->LRU_head[index]->dirty = 0;
				}
			}
			else
			{
				if (cache_writealloc == 0)
				{
					cache_stat_data.misses++;
					cache_stat_data.copies_back += 1;
				}

				else
				{
					cache_stat_data.demand_fetches += block_size_in_words;
					cache_stat_data.misses++;
					Pcache_line new_item = malloc(sizeof(cache_line));
					new_item->tag = tag;
					new_item->dirty = 1;
					if (cache_writeback == 0)
					{
						cache_stat_data.copies_back += 1;
						new_item->dirty = 0;
					}
					insert(&(c->LRU_head[index]), &(c->LRU_tail[index]), new_item);
					// increasing the number of nodes by adding the new one
					c->set_contents[index]++;
				}
			}
		}
	}
}
/************************************************************************************************************************************************************************************/
void perform_access_aux_plus(cache *c, unsigned addr, unsigned access_type)
{
	static int nl = 0;
	int index;
	unsigned int bitsSet, bitsOffset, tagMask, tag;
	int block_size_in_words = cache_block_size / WORD_SIZE;
	bitsSet = LOG2(c->n_sets);
	bitsOffset = LOG2(cache_block_size);
	tagMask = MASK_ORIG << (bitsOffset + bitsSet);
	tag = addr & tagMask;
	index = (addr & c->index_mask) >> c->index_mask_offset;
	nl++;
	switch (access_type)
	{
	case TRACE_INST_LOAD:
		perform_access_instruction_load(c, addr, index, block_size_in_words, tag);
		break;

	case TRACE_DATA_LOAD:
		perform_access_data_load(c, addr, index, block_size_in_words, tag);
		break;

	case TRACE_DATA_STORE:
		perform_access_data_store(c, addr, index, block_size_in_words, tag);
		break;
	}
}
/************************************************************************************************************************************************************************************/
void perform_access_aux_sp(cache *c1, cache *c2, unsigned addr, unsigned access_type)
{
	static int nl = 0;
	int index_c1, index_c2;
	unsigned int bitsSet_c1, bitsSet_c2, bitsOffset, tagMask_c1, tagMask_c2, tag_c1, tag_c2;
	int block_size_in_words = cache_block_size / WORD_SIZE;
	bitsSet_c1 = LOG2(c1->n_sets);
	bitsSet_c2 = LOG2(c2->n_sets);
	bitsOffset = LOG2(cache_block_size);
	tagMask_c1 = MASK_ORIG << (bitsOffset + bitsSet_c1);
	tagMask_c2 = MASK_ORIG << (bitsOffset + bitsSet_c2);
	tag_c1 = addr & tagMask_c1;
	tag_c2 = addr & tagMask_c2;
	index_c1 = (addr & c1->index_mask) >> c1->index_mask_offset;
	index_c2 = (addr & c2->index_mask) >> c2->index_mask_offset;
	nl++;
	switch (access_type)
	{
	case TRACE_INST_LOAD:
		perform_access_instruction_load(c2, addr, index_c2, block_size_in_words, tag_c2);
		break;
	case TRACE_DATA_LOAD:
		perform_access_data_load(c1, addr, index_c1, block_size_in_words, tag_c1);
		break;
	case TRACE_DATA_STORE:
		perform_access_data_store(c1, addr, index_c1, block_size_in_words, tag_c1);
		break;
	}
}
/***************************************************************************************************************/
void perform_access(addr, access_type) unsigned addr, access_type;
{
	// handle the access to the cache
	if (cache_split)
	{
		perform_access_aux_sp(&c1, &c2, addr, access_type);
	}
	else
	{
		perform_access_aux_plus(&c1, addr, access_type);
	}
}
/************************************************************/

/************************************************************/
void flush()
{
	/* flush the cache */
	int block_size_in_words = cache_block_size / WORD_SIZE;
	Pcache_line cl;

	for (int i = 0; i < c1.n_sets; i++)
	{
		cl = c1.LRU_head[i];
		for (int j = 0; j < c1.set_contents[i]; j++)
		{
			if (cl->dirty)
				cache_stat_data.copies_back += block_size_in_words;
			cl = cl->LRU_next;
		}
	}
	if (cache_split)
	{
		for (int j = 0; j < c2.n_sets; j++)
		{
			cl = c2.LRU_head[j];
			for (int f = 0; f < c2.set_contents[j]; f++)
			{
				if (cl->dirty)
					cache_stat_data.copies_back += block_size_in_words;
				cl = cl->LRU_next; // move the node
			}
		}
	}
}
/************************************************************/

/************************************************************/
void delete (head, tail, item)
	Pcache_line *head,
	*tail;
Pcache_line item;
{
	if (item->LRU_prev)
	{
		item->LRU_prev->LRU_next = item->LRU_next;
	}
	else
	{
		/* item at head */
		*head = item->LRU_next;
	}

	if (item->LRU_next)
	{
		item->LRU_next->LRU_prev = item->LRU_prev;
	}
	else
	{
		/* item at tail */
		*tail = item->LRU_prev;
	}
}
/************************************************************/

/************************************************************/
/* inserts at the head of the list */
void insert(head, tail, item)
	Pcache_line *head,
	*tail;
Pcache_line item;
{
	item->LRU_next = *head;
	item->LRU_prev = (Pcache_line)NULL;

	if (item->LRU_next)
		item->LRU_next->LRU_prev = item;
	else
		*tail = item;

	*head = item;
}
/************************************************************/

/************************************************************/
void dump_settings()
{
	/*printf("*** CACHE SETTINGS ***\n");
	if (cache_split)
	{
		printf("  Split I- D-cache\n");
		printf("  I-cache size: \t%d\n", cache_isize);
		printf("  D-cache size: \t%d\n", cache_dsize);
	}
	else
	{
		printf("  Unified I- D-cache\n");
		printf("  Size: \t%d\n", cache_usize);
	}
	printf("  Associativity: \t%d\n", cache_assoc);
	printf("  Block size: \t%d\n", cache_block_size);
	printf("  Write policy: \t%s\n",
		   cache_writeback ? "WRITE BACK" : "WRITE THROUGH");
	printf("  Allocation policy: \t%s\n",
		   cache_writealloc ? "WRITE ALLOCATE" : "WRITE NO ALLOCATE");*/
}
/************************************************************/

/************************************************************/
void print_stats()
{
	//printf("\n*** CACHE STATISTICS ***\n");

	//printf(" INSTRUCTIONS\n");
	//printf("  accesses:  %d\n", cache_stat_inst.accesses);
	//printf("  misses:    %d\n", cache_stat_inst.misses);
	//if (!cache_stat_inst.accesses)
		//printf("  miss rate: 0 (0)\n");
	//else
		/*printf("  miss rate: %2.4f (hit rate %2.4f)\n",
			   (float)cache_stat_inst.misses / (float)cache_stat_inst.accesses,
			   1.0 - (float)cache_stat_inst.misses / (float)cache_stat_inst.accesses);*/
	printf("%.4f\t%d\t%.4f\t%d\n", 1.0 - (float)cache_stat_inst.misses / (float)cache_stat_inst.accesses, cache_isize,
		1.0 - (float)cache_stat_data.misses / (float)cache_stat_data.accesses, cache_dsize);
	//printf("  replace:   %d\n", cache_stat_inst.replacements);

	/*printf(" DATA\n");
	printf("  accesses:  %d\n", cache_stat_data.accesses);
	printf("  misses:    %d\n", cache_stat_data.misses);
	if (!cache_stat_data.accesses)
		printf("  miss rate: 0 (0)\n");
	else
		printf("  miss rate: %2.4f (hit rate %2.4f)\n",
			   (float)cache_stat_data.misses / (float)cache_stat_data.accesses,
			   1.0 - (float)cache_stat_data.misses / (float)cache_stat_data.accesses);
	printf("  replace:   %d\n", cache_stat_data.replacements);

	printf(" TRAFFIC (in words)\n");
	printf("  demand fetch:  %d\n", cache_stat_inst.demand_fetches +
										cache_stat_data.demand_fetches);
	printf("  copies back:   %d\n", cache_stat_inst.copies_back +
										cache_stat_data.copies_back);*/
}
/************************************************************/

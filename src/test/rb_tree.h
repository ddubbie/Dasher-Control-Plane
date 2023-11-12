#ifndef __RB_TREE_H__
#define __RB_TREE_H__

#include "item.h"
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
//#include "cnic.h"

typedef struct rb_tree_s {
	size_t nb_nodes;
	item *root;
	item sentinel;
	bool (*cmp)(item *x, item *y);
	uint64_t (*key)(item *x);
} rb_tree;

rb_tree *rb_tree_create(bool (*cmp)(item *x, item *y), uint64_t (*key)(item *x));
void rb_tree_destroy(rb_tree *rt);
item *rb_tree_first(rb_tree *rt);
size_t rb_nb_nodes(rb_tree *rt);
void rb_tree_insert(rb_tree *rt, item *z);
void rb_tree_delete(rb_tree *rt, item *z);
item *rb_tree_search_with_key(rb_tree *rt, uint64_t key);
item *rb_tree_get_minimum(rb_tree *rt);
item *rb_tree_get_maximum(rb_tree *rt);
bool rb_tree_is_empty(rb_tree *rt);
item *rb_tree_pop(rb_tree *rt);
#endif

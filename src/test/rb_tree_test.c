#include "rb_tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NB_ITMES 100
#define MAX_NB 10000

static bool 
cmp_hv(item *x, item *y) {
	 return x->sv->hv < y->sv->hv;
}

static uint64_t
get_key(item *x) {
	return x->sv->hv;
}

static void 
check_ascending_order(uint64_t hv_arr[], uint32_t sz) {
	int i;

	for (i = 0; i < sz - 1; i++) {
		if (hv_arr[i] > hv_arr[i+1]) {
			printf("error\n");
			return;
		}
	}
}

int
main(void) {

	int i;
	uint64_t ret_hv[NB_ITMES];
	rb_tree *rbt = rb_tree_create(cmp_hv, get_key);
	item *it = malloc(sizeof(item) * NB_ITMES);
	varying_value *vv = malloc(sizeof(varying_value) * NB_ITMES);
	static_value *sv = malloc(sizeof(static_value) * NB_ITMES);

	srand(time(NULL));

	for (i = 0; i < NB_ITMES; i++) {
		sv[i].hv = rand() % MAX_NB;
		it[i].vv = &vv[i];
		it[i].sv = &sv[i];

		rb_tree_insert(rbt, &it[i]);
	}

	for (i = 0; i < NB_ITMES; i++) {
		item *ret_it = rb_tree_get_minimum(rbt);
		rb_tree_delete(rbt, ret_it);
		ret_hv[i] = ret_it->sv->hv;
		printf("%lu ", ret_it->sv->hv);
	}

	check_ascending_order(ret_hv, NB_ITMES);

	printf("\n");

	rb_tree_destroy(rbt);

	free(it);
	free(vv);
	free(sv);
}

#include "item.h"
#include "hyperbolic.h"

#include <stdio.h>
#include <stdlib.h>

#define NB_ITEMS 10

int
main(void) {

	int i;
	int x = 10;
	item *it = malloc(sizeof(item) * NB_ITEMS);
	varying_value *vv = malloc(sizeof(varying_value) * NB_ITEMS);
	static_value *sv = malloc(sizeof(static_value) * NB_ITEMS);
	void *cache = hyperbolic_cache_setup((void *)&x);

	for (i = 0; i < NB_ITEMS; i++) {
		sv[i].hv = i;
		it[i].vv = &vv[i];
		it[i].sv = &sv[i];
		hyperbolic_cache_insert(cache, &it[i]);
	}

	for (i = 0; i < NB_ITEMS / 2; i++) {
		hyperbolic_cache_delete(cache, &it[i]);
	}


	for (i = NB_ITEMS / 2; i < NB_ITEMS; i++) {
		hyperbolic_cache_delete(cache, &it[i]);
	}

}

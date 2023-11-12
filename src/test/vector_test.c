#include "../vector64.h"

#define NB_ELES 1000

int
main(void) {

	int i;
	uint64_t test[NB_ELES];
	vector64 *vec = vector64_create(0);

	for (i = 0; i < NB_ELES; i++) {
		vector64_push_back(vec, i);
	}

	for (i = 0; i < NB_ELES; i++) {
		printf("%lu ", vector64_pop_back(vec));
	}

	printf("\n");

	vector64_destroy(vec);
}

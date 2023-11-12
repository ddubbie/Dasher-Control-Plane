#include "../heap.h"
#include <stdlib.h>
#include <time.h>

#define ARR_SZ 1000
#define MAX_NB 200000

// 842 508 651 234 727

static bool 
cmp_int(void *x, void *y) {
	int x_int = *(int *)x;
	int y_int = *(int *)y;
	return x_int < y_int;
}

static void 
is_ascending_order(int arr[], int sz) {
	int i;

	for (i = 0 ; i < sz - 1 ; i++) {
		if (arr[i] > arr[i+1])
			printf("error\n");
	}
}

int
main(void) {
	heap *hp;
	int i;
	int arr[ARR_SZ];
	/*
	int arr[] = {
		842, 508, 651, 234, 727,
	};*/
	//int res[ARR_SZ];
	int res[ARR_SZ];
	srand(time(NULL));

	for (i = 0; i < ARR_SZ; i++)
		arr[i] = rand() % MAX_NB;

	/*
	for (i = 0; i < ARR_SZ; i++)
		printf("%d ", arr[i]);

		*/
	//printf("\n");

	hp = heap_create(cmp_int, 0);

	for (i = 0; i < ARR_SZ; i++)
		heap_push(hp, (void *)&arr[i]);

	for (i = 0 ; i < ARR_SZ; i++) {
		res[i] = *(int *)heap_pop(hp);
	}

	is_ascending_order(res, ARR_SZ);

	//printf("\n");
}

#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>

#include "shortbase128.h"
#include "util.h"

extern "C" {
#include "sfmt-st.h"
}

NTL_CLIENT;

#define MIN(a,b) ((a)>(b)?(b):(a))

//mat_GF2 debug_mat0;
//mat_GF2 debug_mat;
//bool debug_dependent[128];
//uint32_t debug_count;
int debug_flag = 0;
static unsigned int bit_len;
static unsigned int mode;	// 0-3 状態空間のどこを見るか
static unsigned int active_len;	// 0-3 ノルムの加重をどこにするか
static void (*get_next_random)(vec_GF2& vec, sfmt_t *sfmt);

static void get_next_random128(vec_GF2& vec, sfmt_t *sfmt);
static void get_next_random64(vec_GF2& vec, sfmt_t *sfmt);
static void get_next_random32(vec_GF2& vec, sfmt_t *sfmt);

uint64_t vecto64(vec_GF2& vec) {
    uint64_t v = 0;
    int i;
    for (i = 0; i < vec.length(); i++) {
	v = (v << 1) | IsOne(vec[i]);
    }
    return v ;
}

static void copy_status(in_status *dist, const in_status *src) {
    dist->zero = src->zero;
    dist->special = src->special;
    dist->next = src->next;
    dist->count = src->count;
    dist->random = src->random;
}

void set_up(uint32_t bit_mode, uint32_t len, 
	    uint32_t p_mode, uint32_t weight_pos) {
    if (bit_mode == 128) {
	if ((len <= 0) || (len > 128)) {
	    printf("bitLength error mode 128\n");
	    exit(1);
	}
	bit_len = len;
	get_next_random = get_next_random128;
	mode = 0;
	active_len = len;
    } else if (bit_mode == 64) {
	if ((len <= 0) || (len > 64)) {
	    printf("bitLength error mode 64\n");
	    exit(1);
	}
	bit_len = len * 2;
	get_next_random = get_next_random64;
	mode = p_mode;
	active_len = len * (2 - weight_pos);
    } else {
	if ((len <= 0) || (len > 32)) {
	    printf("bitLength error mode 32\n");
	    exit(1);
	}
	bit_len = len * 4;
	get_next_random = get_next_random32;
	mode = p_mode;
	active_len = len * (4 - weight_pos);
    }
    //printf("active_len = %u\n", active_len);
}

void set_special(in_status *st, unsigned int special_bit) {
    st->special = true;
    st->zero = false;
    st->count = 0;
    st->next.SetLength(0);
    st->next.SetLength(bit_len);
    st->next.put(special_bit, 1);
    memset(&(st->random), 0, sizeof(st->random));
}

static void get_next_random128(vec_GF2& vec, sfmt_t *sfmt) {
    uint64_t hi, low;
    uint64_t mask;
    unsigned int i;

    //clear(vec);
    gen_rand128(sfmt, &hi, &low);
    mask = 0x8000000000000000ULL;
    for (i = 0; (i < 64) && (i < bit_len); i++) {
	if (hi & mask) {
	    vec.put(i, 1);
	} else {
	    vec.put(i, 0);
	}
	mask = mask >> 1;
    }
    mask = 0x8000000000000000ULL;
    for (i = 0; (i < 64) && (i + 64 < bit_len); i++) {
	if (low & mask) {
	    vec.put(i + 64, 1);
	} else {
	    vec.put(i + 64, 0);
	}
	mask = mask >> 1;
    }
#if 0
    if (debug_flag) {
	cout << vec << endl;
    }
#endif
}

static void get_next_random64(vec_GF2& vec, sfmt_t *sfmt) {
    uint32_t array32[4];
    uint64_t array64[2];
    uint64_t mask;
    unsigned int i, j;

    assert(mode % 2 == 0);
    gen_rand128sp(sfmt, array32, mode);
    array64[0] = (uint64_t)array32[0] | (uint64_t)array32[1] << 32;
    array64[1] = (uint64_t)array32[2] | (uint64_t)array32[3] << 32;
    for (i = 0; i < 2; i++) {
	mask = 0x8000000000000000ULL;
	for (j = 0; j < bit_len; j += 2) {
	    if (array64[i] & mask) {
		vec.put(i + j, 1);
	    } else {
		vec.put(i + j, 0);
	    }
	    mask = mask >> 1;
	}
    }
#if 0
    if (debug_flag) {
	cout << vec << endl;
    }
#endif
}

static void get_next_random32(vec_GF2& vec, sfmt_t *sfmt) {
    uint32_t array[4];
    uint32_t mask;
    unsigned int i, j;

    gen_rand128sp(sfmt, array, mode);
    for (i = 0; i < 4; i++) {
	mask = 0x80000000UL;
	for (j = 0; j < bit_len; j += 4) {
	    if (array[i] & mask) {
		vec.put(i + j, 1);
	    } else {
		vec.put(i + j, 0);
	    }
	    mask = mask >> 1;
	}
    }
#if 0
    if (debug_flag) {
	cout << vec << endl;
    }
#endif
}

void set_normal(in_status *st, sfmt_t *sfmt) {
    int zero_count = 0;
    st->random = *sfmt;
    st->special = false;
    st->zero = false;
    st->count = 0;
    st->next.SetLength(bit_len);
    //st->random.gen_rand(st->next, bit_len);
    get_next_random(st->next, &(st->random));
    st->count++;
    while (IsZero(st->next)) {
	zero_count++;
	if (zero_count > MAXDEGREE) {
	    st->zero = true;
	    break;
	}
	//st->random.gen_rand(st->next, bit_len);
	get_next_random(st->next, &(st->random));
	st->count++;
    }
}

void add_status(in_status *dist, in_status *src) {
    unsigned int c;
    vec_GF2 next;

    c = dist->count;
    next = dist->next;
#if 0
    if (dist->special && src->special) {
	printf("add special to special\n");
	//cout << "debug mat =\n" << debug_mat << endl;
	//printf("debug_dependent:");
	//for (uint32_t i = 0; i <= bit_len; i++) {
	//    printf("%d ", debug_dependent[i]);
	//}
	//printf("\n");
	exit(1);
    }
#endif
    if (dist->special && (!src->special)) {
	copy_status(dist, src);
    } else if ((! dist->special) && (! src->special)) {
	//dist->random.add(src->random);
	add_rnd(&(dist->random), &(src->random));
    }
    next += src->next;
    if ((IsZero(next)) && dist->special) {
	printf("something wrong!\n");
	//printf("debug count = %u\n", debug_count);
        cout << "src:" << src << endl;
        cout << "dist:" << dist << endl;
	cout << "dist->next:" << dist->next << endl;
	cout << "src->next:" << src->next << endl;
	cout << "src->special:" << src->special << endl;
	//cout << "debug mat0:\n" << debug_mat0 << endl;
	//cout << "debug mat:\n" << debug_mat << endl;
	//printf("debug_dependent:");
	//for (uint32_t i = 0; i <= bit_len; i++) {
	//    printf("%d ", debug_dependent[i]);
	//}
	//printf("\n");
	exit(1);
    }
    dist->next = next;
    dist->count = MIN(c, src->count);
}

void get_next_state(in_status *st) {
    int zero_count = 0;

    //DPRINTHT("before next", &(st->random));
    if (st->special) {
	printf("Request next to special\n");
	exit(1);
    }
    if (st->zero) {
	return;
    }
    //st->random.gen_rand(st->next, bit_len);
    get_next_random(st->next, &(st->random));
    st->count++;
    while (IsZero(st->next)) {
	zero_count++;
	if (zero_count > MAXDEGREE) {
	    st->zero = true;
	    break;
	}
	//st->random.gen_rand(st->next, bit_len);
	get_next_random(st->next, &(st->random));
	st->count++;
    }
    //DPRINTHT("after next", &(st->random));
}
  
int get_shortest_base(sfmt_t *sfmt) {
    static in_status bases[128 + 1];
    vec_GF2 next[bit_len + 1];
    bool dependents[bit_len + 1];
    unsigned int shortest;
    unsigned int i;
    unsigned int count;
    bool dependent_found;

    //DPRINT("in get_shortest_base bit_len:%u", bit_len);
    //set_bit_len(bit_len);
    //debug_count = 0;
    for (i = 0; i < bit_len; i++) {
	set_special(&(bases[i]), i);
    }
    set_normal(&(bases[bit_len]), sfmt);
    for (;;) {
#ifdef DEBUG
	//DPRINT("base", NULL);
	for (i = 0; i <= bit_len; i++) {
	    //DPRINTBASE(i, &(bases[i]));
	}
#endif
	//debug_count++;
	for (i = 0; i <= bit_len; i++) {
	    next[i] = bases[i].next;
	}
	dependent_found = get_dependent_trans(dependents, next);
	if (!dependent_found) {
	    break;
	}
#ifdef DEBUG
	fprintf(stderr, "dependents:");
	for (i = 0; i <= bit_len; i++) {
	    fprintf(stderr, "%1d", dependents[i]);
	}
	fprintf(stderr, "\n");
#endif
	shortest = get_shortest(dependents, bases);
	//memcpy(debug_dependent, dependents, sizeof(dependents));
	for (i = 0; i <= bit_len; i++) {
	    if (i == shortest) {
		continue;
	    }
	    if (dependents[i]) {
		add_status(&(bases[shortest]), &(bases[i]));
	    }
	}
	if (IsZero(bases[shortest].next)) {
	    get_next_state(&(bases[shortest]));
	} else {
	    fprintf(stderr, "next is not zero\n");
	    //cout << "debug_mat:" << debug_mat << endl;
	    cout << "dependent:";
	    for (i = 0; i <= bit_len; i++) {
		cout << dependents[i] ;
	    }
	    cout << endl;
	    exit(1);
	}
    }
    shortest = INT_MAX;
    for (i = 0; i <= bit_len; i++) {
	if (!bases[i].zero) {
	    count = bases[i].count;
	    bases[i].next.SetLength(active_len);
	    if (IsZero(bases[i].next)) {
		count++;
	    }
	    if (count < shortest) {
		shortest = count;
	    }
	}
    }
    return shortest;
}

uint32_t get_shortest(bool dependents[], in_status bases[]) {
    uint32_t index = 0;
    uint32_t min = UINT_MAX;
    uint32_t count;
    uint32_t i;
    vec_GF2 next;

    for (i = 0; i <= bit_len; i++) {
	if (dependents[i]) {
	    count = bases[i].count;
	    next = bases[i].next;
	    next.SetLength(active_len);
	    if (IsZero(next)) {
		count++;
	    }
	    if (count < min) {
		min = count;
		index = i;
	    }
	}
    }
    if (min == UINT_MAX) {
	printf("can't find shortest\n");
	exit(1);
    }
    return index;
}

bool get_dependent_trans(bool dependent[], vec_GF2 array[]) {
    mat_GF2 mat;
    uint32_t rank;

    convert(mat, array, bit_len);
    //debug_mat0 = mat;
    rank = (uint32_t) gauss_plus(mat);
    return dependent_rows(dependent, mat);
}

bool dependent_rows(bool result[], mat_GF2& mat) {
    int32_t rows;
    int32_t cols;
    int i, j, pos;
    bool found = false;

    rows = mat.NumRows();
    cols = mat.NumCols();
    for (i = 0; i < cols; i++) {
	result[i] = false;
    }
    pos = 0;
    for (i = 0; (i < cols - pos) && (i < rows); i++) {
	if (IsOne(mat.get(i, i + pos))) {
	    continue;
	}
	for (j = 0; j < i; j++) {
	    if (IsOne(mat.get(j, i + pos))) {
		found = true;
		result[j] = true;
	    }
	}
	if (found) {
	    result[i + pos] = true;
	    break;
	} else {
	    pos++;
	}
    }
    if (!found) {
#if 0
	cout << "not found" << endl;
	cout << mat << endl;
	exit(1);
#endif
    }
    //debug_mat = mat;
    return found;
}

void convert(mat_GF2& mat, vec_GF2 array[], uint32_t size) {
    uint32_t i, j;

    mat.SetDims(size + 1, size + 1);
    for (i = 0; i < size + 1; i++) {
	for (j = 0; j < size; j++) {
	    mat.put(j, i, array[i].get(j));
	}
    }
    for (i = 0; i < size + 1; i++) {
	mat.put(size, i, 0);
    }
}

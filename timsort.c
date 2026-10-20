#include "timsort.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>

#define MIN_MERGE 32
#define MIN_GALLOP 7
#define MAX_STACK 85

typedef struct {
    size_t len;
    size_t base;
} run;

typedef struct {
    void *a;
    size_t width;
    size_t nel;
    int (*compar)(const void *, const void *);
    char *tmp;
    size_t tmp_length;
    run stack[MAX_STACK];
    size_t stack_size;
    size_t min_gallop;
} timsort_t;

static size_t gallop_left(timsort_t *ts, void *key, char *a, size_t base, size_t len, size_t hint) {
    size_t width = ts->width;
    size_t ofs = 1;
    size_t lastofs = 0;
    ssize_t k;

    a += base * width;
    if (ts->compar(a + hint * width, key) < 0) {
        size_t maxofs = len - hint;
        while (ofs < maxofs) {
            if (ts->compar(a + (hint + ofs) * width, key) < 0) {
                lastofs = ofs;
                ofs = (ofs << 1) + 1;
            } else break;
        }
        if (ofs > maxofs) ofs = maxofs;
        lastofs += hint;
        ofs += hint;
    } else {
        size_t maxofs = hint + 1;
        while (ofs < maxofs) {
            if (ts->compar(a + (hint - ofs) * width, key) < 0)
                break;
            lastofs = ofs;
            ofs = (ofs << 1) + 1;
        }
        if (ofs > maxofs) ofs = maxofs;
        k = lastofs;
        lastofs = hint - ofs;
        ofs = hint - k;
    }
    a -= base * width;
    lastofs++;
    while (lastofs < ofs) {
        size_t m = lastofs + ((ofs - lastofs) >> 1);
        if (ts->compar(a + (base + m) * width, key) < 0)
            lastofs = m + 1;
        else
            ofs = m;
    }
    return ofs;
}

static size_t gallop_right(timsort_t *ts, void *key, char *a, size_t base, size_t len, size_t hint) {
    size_t width = ts->width;
    size_t ofs = 1;
    size_t lastofs = 0;
    ssize_t k;

    a += base * width;
    if (ts->compar(key, a + hint * width) < 0) {
        size_t maxofs = hint + 1;
        while (ofs < maxofs) {
            if (ts->compar(key, a + (hint - ofs) * width) < 0) {
                lastofs = ofs;
                ofs = (ofs << 1) + 1;
            } else break;
        }
        if (ofs > maxofs) ofs = maxofs;
        k = lastofs;
        lastofs = hint - ofs;
        ofs = hint - k;
    } else {
        size_t maxofs = len - hint;
        while (ofs < maxofs) {
            if (ts->compar(key, a + (hint + ofs) * width) < 0)
                break;
            lastofs = ofs;
            ofs = (ofs << 1) + 1;
        }
        if (ofs > maxofs) ofs = maxofs;
        lastofs += hint;
        ofs += hint;
    }
    a -= base * width;
    lastofs++;
    while (lastofs < ofs) {
        size_t m = lastofs + ((ofs - lastofs) >> 1);
        if (ts->compar(key, a + (base + m) * width) < 0)
            ofs = m;
        else
            lastofs = m + 1;
    }
    return ofs;
}

static void binary_sort(timsort_t *ts, size_t lo, size_t hi, size_t start) {
    size_t l, r, mid;
    char *a = (char *)ts->a;
    size_t width = ts->width;
    char *pivot = (char *)malloc(width);
    if (!pivot) return;

    for (size_t i = start; i < hi; i++) {
        memcpy(pivot, a + i * width, width);
        l = lo;
        r = i;
        while (l < r) {
            mid = l + ((r - l) >> 1);
            if (ts->compar(a + mid * width, pivot) <= 0) l = mid + 1;
            else r = mid;
        }
        for (size_t j = i; j > l; j--) {
            memcpy(a + j * width, a + (j - 1) * width, width);
        }
        memcpy(a + l * width, pivot, width);
    }
    free(pivot);
}

static size_t count_run_and_make_ascending(timsort_t *ts, size_t lo, size_t hi) {
    size_t run_hi = lo + 1;
    if (run_hi == hi) return 1;

    char *a = (char *)ts->a;
    size_t width = ts->width;
    int descending = ts->compar(a + run_hi * width, a + lo * width) < 0;
    run_hi++;

    if (descending) {
        while (run_hi < hi && ts->compar(a + run_hi * width, a + (run_hi - 1) * width) < 0) {
            run_hi++;
        }
        for (size_t i = lo, j = run_hi - 1; i < j; i++, j--) {
            char *pi = a + i * width;
            char *pj = a + j * width;
            for (size_t k = 0; k < width; k++) {
                char t = pi[k]; pi[k] = pj[k]; pj[k] = t;
            }
        }
    } else {
        while (run_hi < hi && ts->compar(a + run_hi * width, a + (run_hi - 1) * width) >= 0) {
            run_hi++;
        }
    }
    return run_hi - lo;
}

static size_t min_run_length(size_t n) {
    size_t r = 0;
    while (n >= MIN_MERGE) {
        r |= (n & 1);
        n >>= 1;
    }
    return n + r;
}

static void merge_lo(timsort_t *ts, size_t base1, size_t len1, size_t base2, size_t len2) {
    char *a = (char *)ts->a;
    size_t width = ts->width;
    char *tmp = ts->tmp;
    memcpy(tmp, a + base1 * width, len1 * width);

    size_t cursor1 = 0, cursor2 = base2;
    size_t dest = base1;
    size_t min_gallop = ts->min_gallop;

    while (cursor1 < len1 && cursor2 < base2 + len2) {
        size_t k = 0;
        while (k < min_gallop) {
            if (ts->compar(a + cursor2 * width, tmp + cursor1 * width) < 0) {
                memcpy(a + dest * width, a + cursor2 * width, width);
                dest++; cursor2++; k++;
                if (cursor2 == base2 + len2) goto epilogue;
            } else {
                memcpy(a + dest * width, tmp + cursor1 * width, width);
                dest++; cursor1++; k = 0;
            }
            if (cursor1 == len1) goto epilogue;
        }

        min_gallop = MIN_GALLOP;
        while (1) {
            size_t count1 = gallop_right(ts, a + cursor2 * width, tmp, cursor1, len1 - cursor1, 0);
            if (count1 > 0) {
                memcpy(a + dest * width, tmp + cursor1 * width, count1 * width);
                dest += count1; cursor1 += count1;
                if (cursor1 == len1) goto epilogue;
            }

            size_t count2 = gallop_left(ts, tmp + cursor1 * width, a, cursor2, base2 + len2 - cursor2, 0);
            if (count2 > 0) {
                memcpy(a + dest * width, a + cursor2 * width, count2 * width);
                dest += count2; cursor2 += count2;
                if (cursor2 == base2 + len2) goto epilogue;
            }

            if (count1 >= MIN_GALLOP || count2 >= MIN_GALLOP) {
                if (min_gallop > 1) min_gallop--;
            } else {
                break;
            }
        }
        if (min_gallop < MIN_GALLOP) min_gallop++;
        ts->min_gallop = min_gallop;
    }
epilogue:
    if (cursor1 < len1) {
        memcpy(a + dest * width, tmp + cursor1 * width, (len1 - cursor1) * width);
    }
}

static void merge_hi(timsort_t *ts, size_t base1, size_t len1, size_t base2, size_t len2) {
    char *a = (char *)ts->a;
    size_t width = ts->width;
    char *tmp = ts->tmp;
    memcpy(tmp, a + base2 * width, len2 * width);

    ssize_t cursor1 = base1 + len1 - 1;
    ssize_t cursor2 = len2 - 1;
    ssize_t dest = base2 + len2 - 1;
    size_t min_gallop = ts->min_gallop;

    while (cursor1 >= (ssize_t)base1 && cursor2 >= 0) {
        size_t k = 0;
        while (k < min_gallop) {
            if (ts->compar(tmp + cursor2 * width, a + cursor1 * width) >= 0) {
                memcpy(a + dest * width, tmp + cursor2 * width, width);
                dest--; cursor2--; k++;
                if (cursor2 < 0) goto epilogue;
            } else {
                memcpy(a + dest * width, a + cursor1 * width, width);
                dest--; cursor1--; k = 0;
            }
            if (cursor1 < (ssize_t)base1) goto epilogue;
        }

        min_gallop = MIN_GALLOP;
        while (1) {
            size_t k1 = gallop_right(ts, tmp + cursor2 * width, a, base1, cursor1 - base1 + 1, cursor1 - base1);
            size_t count1 = (cursor1 - base1 + 1) - k1;
            if (count1 > 0) {
                memcpy(a + (dest - count1 + 1) * width, a + (cursor1 - count1 + 1) * width, count1 * width);
                dest -= count1; cursor1 -= count1;
                if (cursor1 < (ssize_t)base1) goto epilogue;
            }

            size_t k2 = gallop_left(ts, a + cursor1 * width, tmp, 0, cursor2 + 1, cursor2);
            size_t count2 = (cursor2 + 1) - k2;
            if (count2 > 0) {
                memcpy(a + (dest - count2 + 1) * width, tmp + (cursor2 - count2 + 1) * width, count2 * width);
                dest -= count2; cursor2 -= count2;
                if (cursor2 < 0) goto epilogue;
            }

            if (count1 >= MIN_GALLOP || count2 >= MIN_GALLOP) {
                if (min_gallop > 1) min_gallop--;
            } else {
                break;
            }
        }
        if (min_gallop < MIN_GALLOP) min_gallop++;
        ts->min_gallop = min_gallop;
    }
epilogue:
    if (cursor2 >= 0) {
        memcpy(a + (dest - cursor2) * width, tmp, (cursor2 + 1) * width);
    }
}

static void merge_at(timsort_t *ts, size_t i) {
    size_t base1 = ts->stack[i].base;
    size_t len1 = ts->stack[i].len;
    size_t base2 = ts->stack[i + 1].base;
    size_t len2 = ts->stack[i + 1].len;

    ts->stack[i].len = len1 + len2;
    if (i == ts->stack_size - 3) {
        ts->stack[i + 1] = ts->stack[i + 2];
    }
    ts->stack_size--;

    if (len1 <= len2) merge_lo(ts, base1, len1, base2, len2);
    else merge_hi(ts, base1, len1, base2, len2);
}

static void merge_collapse(timsort_t *ts) {
    while (ts->stack_size > 1) {
        size_t n = ts->stack_size - 2;
        if (n > 0 && ts->stack[n - 1].len <= ts->stack[n].len + ts->stack[n + 1].len) {
            if (ts->stack[n - 1].len < ts->stack[n + 1].len) n--;
            merge_at(ts, n);
        } else if (ts->stack[n].len <= ts->stack[n + 1].len) {
            merge_at(ts, n);
        } else break;
    }
}

static void merge_force_collapse(timsort_t *ts) {
    while (ts->stack_size > 1) {
        size_t n = ts->stack_size - 2;
        if (n > 0 && ts->stack[n - 1].len < ts->stack[n + 1].len) n--;
        merge_at(ts, n);
    }
}

int timsort(void *base, size_t nel, size_t width, int (*compar)(const void *, const void *)) {
    if (nel < 2) return 0;

    timsort_t ts;
    ts.a = base;
    ts.width = width;
    ts.nel = nel;
    ts.compar = compar;
    ts.stack_size = 0;
    ts.min_gallop = MIN_GALLOP;

    ts.tmp_length = nel;
    ts.tmp = (char *)malloc(ts.tmp_length * width);
    if (!ts.tmp) return -1;

    size_t min_run = min_run_length(nel);
    size_t lo = 0;

    while (lo < nel) {
        size_t run_len = count_run_and_make_ascending(&ts, lo, nel);
        if (run_len < min_run) {
            size_t force = (nel - lo < min_run) ? nel - lo : min_run;
            binary_sort(&ts, lo, lo + force, lo + run_len);
            run_len = force;
        }
        ts.stack[ts.stack_size].base = lo;
        ts.stack[ts.stack_size].len = run_len;
        ts.stack_size++;
        merge_collapse(&ts);
        lo += run_len;
    }

    merge_force_collapse(&ts);
    free(ts.tmp);
    return 0;
}

void timsort_merge_blocks(void *arr, size_t left, size_t mid, size_t right, size_t width, int (*compar)(const void *, const void *)) {
    if (left == mid || mid == right) return;

    size_t len1 = mid - left;
    size_t len2 = right - mid;

    char *L = (char *)malloc(len1 * width);
    char *R = (char *)malloc(len2 * width);

    if (!L || !R) {
        perror("malloc in timsort_merge_blocks");
        free(L); free(R);
        exit(1);
    }

    char *a = (char *)arr;
    memcpy(L, a + left * width, len1 * width);
    memcpy(R, a + mid * width, len2 * width);

    size_t i = 0, j = 0, k = left;

    while (i < len1 && j < len2) {
        if (compar(L + i * width, R + j * width) <= 0) {
            memcpy(a + k * width, L + i * width, width);
            i++;
        } else {
            memcpy(a + k * width, R + j * width, width);
            j++;
        }
        k++;
    }

    while (i < len1) {
        memcpy(a + k * width, L + i * width, width);
        i++; k++;
    }

    while (j < len2) {
        memcpy(a + k * width, R + j * width, width);
        j++; k++;
    }

    free(L);
    free(R);
}
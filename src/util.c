#include "util.h"

#include <assert.h>
#include <fcntl.h>
#include <gmp.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>

bool g_verbose = true;
debug_e g_debug = ERROR;

double current_time(void) {
    struct timeval t;
    (void) gettimeofday(&t, NULL);
    return (double) (t.tv_sec + (double) (t.tv_usec / 1000000.0));
}
int max(int x, int y) {
    if (x >= y)
        return x;
    else
        return y;
}

void array_add(size_t *rop, size_t *xs, size_t *ys, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        rop[i] = xs[i] + ys[i];
    }
}

bool array_eq(size_t *xs, size_t *ys, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (xs[i] != ys[i])
            return false;
    }
    return true;
}

void array_printstring(int *xs, size_t n)
{
    for (size_t i = 0; i < n; i++)
        printf("%d", xs[i]);
}

void array_printstring_rev(int *xs, size_t n)
{
    for (size_t i = n; i > 0; i--)
        printf("%d", xs[i-1]);
}

void array_print(int *xs, size_t len) {
    if (len == 1){
        printf("[%d]", xs[0]);
        return;
    }
    for (size_t i = 0; i < len; i++) {
        if (i == 0) {
            printf("[%d,", xs[i]);
        } else if (i == len - 1) {
            printf("%d]", xs[i]);
        } else {
            printf("%d,", xs[i]);
        }
    }
}

void array_print_ui (size_t *xs, size_t len) {
    if (len == 1){
        printf("[%lu]", xs[0]);
        return;
    }
    for (size_t i = 0; i < len; i++) {
        if (i == 0) {
            printf("[%lu,", xs[i]);
        } else if (i == len - 1) {
            printf("%lu]", xs[i]);
        } else {
            printf("%lu,", xs[i]);
        }
    }
}

mpz_t *
mpz_vect_new(size_t n)
{
    mpz_t *vec = malloc(n * sizeof(mpz_t));
    mpz_vect_init(vec, n);
    return vec;
}

void
mpz_vect_init(mpz_t *vec, size_t n)
{
    for (size_t i = 0; i < n; i++)
        mpz_init(vec[i]);
}

mpz_t * mpz_vect_create_of_fmpz(fmpz_t *fvec, size_t n)
{
    mpz_t *vec = mpz_vect_new(n);
    for (size_t i = 0; i < n; ++i) {
        fmpz_get_mpz(vec[i], fvec[i]);
        fmpz_clear(fvec[i]);
    }
    free(fvec);
    return vec;
}

void mpz_vect_print(mpz_t *xs, size_t len)
{
    if (len == 1){
        gmp_printf("[%Zd]", xs[0]);
        return;
    }
    for (size_t i = 0; i < len; i++) {
        if (i == 0) {
            gmp_printf("[%Zd,", xs[i]);
        } else if (i == len - 1) {
            gmp_printf("%Zd]", xs[i]);
        } else {
            gmp_printf("%Zd,", xs[i]);
        }
    }
}

void
mpz_vect_free(mpz_t *vec, size_t n)
{
    mpz_vect_clear(vec, n);
    free(vec);
}

void
mpz_vect_clear(mpz_t *vec, size_t n)
{
    for (size_t i = 0; i < n; i++)
        mpz_clear(vec[i]);
}

void mpz_vect_set(mpz_t *rop, const mpz_t *const xs, size_t n)
{
    for (size_t i = 0; i < n; i++)
        mpz_set(rop[i], xs[i]);
}

void mpz_vect_urandomm(mpz_t *vec, const mpz_t modulus, size_t n, aes_randstate_t rng)
{
    for (size_t i = 0; i < n; i++) {
        do {
            mpz_urandomm_aes(vec[i], rng, modulus);
        } while (mpz_cmp_ui(vec[i], 0) == 0);
    }
}

void mpz_vect_urandomms(mpz_t *vec, const mpz_t *const moduli, size_t n, aes_randstate_t rng)
{
    for (size_t i = 0; i < n; i++) {
        do {
            mpz_urandomm_aes(vec[i], rng, moduli[i]);
        } while (mpz_cmp_ui(vec[i], 0) == 0);
    }
}

void mpz_vect_mul(mpz_t *const rop, const mpz_t *const xs, const mpz_t *const ys, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        mpz_mul(rop[i], xs[i], ys[i]);
    }
}

void mpz_vect_mod(mpz_t *const rop, const mpz_t *const xs, const mpz_t *const moduli, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        mpz_mod(rop[i], xs[i], moduli[i]);
    }
}

void mpz_vect_mul_mod(mpz_t *const rop, const mpz_t *const xs, const mpz_t *const ys, const mpz_t *const moduli, size_t n)
{
    mpz_vect_mul(rop, xs, ys, n);
    mpz_vect_mod(rop, (const mpz_t *const) rop, moduli, n);
}

void
mpz_randomm_inv(mpz_t rop, aes_randstate_t rng, const mpz_t modulus)
{
    mpz_t inv;
    mpz_init(inv);
    do {
        mpz_urandomm_aes(rop, rng, modulus);
    } while (mpz_invert(inv, rop, modulus) == 0);
    mpz_clear(inv);
}

size_t bit(size_t x, size_t i)
{
    return (x & (1 << i)) > 0;
}

////////////////////////////////////////////////////////////////////////////////
// custom allocators that complain when they fail

void *
my_calloc(size_t nmemb, size_t size)
{
    void *ptr = calloc(nmemb, size);
    if (ptr == NULL) {
        fprintf(stderr, "[%s] couldn't allocate %lu bytes!\n", __func__,
                nmemb * size);
        assert(false);
        return NULL;
    }
    return ptr;
}

void *
my_malloc(size_t size)
{
    void *ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "[%s] couldn't allocate %lu bytes!\n", __func__, size);
        assert(false);
        return NULL;
    }
    return ptr;
}

void *
my_realloc(void *ptr, size_t size)
{
    void *ptr_ = realloc(ptr, size);
    if (ptr_ == NULL) {
        fprintf(stderr, "[%s] couldn't reallocate %lu bytes!\n", __func__, size);
        assert(false);
        return NULL;
    }
    return ptr_;
}

////////////////////////////////////////////////////////////////////////////////
// serialization

void
ulong_fread(unsigned long *x, FILE *const fp)
{
    fscanf(fp, "%lu", x);
    GET_NEWLINE(fp);
}

void
ulong_fwrite(unsigned long x, FILE *const fp)
{
    fprintf(fp, "%lu", x);
    PUT_NEWLINE(fp);
}

void
size_t_fread(size_t *x, FILE *const fp)
{
    fscanf(fp, "%lu", x);
}

void
size_t_fwrite(size_t x, FILE *const fp)
{
    fprintf(fp, "%lu", x);
}

void
bool_fread(bool *x, FILE *const fp)
{
    int tmp;
    fscanf(fp, "%d", &tmp);
    *x = tmp;
}

void
bool_fwrite(bool x, FILE *const fp)
{
    int tmp = x;
    fprintf(fp, "%d", tmp);
}

#define PBSTR "||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||"
#define PBWIDTH 60

void
print_progress(size_t cur, size_t total)
{
    static int last_val = 0;
    double percentage = (double) cur / total;
    int val  = percentage * 100;
    int lpad = percentage * PBWIDTH;
    int rpad = PBWIDTH - lpad;
    if (val != last_val) {
        fprintf(stdout, "\r\t%3d%% [%.*s%*s] %lu/%lu", val, lpad, PBSTR, rpad, "", cur, total);
        if (cur == total)
            fprintf(stdout, "\n");
        fflush(stdout);
        last_val = val;
    }
}
